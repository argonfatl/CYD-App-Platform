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

/* Panel settings for the ESP32-2432S028R (ILI9341). Some batches need inverted colors. */
#define CYD_LCD_INVERT_COLORS false
#define CYD_LCD_MIRROR_X      false
#define CYD_LCD_MIRROR_Y      false

/* Touch: XPT2046, bit-banged because both hardware SPI hosts are in use. */
#define CYD_TOUCH_PIN_CLK    25
#define CYD_TOUCH_PIN_MOSI   32
#define CYD_TOUCH_PIN_MISO   39
#define CYD_TOUCH_PIN_CS     33
#define CYD_TOUCH_PIN_IRQ    36

/*
 * Default raw touch range and orientation for landscape, used until the touch
 * calibration app has saved a calibration (see cyd_platform_get_touch_calibration).
 */
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

/*
 * Battery voltage: ELEGOO/LCDwiki ESP32-32E boards (E32R28T) divide BAT+ with
 * 2 x 100 kOhm into GPIO34 (ADC1). Boards without this divider read as "no battery".
 */
#define CYD_BAT_ADC_GPIO     34
#define CYD_BAT_DIVIDER      2.0f

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

/* Initializes touch and loads the saved calibration, or the defaults above. */
esp_err_t cyd_touch_init(void);
/* Returns true and screen coordinates while the panel is pressed. */
bool cyd_touch_read(int *x, int *y);
/* Returns true and averaged raw 12-bit readings (X = cmd 0xD0, Y = cmd 0x90) while pressed. */
bool cyd_touch_read_raw(int *raw_x, int *raw_y);
/* Affine map used by cyd_touch_read: x = m0*rx + m1*ry + m2, y = m3*rx + m4*ry + m5. */
void cyd_touch_set_calibration(const float m[6]);
void cyd_touch_get_calibration(float m[6]);
void cyd_touch_get_default_calibration(float m[6]);

esp_err_t cyd_button_init(void);
bool cyd_button_pressed(void);

/* Battery monitor. Safe to call on boards without a battery divider. */
esp_err_t cyd_battery_init(void);
/*
 * Sample the battery. Returns false when no battery is detected (reading below 2.5 V
 * or above 4.5 V). Readings are smoothed across calls; call every few seconds.
 * Either pointer may be NULL.
 */
bool cyd_battery_read(int *millivolts, int *percent);

esp_err_t cyd_sd_mount(void);
void cyd_sd_unmount(void);

#ifdef __cplusplus
}
#endif
