#include "app_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cJSON.h"
#include "cyd_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "http_util.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#define APPS_DIR        CYD_SD_MOUNT_POINT "/apps"
#define CATALOG_URL     "https://github.com/" CONFIG_CYD_UPDATE_REPO "/releases/latest/download/catalog.json"
#define CATALOG_MAX     12288
#define APP_JSON_MAX    2048
#define NVS_NAMESPACE   "cyd_store"

static const char *TAG = "app_store";

static store_list_t s_list;
static SemaphoreHandle_t s_lock;
static volatile bool s_repos_changed;

/* ---------- Shared list ---------- */

store_list_t *store_list(void)
{
    return &s_list;
}

void store_init(void)
{
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
}

void store_lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

void store_unlock(void)
{
    xSemaphoreGive(s_lock);
}

bool store_repos_changed(void)
{
    return s_repos_changed;
}

/* ---------- Validation ---------- */

bool store_valid_id(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n > 31) return false;
    for (const char *p = id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
            return false;
        }
    }
    return true;
}

static bool valid_repo_part(const char *s, size_t n)
{
    if (n == 0 || n > 39) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return !(n == 1 && s[0] == '.') && !(n == 2 && s[0] == '.' && s[1] == '.');
}

/* Turns "https://github.com/owner/name(.git)(/)" or "owner/name" into "owner/name". */
static bool normalize_repo(const char *in, char out[STORE_REPO_LEN])
{
    while (*in == ' ') in++;
    const char *prefixes[] = {"https://github.com/", "http://github.com/", "github.com/"};
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t n = strlen(prefixes[i]);
        if (strncasecmp(in, prefixes[i], n) == 0) {
            in += n;
            break;
        }
    }
    char tmp[STORE_REPO_LEN];
    snprintf(tmp, sizeof(tmp), "%s", in);
    size_t len = strlen(tmp);
    while (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == ' ')) tmp[--len] = '\0';
    if (len > 4 && strcmp(tmp + len - 4, ".git") == 0) tmp[len -= 4] = '\0';

    char *slash = strchr(tmp, '/');
    if (slash == NULL || strchr(slash + 1, '/') != NULL) return false;
    if (!valid_repo_part(tmp, slash - tmp) || !valid_repo_part(slash + 1, strlen(slash + 1))) return false;
    snprintf(out, STORE_REPO_LEN, "%s", tmp);
    return true;
}

/* ---------- User repositories ---------- */

/* Stored as one NVS string with one "owner/name" per line. */
int store_get_repos(char repos[][STORE_REPO_LEN], int max)
{
    char text[STORE_MAX_REPOS * STORE_REPO_LEN] = "";
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(text);
        if (nvs_get_str(h, "repos", text, &len) != ESP_OK) text[0] = '\0';
        nvs_close(h);
    }
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line && count < max; line = strtok_r(NULL, "\n", &save)) {
        snprintf(repos[count++], STORE_REPO_LEN, "%s", line);
    }
    return count;
}

static esp_err_t save_repos(char repos[][STORE_REPO_LEN], int count)
{
    char text[STORE_MAX_REPOS * STORE_REPO_LEN] = "";
    for (int i = 0; i < count; ++i) {
        strlcat(text, repos[i], sizeof(text));
        if (i + 1 < count) strlcat(text, "\n", sizeof(text));
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "repos", text);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) s_repos_changed = true;
    return err;
}

esp_err_t store_add_repo(const char *repo)
{
    char name[STORE_REPO_LEN];
    if (!normalize_repo(repo, name)) return ESP_ERR_INVALID_ARG;

    char repos[STORE_MAX_REPOS][STORE_REPO_LEN];
    int count = store_get_repos(repos, STORE_MAX_REPOS);
    for (int i = 0; i < count; ++i) {
        if (strcasecmp(repos[i], name) == 0) return ESP_OK;
    }
    if (count >= STORE_MAX_REPOS) return ESP_ERR_NO_MEM;
    snprintf(repos[count++], STORE_REPO_LEN, "%s", name);
    ESP_LOGI(TAG, "Added repository %s", name);
    return save_repos(repos, count);
}

esp_err_t store_remove_repo(const char *repo)
{
    char name[STORE_REPO_LEN];
    if (!normalize_repo(repo, name)) return ESP_ERR_INVALID_ARG;

    char repos[STORE_MAX_REPOS][STORE_REPO_LEN];
    int count = store_get_repos(repos, STORE_MAX_REPOS);
    int out = 0;
    for (int i = 0; i < count; ++i) {
        if (strcasecmp(repos[i], name) != 0) {
            if (out != i) memcpy(repos[out], repos[i], STORE_REPO_LEN);
            out++;
        }
    }
    if (out == count) return ESP_ERR_NOT_FOUND;
    ESP_LOGI(TAG, "Removed repository %s", name);
    return save_repos(repos, out);
}

/* ---------- Building the list ---------- */

static void copy_str(const cJSON *obj, const char *key, char *dst, size_t len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, len, "%s", item->valuestring);
    }
}

static bool is_hex_sha256(char *s)
{
    if (strlen(s) != 64) return false;
    for (char *p = s; *p; ++p) {
        if (*p >= 'A' && *p <= 'F') *p = (char)(*p - 'A' + 'a');
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    }
    return true;
}

static bool is_https_url(const char *url)
{
    return strncmp(url, "https://", 8) == 0 && strlen(url) > 8;
}

/* Fills an entry from a catalog item or cyd-app.json; returns false if anything is missing or invalid. */
static bool parse_entry(const cJSON *obj, store_app_t *app)
{
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(obj, "size");
    copy_str(obj, "id", app->id, sizeof(app->id));
    copy_str(obj, "name", app->name, sizeof(app->name));
    copy_str(obj, "version", app->version, sizeof(app->version));
    copy_str(obj, "description", app->description, sizeof(app->description));
    copy_str(obj, "sha256", app->sha256, sizeof(app->sha256));
    if (app->name[0] == '\0') snprintf(app->name, sizeof(app->name), "%s", app->id);
    if (!cJSON_IsNumber(size) || size->valuedouble <= 0 || size->valuedouble > 4 * 1024 * 1024) return false;
    app->size = (uint32_t)size->valuedouble;
    return store_valid_id(app->id) && app->version[0] && is_hex_sha256(app->sha256);
}

/* Adds an app unless one with the same id is already listed (official entries come first). */
static void add_app(store_list_t *list, const store_app_t *app)
{
    for (int i = 0; i < list->count; ++i) {
        if (strcmp(list->apps[i].id, app->id) == 0) {
            ESP_LOGW(TAG, "Skipping %s from %s: id already provided by %s", app->id, app->source,
                     list->apps[i].source);
            return;
        }
    }
    if (list->count < STORE_MAX_APPS) {
        list->apps[list->count++] = *app;
    }
}

static void note_error(store_list_t *list, const char *what)
{
    list->failed_sources++;
    snprintf(list->last_error, sizeof(list->last_error), "%.24s: %.20s", what, http_last_error());
}

static void load_catalog(store_list_t *list, char *buf)
{
    if (http_get_text(CATALOG_URL, buf, CATALOG_MAX) != ESP_OK) {
        note_error(list, "CATALOG");
        return;
    }
    cJSON *root = cJSON_Parse(buf);
    const cJSON *apps = cJSON_GetObjectItemCaseSensitive(root, "apps");
    const cJSON *item;
    cJSON_ArrayForEach(item, apps) {
        store_app_t app = {0};
        copy_str(item, "url", app.url, sizeof(app.url));
        if (parse_entry(item, &app) && is_https_url(app.url)) {
            app.official = true;
            snprintf(app.source, sizeof(app.source), "official");
            add_app(list, &app);
        } else {
            ESP_LOGW(TAG, "Ignoring invalid catalog entry");
        }
    }
    if (root == NULL) {
        list->failed_sources++;
        snprintf(list->last_error, sizeof(list->last_error), "CATALOG: INVALID JSON");
    }
    cJSON_Delete(root);
}

static void load_repo(store_list_t *list, const char *repo, char *buf)
{
    char url[160];
    snprintf(url, sizeof(url), "https://github.com/%s/releases/latest/download/cyd-app.json", repo);
    if (http_get_text(url, buf, APP_JSON_MAX) != ESP_OK) {
        note_error(list, repo);
        return;
    }
    cJSON *root = cJSON_Parse(buf);
    store_app_t app = {0};
    char binary[64] = "app.bin";
    copy_str(root, "binary", binary, sizeof(binary));
    if (root && parse_entry(root, &app) && strchr(binary, '/') == NULL && strchr(binary, '?') == NULL) {
        snprintf(app.url, sizeof(app.url), "https://github.com/%s/releases/latest/download/%s", repo, binary);
        snprintf(app.source, sizeof(app.source), "github:%s", repo);
        add_app(list, &app);
    } else {
        list->failed_sources++;
        snprintf(list->last_error, sizeof(list->last_error), "%.24s: BAD CYD-APP.JSON", repo);
    }
    cJSON_Delete(root);
}

static void read_installed_version(const char *id, char *version, size_t len)
{
    char path[96], text[1024];
    version[0] = '\0';
    snprintf(path, sizeof(path), APPS_DIR "/%s/manifest.json", id);
    FILE *f = fopen(path, "r");
    if (f == NULL) return;
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    cJSON *root = cJSON_Parse(text);
    copy_str(root, "version", version, len);
    cJSON_Delete(root);

    snprintf(path, sizeof(path), APPS_DIR "/%s/app.bin", id);
    struct stat st;
    if (stat(path, &st) != 0) version[0] = '\0'; /* manifest without binary counts as missing */
}

esp_err_t store_refresh(store_progress_t progress)
{
    s_repos_changed = false;
    store_list_t *fresh = calloc(1, sizeof(store_list_t));
    char *buf = malloc(CATALOG_MAX);
    if (fresh == NULL || buf == NULL) {
        free(fresh);
        free(buf);
        return ESP_ERR_NO_MEM;
    }

    char repos[STORE_MAX_REPOS][STORE_REPO_LEN];
    int repo_count = store_get_repos(repos, STORE_MAX_REPOS);
    int steps = repo_count + 1;

    if (progress) progress("LOADING CATALOG", 0);
    load_catalog(fresh, buf);
    for (int i = 0; i < repo_count; ++i) {
        if (progress) progress(NULL, (i + 1) * 100 / steps);
        load_repo(fresh, repos[i], buf);
    }
    free(buf);

    for (int i = 0; i < fresh->count; ++i) {
        store_app_t *app = &fresh->apps[i];
        read_installed_version(app->id, app->installed_version, sizeof(app->installed_version));
        if (app->installed_version[0] == '\0') {
            app->state = STORE_NEW;
        } else if (strcmp(app->installed_version, app->version) == 0) {
            app->state = STORE_INSTALLED;
        } else {
            app->state = STORE_UPDATE;
        }
    }

    store_lock();
    s_list = *fresh;
    store_unlock();
    free(fresh);
    ESP_LOGI(TAG, "%d apps available, %d sources failed", s_list.count, s_list.failed_sources);
    return ESP_OK;
}

/* ---------- Installing to the SD card ---------- */

typedef struct {
    FILE *file;
    mbedtls_sha256_context sha;
    uint32_t written;
    uint32_t expected;
    int last_pct;
    store_progress_t progress;
} download_t;

static esp_err_t save_chunk(const void *data, int len, void *ctx)
{
    download_t *d = ctx;
    if (d->written + (uint32_t)len > d->expected) return ESP_ERR_INVALID_SIZE;
    if (fwrite(data, 1, len, d->file) != (size_t)len) return ESP_FAIL;
    mbedtls_sha256_update(&d->sha, data, len);
    d->written += len;
    int pct = (int)((uint64_t)d->written * 100 / d->expected) / 2 * 2;
    if (pct != d->last_pct && d->progress) {
        d->last_pct = pct;
        d->progress(NULL, pct);
    }
    return ESP_OK;
}

static esp_err_t write_manifest(const store_app_t *app, const char *path)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", app->id);
    cJSON_AddStringToObject(root, "name", app->name);
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "binary", "app.bin");
    cJSON_AddStringToObject(root, "sha256", app->sha256);
    if (app->description[0]) cJSON_AddStringToObject(root, "description", app->description);
    cJSON_AddStringToObject(root, "source", app->source);
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_FAIL;
    FILE *f = fopen(path, "w");
    if (f) {
        if (fputs(text, f) >= 0) err = ESP_OK;
        fclose(f);
    }
    free(text);
    return err;
}

esp_err_t store_install(const store_app_t *app, store_progress_t progress)
{
    if (!store_valid_id(app->id)) return ESP_ERR_INVALID_ARG;

    char dir[64], part[96], bin[96], manifest[96];
    snprintf(dir, sizeof(dir), APPS_DIR "/%s", app->id);
    snprintf(part, sizeof(part), "%s/app.bin.part", dir);
    snprintf(bin, sizeof(bin), "%s/app.bin", dir);
    snprintf(manifest, sizeof(manifest), "%s/manifest.json", dir);
    mkdir(APPS_DIR, 0775);
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Cannot create %s", dir);
        return ESP_FAIL;
    }

    download_t *d = calloc(1, sizeof(download_t));
    if (d == NULL) return ESP_ERR_NO_MEM;
    d->file = fopen(part, "wb");
    if (d->file == NULL) {
        free(d);
        return ESP_FAIL;
    }
    d->expected = app->size;
    d->last_pct = -1;
    d->progress = progress;
    mbedtls_sha256_init(&d->sha);
    mbedtls_sha256_starts(&d->sha, 0);

    esp_err_t err = http_get(app->url, save_chunk, d);
    fclose(d->file);
    if (err == ESP_OK && d->written != d->expected) err = ESP_ERR_INVALID_SIZE;
    if (err == ESP_OK) {
        uint8_t digest[32];
        char hex[65];
        mbedtls_sha256_finish(&d->sha, digest);
        for (int i = 0; i < 32; ++i) sprintf(hex + i * 2, "%02x", digest[i]);
        if (strcmp(hex, app->sha256) != 0) {
            ESP_LOGE(TAG, "%s: SHA-256 mismatch", app->id);
            err = ESP_ERR_INVALID_CRC;
        }
    }
    mbedtls_sha256_free(&d->sha);
    free(d);

    if (err != ESP_OK) {
        unlink(part);
        return err;
    }
    unlink(bin); /* FAT cannot rename over an existing file */
    if (rename(part, bin) != 0) {
        unlink(part);
        return ESP_FAIL;
    }
    err = write_manifest(app, manifest);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Installed %s %s from %s", app->id, app->version, app->source);
    }
    return err;
}
