#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_elf.h"
#include "esp_err.h"
#include "esp_idf_version.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_H_RES 320
#define LCD_V_RES 240
#define LCD_PIN_MOSI 13
#define LCD_PIN_MISO 12
#define LCD_PIN_SCLK 14
#define LCD_PIN_CS   15
#define LCD_PIN_DC   2
#define LCD_PIN_BL   21

static const char *TAG = "cyd-platform";

extern const uint8_t clock_elf_start[] asm("_binary_clock_app_elf_start");

static esp_lcd_panel_handle_t s_panel;
static DMA_ATTR uint16_t s_framebuffer[LCD_H_RES * LCD_V_RES];
static int64_t s_clock_start_us;
static volatile bool s_clock_app_running;
static volatile bool s_clock_app_finished;

static const uint8_t s_font[][5] = {
    /* A-Z */
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    /* 0-9 */
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    /* colon and hyphen */
    {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    /* slash */
    {0x20,0x10,0x08,0x04,0x02}
};

enum { FONT_COLON = 36, FONT_HYPHEN = 37, FONT_SLASH = 38 };

static const uint8_t *glyph_for(char c)
{
    if (c >= 'A' && c <= 'Z') return s_font[c - 'A'];
    if (c >= 'a' && c <= 'z') return s_font[c - 'a'];
    if (c >= '0' && c <= '9') return s_font[26 + c - '0'];
    if (c == ':') return s_font[FONT_COLON];
    if (c == '-') return s_font[FONT_HYPHEN];
    if (c == '/') return s_font[FONT_SLASH];
    return NULL;
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_H_RES) w = LCD_H_RES - x;
    if (y + h > LCD_V_RES) h = LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    for (int row = y; row < y + h; ++row) {
        uint16_t *dst = &s_framebuffer[row * LCD_H_RES + x];
        for (int col = 0; col < w; ++col) dst[col] = color;
    }
}

static void draw_text(const char *text, int x, int y, int scale, uint16_t color)
{
    for (const char *p = text; *p; ++p, x += 6 * scale) {
        const uint8_t *glyph = glyph_for(*p);
        if (!glyph) continue;
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (glyph[col] & (1U << row)) {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

static esp_err_t display_init(void)
{
    gpio_config_t backlight_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_cfg), TAG, "Configure LCD backlight");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_PIN_BL, 1), TAG, "Enable LCD backlight");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(s_framebuffer),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "Initialize LCD SPI bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io_handle),
        TAG, "Create LCD panel IO");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#endif
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_handle, &panel_cfg, &s_panel),
                        TAG, "Create ILI9341 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Reset LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Initialize LCD panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "Set LCD color inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "Set LCD landscape orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "Set LCD mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Turn on LCD");
    ESP_LOGI(TAG, "ILI9341 display initialized (320x240 landscape)");
    return ESP_OK;
}

static void draw_clock_screen(void)
{
    const uint16_t bg = 0x0841;
    const uint16_t panel = 0x10A2;
    const uint16_t cyan = 0x07FF;
    const uint16_t white = 0xFFFF;
    const uint16_t green = 0x07E0;
    const uint16_t amber = 0xFD20;
    char clock_text[16];
    char elapsed_text[32];
    const uint64_t elapsed = (uint64_t)((esp_timer_get_time() - s_clock_start_us) / 1000000);
    const unsigned int hours = (unsigned int)((elapsed / 3600) % 100);
    const unsigned int minutes = (unsigned int)((elapsed / 60) % 60);
    const unsigned int seconds = (unsigned int)(elapsed % 60);

    snprintf(clock_text, sizeof(clock_text), "%02u:%02u:%02u", hours, minutes, seconds);
    snprintf(elapsed_text, sizeof(elapsed_text), "%s", s_clock_app_running ? "APP RUNNING" :
             (s_clock_app_finished ? "APP FINISHED" : "STARTING APP"));

    for (size_t i = 0; i < LCD_H_RES * LCD_V_RES; ++i) s_framebuffer[i] = bg;
    fill_rect(0, 0, LCD_H_RES, 5, cyan);
    fill_rect(16, 16, LCD_H_RES - 32, LCD_V_RES - 32, panel);
    fill_rect(16, 16, LCD_H_RES - 32, 2, cyan);
    draw_text("CYD CLOCK", 106, 30, 2, white);
    draw_text(elapsed_text, 127, 62, 1, s_clock_app_running ? green : amber);
    draw_text(clock_text, 64, 94, 4, white);
    draw_text("ELAPSED SINCE START", 103, 145, 1, cyan);
    draw_text("WALL CLOCK NOT SET", 103, 178, 1, white);
    draw_text("ELF MODULE / 30 SEC", 103, 198, 1, amber);
    fill_rect(0, LCD_V_RES - 5, LCD_H_RES, 5, cyan);

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_framebuffer);
    if (err != ESP_OK) ESP_LOGE(TAG, "LCD frame update failed: %s", esp_err_to_name(err));
}

static void display_task(void *arg)
{
    (void)arg;
    for (;;) {
        draw_clock_screen();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_elf_t elf;
    int ret;

    ESP_LOGI(TAG, "CYD App Platform booted");
    ESP_ERROR_CHECK(display_init());
    s_clock_start_us = esp_timer_get_time();
    xTaskCreate(display_task, "clock_display", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Clock UI started on the CYD display");
    ESP_LOGI(TAG, "Starting embedded Clock ELF prototype");

    ret = esp_elf_init(&elf);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_elf_init failed: %d", ret);
        return;
    }

    ret = esp_elf_relocate(&elf, clock_elf_start);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_elf_relocate failed: %d", ret);
        esp_elf_deinit(&elf);
        return;
    }

    s_clock_app_running = true;
    ESP_LOGI(TAG, "Clock ELF loaded; transferring control to app module");
    esp_elf_request(&elf, 0, 0, NULL);
    s_clock_app_running = false;
    s_clock_app_finished = true;
    ESP_LOGI(TAG, "Clock app returned; unloading ELF");

    esp_elf_deinit(&elf);
    ESP_LOGI(TAG, "Clock ELF unloaded; launcher core remains active");

    for (;;) {
        ESP_LOGI(TAG, "Launcher core alive; free heap: %" PRIu32 " bytes",
                 (uint32_t)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
