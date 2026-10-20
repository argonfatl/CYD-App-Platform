#include "web_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_store.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "web_ui";

static httpd_handle_t s_server;
static SemaphoreHandle_t s_req_lock;
static char s_install_request[32];
static char s_activity[64];

static const char PAGE[] =
"<!doctype html><html lang=en><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>CYD Apps</title><style>"
"body{font:15px system-ui,sans-serif;margin:0;background:#11131a;color:#e8eaf0}"
"main{max-width:640px;margin:auto;padding:16px}h1{font-size:20px;margin:0 0 4px}"
"h2{font-size:15px;margin:24px 0 8px;color:#9aa3b5;text-transform:uppercase;letter-spacing:.05em}"
".muted{color:#9aa3b5;font-size:13px}.card{background:#1c2030;border-radius:8px;padding:12px;margin:8px 0;"
"display:flex;gap:12px;align-items:center}.card div{flex:1;min-width:0}.name{font-weight:600}"
".badge{font-size:11px;padding:2px 6px;border-radius:4px;margin-left:6px;vertical-align:middle}"
".official{background:#0b4f63;color:#7fe3ff}.github{background:#5a3d00;color:#ffc95c}"
"button{font:inherit;border:0;border-radius:6px;padding:8px 12px;background:#00b8d9;color:#000;cursor:pointer}"
"button.rm{background:#3a2a2e;color:#ff8a8a}button:disabled{opacity:.5}"
"form{display:flex;gap:8px}input{flex:1;font:inherit;padding:8px;border-radius:6px;border:1px solid #333a50;"
"background:#0c0e14;color:inherit}#msg{min-height:1.2em;margin-top:8px}.err{color:#ff8a8a}"
"</style></head><body><main><h1>CYD Apps</h1><div class=muted id=info></div><div id=activity class=muted></div>"
"<h2>Available apps</h2><div id=apps class=muted>Loading...</div>"
"<h2>GitHub repositories</h2><div class=muted>Apps from these repositories are not reviewed. They run with full "
"access to the device. Each repository needs a release with <code>cyd-app.json</code> and the app binary.</div>"
"<div id=repos></div><form id=add><input id=repo placeholder='owner/name or https://github.com/owner/name' required>"
"<button>Add</button></form><div id=msg></div></main><script>"
"const $=s=>document.querySelector(s);"
"function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e}"
"async function post(u,b){const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify(b)});const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||r.statusText);return j}"
"function msg(t,e){$('#msg').textContent=t;$('#msg').className=e?'err':''}"
"async function load(){try{const s=await (await fetch('/api/state')).json();"
"$('#info').textContent='Launcher '+s.launcher+(s.error?' \\u2014 last problem: '+s.error:'');"
"$('#activity').textContent=s.activity||'';"
"const a=$('#apps');a.textContent='';a.className='';if(!s.apps.length)a.append(el('div','muted','No apps found yet.'));"
"for(const p of s.apps){const c=el('div','card'),d=el('div');const n=el('span','name',p.name);d.append(n);"
"d.append(el('span','badge '+(p.official?'official':'github'),p.official?'OFFICIAL':'UNREVIEWED'));"
"d.append(el('div','muted',p.version+' \\u00b7 '+p.source+(p.description?' \\u00b7 '+p.description:'')));c.append(d);"
"const st={new:'Download',update:'Update to '+p.version,installed:'Installed'}[p.state];const b=el('button','',st);"
"b.disabled=p.state==='installed';b.onclick=async()=>{try{await post('/api/install',{id:p.id});"
"msg('Downloading '+p.name+' on the device...')}catch(e){msg(e.message,1)}};c.append(b);a.append(c)}"
"const r=$('#repos');r.textContent='';for(const x of s.repos){const c=el('div','card'),d=el('div','',x);"
"const b=el('button','rm','Remove');b.onclick=async()=>{try{await post('/api/repos/remove',{repo:x});msg('Removed '+x);load()}"
"catch(e){msg(e.message,1)}};c.append(d,b);r.append(c)}}catch(e){$('#info').textContent='Device not reachable'}}"
"$('#add').onsubmit=async e=>{e.preventDefault();try{await post('/api/repos',{repo:$('#repo').value});"
"msg('Added. The device is loading it...');$('#repo').value='';load()}catch(err){msg(err.message,1)}};"
"load();setInterval(load,3000);</script></body></html>";

static esp_err_t send_json(httpd_req_t *req, cJSON *root, const char *status)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == NULL) return httpd_resp_send_500(req);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", message);
    return send_json(req, root, status);
}

static esp_err_t handle_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t handle_state(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "launcher", esp_app_get_description()->version);
    xSemaphoreTake(s_req_lock, portMAX_DELAY);
    cJSON_AddStringToObject(root, "activity", s_activity);
    xSemaphoreGive(s_req_lock);

    cJSON *apps = cJSON_AddArrayToObject(root, "apps");
    static const char *states[] = {"new", "update", "installed"};
    store_lock();
    const store_list_t *list = store_list();
    for (int i = 0; i < list->count; ++i) {
        const store_app_t *a = &list->apps[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", a->id);
        cJSON_AddStringToObject(o, "name", a->name);
        cJSON_AddStringToObject(o, "version", a->version);
        cJSON_AddStringToObject(o, "installed_version", a->installed_version);
        cJSON_AddStringToObject(o, "description", a->description);
        cJSON_AddStringToObject(o, "source", a->source);
        cJSON_AddBoolToObject(o, "official", a->official);
        cJSON_AddStringToObject(o, "state", states[a->state]);
        cJSON_AddItemToArray(apps, o);
    }
    if (list->failed_sources > 0) cJSON_AddStringToObject(root, "error", list->last_error);
    store_unlock();

    char repos[STORE_MAX_REPOS][STORE_REPO_LEN];
    int count = store_get_repos(repos, STORE_MAX_REPOS);
    cJSON *arr = cJSON_AddArrayToObject(root, "repos");
    for (int i = 0; i < count; ++i) cJSON_AddItemToArray(arr, cJSON_CreateString(repos[i]));
    return send_json(req, root, "200 OK");
}

/* Reads a small JSON body and returns the string field `key`, or NULL. Caller frees the cJSON. */
static cJSON *read_body(httpd_req_t *req)
{
    char buf[256];
    if (req->content_len <= 0 || req->content_len >= sizeof(buf)) return NULL;
    int got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n <= 0) return NULL;
        got += n;
    }
    buf[got] = '\0';
    return cJSON_Parse(buf);
}

static const char *body_string(cJSON *body, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(body, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static esp_err_t handle_add_repo(httpd_req_t *req)
{
    cJSON *body = read_body(req);
    const char *repo = body_string(body, "repo");
    esp_err_t err = repo ? store_add_repo(repo) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(body);
    if (err == ESP_ERR_INVALID_ARG) return send_error(req, "400 Bad Request", "Enter a repository as owner/name");
    if (err == ESP_ERR_NO_MEM) return send_error(req, "400 Bad Request", "Too many repositories (12 at most)");
    if (err != ESP_OK) return send_error(req, "500 Internal Server Error", "Could not save the repository");
    return send_json(req, cJSON_CreateObject(), "200 OK");
}

static esp_err_t handle_remove_repo(httpd_req_t *req)
{
    cJSON *body = read_body(req);
    const char *repo = body_string(body, "repo");
    esp_err_t err = repo ? store_remove_repo(repo) : ESP_ERR_INVALID_ARG;
    cJSON_Delete(body);
    if (err == ESP_ERR_NOT_FOUND) return send_error(req, "404 Not Found", "Repository not in the list");
    if (err != ESP_OK) return send_error(req, "400 Bad Request", "Could not remove the repository");
    return send_json(req, cJSON_CreateObject(), "200 OK");
}

static esp_err_t handle_install(httpd_req_t *req)
{
    cJSON *body = read_body(req);
    const char *id = body_string(body, "id");
    bool ok = id && store_valid_id(id);
    if (ok) {
        xSemaphoreTake(s_req_lock, portMAX_DELAY);
        snprintf(s_install_request, sizeof(s_install_request), "%s", id);
        xSemaphoreGive(s_req_lock);
    }
    cJSON_Delete(body);
    if (!ok) return send_error(req, "400 Bad Request", "Unknown app");
    return send_json(req, cJSON_CreateObject(), "202 Accepted");
}

esp_err_t web_ui_start(void)
{
    if (s_server) return ESP_OK;
    if (s_req_lock == NULL) s_req_lock = xSemaphoreCreateMutex();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 6;
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web server failed to start: %s", esp_err_to_name(err));
        return err;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_page},
        {.uri = "/api/state", .method = HTTP_GET, .handler = handle_state},
        {.uri = "/api/repos", .method = HTTP_POST, .handler = handle_add_repo},
        {.uri = "/api/repos/remove", .method = HTTP_POST, .handler = handle_remove_repo},
        {.uri = "/api/install", .method = HTTP_POST, .handler = handle_install},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "Web interface started");
    return ESP_OK;
}

void web_ui_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web interface stopped");
    }
}

bool web_ui_take_install_request(char id[32])
{
    if (s_req_lock == NULL) return false;
    xSemaphoreTake(s_req_lock, portMAX_DELAY);
    bool has = s_install_request[0] != '\0';
    if (has) {
        snprintf(id, 32, "%s", s_install_request);
        s_install_request[0] = '\0';
    }
    xSemaphoreGive(s_req_lock);
    return has;
}

void web_ui_set_activity(const char *text)
{
    if (s_req_lock == NULL) return;
    xSemaphoreTake(s_req_lock, portMAX_DELAY);
    snprintf(s_activity, sizeof(s_activity), "%s", text ? text : "");
    xSemaphoreGive(s_req_lock);
}
