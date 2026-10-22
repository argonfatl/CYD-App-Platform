/*
 * Satellite Tracker for CYD App Platform.
 *
 * Downloads current orbital elements (TLE) from CelesTrak, works out where each
 * satellite is from this device's location with SGP4, and shows:
 *   SKY     a polar sky map of satellites above the horizon
 *   LIST    satellites above the horizon, highest first
 *   PASSES  upcoming passes, computed in the background
 *
 * Wi-Fi comes from the launcher's settings; the location from config.json on the
 * SD card or, if absent, from an IP lookup. See README.md.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <vector>

#include "cJSON.h"
#include "cyd_board.h"
#include "cyd_platform.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sat_math.h"

static const char *TAG = "sat_tracker";

#define APP_DIR          CYD_SD_MOUNT_POINT "/apps/sat-tracker"
#define CONFIG_PATH      APP_DIR "/config.json"
#define NVS_NS           "sat_tracker"
#define TLE_MAX_BYTES    (160 * 1024)
#define TLE_MAX_AGE_S    (12 * 3600)  /* CelesTrak asks clients not to refetch more often than needed */
#define MAX_SATS         220
/*
 * Each satellite needs about 1 KB for its SGP4 state. Stop loading while this much
 * heap is still free, for the pass list, vectors, and the HTTP/display code.
 */
#define HEAP_RESERVE     (64 * 1024)
#define PASS_HORIZON_S   (12 * 3600)
#define MAX_PASSES       40
#define ISS_CATNUM       25544
#define LONG_PRESS_US    700000

/* ---------- Layout ---------- */

#define HEADER_H   34
#define FOOTER_Y   214
#define FOOTER_H   24
#define SKY_CX     118
#define SKY_CY     124
#define SKY_R      86
#define PANEL_X    218
#define ROW_Y0     40
#define ROW_H      24
#define ROWS       7
#define SCROLL_W   30   /* touch scroll buttons beside long lists */

enum screen_t { SCREEN_SKY, SCREEN_LIST, SCREEN_PASSES };

/* ---------- State ---------- */

struct config_t {
    observer_t obs;
    bool have_location;
    char place[24];
    int utc_offset_s;
    double min_el;
    char group[24];
};

struct entry_t {
    satellite_t *sat;
    look_t look;
    bool valid;
};

static config_t s_cfg;
static std::vector<entry_t> s_sats;
static SemaphoreHandle_t s_sat_lock;   /* guards sgp4 state of each satellite */

static SemaphoreHandle_t s_pass_lock;  /* guards s_passes */
static std::vector<std::pair<int, pass_t>> s_passes; /* (satellite index, pass) */
static volatile int s_pass_progress = -1; /* 0..100 while computing, -1 when idle */
static volatile bool s_pass_ready;

static screen_t s_screen = SCREEN_SKY;
static int s_selected = -1;
static int64_t s_exit_armed_until;
static char s_message[64];
static int s_scroll; /* first row shown on LIST and PASSES */
static bool s_crashed; /* the previous run ended in a panic or watchdog reset */

/* ---------- Small drawing helpers ---------- */

static void pixel(cyd_canvas_t *c, int x, int y, uint16_t color)
{
    cyd_fill_rect(c, x, y, 1, 1, color);
}

static void circle(cyd_canvas_t *c, int cx, int cy, int r, uint16_t color)
{
    if (cy + r < c->y0 || cy - r >= c->y0 + c->h) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        pixel(c, cx + x, cy + y, color); pixel(c, cx - x, cy + y, color);
        pixel(c, cx + x, cy - y, color); pixel(c, cx - x, cy - y, color);
        pixel(c, cx + y, cy + x, color); pixel(c, cx - y, cy + x, color);
        pixel(c, cx + y, cy - x, color); pixel(c, cx - y, cy - x, color);
        ++y;
        if (err < 0) err += 2 * y + 1;
        else { --x; err += 2 * (y - x) + 1; }
    }
}

static void text_clipped(cyd_canvas_t *c, const char *text, int x, int y, int scale, int max_w, uint16_t color)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%s", text);
    while (buf[0] && cyd_text_width(buf, scale) > max_w) buf[strlen(buf) - 1] = '\0';
    cyd_draw_text(c, buf, x, y, scale, color);
}

static void text_right(cyd_canvas_t *c, const char *text, int right, int y, int scale, uint16_t color)
{
    cyd_draw_text(c, text, right - cyd_text_width(text, scale), y, scale, color);
}

static void text_center(cyd_canvas_t *c, const char *text, int y, int scale, uint16_t color)
{
    cyd_draw_text(c, text, (CYD_LCD_H_RES - cyd_text_width(text, scale)) / 2, y, scale, color);
}

static const char *compass(double az)
{
    static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return names[(int)floor((az + 22.5) / 45.0) % 8];
}

static void local_hhmm(time_t utc, char *out, size_t len, bool seconds)
{
    time_t local = utc + s_cfg.utc_offset_s;
    struct tm tm;
    gmtime_r(&local, &tm);
    if (seconds) snprintf(out, len, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    else snprintf(out, len, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* ---------- Status screen (startup and errors) ---------- */

static struct {
    const char *title;
    char line1[48];
    char line2[48];
    uint16_t color;
} s_status;

static void draw_status(cyd_canvas_t *c, void *ctx)
{
    cyd_fill(c, CYD_DARK);
    cyd_fill_rect(c, 0, 0, CYD_LCD_H_RES, HEADER_H - 2, CYD_PANEL);
    cyd_fill_rect(c, 0, HEADER_H - 2, CYD_LCD_H_RES, 2, CYD_CYAN);
    cyd_draw_text(c, "SATELLITES", 10, 9, 2, CYD_WHITE);
    text_center(c, s_status.title, 92, 2, s_status.color);
    text_center(c, s_status.line1, 124, 1, CYD_WHITE);
    text_center(c, s_status.line2, 140, 1, CYD_GREY);
    if (s_status.color == CYD_RED) text_center(c, "TAP OR HOLD BOOT TO EXIT", 210, 1, CYD_GREY);
}

static void show_status(const char *title, const char *line1, const char *line2, uint16_t color)
{
    s_status.title = title;
    snprintf(s_status.line1, sizeof(s_status.line1), "%s", line1 ? line1 : "");
    snprintf(s_status.line2, sizeof(s_status.line2), "%s", line2 ? line2 : "");
    s_status.color = color;
    cyd_display_render(draw_status, NULL);
}

/* Shows an error and waits until the user leaves for the launcher. */
[[noreturn]] static void fatal(const char *title, const char *line1, const char *line2)
{
    ESP_LOGE(TAG, "%s: %s %s", title, line1 ? line1 : "", line2 ? line2 : "");
    show_status(title, line1, line2, CYD_RED);
    int64_t pressed_since = -1;
    for (;;) {
        int x, y;
        if (cyd_touch_read(&x, &y)) cyd_platform_return_to_launcher(); /* any tap */
        if (cyd_button_pressed()) {
            if (pressed_since < 0) pressed_since = esp_timer_get_time();
            if (esp_timer_get_time() - pressed_since > LONG_PRESS_US) cyd_platform_return_to_launcher();
        } else {
            pressed_since = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

/* ---------- Configuration and location ---------- */

static double json_num(const cJSON *o, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void json_str(const cJSON *o, const char *key, char *out, size_t len)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring) snprintf(out, len, "%s", v->valuestring);
}

static void load_config(void)
{
    s_cfg.min_el = 10;
    s_cfg.utc_offset_s = 0;
    snprintf(s_cfg.group, sizeof(s_cfg.group), "visual");

    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) return;
    char text[512];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    cJSON *root = cJSON_Parse(text);
    if (root == NULL) {
        ESP_LOGW(TAG, "%s is not valid JSON", CONFIG_PATH);
        return;
    }
    double lat = json_num(root, "lat", NAN), lon = json_num(root, "lon", NAN);
    if (!isnan(lat) && !isnan(lon) && fabs(lat) <= 90 && fabs(lon) <= 180) {
        s_cfg.obs.lat_deg = lat;
        s_cfg.obs.lon_deg = lon;
        s_cfg.obs.alt_m = json_num(root, "alt_m", 0);
        s_cfg.have_location = true;
        snprintf(s_cfg.place, sizeof(s_cfg.place), "%.2f %.2f", lat, lon);
    }
    json_str(root, "name", s_cfg.place, sizeof(s_cfg.place));
    s_cfg.utc_offset_s = (int)(json_num(root, "utc_offset_min", 0) * 60);
    s_cfg.min_el = json_num(root, "min_elevation", 10);
    json_str(root, "group", s_cfg.group, sizeof(s_cfg.group));
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Config: group %s, min elevation %.0f", s_cfg.group, s_cfg.min_el);
}

static bool config_has_offset(void)
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (f == NULL) return false;
    char text[512];
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return strstr(text, "utc_offset_min") != NULL;
}

/*
 * Approximate location and current UTC offset from the public IP address. The device's
 * own address is private (192.168.x.x), but these services see the router's public
 * address. Several providers are tried because some networks block one or another.
 */
static char s_locate_error[40];

static bool apply_ip_result(double lat, double lon, const char *city, bool have_offset, int offset_s)
{
    if (!s_cfg.have_location) {
        s_cfg.obs.lat_deg = lat;
        s_cfg.obs.lon_deg = lon;
        s_cfg.obs.alt_m = 0;
        s_cfg.have_location = true;
        snprintf(s_cfg.place, sizeof(s_cfg.place), "%s", city ? city : "");
    }
    if (have_offset && !config_has_offset()) s_cfg.utc_offset_s = offset_s;
    return true;
}

static bool locate_by_ip(void)
{
    static const char *urls[] = {
        "https://ipwho.is/?fields=success,latitude,longitude,city,timezone",
        "https://ipapi.co/json/",
        "http://ip-api.com/json/?fields=status,lat,lon,city,offset",
    };
    for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i) {
        char *body = NULL;
        esp_err_t err = net_get(urls[i], 4096, &body, NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s: %s", urls[i], esp_err_to_name(err));
            snprintf(s_locate_error, sizeof(s_locate_error), "%s", esp_err_to_name(err));
            continue;
        }
        cJSON *root = cJSON_Parse(body);
        free(body);
        bool ok = false;
        char city[24] = "";
        json_str(root, "city", city, sizeof(city));
        if (i == 0) {
            const cJSON *success = cJSON_GetObjectItemCaseSensitive(root, "success");
            const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "timezone");
            const cJSON *off = cJSON_GetObjectItemCaseSensitive(tz, "offset");
            if (cJSON_IsTrue(success)) {
                ok = apply_ip_result(json_num(root, "latitude", 0), json_num(root, "longitude", 0), city,
                                     cJSON_IsNumber(off), cJSON_IsNumber(off) ? (int)off->valuedouble : 0);
            }
        } else if (i == 1) {
            double lat = json_num(root, "latitude", NAN), lon = json_num(root, "longitude", NAN);
            char off[8] = "";
            json_str(root, "utc_offset", off, sizeof(off)); /* "-0500" */
            int hhmm = atoi(off + (off[0] == '+' || off[0] == '-'));
            int secs = (hhmm / 100 * 60 + hhmm % 100) * 60 * (off[0] == '-' ? -1 : 1);
            if (!isnan(lat) && !isnan(lon)) ok = apply_ip_result(lat, lon, city, off[0] != '\0', secs);
        } else {
            const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
            if (cJSON_IsString(status) && strcmp(status->valuestring, "success") == 0) {
                ok = apply_ip_result(json_num(root, "lat", 0), json_num(root, "lon", 0), city, true,
                                     (int)json_num(root, "offset", 0));
            }
        }
        cJSON_Delete(root);
        if (ok) {
            ESP_LOGI(TAG, "IP location from %s", urls[i]);
            return true;
        }
        snprintf(s_locate_error, sizeof(s_locate_error), "UNEXPECTED REPLY");
    }
    return false;
}

static void save_location(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i32(h, "lat_u", (int32_t)llround(s_cfg.obs.lat_deg * 1e6));
    nvs_set_i32(h, "lon_u", (int32_t)llround(s_cfg.obs.lon_deg * 1e6));
    nvs_set_i32(h, "alt_m", (int32_t)s_cfg.obs.alt_m);
    nvs_set_i32(h, "utc_off", s_cfg.utc_offset_s);
    nvs_set_str(h, "place", s_cfg.place);
    nvs_commit(h);
    nvs_close(h);
}

static bool load_saved_location(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    int32_t lat, lon, alt = 0, off = 0;
    bool ok = nvs_get_i32(h, "lat_u", &lat) == ESP_OK && nvs_get_i32(h, "lon_u", &lon) == ESP_OK;
    if (ok) {
        nvs_get_i32(h, "alt_m", &alt);
        nvs_get_i32(h, "utc_off", &off);
        size_t len = sizeof(s_cfg.place);
        if (nvs_get_str(h, "place", s_cfg.place, &len) != ESP_OK) s_cfg.place[0] = '\0';
        s_cfg.obs.lat_deg = lat / 1e6;
        s_cfg.obs.lon_deg = lon / 1e6;
        s_cfg.obs.alt_m = alt;
        if (!config_has_offset()) s_cfg.utc_offset_s = off;
        s_cfg.have_location = true;
    }
    nvs_close(h);
    return ok;
}

/* ---------- Orbital elements ---------- */

static void tle_cache_path(char *out, size_t len)
{
    snprintf(out, len, APP_DIR "/%s.tle", s_cfg.group);
}

static int64_t tle_saved_at(void)
{
    nvs_handle_t h;
    int64_t t = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "tle_%.10s", s_cfg.group);
        nvs_get_i64(h, key, &t);
        nvs_close(h);
    }
    return t;
}

static bool save_tle_cache(const char *text)
{
    char path[64];
    tle_cache_path(path, sizeof(path));
    mkdir(CYD_SD_MOUNT_POINT "/apps", 0775);
    mkdir(APP_DIR, 0775);
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    bool ok = fputs(text, f) >= 0;
    fclose(f);
    nvs_handle_t h;
    if (ok && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "tle_%.10s", s_cfg.group);
        nvs_set_i64(h, key, (int64_t)time(NULL));
        nvs_commit(h);
        nvs_close(h);
    }
    return ok;
}

/*
 * Makes sure fresh TLEs are available. Prefers the SD card copy (and parsing it line by
 * line, so the whole text never sits in memory next to the satellites). Returns the text
 * in *text only when there is no usable SD copy; otherwise *text is NULL and the data is
 * in the cache file. Returns false if there is no data at all.
 */
static bool get_tles(char **text, bool *from_cache)
{
    char path[64];
    tle_cache_path(path, sizeof(path));
    struct stat st;
    bool have_cache = stat(path, &st) == 0 && st.st_size > 0;
    int64_t age = (int64_t)time(NULL) - tle_saved_at();
    *text = NULL;
    if (have_cache && age >= 0 && age < TLE_MAX_AGE_S) {
        *from_cache = true;
        ESP_LOGI(TAG, "Using cached TLEs (%lld min old)", (long long)(age / 60));
        return true;
    }

    char url[128];
    snprintf(url, sizeof(url), "https://celestrak.org/NORAD/elements/gp.php?GROUP=%s&FORMAT=tle", s_cfg.group);
    char *body = NULL;
    size_t len = 0;
    esp_err_t err = net_get(url, TLE_MAX_BYTES, &body, &len);
    if (err == ESP_OK && strstr(body, "\n1 ") != NULL) {
        ESP_LOGI(TAG, "Downloaded %u bytes of TLEs", (unsigned)len);
        *from_cache = false;
        if (save_tle_cache(body)) {
            free(body);
            return true;
        }
        *text = body; /* no SD card: parse from memory */
        return true;
    }
    ESP_LOGW(TAG, "TLE download failed (%s)", esp_err_to_name(err));
    free(body);
    *from_cache = true;
    return have_cache;
}

/* Adds one satellite unless memory is getting low. Returns false to stop loading. */
static bool add_satellite(const char *name, const char *line1, const char *line2, int limit)
{
    if ((int)s_sats.size() >= limit) return false;
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < HEAP_RESERVE + sizeof(satellite_t)) {
        ESP_LOGW(TAG, "Stopping at %u satellites to keep %u KB of memory free", (unsigned)s_sats.size(),
                 (unsigned)(HEAP_RESERVE / 1024));
        return false;
    }
    satellite_t *sat = (satellite_t *)malloc(sizeof(satellite_t));
    if (sat == NULL) return false;
    if (sat_parse_tle(name, line1, line2, sat)) {
        entry_t e = {};
        e.sat = sat;
        s_sats.push_back(e);
    } else {
        free(sat);
    }
    return true;
}

static bool is_line(const char *l, char kind)
{
    return l[0] == kind && l[1] == ' ';
}

static void trim_eol(char *l)
{
    l[strcspn(l, "\r\n")] = '\0';
}

/* Reads "name / line 1 / line 2" triples (the name is optional) from text or the cache file. */
static int load_satellites(char *text, int limit)
{
    s_sats.reserve(limit);
    char prev[80] = "", l1[80] = "", cur[80];
    if (text) {
        char *save = NULL;
        for (char *l = strtok_r(text, "\r\n", &save); l; l = strtok_r(NULL, "\r\n", &save)) {
            snprintf(cur, sizeof(cur), "%s", l);
            if (is_line(cur, '1')) {
                snprintf(l1, sizeof(l1), "%s", cur);
            } else if (is_line(cur, '2') && l1[0]) {
                if (!add_satellite(prev[0] ? prev : NULL, l1, cur, limit)) break;
                l1[0] = prev[0] = '\0';
            } else {
                snprintf(prev, sizeof(prev), "%s", cur);
            }
        }
        return (int)s_sats.size();
    }
    char path[64];
    tle_cache_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (f == NULL) return 0;
    while (fgets(cur, sizeof(cur), f)) {
        trim_eol(cur);
        if (!cur[0]) continue;
        if (is_line(cur, '1')) {
            snprintf(l1, sizeof(l1), "%s", cur);
        } else if (is_line(cur, '2') && l1[0]) {
            if (!add_satellite(prev[0] ? prev : NULL, l1, cur, limit)) break;
            l1[0] = prev[0] = '\0';
        } else {
            snprintf(prev, sizeof(prev), "%s", cur);
        }
    }
    fclose(f);
    return (int)s_sats.size();
}

/* ---------- Positions ---------- */

static void update_positions(void)
{
    time_t now = time(NULL);
    double jd = sat_jd(now, 0);
    for (auto &e : s_sats) {
        xSemaphoreTake(s_sat_lock, portMAX_DELAY);
        e.valid = sat_look(e.sat, &s_cfg.obs, jd, &e.look);
        xSemaphoreGive(s_sat_lock);
    }
}

/* Indices of satellites above the horizon, highest first. */
static std::vector<int> visible_sorted(void)
{
    std::vector<int> v;
    for (int i = 0; i < (int)s_sats.size(); ++i) {
        if (s_sats[i].valid && s_sats[i].look.el_deg > 0) v.push_back(i);
    }
    std::sort(v.begin(), v.end(), [](int a, int b) { return s_sats[a].look.el_deg > s_sats[b].look.el_deg; });
    return v;
}

/* ---------- Pass prediction (background task) ---------- */

static void sort_and_trim(std::vector<std::pair<int, pass_t>> &found)
{
    std::sort(found.begin(), found.end(),
              [](const std::pair<int, pass_t> &a, const std::pair<int, pass_t> &b) { return a.second.rise < b.second.rise; });
    if (found.size() > MAX_PASSES) found.resize(MAX_PASSES);
}

static void pass_task(void *arg)
{
    satellite_t *copy = (satellite_t *)malloc(sizeof(satellite_t));
    if (copy == NULL) {
        ESP_LOGE(TAG, "Not enough memory for pass prediction");
        vTaskDelete(NULL);
    }
    for (;;) {
        time_t from = time(NULL);
        std::vector<std::pair<int, pass_t>> found;
        found.reserve(2 * MAX_PASSES + 3);
        int n = (int)s_sats.size();
        for (int i = 0; i < n; ++i) {
            s_pass_progress = i * 100 / n;
            xSemaphoreTake(s_sat_lock, portMAX_DELAY);
            memcpy(copy, s_sats[i].sat, sizeof(satellite_t));
            xSemaphoreGive(s_sat_lock);

            /* Up to three passes per satellite within the horizon. */
            time_t t = from;
            for (int k = 0; k < 3; ++k) {
                pass_t p;
                if (!sat_next_pass(copy, &s_cfg.obs, t, from + PASS_HORIZON_S, s_cfg.min_el, 60, &p)) break;
                found.push_back({i, p});
                t = p.set + 60;
            }
            /* Keep only the earliest passes so memory use stays bounded. */
            if (found.size() > 2 * MAX_PASSES) sort_and_trim(found);
            vTaskDelay(1); /* let the UI task run */
        }
        sort_and_trim(found);

        xSemaphoreTake(s_pass_lock, portMAX_DELAY);
        s_passes = found;
        xSemaphoreGive(s_pass_lock);
        s_pass_ready = true;
        s_pass_progress = -1;
        ESP_LOGI(TAG, "%u passes in the next %d h (took %lld s)", (unsigned)found.size(), PASS_HORIZON_S / 3600,
                 (long long)(time(NULL) - from));

        /* Recompute when the earliest listed pass is over, or every 30 minutes. */
        for (int waited = 0; waited < 1800; waited += 10) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            xSemaphoreTake(s_pass_lock, portMAX_DELAY);
            bool stale = s_passes.empty() || s_passes.front().second.set < time(NULL);
            xSemaphoreGive(s_pass_lock);
            if (stale && waited >= 300) break;
        }
    }
}

/* ---------- Screens ---------- */

static void sky_point(double az, double el, int *x, int *y)
{
    double d = SKY_R * (90.0 - el) / 90.0;
    *x = SKY_CX + (int)lround(d * sin(az * M_PI / 180.0));
    *y = SKY_CY - (int)lround(d * cos(az * M_PI / 180.0));
}

static uint16_t el_color(double el)
{
    return el >= 45 ? CYD_GREEN : (el >= 15 ? CYD_CYAN : CYD_GREY);
}

static void draw_sky(cyd_canvas_t *c, const std::vector<int> &up)
{
    circle(c, SKY_CX, SKY_CY, SKY_R, CYD_GREY);
    circle(c, SKY_CX, SKY_CY, SKY_R * 2 / 3, CYD_PANEL);
    circle(c, SKY_CX, SKY_CY, SKY_R / 3, CYD_PANEL);
    cyd_fill_rect(c, SKY_CX, SKY_CY - SKY_R, 1, 2 * SKY_R, CYD_PANEL);
    cyd_fill_rect(c, SKY_CX - SKY_R, SKY_CY, 2 * SKY_R, 1, CYD_PANEL);
    cyd_draw_text(c, "N", SKY_CX - 2, SKY_CY - SKY_R - 9, 1, CYD_WHITE);
    cyd_draw_text(c, "S", SKY_CX - 2, SKY_CY + SKY_R + 3, 1, CYD_GREY);
    cyd_draw_text(c, "E", SKY_CX + SKY_R + 4, SKY_CY - 3, 1, CYD_GREY);
    cyd_draw_text(c, "W", SKY_CX - SKY_R - 9, SKY_CY - 3, 1, CYD_GREY);

    for (int i : up) {
        if (i == s_selected) continue;
        int x, y;
        sky_point(s_sats[i].look.az_deg, s_sats[i].look.el_deg, &x, &y);
        bool iss = s_sats[i].sat->catnum == ISS_CATNUM;
        int r = iss ? 3 : 1;
        cyd_fill_rect(c, x - r, y - r, 2 * r + 1, 2 * r + 1, iss ? CYD_WHITE : el_color(s_sats[i].look.el_deg));
    }
    if (s_selected >= 0 && s_sats[s_selected].valid && s_sats[s_selected].look.el_deg > 0) {
        int x, y;
        sky_point(s_sats[s_selected].look.az_deg, s_sats[s_selected].look.el_deg, &x, &y);
        cyd_fill_rect(c, x - 3, y - 3, 7, 7, CYD_AMBER);
        circle(c, x, y, 6, CYD_AMBER);
    }

    /* Right panel: count and the selected satellite. */
    char buf[40];
    snprintf(buf, sizeof(buf), "%d UP", (int)up.size());
    cyd_draw_text(c, buf, PANEL_X, 42, 2, CYD_WHITE);
    snprintf(buf, sizeof(buf), "OF %d", (int)s_sats.size());
    cyd_draw_text(c, buf, PANEL_X, 62, 1, CYD_GREY);

    if (s_selected < 0) return;
    const entry_t &e = s_sats[s_selected];
    int y = 82;
    cyd_fill_rect(c, PANEL_X, y, CYD_LCD_H_RES - PANEL_X - 4, 1, CYD_PANEL);
    y += 6;
    text_clipped(c, e.sat->name, PANEL_X, y, 1, CYD_LCD_H_RES - PANEL_X - 4, CYD_AMBER);
    y += 14;
    if (!e.valid) {
        cyd_draw_text(c, "NO POSITION", PANEL_X, y, 1, CYD_RED);
        return;
    }
    if (e.look.el_deg > 0) {
        snprintf(buf, sizeof(buf), "EL %.0f", e.look.el_deg);
        cyd_draw_text(c, buf, PANEL_X, y, 2, CYD_GREEN);
        y += 20;
        snprintf(buf, sizeof(buf), "AZ %.0f %s", e.look.az_deg, compass(e.look.az_deg));
        cyd_draw_text(c, buf, PANEL_X, y, 1, CYD_WHITE);
        y += 12;
    } else {
        cyd_draw_text(c, "BELOW HORIZON", PANEL_X, y, 1, CYD_GREY);
        y += 12;
        xSemaphoreTake(s_pass_lock, portMAX_DELAY);
        for (auto &p : s_passes) {
            if (p.first == s_selected) {
                char t[8];
                local_hhmm(p.second.rise, t, sizeof(t), false);
                snprintf(buf, sizeof(buf), "NEXT %s", t);
                cyd_draw_text(c, buf, PANEL_X, y, 1, CYD_CYAN);
                y += 12;
                snprintf(buf, sizeof(buf), "MAX EL %.0f", p.second.max_el_deg);
                cyd_draw_text(c, buf, PANEL_X, y, 1, CYD_CYAN);
                y += 12;
                break;
            }
        }
        xSemaphoreGive(s_pass_lock);
    }
    snprintf(buf, sizeof(buf), "RANGE %.0f KM", e.look.range_km);
    cyd_draw_text(c, buf, PANEL_X, y, 1, CYD_GREY);
    y += 12;
    snprintf(buf, sizeof(buf), "ALT %.0f KM", e.look.alt_km);
    cyd_draw_text(c, buf, PANEL_X, y, 1, CYD_GREY);
}

/* Upcoming or current passes (the background list also holds ones that ended). */
static std::vector<std::pair<int, pass_t>> current_passes(void)
{
    xSemaphoreTake(s_pass_lock, portMAX_DELAY);
    std::vector<std::pair<int, pass_t>> passes = s_passes;
    xSemaphoreGive(s_pass_lock);
    time_t now = time(NULL);
    passes.erase(std::remove_if(passes.begin(), passes.end(),
                                [now](const std::pair<int, pass_t> &p) { return p.second.set < now; }),
                 passes.end());
    return passes;
}

/* Everything works by touch (BOOT may be out of reach): long lists get up/down buttons. */
static int row_right(int count)
{
    return count > ROWS ? CYD_LCD_H_RES - 4 - SCROLL_W - 4 : CYD_LCD_H_RES - 4;
}

static int clamp_scroll(int count)
{
    int max = count > ROWS ? count - ROWS : 0;
    if (s_scroll > max) s_scroll = max;
    if (s_scroll < 0) s_scroll = 0;
    return s_scroll;
}

static void draw_arrow(cyd_canvas_t *c, int cx, int cy, bool up, uint16_t color)
{
    for (int i = 0; i < 9; ++i) cyd_fill_rect(c, cx - i, up ? cy - 4 + i : cy + 4 - i, 2 * i + 1, 1, color);
}

static void draw_scroll_buttons(cyd_canvas_t *c, int count)
{
    if (count <= ROWS) return;
    int x = CYD_LCD_H_RES - 4 - SCROLL_W, h = (ROWS * ROW_H - 3 - 4) / 2;
    cyd_fill_rect(c, x, ROW_Y0, SCROLL_W, h, CYD_PANEL);
    cyd_fill_rect(c, x, ROW_Y0 + h + 4, SCROLL_W, h, CYD_PANEL);
    draw_arrow(c, x + SCROLL_W / 2, ROW_Y0 + h / 2, true, s_scroll > 0 ? CYD_WHITE : CYD_GREY);
    draw_arrow(c, x + SCROLL_W / 2, ROW_Y0 + h + 4 + h / 2, false, s_scroll < count - ROWS ? CYD_WHITE : CYD_GREY);
}

/* Returns -1/+1 when the tap hit a scroll button, else 0. */
static int scroll_button_at(int x, int y, int count)
{
    if (count <= ROWS || x < CYD_LCD_H_RES - 6 - SCROLL_W || y < ROW_Y0 || y >= ROW_Y0 + ROWS * ROW_H) return 0;
    return y < ROW_Y0 + ROWS * ROW_H / 2 ? -1 : 1;
}

static void draw_list(cyd_canvas_t *c, const std::vector<int> &up)
{
    if (up.empty()) {
        text_center(c, "NOTHING ABOVE THE HORIZON", 110, 1, CYD_GREY);
        return;
    }
    int first = clamp_scroll((int)up.size()), right = row_right((int)up.size());
    for (int row = 0; row < ROWS && first + row < (int)up.size(); ++row) {
        int idx = up[first + row];
        const entry_t &e = s_sats[idx];
        int y = ROW_Y0 + row * ROW_H;
        bool sel = idx == s_selected;
        cyd_fill_rect(c, 4, y, right - 4, ROW_H - 3, sel ? CYD_AMBER : CYD_PANEL);
        uint16_t fg = sel ? CYD_BLACK : CYD_WHITE;
        text_clipped(c, e.sat->name, 10, y + 4, 2, right - 150, fg);
        char buf[32];
        snprintf(buf, sizeof(buf), "EL %2.0f %s", e.look.el_deg, compass(e.look.az_deg));
        text_right(c, buf, right - 6, y + 3, 1, sel ? CYD_BLACK : el_color(e.look.el_deg));
        snprintf(buf, sizeof(buf), "%.0f KM", e.look.range_km);
        text_right(c, buf, right - 6, y + 12, 1, sel ? CYD_BLACK : CYD_GREY);
    }
    draw_scroll_buttons(c, (int)up.size());
}

static void draw_passes(cyd_canvas_t *c)
{
    std::vector<std::pair<int, pass_t>> passes = current_passes();
    time_t now = time(NULL);

    if (passes.empty()) {
        char buf[40];
        if (s_pass_progress >= 0) snprintf(buf, sizeof(buf), "COMPUTING PASSES %d%%", s_pass_progress);
        else snprintf(buf, sizeof(buf), "NO PASSES ABOVE %.0f DEG", s_cfg.min_el);
        text_center(c, buf, 110, 1, CYD_GREY);
        return;
    }
    int first = clamp_scroll((int)passes.size()), right = row_right((int)passes.size());
    for (int row = 0; row < ROWS && first + row < (int)passes.size(); ++row) {
        const pass_t &p = passes[first + row].second;
        int idx = passes[first + row].first;
        int y = ROW_Y0 + row * ROW_H;
        bool now_up = p.rise <= now && now <= p.set;
        bool sel = idx == s_selected;
        cyd_fill_rect(c, 4, y, right - 4, ROW_H - 3, sel ? CYD_AMBER : (now_up ? CYD_RGB(10, 60, 30) : CYD_PANEL));
        uint16_t fg = sel ? CYD_BLACK : CYD_WHITE;
        char t[8], buf[48];
        local_hhmm(p.rise, t, sizeof(t), false);
        cyd_draw_text(c, now_up ? "NOW" : t, 10, y + 4, 2, sel ? CYD_BLACK : (now_up ? CYD_GREEN : CYD_CYAN));
        text_clipped(c, s_sats[idx].sat->name, 78, y + 3, 1, right - 84, fg);
        snprintf(buf, sizeof(buf), "MAX %.0f  %d MIN  %s>%s", p.max_el_deg, (int)((p.set - p.rise + 30) / 60),
                 compass(p.rise_az_deg), compass(p.set_az_deg));
        text_clipped(c, buf, 78, y + 12, 1, right - 84, sel ? CYD_BLACK : CYD_GREY);
    }
    draw_scroll_buttons(c, (int)passes.size());
}

static const char *FOOTER[] = {"SKY", "LIST", "PASSES", "EXIT"};

static void footer_rect(int i, int *x, int *w)
{
    *w = (CYD_LCD_H_RES - 8 - 3 * 4) / 4;
    *x = 4 + i * (*w + 4);
}

static void draw_screen(cyd_canvas_t *c, void *ctx)
{
    const std::vector<int> &up = *(const std::vector<int> *)ctx;
    cyd_fill(c, CYD_DARK);

    /* Header: screen title, local time, place. */
    cyd_fill_rect(c, 0, 0, CYD_LCD_H_RES, HEADER_H - 2, CYD_PANEL);
    cyd_fill_rect(c, 0, HEADER_H - 2, CYD_LCD_H_RES, 2, CYD_CYAN);
    const char *titles[] = {"SKY", "OVERHEAD", "PASSES"};
    cyd_draw_text(c, titles[s_screen], 10, 9, 2, CYD_WHITE);
    char clock[12];
    local_hhmm(time(NULL), clock, sizeof(clock), true);
    text_right(c, clock, CYD_LCD_H_RES - 8, 6, 1, CYD_CYAN);
    char place[24];
    snprintf(place, sizeof(place), "%.20s", s_cfg.place);
    text_right(c, place, CYD_LCD_H_RES - 8, 18, 1, CYD_GREY);

    if (s_screen == SCREEN_SKY) draw_sky(c, up);
    else if (s_screen == SCREEN_LIST) draw_list(c, up);
    else draw_passes(c);

    if (s_message[0]) text_center(c, s_message, FOOTER_Y - 11, 1, CYD_AMBER);

    for (int i = 0; i < 4; ++i) {
        int x, w;
        footer_rect(i, &x, &w);
        bool active = i == (int)s_screen;
        bool armed = i == 3 && esp_timer_get_time() < s_exit_armed_until;
        uint16_t bg = armed ? CYD_RED : (active ? CYD_CYAN : CYD_PANEL);
        const char *label = armed ? "AGAIN" : FOOTER[i];
        cyd_fill_rect(c, x, FOOTER_Y, w, FOOTER_H, bg);
        cyd_draw_text(c, label, x + (w - cyd_text_width(label, 1)) / 2, FOOTER_Y + 9, 1,
                      (active || armed) ? CYD_BLACK : CYD_WHITE);
    }
}

/* ---------- Input ---------- */

static void handle_tap(int x, int y, const std::vector<int> &up)
{
    s_message[0] = '\0';
    if (y >= FOOTER_Y - 4) {
        for (int i = 0; i < 4; ++i) {
            int bx, bw;
            footer_rect(i, &bx, &bw);
            if (x < bx || x >= bx + bw) continue;
            if (i == 3) {
                if (esp_timer_get_time() < s_exit_armed_until) cyd_platform_return_to_launcher();
                s_exit_armed_until = esp_timer_get_time() + 3000000;
            } else {
                if (s_screen != (screen_t)i) s_scroll = 0;
                s_screen = (screen_t)i;
            }
        }
        return;
    }
    if (y < ROW_Y0) return;
    int row = (y - ROW_Y0) / ROW_H;
    if (s_screen == SCREEN_LIST) {
        int count = (int)up.size(), step = scroll_button_at(x, y, count);
        if (step) {
            s_scroll += step * (ROWS - 1);
            clamp_scroll(count);
        } else if (row < ROWS && s_scroll + row < count) {
            s_selected = up[s_scroll + row];
            s_screen = SCREEN_SKY;
        }
    } else if (s_screen == SCREEN_PASSES) {
        std::vector<std::pair<int, pass_t>> passes = current_passes();
        int count = (int)passes.size(), step = scroll_button_at(x, y, count);
        if (step) {
            s_scroll += step * (ROWS - 1);
            clamp_scroll(count);
        } else if (row < ROWS && s_scroll + row < count) {
            s_selected = passes[s_scroll + row].first;
            s_screen = SCREEN_SKY;
        }
    } else if (s_screen == SCREEN_SKY && x < PANEL_X - 8) {
        /* Select the satellite nearest to the tap on the sky map. */
        int best = -1, best_d = 18 * 18;
        for (int i : up) {
            int sx, sy;
            sky_point(s_sats[i].look.az_deg, s_sats[i].look.el_deg, &sx, &sy);
            int d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best >= 0) s_selected = best;
    }
}

/* ---------- Startup ---------- */

extern "C" void app_main(void)
{
    esp_reset_reason_t reset = esp_reset_reason();
    s_crashed = reset == ESP_RST_PANIC || reset == ESP_RST_TASK_WDT || reset == ESP_RST_INT_WDT || reset == ESP_RST_WDT;
    if (s_crashed) ESP_LOGW(TAG, "Previous run crashed (reset reason %d)", (int)reset);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(cyd_display_init());
    ESP_ERROR_CHECK(cyd_touch_init());
    ESP_ERROR_CHECK(cyd_button_init());
    s_sat_lock = xSemaphoreCreateMutex();
    s_pass_lock = xSemaphoreCreateMutex();

    bool sd = cyd_sd_mount() == ESP_OK;
    if (sd) load_config();
    else {
        s_cfg.min_el = 10;
        snprintf(s_cfg.group, sizeof(s_cfg.group), "visual");
    }

    show_status("CONNECTING", "WI-FI FROM THE LAUNCHER", NULL, CYD_CYAN);
    err = net_wifi_start(20000);
    if (err == ESP_ERR_NOT_FOUND) fatal("NO WI-FI", "SET UP WIFI.JSON IN THE LAUNCHER", NULL);
    if (err != ESP_OK) fatal("WI-FI FAILED", "COULD NOT CONNECT", NULL);

    /*
     * The launcher sets the clock from NTP and it survives the restart into this app.
     * Otherwise try NTP here, then the Date header of a web server (for networks
     * that block NTP).
     */
    const char *clock_source = "LAUNCHER";
    if (time(NULL) < 1700000000) {
        show_status("SETTING CLOCK", "NTP", NULL, CYD_CYAN);
        clock_source = "NTP";
        if (net_sync_time(10000) != ESP_OK) {
            show_status("SETTING CLOCK", "WEB SERVER TIME", NULL, CYD_CYAN);
            clock_source = "WEB";
            if (net_time_from_http("https://celestrak.org/") != ESP_OK &&
                net_time_from_http("http://ip-api.com/") != ESP_OK) {
                fatal("NO TIME", "COULD NOT GET THE TIME FROM", "NTP OR WEB SERVERS");
            }
        }
    }
    ESP_LOGI(TAG, "Clock from %s", clock_source);

    /*
     * Location, in order: config.json on the SD card, the location saved in the
     * launcher (GET APPS web page), an IP lookup, the last location this app used.
     */
    const char *source = "CONFIG.JSON";
    bool have_offset = config_has_offset();
    cyd_location_t shared;
    if (!s_cfg.have_location && cyd_platform_get_location(&shared)) {
        s_cfg.obs.lat_deg = shared.lat_deg;
        s_cfg.obs.lon_deg = shared.lon_deg;
        s_cfg.obs.alt_m = shared.alt_m;
        snprintf(s_cfg.place, sizeof(s_cfg.place), "%.23s", shared.name);
        s_cfg.have_location = true;
        source = "LAUNCHER";
        if (!have_offset) s_cfg.utc_offset_s = shared.utc_offset_min * 60;
    }
    if (!s_cfg.have_location || !have_offset) {
        /* Also refreshes the UTC offset (daylight saving time) when only that is missing. */
        show_status("LOCATING", s_cfg.have_location ? "UTC OFFSET FROM YOUR IP" : "FROM YOUR IP ADDRESS", NULL, CYD_CYAN);
        bool had_location = s_cfg.have_location;
        if (locate_by_ip()) {
            if (!had_location) source = "IP ADDRESS";
            save_location();
        } else if (!s_cfg.have_location && load_saved_location()) {
            source = "LAST RUN";
        }
    }
    if (!s_cfg.have_location) {
        ESP_LOGE(TAG, "IP lookup failed: %s", s_locate_error);
        fatal("NO LOCATION", "SET IT IN THE LAUNCHER: GET APPS,", "THEN THE WEB PAGE SHOWN ON SCREEN");
    }
    ESP_LOGI(TAG, "Location from %s", source);
    ESP_LOGI(TAG, "Observer %.4f, %.4f (%s), UTC offset %d s", s_cfg.obs.lat_deg, s_cfg.obs.lon_deg,
             s_cfg.place, s_cfg.utc_offset_s);

    char line[48];
    snprintf(line, sizeof(line), "CELESTRAK GROUP %.20s", s_cfg.group);
    show_status("LOADING ORBITS", line, NULL, CYD_CYAN);
    bool cached = false;
    char *tles = NULL;
    bool have_tles = get_tles(&tles, &cached);

    /* Wi-Fi is no longer needed; its memory holds the satellite data instead. */
    net_wifi_stop();
    if (!have_tles) fatal("NO ORBIT DATA", "CELESTRAK DOWNLOAD FAILED", "AND NO COPY ON THE SD CARD");

    /* After a crash, load fewer satellites so a memory problem cannot loop forever. */
    int limit = MAX_SATS;
    if (s_crashed) {
        limit = 60;
        snprintf(s_message, sizeof(s_message), "RESTARTED AFTER A CRASH: 60 SATELLITES");
    }
    ESP_LOGI(TAG, "Free heap before loading %u (largest block %u)", (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    int count = load_satellites(tles, limit);
    free(tles);
    if (count == 0) fatal("NO SATELLITES", "THE ORBIT DATA COULD NOT BE READ", NULL);
    ESP_LOGI(TAG, "%d satellites loaded (%s); free heap %u", count, cached ? "cache" : "download",
             (unsigned)esp_get_free_heap_size());

    for (int i = 0; i < count; ++i) {
        if (s_sats[i].sat->catnum == ISS_CATNUM) s_selected = i;
    }
    if (s_selected < 0) s_selected = 0;
    if (cached && !s_message[0]) snprintf(s_message, sizeof(s_message), "USING SAVED ORBITS");

    if (xTaskCreatePinnedToCore(pass_task, "passes", 10240, NULL, 2, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Not enough memory for pass prediction");
    }

    /* ---------- Main loop ---------- */
    std::vector<int> up;
    int64_t last_update = 0, button_since = -1;
    bool was_touched = false;
    for (;;) {
        int64_t now = esp_timer_get_time();
        bool dirty = false;

        if (now - last_update >= 1000000) {
            last_update = now;
            update_positions();
            up = visible_sorted();
            dirty = true;
        }

        int tx, ty;
        bool touched = cyd_touch_read(&tx, &ty);
        if (touched && !was_touched) {
            handle_tap(tx, ty, up);
            dirty = true;
        }
        was_touched = touched;

        if (cyd_button_pressed()) {
            if (button_since < 0) button_since = now;
            if (now - button_since > LONG_PRESS_US) cyd_platform_return_to_launcher();
        } else if (button_since >= 0) {
            button_since = -1;
            s_screen = (screen_t)((s_screen + 1) % 3);
            s_scroll = 0;
            dirty = true;
        }

        if (dirty) cyd_display_render(draw_screen, &up);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
