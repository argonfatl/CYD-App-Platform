#include "self_update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "spi_flash_mmap.h"

#define RELEASE_BASE    "https://github.com/" CONFIG_CYD_UPDATE_REPO "/releases"
#define MANIFEST_URL    RELEASE_BASE "/latest/download/cyd-launcher.json"
#define MANIFEST_MAX    1024
#define COPY_CHUNK      4096

static const char *TAG = "self_update";

/* ---------- HTTP ---------- */

typedef esp_err_t (*body_cb_t)(const void *data, int len, void *ctx);

typedef struct {
    body_cb_t on_body;
    void *ctx;
    esp_err_t error;
} http_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = evt->user_data;
    /* Redirect responses (GitHub -> asset CDN) also carry bodies; ignore them. */
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx->error == ESP_OK &&
        esp_http_client_get_status_code(evt->client) == 200) {
        ctx->error = ctx->on_body(evt->data, evt->data_len, ctx->ctx);
    }
    return ESP_OK;
}

static esp_err_t http_get(const char *url, body_cb_t on_body, void *body_ctx)
{
    http_ctx_t ctx = {.on_body = on_body, .ctx = body_ctx, .error = ESP_OK};
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_event,
        .user_data = &ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,     /* GitHub's redirect headers are long */
        .buffer_size_tx = 2048,
        .timeout_ms = 15000,
        .max_redirection_count = 5,
        .user_agent = "cyd-app-platform-launcher",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s returned HTTP %d", url, status);
        return status == 404 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    return ctx.error;
}

/* ---------- Release manifest ---------- */

typedef struct {
    char text[MANIFEST_MAX];
    int len;
} text_buf_t;

static esp_err_t collect_text(const void *data, int len, void *ctx)
{
    text_buf_t *buf = ctx;
    if (buf->len + len >= (int)sizeof(buf->text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(buf->text + buf->len, data, len);
    buf->len += len;
    buf->text[buf->len] = '\0';
    return ESP_OK;
}

static bool is_hex_sha256(const char *s)
{
    if (strlen(s) != 64) return false;
    for (const char *p = s; *p; ++p) {
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    }
    return true;
}

esp_err_t self_update_check(launcher_release_t *out)
{
    text_buf_t *buf = calloc(1, sizeof(text_buf_t));
    if (buf == NULL) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get(MANIFEST_URL, collect_text, buf);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    cJSON *root = cJSON_Parse(buf->text);
    free(buf);
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    err = ESP_ERR_INVALID_RESPONSE;
    if (cJSON_IsString(version) && cJSON_IsString(sha) && cJSON_IsNumber(size) &&
        strlen(version->valuestring) < sizeof(out->version) && is_hex_sha256(sha->valuestring) &&
        size->valuedouble > 0) {
        memset(out, 0, sizeof(*out));
        snprintf(out->version, sizeof(out->version), "%s", version->valuestring);
        snprintf(out->sha256, sizeof(out->sha256), "%s", sha->valuestring);
        out->size = (uint32_t)size->valuedouble;
        err = ESP_OK;
    }
    cJSON_Delete(root);
    return err;
}

/* ---------- Version comparison ---------- */

static bool parse_version(const char *s, int v[3])
{
    if (*s == 'v' || *s == 'V') s++;
    char extra;
    int n = sscanf(s, "%d.%d.%d%c", &v[0], &v[1], &v[2], &extra);
    return n == 3; /* exactly X.Y.Z: suffixes like -3-gabc mean a development build */
}

bool self_update_is_newer(const char *remote, const char *local)
{
    int r[3], l[3];
    if (!parse_version(remote, r)) return false;
    if (!parse_version(local, l)) return true;
    for (int i = 0; i < 3; ++i) {
        if (r[i] != l[i]) return r[i] > l[i];
    }
    return false;
}

/* ---------- Download into ota_0 ---------- */

typedef struct {
    esp_ota_handle_t ota;
    mbedtls_sha256_context sha;
    uint32_t written;
    uint32_t expected;
    int last_pct;
    self_update_progress_t progress;
} download_t;

static esp_err_t write_chunk(const void *data, int len, void *ctx)
{
    download_t *d = ctx;
    if (d->written + (uint32_t)len > d->expected) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = esp_ota_write(d->ota, data, len);
    if (err != ESP_OK) return err;
    mbedtls_sha256_update(&d->sha, data, len);
    d->written += len;

    /* Redraw every 2% to keep the download fast. */
    int pct = (int)((uint64_t)d->written * 100 / d->expected) / 2 * 2;
    if (pct != d->last_pct && d->progress) {
        d->last_pct = pct;
        d->progress(NULL, pct);
    }
    return ESP_OK;
}

static void to_hex(const uint8_t *digest, char *out)
{
    for (int i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", digest[i]);
}

esp_err_t self_update_download(const launcher_release_t *release, self_update_progress_t progress)
{
    const esp_partition_t *slot = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (slot == NULL || factory == NULL) return ESP_ERR_NOT_FOUND;
    if (release->size > factory->size) {
        ESP_LOGE(TAG, "Release is %" PRIu32 " bytes; factory holds %" PRIu32, release->size, factory->size);
        return ESP_ERR_INVALID_SIZE;
    }

    char url[192];
    snprintf(url, sizeof(url), RELEASE_BASE "/download/%s/cyd-launcher.bin", release->version);

    download_t *d = calloc(1, sizeof(download_t));
    if (d == NULL) return ESP_ERR_NO_MEM;
    d->expected = release->size;
    d->last_pct = -1;
    d->progress = progress;
    mbedtls_sha256_init(&d->sha);
    mbedtls_sha256_starts(&d->sha, 0);

    esp_err_t err = esp_ota_begin(slot, release->size, &d->ota);
    if (err == ESP_OK) {
        err = http_get(url, write_chunk, d);
        if (err == ESP_OK && d->written != d->expected) {
            err = ESP_ERR_INVALID_SIZE;
        }
        if (err == ESP_OK) {
            uint8_t digest[32];
            char hex[65];
            mbedtls_sha256_finish(&d->sha, digest);
            to_hex(digest, hex);
            if (strcmp(hex, release->sha256) != 0) {
                ESP_LOGE(TAG, "SHA-256 mismatch: got %s, expected %s", hex, release->sha256);
                err = ESP_ERR_INVALID_CRC;
            }
        }
        if (err == ESP_OK) {
            err = esp_ota_end(d->ota); /* also validates the image */
        } else {
            esp_ota_abort(d->ota);
        }
    }
    mbedtls_sha256_free(&d->sha);
    free(d);
    ESP_LOGI(TAG, "Download finished: %s; main stack headroom %u bytes, free heap %u bytes",
             esp_err_to_name(err), (unsigned)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t),
             (unsigned)esp_get_free_heap_size());

    if (err == ESP_OK) {
        esp_app_desc_t desc;
        err = esp_ota_get_partition_description(slot, &desc);
        if (err == ESP_OK && strcmp(desc.project_name, esp_app_get_description()->project_name) != 0) {
            ESP_LOGE(TAG, "Downloaded image is \"%s\", not a launcher", desc.project_name);
            err = ESP_ERR_INVALID_VERSION;
        }
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(slot);
    }
    return err;
}

/* ---------- Copy from ota_0 into factory ---------- */

static esp_err_t image_length(const esp_partition_t *part, uint32_t *len)
{
    esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
    esp_image_metadata_t meta;
    esp_err_t err = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta);
    if (err == ESP_OK) *len = meta.image_len;
    return err;
}

static void boot_factory(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory && esp_ota_set_boot_partition(factory) == ESP_OK) {
        esp_restart();
    }
}

void self_update_finish_if_staged(self_update_progress_t progress)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        return;
    }
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(TAG, "No factory partition; staying in ota_0");
        return;
    }

    uint8_t running_sha[32], factory_sha[32];
    uint32_t len = 0;
    if (image_length(running, &len) != ESP_OK || len > factory->size ||
        esp_partition_get_sha256(running, running_sha) != ESP_OK) {
        ESP_LOGE(TAG, "Staged launcher image is invalid");
        return;
    }
    if (esp_partition_get_sha256(factory, factory_sha) == ESP_OK &&
        memcmp(running_sha, factory_sha, sizeof(running_sha)) == 0) {
        ESP_LOGI(TAG, "Factory already holds this launcher");
        boot_factory();
        return;
    }

    ESP_LOGI(TAG, "Installing launcher %s into factory (%" PRIu32 " bytes)",
             esp_app_get_description()->version, len);
    if (progress) progress("UPDATING LAUNCHER", 0);

    uint32_t erase_len = (len + SPI_FLASH_SEC_SIZE - 1) & ~(SPI_FLASH_SEC_SIZE - 1);
    esp_err_t err = esp_partition_erase_range(factory, 0, erase_len);

    uint8_t *chunk = malloc(COPY_CHUNK);
    if (chunk == NULL) err = ESP_ERR_NO_MEM;
    int last = -1;
    for (uint32_t off = 0; err == ESP_OK && off < len; off += COPY_CHUNK) {
        uint32_t n = (len - off < COPY_CHUNK) ? len - off : COPY_CHUNK;
        err = esp_partition_read(running, off, chunk, n);
        if (err == ESP_OK) err = esp_partition_write(factory, off, chunk, n);
        int pct = (int)((uint64_t)(off + n) * 100 / len);
        if (progress && pct != last) { last = pct; progress(NULL, pct); }
    }
    free(chunk);

    if (err == ESP_OK && (esp_partition_get_sha256(factory, factory_sha) != ESP_OK ||
                          memcmp(running_sha, factory_sha, sizeof(running_sha)) != 0)) {
        err = ESP_ERR_INVALID_CRC;
    }
    if (err != ESP_OK) {
        /* Keep booting from ota_0 so the next start retries the copy. */
        ESP_LOGE(TAG, "Installing into factory failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Launcher updated; restarting from factory");
    boot_factory();
}
