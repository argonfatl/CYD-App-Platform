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
#include "launcher_wifi.h"
#include "self_update.h"
#include "esp_app_desc.h"
#include "esp_log.h"
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
#define FOOTER_SPLIT      150
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
} app_entry_t;

typedef struct {
    app_entry_t apps[MAX_APPS];
    int count;
    int selected;         /* 0..count-1 apps, count = power-on setting, count+1 = update */
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
} launcher_t;

static launcher_t s_ui;
static uint8_t s_chunk[COPY_CHUNK];

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

static void draw_screen(cyd_canvas_t *c, void *ctx)
{
    (void)ctx;
    cyd_fill(c, CYD_DARK);

    cyd_fill_rect(c, 0, 0, CYD_LCD_H_RES, 32, CYD_PANEL);
    cyd_fill_rect(c, 0, 32, CYD_LCD_H_RES, 2, CYD_CYAN);
    cyd_draw_text(c, "CYD APPS", 10, 9, 2, CYD_WHITE);
    const esp_app_desc_t *desc = esp_app_get_description();
    cyd_draw_text(c, desc->version, CYD_LCD_H_RES - 10 - cyd_text_width(desc->version, 1), 13, 1, CYD_GREY);

    if (s_ui.progress >= 0) {
        cyd_draw_text(c, s_ui.progress_label, 20, 90, 2, CYD_WHITE);
        cyd_fill_rect(c, 20, 120, CYD_LCD_H_RES - 40, 16, CYD_PANEL);
        cyd_fill_rect(c, 20, 120, (CYD_LCD_H_RES - 40) * s_ui.progress / 100, 16, CYD_CYAN);
    } else if (!s_ui.sd_ok) {
        cyd_draw_text(c, "NO SD CARD", 20, 80, 2, CYD_AMBER);
        cyd_draw_text(c, "INSERT A FAT32 CARD WITH", 20, 115, 1, CYD_WHITE);
        cyd_draw_text(c, "/APPS/<ID>/MANIFEST.JSON + APP.BIN", 20, 130, 1, CYD_WHITE);
    } else if (s_ui.count == 0) {
        cyd_draw_text(c, "NO APPS FOUND", 20, 80, 2, CYD_AMBER);
        cyd_draw_text(c, "COPY APPS TO /APPS/<ID>/ ON THE CARD", 20, 115, 1, CYD_WHITE);
    } else {
        for (int row = 0; row < VISIBLE_ROWS; ++row) {
            int i = s_ui.scroll + row;
            if (i >= s_ui.count) break;
            const app_entry_t *app = &s_ui.apps[i];
            int y = LIST_TOP + row * ROW_HEIGHT;
            bool sel = (i == s_ui.selected);
            cyd_fill_rect(c, 6, y, CYD_LCD_H_RES - 12, ROW_HEIGHT - 4, sel ? CYD_CYAN : CYD_PANEL);
            uint16_t fg = sel ? CYD_BLACK : CYD_WHITE;
            cyd_draw_text(c, app->name, 16, y + 8, 2, fg);
            if (app->version[0]) {
                cyd_draw_text(c, app->version, CYD_LCD_H_RES - 16 - cyd_text_width(app->version, 1),
                              y + 6, 1, sel ? CYD_BLACK : CYD_GREY);
            }
            if (strcmp(app->id, s_ui.installed_id) == 0) {
                cyd_draw_text(c, "INSTALLED", CYD_LCD_H_RES - 16 - cyd_text_width("INSTALLED", 1),
                              y + 18, 1, sel ? CYD_BLACK : CYD_GREEN);
            }
        }
    }

    /* Footer: power-on setting (left) and launcher update (right). */
    bool power_sel = (s_ui.selected == s_ui.count);
    cyd_fill_rect(c, 6, FOOTER_TOP, FOOTER_SPLIT - 9, FOOTER_HEIGHT, power_sel ? CYD_AMBER : CYD_PANEL);
    cyd_draw_text(c, s_ui.keep_app ? "POWER-ON: LAST APP" : "POWER-ON: LAUNCHER",
                  14, FOOTER_TOP + 7, 1, power_sel ? CYD_BLACK : CYD_WHITE);

    bool update_sel = (s_ui.selected == s_ui.count + 1);
    char update_text[40];
    if (s_ui.update_ready) {
        snprintf(update_text, sizeof(update_text), "INSTALL %s", s_ui.release.version);
    } else {
        snprintf(update_text, sizeof(update_text), "CHECK UPDATE");
    }
    uint16_t update_bg = update_sel ? CYD_AMBER : (s_ui.update_ready ? CYD_GREEN : CYD_PANEL);
    cyd_fill_rect(c, FOOTER_SPLIT + 3, FOOTER_TOP, CYD_LCD_H_RES - FOOTER_SPLIT - 9, FOOTER_HEIGHT, update_bg);
    cyd_draw_text(c, update_text, FOOTER_SPLIT + 11, FOOTER_TOP + 7, 1,
                  (update_sel || s_ui.update_ready) ? CYD_BLACK : CYD_WHITE);

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

/* ---------- Input ---------- */

static void select_item(int index)
{
    int total = s_ui.count + 2;
    s_ui.selected = (index % total + total) % total;
    if (s_ui.selected < s_ui.count) {
        if (s_ui.selected < s_ui.scroll) s_ui.scroll = s_ui.selected;
        if (s_ui.selected >= s_ui.scroll + VISIBLE_ROWS) s_ui.scroll = s_ui.selected - VISIBLE_ROWS + 1;
    }
}

static void activate_selected(void)
{
    if (s_ui.selected == s_ui.count) {
        bool keep = !s_ui.keep_app;
        if (cyd_platform_set_keep_app_on_power_on(keep) == ESP_OK) {
            s_ui.keep_app = keep;
            set_status("SETTING SAVED", CYD_GREEN);
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
    if (s_ui.selected >= 0 && s_ui.selected < s_ui.count) {
        launch_app(&s_ui.apps[s_ui.selected]);
    }
}

/* Returns the item index under a touch point, or -1. */
static int item_at(int x, int y)
{
    if (y >= FOOTER_TOP && y < FOOTER_TOP + FOOTER_HEIGHT) {
        return x < FOOTER_SPLIT ? s_ui.count : s_ui.count + 1;
    }
    if (y >= LIST_TOP && y < LIST_TOP + VISIBLE_ROWS * ROW_HEIGHT) {
        int i = s_ui.scroll + (y - LIST_TOP) / ROW_HEIGHT;
        if (i < s_ui.count) return i;
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
    set_status(s_ui.count > 0 ? "TAP OR BOOT: NEXT / HOLD BOOT: OPEN" : "", CYD_GREY);
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
    ESP_ERROR_CHECK(cyd_touch_init());
    ESP_ERROR_CHECK(cyd_button_init());

    /* If this is a freshly downloaded launcher running from ota_0, finish installing it. */
    self_update_finish_if_staged(on_progress);

    s_ui.progress = -1;
    s_ui.keep_app = cyd_platform_get_keep_app_on_power_on();
    refresh_apps();
    redraw();
    ESP_LOGI(TAG, "Launcher ready; free heap %" PRIu32 " bytes", esp_get_free_heap_size());

    bool was_touched = false;
    bool button_down = false;
    int64_t button_since = 0;
    int64_t last_sd_retry = esp_timer_get_time();

    for (;;) {
        int64_t now = esp_timer_get_time();

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

        /* Pick up a card inserted after boot. */
        if (!s_ui.sd_ok && now - last_sd_retry > 2000000) {
            last_sd_retry = now;
            refresh_apps();
            if (s_ui.sd_ok) redraw();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
