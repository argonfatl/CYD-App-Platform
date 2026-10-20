#include "launcher_wifi.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "cyd_board.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"

#define WIFI_FILE      CYD_SD_MOUNT_POINT "/wifi.json"
#define NVS_NAMESPACE  "cyd_wifi"
#define BIT_CONNECTED  BIT0
#define BIT_FAILED     BIT1
#define MAX_RETRIES    5

static const char *TAG = "launcher_wifi";

static EventGroupHandle_t s_events;
static bool s_started;
static int s_retries;

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t a = ssid_len, b = pass_len;
    bool ok = nvs_get_str(h, "ssid", ssid, &a) == ESP_OK && ssid[0] != '\0';
    if (ok && nvs_get_str(h, "password", pass, &b) != ESP_OK) {
        pass[0] = '\0';
    }
    nvs_close(h);
    return ok;
}

void launcher_wifi_import_from_sd(void)
{
    FILE *f = fopen(WIFI_FILE, "r");
    if (f == NULL) {
        return;
    }
    char text[256];
    size_t len = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[len] = '\0';

    cJSON *root = cJSON_Parse(text);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *pass = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || ssid->valuestring[0] == '\0' || strlen(ssid->valuestring) > 32 ||
        (pass && (!cJSON_IsString(pass) || strlen(pass->valuestring) > 64))) {
        ESP_LOGW(TAG, "%s is invalid; expected {\"ssid\": \"...\", \"password\": \"...\"}", WIFI_FILE);
        cJSON_Delete(root);
        return;
    }

    char old_ssid[33], old_pass[65];
    bool same = load_credentials(old_ssid, sizeof(old_ssid), old_pass, sizeof(old_pass)) &&
                strcmp(old_ssid, ssid->valuestring) == 0 &&
                strcmp(old_pass, pass ? pass->valuestring : "") == 0;
    if (!same) {
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "ssid", ssid->valuestring);
            nvs_set_str(h, "password", pass ? pass->valuestring : "");
            nvs_commit(h);
            nvs_close(h);
            ESP_LOGI(TAG, "Imported Wi-Fi settings for \"%s\"", ssid->valuestring);
        }
    }
    cJSON_Delete(root);
}

bool launcher_wifi_configured(void)
{
    char ssid[33], pass[65];
    return load_credentials(ssid, sizeof(ssid), pass, sizeof(pass));
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        if (s_retries++ < MAX_RETRIES) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, BIT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retries = 0;
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

void launcher_wifi_ip(char *out, int len)
{
    out[0] = '\0';
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info;
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
        snprintf(out, len, IPSTR, IP2STR(&info.ip));
    }
}

esp_err_t launcher_wifi_connect(int timeout_ms)
{
    char ssid[33], pass[65];
    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        return ESP_ERR_NOT_FOUND;
    }

    bool first_start = !s_started;
    if (!s_started) {
        s_events = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_netif_init());
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        s_started = true;
    } else if (xEventGroupGetBits(s_events) & BIT_CONNECTED) {
        return ESP_OK;
    }

    wifi_config_t cfg = {0};
    /* Lengths were checked on import: SSID <= 32, password <= 64 bytes (not NUL-terminated if full). */
    memcpy(cfg.sta.ssid, ssid, strnlen(ssid, sizeof(cfg.sta.ssid)));
    memcpy(cfg.sta.password, pass, strnlen(pass, sizeof(cfg.sta.password)));
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    s_retries = 0;
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);
    if (first_start) {
        ESP_ERROR_CHECK(esp_wifi_start()); /* WIFI_EVENT_STA_START starts the connection */
    } else {
        esp_wifi_connect();
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_CONNECTED) {
        ESP_LOGI(TAG, "Connected to \"%s\"", ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Could not connect to \"%s\"", ssid);
    esp_wifi_disconnect();
    return (bits & BIT_FAILED) ? ESP_FAIL : ESP_ERR_TIMEOUT;
}
