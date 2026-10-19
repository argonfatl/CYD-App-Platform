/*
 * Board support for the common 2.8-inch ESP32 Cheap Yellow Display
 * (ESP32-2432S028R: ILI9341 + XPT2046 resistive touch + microSD).
 *
 * Shared by the launcher and example apps. Other CYD variants need different
 * pins or panel settings; change them here.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Display: ILI9341 on SPI2 (HSPI). */
#define CYD_LCD_H_RES        320
#define CYD_LCD_V_RES        240
#define CYD_LCD_PIN_MOSI     13
#define CYD_LCD_PIN_MISO     12
#define CYD_LCD_PIN_SCLK     14
#define CYD_LCD_PIN_CS       15
#define CYD_LCD_PIN_DC       2
#define CYD_LCD_PIN_BL       21

/* Touch: XPT2046, bit-banged because both hardware SPI hosts are in use. */
#define CYD_TOUCH_PIN_CLK    25
#define CYD_TOUCH_PIN_MOSI   32
#define CYD_TOUCH_PIN_MISO   39
#define CYD_TOUCH_PIN_CS     33
#define CYD_TOUCH_PIN_IRQ    36

/* Raw touch range and orientation for landscape. Calibrate on your board. */
#define CYD_TOUCH_RAW_X_MIN  200
#define CYD_TOUCH_RAW_X_MAX  3700
#define CYD_TOUCH_RAW_Y_MIN  240
#define CYD_TOUCH_RAW_Y_MAX  3800
#define CYD_TOUCH_SWAP_XY    0
#define CYD_TOUCH_INVERT_X   0
#define CYD_TOUCH_INVERT_Y   0

/* microSD on SPI3 (VSPI). */
#define CYD_SD_PIN_MOSI      23
#define CYD_SD_PIN_MISO      19
#define CYD_SD_PIN_SCLK      18
#define CYD_SD_PIN_CS        5
#define CYD_SD_MOUNT_POINT   "/sdcard"

/* BOOT button (GPIO0, active low). Usable as an input after boot. */
#define CYD_BUTTON_PIN       0

/* RGB565 colors. */
#define CYD_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define CYD_BLACK  CYD_RGB(0, 0, 0)
#define CYD_WHITE  CYD_RGB(255, 255, 255)
#define CYD_GREY   CYD_RGB(120, 120, 130)
#define CYD_DARK   CYD_RGB(16, 18, 24)
#define CYD_PANEL  CYD_RGB(32, 36, 48)
#define CYD_CYAN   CYD_RGB(0, 220, 255)
#define CYD_GREEN  CYD_RGB(40, 220, 90)
#define CYD_AMBER  CYD_RGB(255, 170, 0)
#define CYD_RED    CYD_RGB(240, 60, 60)

/*
 * The display is drawn in horizontal stripes so no full framebuffer is needed.
 * A draw callback is called once per stripe; drawing primitives clip to it.
 */
typedef struct {
    uint16_t *buf; /* stripe pixels, already in panel byte order */
    int y0;        /* first screen row of this stripe */
    int h;         /* rows in this stripe */
} cyd_canvas_t;

typedef void (*cyd_draw_fn_t)(cyd_canvas_t *canvas, void *ctx);

esp_err_t cyd_display_init(void);
esp_err_t cyd_display_render(cyd_draw_fn_t draw, void *ctx);

void cyd_fill(cyd_canvas_t *c, uint16_t color);
void cyd_fill_rect(cyd_canvas_t *c, int x, int y, int w, int h, uint16_t color);
/* 5x7 font, scaled. Lower case is drawn as upper case. */
void cyd_draw_text(cyd_canvas_t *c, const char *text, int x, int y, int scale, uint16_t color);
int cyd_text_width(const char *text, int scale);

esp_err_t cyd_touch_init(void);
/* Returns true and screen coordinates while the panel is pressed. */
bool cyd_touch_read(int *x, int *y);

esp_err_t cyd_button_init(void);
bool cyd_button_pressed(void);

esp_err_t cyd_sd_mount(void);
void cyd_sd_unmount(void);

#ifdef __cplusplus
}
#endif
