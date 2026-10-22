#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

#define BIT_CONNECTED BIT0
#define BIT_FAILED    BIT1

static const char *TAG = "net";
static EventGroupHandle_t s_events;
static esp_netif_t *s_netif;
static int s_retries;
static bool s_started;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries++ < 5) esp_wifi_connect();
        else xEventGroupSetBits(s_events, BIT_FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

esp_err_t net_wifi_start(int timeout_ms)
{
    char ssid[33] = "", pass[65] = "";
    nvs_handle_t h;
    if (nvs_open("cyd_wifi", NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    size_t a = sizeof(ssid), b = sizeof(pass);
    esp_err_t err = nvs_get_str(h, "ssid", ssid, &a);
    if (nvs_get_str(h, "password", pass, &b) != ESP_OK) pass[0] = '\0';
    nvs_close(h);
    if (err != ESP_OK || ssid[0] == '\0') return ESP_ERR_NOT_FOUND;

    if (s_events == NULL) s_events = xEventGroupCreate();
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);
    s_retries = 0;

    if (!s_started) {
        ESP_ERROR_CHECK(esp_netif_init());
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        s_netif = esp_netif_create_default_wifi_sta();
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        s_started = true;
    }

    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        ESP_LOGI(TAG, "Connected to \"%s\"", ssid);
        return ESP_OK;
    }
    return (bits & BIT_FAILED) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

esp_err_t net_ip_string(char *out, size_t len)
{
    esp_netif_ip_info_t ip;
    if (s_netif == NULL || esp_netif_get_ip_info(s_netif, &ip) != ESP_OK || ip.ip.addr == 0) return ESP_FAIL;
    snprintf(out, len, IPSTR, IP2STR(&ip.ip));
    return ESP_OK;
}

void net_wifi_stop(void)
{
    if (!s_started) return;
    esp_netif_sntp_deinit();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event);
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    s_started = false;
    ESP_LOGI(TAG, "Wi-Fi off");
}

esp_err_t net_sync_time(int timeout_ms)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com", "time.cloudflare.com"));
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_OK) {
        time_t now = time(NULL);
        ESP_LOGI(TAG, "Time set: %s", ctime(&now));
    }
    return err;
}

/* ---------- Time from an HTTP "Date" header (when NTP/UDP is blocked) ---------- */

static const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm). */
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Parses "Wed, 21 Oct 2026 23:01:02 GMT". Returns 0 on failure. */
static time_t parse_http_date(const char *s)
{
    int day, year, hh, mm, ss;
    char mon[4];
    if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d", &day, mon, &year, &hh, &mm, &ss) != 6) return 0;
    for (int m = 0; m < 12; ++m) {
        if (strncmp(mon, MONTHS[m], 3) == 0) {
            return (time_t)(days_from_civil(year, m + 1, day) * 86400L + hh * 3600L + mm * 60L + ss);
        }
    }
    return 0;
}

static esp_err_t on_date_header(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "Date") == 0) {
        *(time_t *)evt->user_data = parse_http_date(evt->header_value);
    }
    return ESP_OK;
}

esp_err_t net_time_from_http(const char *url)
{
    time_t t = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_HEAD,
        .event_handler = on_date_header,
        .user_data = &t,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .user_agent = "cyd-app-platform-sat-tracker",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);
    if (t < 1700000000) return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;

    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Time set from HTTP Date header: %s", ctime(&t));
    return ESP_OK;
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    size_t max;
    esp_err_t error;
} body_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    body_t *b = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || b->error != ESP_OK ||
        esp_http_client_get_status_code(evt->client) != 200) {
        return ESP_OK;
    }
    if (b->len + evt->data_len + 1 > b->max) {
        b->error = ESP_ERR_INVALID_SIZE;
        return ESP_OK;
    }
    if (b->len + evt->data_len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8192;
        while (cap < b->len + evt->data_len + 1) cap *= 2;
        if (cap > b->max) cap = b->max;
        char *p = realloc(b->buf, cap);
        if (p == NULL) {
            b->error = ESP_ERR_NO_MEM;
            return ESP_OK;
        }
        b->buf = p;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, evt->data, evt->data_len);
    b->len += evt->data_len;
    b->buf[b->len] = '\0';
    return ESP_OK;
}

esp_err_t net_get(const char *url, size_t max_len, char **out, size_t *out_len)
{
    body_t b = {.max = max_len + 1, .error = ESP_OK};
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = &b,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .timeout_ms = 20000,
        .max_redirection_count = 5,
        .user_agent = "cyd-app-platform-sat-tracker",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status != 200) {
        ESP_LOGE(TAG, "GET %s: HTTP %d", url, status);
        err = ESP_FAIL;
    }
    if (err == ESP_OK) err = b.error;
    if (err == ESP_OK && b.len == 0) err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) {
        free(b.buf);
        return err;
    }
    *out = b.buf;
    if (out_len) *out_len = b.len;
    return ESP_OK;
}
