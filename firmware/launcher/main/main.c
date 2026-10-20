/*
 * CYD App Platform launcher.
 *
 * Lists apps from /sdcard/apps/<id>/manifest.json, installs the chosen app
 * into the ota_0 slot (verifying SHA-256), and boots it. See ADR 0002.
 */
#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "cyd_board.h"
#include "cyd_platform.h"
#include "app_store.h"
#include "launcher_wifi.h"
#include "http_util.h"
#include "self_update.h"
#include "web_ui.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"

#define APPS_DIR          CYD_SD_MOUNT_POINT "/apps"
#define MAX_APPS          16
#define ROW_HEIGHT        34
#define LIST_TOP          40
#define VISIBLE_ROWS      4
#define FOOTER_TOP        (LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT + 8)
#define FOOTER_HEIGHT     22
#define STATUS_Y          (CYD_LCD_V_RES - 14)
#define LONG_PRESS_US     700000
#define COPY_CHUNK        4096
#define NVS_NAMESPACE     "cyd_launcher"

static const char *TAG = "launcher";

typedef struct {
    char id[32];
    char name[32];
    char version[16];
    char binary_path[160];
    char sha256[65]; /* expected hash from the manifest, empty if absent */
    bool unreviewed; /* downloaded from a user-added GitHub repository */
} app_entry_t;

typedef struct {
    app_entry_t apps[MAX_APPS];
    int count;
    int selected;         /* list rows first, then footer buttons (see footer_count) */
    int scroll;
    bool sd_ok;
    bool keep_app;
    char installed_id[32];
    char status[48];
    uint16_t status_color;
    int progress;         /* -1 = hidden, 0..100 */
    char progress_label[48];
    bool update_ready;    /* a newer release was found and can be installed */
    launcher_release_t release;
    bool store_mode;      /* GET APPS screen is open */
    char web_ip[20];      /* address of the web interface while the store is open */
} launcher_t;

static launcher_t s_ui;

static void refresh_apps(void);
static char s_version[24];        /* short launcher version shown in the header */
static bool s_version_is_release; /* built exactly from a vX.Y.Z tag */
static uint8_t s_chunk[COPY_CHUNK];

/* ELEGOO ESP32-32E battery monitor: battery divider is routed to GPIO34. */
#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_6 /* GPIO34 */
#define BATTERY_DIVIDER_RATIO 2.0f
static esp_adc_cal_characteristics_t s_battery_adc_chars;
static bool s_battery_adc_ready;
static float s_battery_voltage;
static int s_battery_percent = -1;

static void battery_init(void)
{
    if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK ||
        adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11) != ESP_OK) {
        ESP_LOGW(TAG, "Battery ADC setup failed");
        return;
    }
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                             1100, &s_battery_adc_chars);
    s_battery_adc_ready = true;
}

static void battery_update(void)
{
    if (!s_battery_adc_ready) return;
    uint32_t total_mv = 0;
    for (int i = 0; i < 8; ++i) {
        int raw = adc1_get_raw(BATTERY_ADC_CHANNEL);
        if (raw < 0) return;
        total_mv += esp_adc_cal_raw_to_voltage((uint32_t)raw, &s_battery_adc_chars);
    }
    float voltage = ((float)total_mv / 8.0f) * BATTERY_DIVIDER_RATIO / 1000.0f;
    /* Reject disconnected or implausible readings rather than showing a false percentage. */
    if (voltage < 2.5f || voltage > 4.5f) {
        s_battery_voltage = 0.0f;
        s_battery_percent = -1;
        return;
    }
    s_battery_voltage = voltage;

    /* Approximate rested Li-ion cell curve; actual percentage varies under load. */
    static const struct { float volts; int percent; } curve[] = {
        {3.30f, 0}, {3.50f, 5}, {3.60f, 10}, {3.70f, 20},
        {3.75f, 30}, {3.80f, 40}, {3.85f, 50}, {3.90f, 60},
        {3.95f, 70}, {4.00f, 80}, {4.10f, 90}, {4.20f, 100}
    };
    if (voltage <= curve[0].volts) {
        s_battery_percent = 0;
    } else {
        s_battery_percent = 100;
        for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
            if (voltage <= curve[i].volts) {
                float fraction = (voltage - curve[i - 1].volts) /
                                 (curve[i].volts - curve[i - 1].volts);
                s_battery_percent = curve[i - 1].percent +
                    (int)((curve[i].percent - curve[i - 1].percent) * fraction + 0.5f);
                break;
            }
        }
    }
}

static void draw_battery(cyd_canvas_t *c)
{
    const int x = CYD_LCD_H_RES - 47;
    const int y = 9;
    if (s_battery_percent < 0) {
        cyd_draw_text(c, "BAT --", CYD_LCD_H_RES - 48, 10, 1, CYD_GREY);
        return;
    }
    uint16_t color = s_battery_percent <= 15 ? CYD_RED :
                     (s_battery_percent <= 30 ? CYD_AMBER : CYD_GREEN);
    /* Battery outline and terminal, drawn using the platform's rectangle primitive. */
    cyd_fill_rect(c, x, y, 20, 2, color);
    cyd_fill_rect(c, x, y + 11, 20, 2, color);
    cyd_fill_rect(c, x, y, 2, 13, color);
    cyd_fill_rect(c, x + 18, y, 2, 13, color);
    cyd_fill_rect(c, x + 20, y + 4, 3, 5, color);
    int fill = 16 * s_battery_percent / 100;
    if (fill > 0) cyd_fill_rect(c, x + 2, y + 2, fill, 9, color);
    char label[5];
    snprintf(label, sizeof(label), "%d%%", s_battery_percent);
    cyd_draw_text(c, label, CYD_LCD_H_RES - 24, 10, 1, color);
}


/* ---------- Version label ---------- */

/*
 * Turn the git-describe version into a short label:
 *   "v0.2.0"                 -> "v0.2.0"    (release)
 *   "v0.2.0-5-g1a2b3c4-dirty" -> "v0.2.0+5"  (5 commits after the release)
 *   "v0.2.0-dirty"           -> "v0.2.0 DEV"
 *   "1a2b3c4"                -> "DEV 1a2b3c4"
 */
static void make_version_label(const char *full, char *out, size_t len, bool *is_release)
{
    int major, minor, patch, ahead = 0;
    char rest;
    const char *p = (*full == 'v' || *full == 'V') ? full + 1 : full;
    int n = sscanf(p, "%d.%d.%d%c", &major, &minor, &patch, &rest);
    *is_release = (n == 3);
    if (n == 3) {
        snprintf(out, len, "v%d.%d.%d", major, minor, patch);
    } else if (n == 4 && rest == '-' && sscanf(p, "%*d.%*d.%*d-%d-g", &ahead) == 1) {
        snprintf(out, len, "v%d.%d.%d+%d", major, minor, patch, ahead);
    } else if (n == 4) {
        snprintf(out, len, "v%d.%d.%d DEV", major, minor, patch); /* e.g. uncommitted changes */
    } else {
        snprintf(out, len, "DEV %.7s", full);
    }
}

/* ---------- Manifest scanning ---------- */

static void copy_json_string(const cJSON *obj, const char *key, char *dst, size_t len)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, len, "%s", item->valuestring);
    }
}

static bool load_manifest(const char *dir_name, app_entry_t *app)
{
    char path[160];
    snprintf(path, sizeof(path), APPS_DIR "/%s/manifest.json", dir_name);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return false;
    }
    char text[1024];
    size_t len = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[len] = '\0';

    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        ESP_LOGW(TAG, "%s: invalid JSON", path);
        return false;
    }

    memset(app, 0, sizeof(*app));
    snprintf(app->id, sizeof(app->id), "%s", dir_name);
    snprintf(app->name, sizeof(app->name), "%s", dir_name);
    copy_json_string(root, "id", app->id, sizeof(app->id));
    copy_json_string(root, "name", app->name, sizeof(app->name));
    copy_json_string(root, "version", app->version, sizeof(app->version));
    copy_json_string(root, "sha256", app->sha256, sizeof(app->sha256));
    char source[80] = "";
    copy_json_string(root, "source", source, sizeof(source));
    app->unreviewed = strncmp(source, "github:", 7) == 0;

    char binary[48] = "app.bin";
    copy_json_string(root, "binary", binary, sizeof(binary));
    cJSON_Delete(root);

    if (strchr(binary, '/') != NULL || strstr(binary, "..") != NULL) {
        ESP_LOGW(TAG, "%s: binary must be a plain file name", path);
        return false;
    }
    snprintf(app->binary_path, sizeof(app->binary_path), APPS_DIR "/%s/%s", dir_name, binary);

    struct stat st;
    if (stat(app->binary_path, &st) != 0 || st.st_size == 0) {
        ESP_LOGW(TAG, "%s: %s is missing", dir_name, binary);
        return false;
    }
    for (char *p = app->sha256; *p; ++p) {
        if (*p >= 'A' && *p <= 'F') *p = (char)(*p - 'A' + 'a');
    }
    return true;
}

static void scan_apps(void)
{
    s_ui.count = 0;
    DIR *dir = opendir(APPS_DIR);
    if (dir == NULL) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_ui.count < MAX_APPS) {
        if (entry->d_name[0] == '.') continue;
        if (load_manifest(entry->d_name, &s_ui.apps[s_ui.count])) {
            ESP_LOGI(TAG, "Found app %s (%s)", s_ui.apps[s_ui.count].name, s_ui.apps[s_ui.count].id);
            s_ui.count++;
        }
    }
    closedir(dir);
}

/* ---------- Persistent launcher state ---------- */

static void nvs_get_str_default(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    size_t size = len;
    if (nvs_get_str(h, key, dst, &size) != ESP_OK) {
        dst[0] = '\0';
    }
}

static const esp_partition_t *app_slot(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
}

/* The record also stores the slot address, so it is ignored after a partition table change. */
static void load_installed(char *id, size_t id_len, char *sha, size_t sha_len)
{
    nvs_handle_t h;
    id[0] = sha[0] = '\0';
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        uint32_t addr = 0;
        const esp_partition_t *slot = app_slot();
        if (nvs_get_u32(h, "slot_addr", &addr) == ESP_OK && slot && addr == slot->address) {
            nvs_get_str_default(h, "slot_id", id, id_len);
            nvs_get_str_default(h, "slot_sha", sha, sha_len);
        }
        nvs_close(h);
    }
}

static void save_installed(const char *id, const char *sha)
{
    nvs_handle_t h;
    const esp_partition_t *slot = app_slot();
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "slot_id", id);
        nvs_set_str(h, "slot_sha", sha);
        nvs_set_u32(h, "slot_addr", slot ? slot->address : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---------- Drawing ---------- */

/* ---------- Layout helpers ---------- */

/* Rows in the current list: apps on the SD card, or apps in the store. */
static int list_len(void)
{
    return s_ui.store_mode ? store_list()->count : s_ui.count;
}

/* Footer buttons: main screen = power-on, update, get apps; store = back, refresh. */
static int footer_count(void)
{
    return s_ui.store_mode ? 2 : 3;
}

static void footer_rect(int i, int *x, int *w)
{
    int n = footer_count();
    *w = (CYD_LCD_H_RES - 12 - (n - 1) * 4) / n;
    *x = 6 + i * (*w + 4);
}

static void draw_footer_button(cyd_canvas_t *c, int i, const char *label, uint16_t bg, uint16_t fg)
{
    int x, w;
    footer_rect(i, &x, &w);
    cyd_fill_rect(c, x, FOOTER_TOP, w, FOOTER_HEIGHT, bg);
    cyd_draw_text(c, label, x + (w - cyd_text_width(label, 1)) / 2, FOOTER_TOP + 7, 1, fg);
}

static void draw_row_frame(cyd_canvas_t *c, int row, bool sel)
{
    cyd_fill_rect(c, 6, LIST_TOP + row * ROW_HEIGHT, CYD_LCD_H_RES - 12, ROW_HEIGHT - 4, sel ? CYD_CYAN : CYD_PANEL);
}

static void draw_right(cyd_canvas_t *c, const char *text, int y, uint16_t color)
{
    cyd_draw_text(c, text, CYD_LCD_H_RES - 16 - cyd_text_width(text, 1), y, 1, color);
}

/* Draw a name without letting it collide with the metadata on the right. */
static void draw_clipped_name(cyd_canvas_t *c, const char *text, int x, int y,
                              int max_width, int scale, uint16_t color)
{
    char clipped[32];
    size_t len = strlen(text);
    if (len >= sizeof(clipped)) len = sizeof(clipped) - 1;
    memcpy(clipped, text, len);
    clipped[len] = '\0';

    if (cyd_text_width(clipped, scale) <= max_width) {
        cyd_draw_text(c, clipped, x, y, scale, color);
        return;
    }

    while (len > 0) {
        if (len >= 3) {
            clipped[len - 3] = '.';
            clipped[len - 2] = '.';
            clipped[len - 1] = '.';
            clipped[len] = '\0';
        } else {
            clipped[len - 1] = '\0';
        }
        if (cyd_text_width(clipped, scale) <= max_width) {
            cyd_draw_text(c, clipped, x, y, scale, color);
            return;
        }
        len--;
        clipped[len] = '\0';
    }
}

/* ---------- Drawing ---------- */

static void draw_main_list(cyd_canvas_t *c)
{
    if (!s_ui.sd_ok) {
        cyd_draw_text(c, "NO SD CARD", 20, 80, 2, CYD_AMBER);
        cyd_draw_text(c, "INSERT A FAT32 CARD WITH", 20, 115, 1, CYD_WHITE);
        cyd_draw_text(c, "/APPS/<ID>/MANIFEST.JSON + APP.BIN", 20, 130, 1, CYD_WHITE);
        return;
    }
    if (s_ui.count == 0) {
        cyd_draw_text(c, "NO APPS YET", 20, 80, 2, CYD_AMBER);
        cyd_draw_text(c, "OPEN GET APPS TO DOWNLOAD SOME", 20, 115, 1, CYD_WHITE);
        return;
    }
    for (int row = 0; row < VISIBLE_ROWS; ++row) {
        int i = s_ui.scroll + row;
        if (i >= s_ui.count) break;
        const app_entry_t *app = &s_ui.apps[i];
        int y = LIST_TOP + row * ROW_HEIGHT;
        bool sel = (i == s_ui.selected);
        draw_row_frame(c, row, sel);
        int name_right = CYD_LCD_H_RES - 16;
        if (app->version[0]) {
            draw_right(c, app->version, y + 6, sel ? CYD_BLACK : CYD_GREY);
            name_right -= cyd_text_width(app->version, 1) + 12;
        }
        draw_clipped_name(c, app->name, 16, y + 4, name_right - 16, 2,
                          sel ? CYD_BLACK : CYD_WHITE);
        int right = CYD_LCD_H_RES - 16;
        if (strcmp(app->id, s_ui.installed_id) == 0) {
            draw_right(c, "INSTALLED", y + 20, sel ? CYD_BLACK : CYD_GREEN);
            right -= cyd_text_width("INSTALLED", 1) + 8;
        }
        if (app->unreviewed) {
            cyd_draw_text(c, "GITHUB", right - cyd_text_width("GITHUB", 1), y + 20, 1, sel ? CYD_BLACK : CYD_AMBER);
        }
    }
}

static void draw_store_list(cyd_canvas_t *c)
{
    const store_list_t *list = store_list();
    if (list->count == 0) {
        cyd_draw_text(c, "NO APPS AVAILABLE", 20, 80, 2, CYD_AMBER);
        cyd_draw_text(c, list->failed_sources ? list->last_error : "THE CATALOG IS EMPTY", 20, 115, 1, CYD_WHITE);
        return;
    }
    for (int row = 0; row < VISIBLE_ROWS; ++row) {
        int i = s_ui.scroll + row;
        if (i >= list->count) break;
        const store_app_t *app = &list->apps[i];
        int y = LIST_TOP + row * ROW_HEIGHT;
        bool sel = (i == s_ui.selected);
        draw_row_frame(c, row, sel);
        char state[32];
        uint16_t color;
        if (app->state == STORE_NEW) {
            snprintf(state, sizeof(state), "NEW %s", app->version);
            color = CYD_CYAN;
        } else if (app->state == STORE_UPDATE) {
            snprintf(state, sizeof(state), "UPDATE %s", app->version);
            color = CYD_AMBER;
        } else {
            snprintf(state, sizeof(state), "INSTALLED");
            color = CYD_GREEN;
        }
        if (sel) color = CYD_BLACK;
        draw_right(c, state, y + 6, color);
        draw_right(c, app->official ? "OFFICIAL" : "GITHUB", y + 20,
                   sel ? CYD_BLACK : (app->official ? CYD_GREY : CYD_AMBER));
        draw_clipped_name(c, app->name, 16, y + 4,
                          CYD_LCD_H_RES - 32 - cyd_text_width(state, 1) - 12,
                          2, sel ? CYD_BLACK : CYD_WHITE);
    }
}

static void draw_screen(cyd_canvas_t *c, void *ctx)
{
    (void)ctx;
    cyd_fill(c, CYD_DARK);

    cyd_fill_rect(c, 0, 0, CYD_LCD_H_RES, 32, CYD_PANEL);
    cyd_fill_rect(c, 0, 32, CYD_LCD_H_RES, 2, CYD_CYAN);
    if (s_ui.store_mode) {
        cyd_draw_text(c, "GET APPS", 10, 9, 2, CYD_WHITE);
        if (s_ui.web_ip[0]) {
            cyd_draw_text(c, "WEB", CYD_LCD_H_RES - 10 - cyd_text_width(s_ui.web_ip, 1), 6, 1, CYD_GREY);
            cyd_draw_text(c, s_ui.web_ip, CYD_LCD_H_RES - 10 - cyd_text_width(s_ui.web_ip, 1), 17, 1, CYD_CYAN);
        }
    } else {
        cyd_draw_text(c, "CYD APPS", 10, 9, 2, CYD_WHITE);
        /* Compact version label leaves room for the battery indicator. */
        int version_x = CYD_LCD_H_RES - 53 - cyd_text_width(s_version, 1);
        cyd_draw_text(c, s_version, version_x, 11, 1,
                      s_version_is_release ? CYD_CYAN : CYD_AMBER);
        draw_battery(c);
    }

    if (s_ui.progress >= 0) {
        cyd_draw_text(c, s_ui.progress_label, 20, 90, 2, CYD_WHITE);
        cyd_fill_rect(c, 20, 120, CYD_LCD_H_RES - 40, 16, CYD_PANEL);
        cyd_fill_rect(c, 20, 120, (CYD_LCD_H_RES - 40) * s_ui.progress / 100, 16, CYD_CYAN);
    } else if (s_ui.store_mode) {
        draw_store_list(c);
    } else {
        draw_main_list(c);
    }

    int base = list_len();
    if (s_ui.store_mode) {
        bool back_sel = s_ui.selected == base, refresh_sel = s_ui.selected == base + 1;
        draw_footer_button(c, 0, "BACK", back_sel ? CYD_CYAN : CYD_PANEL, back_sel ? CYD_BLACK : CYD_WHITE);
        draw_footer_button(c, 1, "REFRESH", refresh_sel ? CYD_CYAN : CYD_PANEL,
                           refresh_sel ? CYD_BLACK : CYD_WHITE);
    } else {
        bool power_sel = s_ui.selected == base, update_sel = s_ui.selected == base + 1,
             store_sel = s_ui.selected == base + 2;
        draw_footer_button(c, 0, s_ui.keep_app ? "BOOT: LAST APP" : "BOOT: LAUNCHER",
                           power_sel ? CYD_CYAN : CYD_PANEL, power_sel ? CYD_BLACK : CYD_WHITE);

        char update_text[40];
        if (s_ui.update_ready) {
            snprintf(update_text, sizeof(update_text), "INSTALL %.16s", s_ui.release.version);
        } else {
            snprintf(update_text, sizeof(update_text), "CHECK UPDATE");
        }
        draw_footer_button(c, 1, update_text,
                           update_sel ? CYD_CYAN : (s_ui.update_ready ? CYD_GREEN : CYD_PANEL),
                           (update_sel || s_ui.update_ready) ? CYD_BLACK : CYD_WHITE);
        draw_footer_button(c, 2, "GET APPS", store_sel ? CYD_CYAN : CYD_PANEL, store_sel ? CYD_BLACK : CYD_WHITE);
    }

    cyd_draw_text(c, s_ui.status, 10, STATUS_Y, 1, s_ui.status_color);
}

static void redraw(void)
{
    esp_err_t err = cyd_display_render(draw_screen, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Render failed: %s", esp_err_to_name(err));
    }
}

static void set_status(const char *text, uint16_t color)
{
    snprintf(s_ui.status, sizeof(s_ui.status), "%s", text);
    s_ui.status_color = color;
}

static void set_progress(const char *label, int percent)
{
    if (label) snprintf(s_ui.progress_label, sizeof(s_ui.progress_label), "%s", label);
    s_ui.progress = percent;
    redraw();
}

/* ---------- Installing and starting apps ---------- */

static void to_hex(const uint8_t *digest, char *out)
{
    for (int i = 0; i < 32; ++i) {
        sprintf(out + i * 2, "%02x", digest[i]);
    }
}

static esp_err_t hash_file(const char *path, long size, char out_hex[65])
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return ESP_ERR_NOT_FOUND;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    long done = 0;
    int last = -1;
    size_t n;
    while ((n = fread(s_chunk, 1, sizeof(s_chunk), f)) > 0) {
        mbedtls_sha256_update(&ctx, s_chunk, n);
        done += (long)n;
        int pct = (int)(done * 30 / size);
        if (pct != last) { last = pct; set_progress(NULL, pct); }
    }
    fclose(f);

    uint8_t digest[32];
    mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    to_hex(digest, out_hex);
    return ESP_OK;
}

static esp_err_t flash_file(const char *path, long size, const esp_partition_t *slot)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return ESP_ERR_NOT_FOUND;

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(slot, (size_t)size, &ota);
    if (err != ESP_OK) {
        fclose(f);
        return err;
    }

    long done = 0;
    int last = -1;
    size_t n;
    while ((n = fread(s_chunk, 1, sizeof(s_chunk), f)) > 0) {
        err = esp_ota_write(ota, s_chunk, n);
        if (err != ESP_OK) break;
        done += (long)n;
        int pct = 30 + (int)(done * 70 / size);
        if (pct != last) { last = pct; set_progress(NULL, pct); }
    }
    fclose(f);

    if (err != ESP_OK) {
        esp_ota_abort(ota);
        return err;
    }
    if (done != size) {
        esp_ota_abort(ota);
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_ota_end(ota); /* validates the image header and checksum */
}

static void fail(const char *message, esp_err_t err)
{
    ESP_LOGE(TAG, "%s: %s", message, esp_err_to_name(err));
    char text[48];
    snprintf(text, sizeof(text), "%s", message);
    set_status(text, CYD_RED);
    s_ui.progress = -1;
    redraw();
}

static void launch_app(const app_entry_t *app)
{
    const esp_partition_t *slot = app_slot();
    if (slot == NULL) {
        fail("NO APP SLOT IN PARTITION TABLE", ESP_ERR_NOT_FOUND);
        return;
    }

    struct stat st;
    if (stat(app->binary_path, &st) != 0) {
        fail("APP FILE MISSING", ESP_ERR_NOT_FOUND);
        return;
    }
    if ((size_t)st.st_size > slot->size) {
        fail("APP TOO BIG FOR THE APP SLOT", ESP_ERR_INVALID_SIZE);
        return;
    }

    char label[48];
    snprintf(label, sizeof(label), "CHECKING %s", app->name);
    set_progress(label, 0);

    char sha[65];
    esp_err_t err = hash_file(app->binary_path, st.st_size, sha);
    if (err != ESP_OK) {
        fail("CANNOT READ APP FILE", err);
        return;
    }
    if (app->sha256[0] && strcmp(app->sha256, sha) != 0) {
        fail("SHA-256 MISMATCH - NOT INSTALLED", ESP_ERR_INVALID_CRC);
        return;
    }

    char slot_id[32], slot_sha[65];
    load_installed(slot_id, sizeof(slot_id), slot_sha, sizeof(slot_sha));
    esp_app_desc_t slot_desc;
    bool already = strcmp(slot_sha, sha) == 0 &&
                   esp_ota_get_partition_description(slot, &slot_desc) == ESP_OK;

    if (!already) {
        snprintf(label, sizeof(label), "INSTALLING %s", app->name);
        set_progress(label, 30);
        save_installed("", ""); /* the slot is about to change */
        err = flash_file(app->binary_path, st.st_size, slot);
        if (err != ESP_OK) {
            fail("INSTALL FAILED", err);
            return;
        }
        save_installed(app->id, sha);
    } else {
        ESP_LOGI(TAG, "%s is already in the slot; skipping flash", app->id);
    }

    err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) {
        fail("CANNOT SELECT APP SLOT", err);
        return;
    }

    snprintf(label, sizeof(label), "STARTING %s", app->name);
    set_progress(label, 100);
    ESP_LOGI(TAG, "Booting %s", app->id);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

/* ---------- Launcher self-update ---------- */

static void on_progress(const char *label, int percent)
{
    set_progress(label, percent);
}

static void update_launcher(void)
{
    if (!launcher_wifi_configured()) {
        set_status("ADD /WIFI.JSON TO THE SD CARD", CYD_AMBER);
        redraw();
        return;
    }

    set_progress("CONNECTING TO WI-FI", 0);
    esp_err_t err = launcher_wifi_connect(20000);
    if (err != ESP_OK) {
        fail("WI-FI CONNECTION FAILED", err);
        return;
    }

    if (!s_ui.update_ready) {
        set_progress("CHECKING GITHUB", 0);
        launcher_release_t release;
        err = self_update_check(&release);
        s_ui.progress = -1;
        if (err == ESP_ERR_NOT_FOUND) {
            fail("NO LAUNCHER RELEASE FOUND", err);
            return;
        }
        if (err != ESP_OK) {
            fail("UPDATE CHECK FAILED", err);
            return;
        }
        const char *current = esp_app_get_description()->version;
        if (!self_update_is_newer(release.version, current)) {
            char text[48];
            snprintf(text, sizeof(text), "UP TO DATE (%.24s)", release.version);
            set_status(text, CYD_GREEN);
            redraw();
            return;
        }
        s_ui.release = release;
        s_ui.update_ready = true;
        char text[48];
        snprintf(text, sizeof(text), "%.16s READY - OPEN AGAIN TO INSTALL", release.version);
        set_status(text, CYD_GREEN);
        redraw();
        return;
    }

    char label[48];
    snprintf(label, sizeof(label), "DOWNLOADING %.24s", s_ui.release.version);
    set_progress(label, 0);
    save_installed("", ""); /* the app slot is used to stage the update */
    s_ui.installed_id[0] = '\0';
    err = self_update_download(&s_ui.release, on_progress);
    if (err != ESP_OK) {
        s_ui.update_ready = false;
        fail("LAUNCHER DOWNLOAD FAILED", err);
        return;
    }
    set_progress("RESTARTING TO INSTALL", 100);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

/* ---------- Get apps (store) ---------- */

static void reset_selection(void)
{
    s_ui.selected = 0;
    s_ui.scroll = 0;
}

static void store_progress(const char *label, int percent)
{
    set_progress(label, percent);
    if (label) web_ui_set_activity(label);
}

static void store_reload(void)
{
    store_refresh(store_progress);
    s_ui.progress = -1;
    web_ui_set_activity("");
    const store_list_t *list = store_list();
    if (list->failed_sources > 0) {
        set_status(list->last_error, CYD_AMBER);
    } else if (s_ui.web_ip[0]) {
        char text[48];
        snprintf(text, sizeof(text), "ADD REPOS AT HTTP://%s", s_ui.web_ip);
        set_status(text, CYD_GREY);
    } else {
        set_status("", CYD_GREY);
    }
    if (s_ui.selected >= list->count + footer_count()) reset_selection();
    redraw();
}

static void open_store(void)
{
    if (!s_ui.sd_ok) {
        set_status("INSERT AN SD CARD FIRST", CYD_AMBER);
        redraw();
        return;
    }
    if (!launcher_wifi_configured()) {
        set_status("ADD /WIFI.JSON TO THE SD CARD", CYD_AMBER);
        redraw();
        return;
    }
    set_progress("CONNECTING TO WI-FI", 0);
    esp_err_t err = launcher_wifi_connect(20000);
    if (err != ESP_OK) {
        fail("WI-FI CONNECTION FAILED", err);
        return;
    }
    s_ui.store_mode = true;
    reset_selection();
    launcher_wifi_ip(s_ui.web_ip, sizeof(s_ui.web_ip));
    web_ui_start();
    store_reload();
}

static void close_store(void)
{
    web_ui_stop();
    s_ui.store_mode = false;
    s_ui.web_ip[0] = '\0';
    refresh_apps(); /* pick up newly downloaded apps */
    redraw();
}

static void install_from_store(int index)
{
    store_app_t app = store_list()->apps[index]; /* copy: the list may be refreshed */
    char label[48];
    snprintf(label, sizeof(label), "DOWNLOADING %.24s", app.name);
    store_progress(label, 0);
    esp_err_t err = store_install(&app, store_progress);
    s_ui.progress = -1;
    web_ui_set_activity("");
    if (err != ESP_OK) {
        char text[48];
        snprintf(text, sizeof(text), "%.24s: %s", app.name,
                 err == ESP_ERR_INVALID_CRC ? "SHA-256 MISMATCH" : http_last_error());
        fail(text, err);
        return;
    }
    store_lock();
    store_app_t *entry = &store_list()->apps[index];
    if (strcmp(entry->id, app.id) == 0) {
        entry->state = STORE_INSTALLED;
        snprintf(entry->installed_version, sizeof(entry->installed_version), "%s", app.version);
    }
    store_unlock();
    char text[48];
    snprintf(text, sizeof(text), "%.24s SAVED TO THE SD CARD", app.name);
    set_status(text, CYD_GREEN);
    redraw();
}

static void activate_store_item(void)
{
    const store_list_t *list = store_list();
    int i = s_ui.selected;
    if (i == list->count) {
        close_store();
    } else if (i == list->count + 1) {
        store_reload();
    } else if (i >= 0 && i < list->count) {
        if (list->apps[i].state == STORE_INSTALLED) {
            set_status("ALREADY ON THE SD CARD", CYD_GREY);
            redraw();
        } else {
            install_from_store(i);
        }
    }
}

/* Handles requests from the web page and repository changes while the store is open. */
static void poll_store(void)
{
    char id[32];
    if (web_ui_take_install_request(id)) {
        const store_list_t *list = store_list();
        for (int i = 0; i < list->count; ++i) {
            if (strcmp(list->apps[i].id, id) == 0) {
                install_from_store(i);
                break;
            }
        }
    }
    if (store_repos_changed()) {
        store_reload();
    }
}

/* ---------- Input ---------- */

static void select_item(int index)
{
    int rows = list_len();
    int total = rows + footer_count();
    s_ui.selected = (index % total + total) % total;
    if (s_ui.selected < rows) {
        if (s_ui.selected < s_ui.scroll) s_ui.scroll = s_ui.selected;
        if (s_ui.selected >= s_ui.scroll + VISIBLE_ROWS) s_ui.scroll = s_ui.selected - VISIBLE_ROWS + 1;
    }
}

static void activate_selected(void)
{
    if (s_ui.store_mode) {
        activate_store_item();
        return;
    }
    if (s_ui.selected == s_ui.count) {
        bool keep = !s_ui.keep_app;
        if (cyd_platform_set_keep_app_on_power_on(keep) == ESP_OK) {
            s_ui.keep_app = keep;
            set_status(keep ? "POWER-ON STARTS THE LAST APP" : "POWER-ON STARTS THE LAUNCHER", CYD_GREEN);
        } else {
            set_status("COULD NOT SAVE SETTING", CYD_RED);
        }
        redraw();
        return;
    }
    if (s_ui.selected == s_ui.count + 1) {
        update_launcher();
        return;
    }
    if (s_ui.selected == s_ui.count + 2) {
        open_store();
        return;
    }
    if (s_ui.selected >= 0 && s_ui.selected < s_ui.count) {
        launch_app(&s_ui.apps[s_ui.selected]);
    }
}

/* Returns the item index under a touch point, or -1. */
static int item_at(int x, int y)
{
    int rows = list_len();
    if (y >= FOOTER_TOP && y < FOOTER_TOP + FOOTER_HEIGHT) {
        for (int i = 0; i < footer_count(); ++i) {
            int bx, bw;
            footer_rect(i, &bx, &bw);
            if (x >= bx && x < bx + bw) return rows + i;
        }
        return -1;
    }
    if (y >= LIST_TOP && y < LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT) {
        int i = s_ui.scroll + (y - LIST_TOP) / ROW_HEIGHT;
        if (i < rows) return i;
    }
    return -1;
}

static void refresh_apps(void)
{
    s_ui.sd_ok = cyd_sd_mount() == ESP_OK;
    if (s_ui.sd_ok) {
        scan_apps();
        launcher_wifi_import_from_sd();
    } else {
        s_ui.count = 0;
    }
    select_item(0);

    char sha[65];
    load_installed(s_ui.installed_id, sizeof(s_ui.installed_id), sha, sizeof(sha));
    set_status(s_ui.count > 0 ? "TAP SELECT, TAP AGAIN OPEN; HOLD BOOT OPEN" : "", CYD_GREY);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(cyd_display_init());
    battery_init();
    battery_update();
    ESP_ERROR_CHECK(cyd_touch_init());
    ESP_ERROR_CHECK(cyd_button_init());

    /* If this is a freshly downloaded launcher running from ota_0, finish installing it. */
    self_update_finish_if_staged(on_progress);

    make_version_label(esp_app_get_description()->version, s_version, sizeof(s_version),
                       &s_version_is_release);
    ESP_LOGI(TAG, "Launcher %s (%s)", s_version, esp_app_get_description()->version);

    store_init();
    s_ui.progress = -1;
    s_ui.keep_app = cyd_platform_get_keep_app_on_power_on();
    refresh_apps();
    redraw();
    ESP_LOGI(TAG, "Launcher ready; free heap %" PRIu32 " bytes", esp_get_free_heap_size());

    bool was_touched = false;
    bool button_down = false;
    int64_t button_since = 0;
    int64_t last_sd_retry = esp_timer_get_time();
    int64_t last_battery_update = esp_timer_get_time();

    for (;;) {
        int64_t now = esp_timer_get_time();
        if (now - last_battery_update >= 2000000) {
            last_battery_update = now;
            int previous_percent = s_battery_percent;
            battery_update();
            if (previous_percent != s_battery_percent) redraw();
        }

        /* Touch: tap selects a row; tapping the selected row opens it. */
        int tx, ty;
        bool touched = cyd_touch_read(&tx, &ty);
        if (touched && !was_touched) {
            int item = item_at(tx, ty);
            if (item >= 0) {
                if (item == s_ui.selected) {
                    activate_selected();
                } else {
                    select_item(item);
                    redraw();
                }
            }
        }
        was_touched = touched;

        /* BOOT button: short press moves, long press opens. */
        bool pressed = cyd_button_pressed();
        if (pressed && !button_down) {
            button_down = true;
            button_since = now;
        } else if (!pressed && button_down) {
            button_down = false;
            if (now - button_since >= LONG_PRESS_US) {
                activate_selected();
            } else {
                select_item(s_ui.selected + 1);
                redraw();
            }
        }

        if (s_ui.store_mode) {
            poll_store();
        }

        /* Pick up a card inserted after boot. */
        if (!s_ui.store_mode && !s_ui.sd_ok && now - last_sd_retry > 2000000) {
            last_sd_retry = now;
            refresh_apps();
            if (s_ui.sd_ok) redraw();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
