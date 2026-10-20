#include "http_util.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http";
static char s_last_error[32];

typedef struct {
    http_body_cb_t on_body;
    void *ctx;
    esp_err_t error;
} http_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    http_ctx_t *ctx = evt->user_data;
    /* Redirect responses also carry bodies; only pass on the final 200 body. */
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx->error == ESP_OK &&
        esp_http_client_get_status_code(evt->client) == 200) {
        ctx->error = ctx->on_body(evt->data, evt->data_len, ctx->ctx);
    }
    return ESP_OK;
}

esp_err_t http_get(const char *url, http_body_cb_t on_body, void *body_ctx)
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
    s_last_error[0] = '\0';
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        snprintf(s_last_error, sizeof(s_last_error), "OUT OF MEMORY");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
        snprintf(s_last_error, sizeof(s_last_error), "NETWORK ERROR");
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s returned HTTP %d", url, status);
        snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", status);
        return status == 404 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if (ctx.error != ESP_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "%.31s", esp_err_to_name(ctx.error));
    }
    return ctx.error;
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
} text_ctx_t;

static esp_err_t collect_text(const void *data, int len, void *ctx)
{
    text_ctx_t *t = ctx;
    if (t->len + (size_t)len >= t->cap) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(t->buf + t->len, data, len);
    t->len += len;
    t->buf[t->len] = '\0';
    return ESP_OK;
}

esp_err_t http_get_text(const char *url, char *buf, size_t buf_len)
{
    text_ctx_t t = {.buf = buf, .cap = buf_len, .len = 0};
    buf[0] = '\0';
    return http_get(url, collect_text, &t);
}

const char *http_last_error(void)
{
    return s_last_error[0] ? s_last_error : "UNKNOWN ERROR";
}
