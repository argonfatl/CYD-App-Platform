#include "web_setup.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app_paths.h"
#include "cJSON.h"
#include "cyd_platform.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "web_setup";

static httpd_handle_t s_server;
static volatile bool s_saved;

#define MAX_BODY      8192
#define CONFIG_MAX    1024
#define FAVORITES_MAX 3072 /* bytes of the comma-separated NVS string */

static const char PAGE[] =
"<!doctype html><html lang=en><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Satellites settings</title><style>"
"body{font:15px system-ui,sans-serif;margin:0;background:#0d1725;color:#e8eaf0}"
"main{max-width:640px;margin:auto;padding:16px 16px 96px}h1{font-size:20px;margin:0 0 4px}"
"h2{font-size:13px;margin:24px 0 8px;color:#7fe3ff;text-transform:uppercase;letter-spacing:.06em}"
".muted{color:#9aa3b5;font-size:13px}.card{background:#14243a;border-radius:8px;padding:12px;margin:8px 0}"
"label{display:block;margin:8px 0 4px}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}"
".row>*{flex:1;min-width:120px}input,select{font:inherit;padding:8px;border-radius:6px;border:1px solid #2a3c58;"
"background:#081019;color:inherit;box-sizing:border-box;width:100%}input[type=radio],input[type=checkbox]{width:auto}"
".opt{display:flex;gap:8px;align-items:flex-start;margin:6px 0}.opt input{margin-top:3px}"
"button{font:inherit;border:0;border-radius:6px;padding:8px 12px;background:#00b8d9;color:#000;cursor:pointer}"
"button.sec{background:#20344f;color:#e8eaf0}"
"#sats{max-height:320px;overflow:auto;margin-top:8px}#sats label{display:flex;gap:8px;margin:0;padding:6px 4px;"
"border-bottom:1px solid #1b2c44}#sats span.id{margin-left:auto;color:#9aa3b5;font-size:13px}"
"#bar{position:fixed;left:0;right:0;bottom:0;background:#0a1320;border-top:2px solid #00b8d9;padding:12px}"
"#bar div{max-width:640px;margin:auto;display:flex;gap:12px;align-items:center}#msg{flex:1}.err{color:#ff8a8a}"
".ok{color:#7dff9b}</style></head><body><main>"
"<h1>Satellites</h1><div class=muted id=status>Loading...</div>"
"<h2>Orbit data</h2><div class=card>"
"<label for=group>Satellites to track (CelesTrak group)</label><select id=group>"
"<option value=visual>Brightest satellites (visual)</option><option value=stations>Space stations</option>"
"<option value=amateur>Amateur radio</option><option value=weather>Weather</option><option value=noaa>NOAA</option>"
"<option value=science>Science</option><option value=cubesat>CubeSats</option><option value=gps-ops>GPS</option>"
"<option value=galileo>Galileo</option><option value=last-30-days>Launched in the last 30 days</option>"
"<option value=custom>Other group...</option></select>"
"<input id=custom placeholder='CelesTrak group name, e.g. geo' style='display:none;margin-top:8px'>"
"<div class=muted style='margin-top:6px'>At most 220 satellites are loaded; memory usually allows 110-120.</div>"
"<label for=minel>Show passes that rise at least (degrees)</label><input id=minel type=number min=0 max=80 step=1>"
"<div class=opt><input type=checkbox id=refresh><label for=refresh style='margin:0'>Download fresh orbit data "
"on the next start<div class=muted>Normally data is reused for 12 hours. Downloading too often can get the "
"address blocked by CelesTrak.</div></label></div></div>"
"<h2>Location</h2><div class=card>"
"<div class=opt><input type=radio name=loc id=loc_l value=launcher><label for=loc_l style='margin:0'>"
"Same as the launcher<div class=muted id=lloc></div></label></div>"
"<div class=opt><input type=radio name=loc id=loc_c value=custom><label for=loc_c style='margin:0'>"
"Only for Satellites</label></div>"
"<div id=custom_loc><div class=row><input id=q placeholder='Search for a town'><button class=sec id=find>"
"Search</button></div><div id=found></div><div class=row style='margin-top:8px'>"
"<input id=name placeholder='Name shown on screen' maxlength=23></div><div class=row style='margin-top:8px'>"
"<input id=lat placeholder='Latitude' inputmode=decimal><input id=lon placeholder='Longitude' inputmode=decimal>"
"<input id=alt placeholder='Altitude, m' inputmode=numeric></div></div></div>"
"<h2>Time zone</h2><div class=card><select id=tzmode><option value=auto>Automatic (launcher or IP address, "
"follows daylight saving time)</option><option value=fixed>Fixed offset</option></select>"
"<div class=row id=tzfixed style='margin-top:8px'><input id=tz type=number step=15 placeholder='Minutes from UTC, "
"e.g. -300'><button class=sec id=tzbrowser>Use this browser's</button></div></div>"
"<h2>Screen</h2><div class=card><label for=filter>Show on the device</label><select id=filter>"
"<option value=0>All satellites</option><option value=1>ISS only</option><option value=2>Favorites only</option>"
"</select></div>"
"<h2>Favorites</h2><div class=card><div class=muted id=favinfo></div>"
"<input id=search placeholder='Filter by name or number' style='margin-top:8px'><div id=sats></div></div>"
"</main><div id=bar><div><span id=msg class=muted>Changes apply after saving; Satellites restarts.</span>"
"<button id=save>Save</button></div></div><script>"
"const $=s=>document.querySelector(s);let S=null,sats=[],fav=new Set();"
"function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e}"
"function msg(t,c){$('#msg').textContent=t;$('#msg').className=c||'muted'}"
"function show(){$('#custom').style.display=$('#group').value==='custom'?'':'none';"
"$('#custom_loc').style.display=$('#loc_c').checked?'':'none';"
"$('#tzfixed').style.display=$('#tzmode').value==='fixed'?'':'none';$('#loc_l').disabled=!S.launcher_location}"
"function ago(m){return m==null?'unknown':m<60?m+' min ago':m<2880?Math.round(m/60)+' h ago':Math.round(m/1440)+' days ago'}"
"function renderSats(){const q=$('#search').value.toLowerCase(),box=$('#sats');box.textContent='';"
"const known=new Set(sats.map(s=>s[0]));const list=sats.concat([...fav].filter(i=>!known.has(i)).map(i=>[i,'(not in this group)']));"
"list.sort((a,b)=>(fav.has(b[0])-fav.has(a[0]))||a[1].localeCompare(b[1]));"
"for(const [id,n] of list){if(q&&!n.toLowerCase().includes(q)&&!String(id).includes(q))continue;"
"const l=el('label'),c=el('input');c.type='checkbox';c.checked=fav.has(id);"
"c.onchange=()=>{c.checked?fav.add(id):fav.delete(id);favinfo()};l.append(c,el('span','',n),el('span','id','#'+id));box.append(l)}"
"if(!list.length)box.append(el('div','muted','No orbit data on the SD card yet. Start Satellites once to download it.'))}"
"function favinfo(){$('#favinfo').textContent=fav.size+' selected. Favorites are marked on the device and can be shown on their own.'}"
"async function load(){S=await (await fetch('/api/settings')).json();"
"const g=$('#group');if([...g.options].some(o=>o.value===S.group))g.value=S.group;else{g.value='custom';$('#custom').value=S.group}"
"$('#minel').value=S.min_elevation;$('#filter').value=S.filter;fav=new Set(S.favorites);"
"const L=S.launcher_location;$('#lloc').textContent=L?L.name+' \\u00b7 '+L.lat.toFixed(4)+', '+L.lon.toFixed(4):"
"'Not set. Set it on the launcher\\'s GET APPS page, or choose a location only for Satellites.';"
"const c=S.location.custom||!L;$(c?'#loc_c':'#loc_l').checked=true;if(S.location.custom){$('#name').value=S.location.name;"
"$('#lat').value=S.location.lat;$('#lon').value=S.location.lon;$('#alt').value=S.location.alt_m}"
"$('#tzmode').value=S.utc_offset_min==null?'auto':'fixed';$('#tz').value=S.utc_offset_min==null?-new Date().getTimezoneOffset():S.utc_offset_min;"
"$('#status').textContent=(S.sd?'':'No SD card: only the screen filter and favorites can be saved. ')+"
"'Orbit data for '+S.group+': '+(S.cache.count?S.cache.count+' satellites, saved '+ago(S.cache.age_min):'none on the SD card')+"
"'. Last download attempt: '+ago(S.last_try_min)+'.';show();favinfo();"
"try{sats=await (await fetch('/api/sats')).json()}catch(e){sats=[]}renderSats()}"
"for(const id of ['#group','#tzmode','#loc_l','#loc_c'])$(id).onchange=show;$('#search').oninput=renderSats;"
"$('#tzbrowser').onclick=()=>{$('#tz').value=-new Date().getTimezoneOffset()};"
"$('#find').onclick=async()=>{const f=$('#found');f.textContent='Searching...';try{"
"const r=await (await fetch('https://geocoding-api.open-meteo.com/v1/search?count=5&name='+encodeURIComponent($('#q').value))).json();"
"f.textContent='';if(!r.results||!r.results.length){f.textContent='Nothing found';return}"
"for(const p of r.results){const b=el('button','sec',p.name+', '+[p.admin1,p.country].filter(Boolean).join(', '));"
"b.style.cssText='display:block;width:100%;text-align:left;margin-top:6px';b.onclick=()=>{$('#name').value=p.name.toUpperCase().slice(0,23);"
"$('#lat').value=p.latitude.toFixed(4);$('#lon').value=p.longitude.toFixed(4);$('#alt').value=Math.round(p.elevation||0);f.textContent=''};f.append(b)}}"
"catch(e){f.textContent='Search needs internet access in this browser'}};"
"$('#save').onclick=async()=>{const g=$('#group').value==='custom'?$('#custom').value.trim():$('#group').value;"
"const b={group:g,min_elevation:parseFloat($('#minel').value),filter:+$('#filter').value,favorites:[...fav],"
"refresh:$('#refresh').checked,utc_offset_min:$('#tzmode').value==='fixed'?parseInt($('#tz').value):null,"
"location:{custom:$('#loc_c').checked,name:$('#name').value.trim(),lat:parseFloat($('#lat').value),"
"lon:parseFloat($('#lon').value),alt_m:parseInt($('#alt').value)||0}};"
"$('#save').disabled=true;msg('Saving...');try{const r=await fetch('/api/settings',{method:'POST',"
"headers:{'Content-Type':'application/json'},body:JSON.stringify(b)});const j=await r.json().catch(()=>({}));"
"if(!r.ok)throw new Error(j.error||r.statusText);msg('Saved. Satellites is restarting with the new settings.','ok')}"
"catch(e){msg(e.message,'err');$('#save').disabled=false}};"
"load().catch(e=>msg('Could not load the settings: '+e.message,'err'));"
"</script></body></html>";

/* ---------- Helpers ---------- */

static bool sd_present(void)
{
    struct stat st;
    return stat(CYD_SD_MOUNT_POINT, &st) == 0;
}

static cJSON *read_config(void)
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) return cJSON_CreateObject();
    char text[CONFIG_MAX];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    cJSON *root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return cJSON_CreateObject();
    }
    return root;
}

static double num(const cJSON *o, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static const char *str(const cJSON *o, const char *key, const char *def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}

static bool valid_group(const char *g)
{
    size_t n = strlen(g);
    if (n == 0 || n > 23) return false;
    for (size_t i = 0; i < n; ++i) {
        if (!(islower((unsigned char)g[i]) || isdigit((unsigned char)g[i]) || g[i] == '-' || g[i] == '_')) return false;
    }
    return true;
}

static void cache_path(const char *group, char *out, size_t len)
{
    snprintf(out, len, APP_DIR "/%s.tle", group);
}

static void tle_key(const char *group, char *out, size_t len)
{
    snprintf(out, len, "tle_%.10s", group);
}

/* Minutes since an NVS timestamp, or -1 if unknown (no value, or the clock is not set). */
static long minutes_since(nvs_handle_t h, const char *key)
{
    int64_t t = 0;
    time_t now = time(NULL);
    if (nvs_get_i64(h, key, &t) != ESP_OK || t <= 0 || now < 1700000000 || now < t) return -1;
    return (long)((now - t) / 60);
}

static void add_minutes(cJSON *o, const char *key, long m)
{
    if (m < 0) cJSON_AddNullToObject(o, key);
    else cJSON_AddNumberToObject(o, key, (double)m);
}

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

/* ---------- Handlers ---------- */

static esp_err_t handle_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

static esp_err_t handle_get_settings(httpd_req_t *req)
{
    cJSON *cfg = read_config();
    const char *group = str(cfg, "group", "visual");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "sd", sd_present());
    cJSON_AddStringToObject(root, "group", group);
    cJSON_AddNumberToObject(root, "min_elevation", num(cfg, "min_elevation", 10));

    cJSON *loc = cJSON_AddObjectToObject(root, "location");
    double lat = num(cfg, "lat", NAN), lon = num(cfg, "lon", NAN);
    bool custom = !isnan(lat) && !isnan(lon);
    cJSON_AddBoolToObject(loc, "custom", custom);
    if (custom) {
        cJSON_AddNumberToObject(loc, "lat", lat);
        cJSON_AddNumberToObject(loc, "lon", lon);
        cJSON_AddNumberToObject(loc, "alt_m", num(cfg, "alt_m", 0));
        cJSON_AddStringToObject(loc, "name", str(cfg, "name", ""));
    }
    cyd_location_t shared;
    if (cyd_platform_get_location(&shared)) {
        cJSON *l = cJSON_AddObjectToObject(root, "launcher_location");
        cJSON_AddStringToObject(l, "name", shared.name);
        cJSON_AddNumberToObject(l, "lat", shared.lat_deg);
        cJSON_AddNumberToObject(l, "lon", shared.lon_deg);
    } else {
        cJSON_AddNullToObject(root, "launcher_location");
    }
    const cJSON *off = cJSON_GetObjectItemCaseSensitive(cfg, "utc_offset_min");
    if (cJSON_IsNumber(off)) cJSON_AddNumberToObject(root, "utc_offset_min", off->valuedouble);
    else cJSON_AddNullToObject(root, "utc_offset_min");

    /* Settings kept in NVS. */
    uint8_t filter = 0;
    long age = -1, tried = -1;
    cJSON *favs = cJSON_AddArrayToObject(root, "favorites");
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "sat_filter", &filter);
        char *csv = (char *)malloc(FAVORITES_MAX);
        size_t len = FAVORITES_MAX;
        if (csv && nvs_get_str(h, "favorites", csv, &len) == ESP_OK) {
            for (char *p = csv; *p;) {
                char *end = NULL;
                long id = strtol(p, &end, 10);
                if (end == p) break;
                if (id > 0) cJSON_AddItemToArray(favs, cJSON_CreateNumber((double)id));
                p = (*end == ',') ? end + 1 : end;
            }
        }
        free(csv);
        char key[16];
        tle_key(group, key, sizeof(key));
        age = minutes_since(h, key);
        tried = minutes_since(h, "tle_try");
        nvs_close(h);
    }
    cJSON_AddNumberToObject(root, "filter", filter < 3 ? filter : 0);

    /* How many satellites the cache for this group holds. */
    int count = 0;
    char path[64], line[96];
    cache_path(group, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '1' && line[1] == ' ') count++;
        }
        fclose(f);
    }
    cJSON *cache = cJSON_AddObjectToObject(root, "cache");
    cJSON_AddNumberToObject(cache, "count", count);
    add_minutes(cache, "age_min", count ? age : -1);
    add_minutes(root, "last_try_min", tried);
    cJSON_Delete(cfg);
    return send_json(req, root, "200 OK");
}

/* Streams [[catalog number, "name"], ...] from the cache file of the configured group. */
static esp_err_t handle_sats(httpd_req_t *req)
{
    cJSON *cfg = read_config();
    char path[64];
    cache_path(str(cfg, "group", "visual"), path, sizeof(path));
    cJSON_Delete(cfg);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send_chunk(req, "[", 1);
    FILE *f = fopen(path, "r");
    if (f) {
        char prev[96] = "", line[96], out[128];
        bool first = true;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '1' && line[1] == ' ' && strlen(line) > 7) {
                char num_s[6];
                memcpy(num_s, line + 2, 5);
                num_s[5] = '\0';
                int id = atoi(num_s);
                /* Name from the line before, trimmed and with characters JSON cannot hold as-is removed. */
                char name[32];
                size_t n = 0;
                const char *p = prev;
                if (p[0] == '0' && p[1] == ' ') p += 2;
                for (; *p && n < sizeof(name) - 1; ++p) {
                    if (*p == '"' || *p == '\\' || (unsigned char)*p < 32) continue;
                    name[n++] = *p;
                }
                while (n > 0 && name[n - 1] == ' ') n--;
                name[n] = '\0';
                int len = snprintf(out, sizeof(out), "%s[%d,\"%s\"]", first ? "" : ",", id, n ? name : num_s);
                if (httpd_resp_send_chunk(req, out, len) != ESP_OK) break;
                first = false;
                prev[0] = '\0';
            } else if (!(line[0] == '2' && line[1] == ' ')) {
                snprintf(prev, sizeof(prev), "%s", line);
            }
        }
        fclose(f);
    }
    httpd_resp_send_chunk(req, "]", 1);
    return httpd_resp_send_chunk(req, NULL, 0);
}

static cJSON *read_body(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > MAX_BODY) return NULL;
    char *buf = (char *)malloc(req->content_len + 1);
    if (buf == NULL) return NULL;
    int got = 0;
    while (got < (int)req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) {
            free(buf);
            return NULL;
        }
        got += n;
    }
    buf[got] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static esp_err_t handle_post_settings(httpd_req_t *req)
{
    cJSON *body = read_body(req);
    if (!cJSON_IsObject(body)) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", "Invalid request");
    }

    /* Validate everything before changing anything. */
    const char *group = str(body, "group", "");
    double min_el = num(body, "min_elevation", NAN);
    const cJSON *loc = cJSON_GetObjectItemCaseSensitive(body, "location");
    bool custom = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(loc, "custom"));
    double lat = num(loc, "lat", NAN), lon = num(loc, "lon", NAN), alt = num(loc, "alt_m", 0);
    const char *name = str(loc, "name", "");
    const cJSON *off = cJSON_GetObjectItemCaseSensitive(body, "utc_offset_min");
    double filter = num(body, "filter", 0);
    const cJSON *favs = cJSON_GetObjectItemCaseSensitive(body, "favorites");
    const char *error = NULL;
    if (!valid_group(group)) error = "The group name may only use a-z, 0-9, - and _ (up to 23 characters)";
    else if (isnan(min_el) || min_el < 0 || min_el > 80) error = "Minimum elevation must be 0 to 80 degrees";
    else if (custom && (isnan(lat) || isnan(lon) || fabs(lat) > 90 || fabs(lon) > 180))
        error = "Latitude must be -90 to 90 and longitude -180 to 180";
    else if (custom && (alt < -500 || alt > 9000)) error = "Altitude must be -500 to 9000 m";
    else if (cJSON_IsNumber(off) && fabs(off->valuedouble) > 14 * 60) error = "The UTC offset must be -840 to 840 minutes";
    else if (!cJSON_IsNull(off) && !cJSON_IsNumber(off)) error = "Invalid UTC offset";
    else if (filter < 0 || filter > 2) error = "Invalid filter";
    else if (favs && !cJSON_IsArray(favs)) error = "Invalid favorites";
    else if (!sd_present() && (custom || cJSON_IsNumber(off) || strcmp(group, "visual") != 0 || min_el != 10))
        error = "No SD card: these settings are stored on the SD card";
    if (error) {
        cJSON_Delete(body);
        return send_error(req, "400 Bad Request", error);
    }

    /* config.json on the SD card. Unknown fields are kept. */
    if (sd_present()) {
        cJSON *cfg = read_config();
        const char *drop[] = {"group", "min_elevation", "lat", "lon", "alt_m", "name", "utc_offset_min"};
        for (const char *k : drop) cJSON_DeleteItemFromObjectCaseSensitive(cfg, k);
        cJSON_AddStringToObject(cfg, "group", group);
        cJSON_AddNumberToObject(cfg, "min_elevation", round(min_el));
        if (custom) {
            cJSON_AddNumberToObject(cfg, "lat", round(lat * 1e5) / 1e5);
            cJSON_AddNumberToObject(cfg, "lon", round(lon * 1e5) / 1e5);
            cJSON_AddNumberToObject(cfg, "alt_m", round(alt));
            char upper[24];
            size_t n = 0;
            for (const char *p = name; *p && n < sizeof(upper) - 1; ++p) upper[n++] = (char)toupper((unsigned char)*p);
            upper[n] = '\0';
            if (n) cJSON_AddStringToObject(cfg, "name", upper);
        }
        if (cJSON_IsNumber(off)) cJSON_AddNumberToObject(cfg, "utc_offset_min", round(off->valuedouble));
        char *text = cJSON_Print(cfg);
        cJSON_Delete(cfg);
        mkdir(CYD_SD_MOUNT_POINT "/apps", 0775);
        mkdir(APP_DIR, 0775);
        FILE *f = text && strlen(text) < CONFIG_MAX ? fopen(CONFIG_PATH, "w") : NULL;
        bool ok = f && fputs(text, f) >= 0;
        if (f) ok = (fclose(f) == 0) && ok;
        free(text);
        if (!ok) {
            cJSON_Delete(body);
            return send_error(req, "500 Internal Server Error", "Could not write config.json on the SD card");
        }
    }

    /* Filter and favorites in NVS. */
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        cJSON_Delete(body);
        return send_error(req, "500 Internal Server Error", "Could not open the settings storage");
    }
    nvs_set_u8(h, "sat_filter", (uint8_t)filter);
    char *csv = (char *)calloc(1, FAVORITES_MAX);
    if (csv) {
        size_t used = 0;
        const cJSON *v;
        cJSON_ArrayForEach(v, favs)
        {
            if (!cJSON_IsNumber(v) || v->valuedouble < 1 || v->valuedouble > 999999) continue;
            int n = snprintf(csv + used, FAVORITES_MAX - used, "%s%d", used ? "," : "", (int)v->valuedouble);
            if (n < 0 || (size_t)n >= FAVORITES_MAX - used) break;
            used += (size_t)n;
        }
        nvs_set_str(h, "favorites", csv);
        free(csv);
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "refresh"))) {
        char key[16];
        tle_key(group, key, sizeof(key));
        nvs_erase_key(h, key);
        nvs_erase_key(h, "tle_try");
    }
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    cJSON_Delete(body);
    if (err != ESP_OK) return send_error(req, "500 Internal Server Error", "Could not save the settings");

    ESP_LOGI(TAG, "Settings saved");
    s_saved = true;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    return send_json(req, root, "200 OK");
}

/* ---------- Server ---------- */

esp_err_t web_setup_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) return err;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_page, .user_ctx = NULL},
        {.uri = "/api/settings", .method = HTTP_GET, .handler = handle_get_settings, .user_ctx = NULL},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = handle_post_settings, .user_ctx = NULL},
        {.uri = "/api/sats", .method = HTTP_GET, .handler = handle_sats, .user_ctx = NULL},
    };
    for (const httpd_uri_t &r : routes) httpd_register_uri_handler(s_server, &r);
    ESP_LOGI(TAG, "Settings page started");
    return ESP_OK;
}

bool web_setup_saved(void)
{
    return s_saved;
}
