/*
 * Touch calibration app for the CYD App Platform.
 *
 * Asks the user to touch four corner targets, fits an affine map from raw
 * XPT2046 readings to screen pixels, checks it with a center target, lets the
 * user try it out, and saves it for the launcher and all apps.
 *
 * Works without any existing calibration because it only uses raw readings
 * until the new calibration is computed. Hold BOOT to leave without saving.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "calibration.h"
#include "cyd_board.h"
#include "cyd_platform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define MARGIN           30
#define NUM_TARGETS      5      /* four corners, then the center as a check */
#define NUM_FIT_TARGETS  4
#define MIN_SAMPLES      6
#define MAX_CHECK_ERROR  15.0f  /* pixels */
#define LONG_PRESS_US    700000
#define TRAIL_LEN        64

#define BTN_Y            186
#define BTN_H            44
#define BTN_W            110
#define REDO_X           12
#define SAVE_X           (CYD_LCD_H_RES - 12 - BTN_W)

static const char *TAG = "touch_cal";

static const int s_target_x[NUM_TARGETS] = {MARGIN, CYD_LCD_H_RES - MARGIN, CYD_LCD_H_RES - MARGIN, MARGIN,
                                            CYD_LCD_H_RES / 2};
static const int s_target_y[NUM_TARGETS] = {MARGIN, MARGIN, CYD_LCD_V_RES - MARGIN, CYD_LCD_V_RES - MARGIN,
                                            CYD_LCD_V_RES / 2};

typedef enum { MODE_TARGET, MODE_TEST, MODE_SAVED } cal_mode_t;

static struct {
    cal_mode_t mode;
    int target;
    float raw_x[NUM_TARGETS];
    float raw_y[NUM_TARGETS];
    float m[6];
    char message[48];
    uint16_t message_color;
    int trail_x[TRAIL_LEN];
    int trail_y[TRAIL_LEN];
    int trail_count;
} s;

/* ---------- Drawing ---------- */

static void draw_crosshair(cyd_canvas_t *c, int x, int y, uint16_t color)
{
    cyd_fill_rect(c, x - 14, y, 29, 1, color);
    cyd_fill_rect(c, x, y - 14, 1, 29, color);
    cyd_fill_rect(c, x - 6, y - 6, 13, 1, color);
    cyd_fill_rect(c, x - 6, y + 6, 13, 1, color);
    cyd_fill_rect(c, x - 6, y - 6, 1, 13, color);
    cyd_fill_rect(c, x + 6, y - 6, 1, 13, color);
}

static void draw_centered(cyd_canvas_t *c, const char *text, int y, int scale, uint16_t color)
{
    cyd_draw_text(c, text, (CYD_LCD_H_RES - cyd_text_width(text, scale)) / 2, y, scale, color);
}

static void draw_button(cyd_canvas_t *c, int x, const char *label, uint16_t bg)
{
    cyd_fill_rect(c, x, BTN_Y, BTN_W, BTN_H, bg);
    cyd_draw_text(c, label, x + (BTN_W - cyd_text_width(label, 2)) / 2, BTN_Y + 15, 2, CYD_BLACK);
}

static void draw(cyd_canvas_t *c, void *ctx)
{
    (void)ctx;
    cyd_fill(c, CYD_DARK);

    if (s.mode == MODE_TARGET) {
        char step[24];
        snprintf(step, sizeof(step), "TARGET %d OF %d", s.target + 1, NUM_TARGETS);
        draw_centered(c, "TOUCH SETUP", 70, 2, CYD_WHITE);
        draw_centered(c, "PRESS THE CENTER OF THE TARGET", 100, 1, CYD_GREY);
        draw_centered(c, "WITH A STYLUS OR FINGERNAIL", 112, 1, CYD_GREY);
        draw_centered(c, step, 136, 1, CYD_CYAN);
        draw_centered(c, s.message, 156, 1, s.message_color);
        draw_centered(c, "HOLD BOOT TO CANCEL", 176, 1, CYD_GREY);
        draw_crosshair(c, s_target_x[s.target], s_target_y[s.target], CYD_AMBER);
    } else if (s.mode == MODE_TEST) {
        draw_centered(c, "DRAW TO TEST THE TOUCH", 12, 1, CYD_WHITE);
        draw_centered(c, "DOTS SHOULD FOLLOW YOUR STYLUS", 26, 1, CYD_GREY);
        for (int i = 0; i < s.trail_count; ++i) {
            cyd_fill_rect(c, s.trail_x[i] - 2, s.trail_y[i] - 2, 5, 5, CYD_CYAN);
        }
        draw_button(c, REDO_X, "REDO", CYD_AMBER);
        draw_button(c, SAVE_X, "SAVE", CYD_GREEN);
    } else {
        draw_centered(c, "CALIBRATION SAVED", 100, 2, CYD_GREEN);
        draw_centered(c, "RETURNING TO THE LAUNCHER", 130, 1, CYD_GREY);
    }
}

static void redraw(void)
{
    cyd_display_render(draw, NULL);
}

static void set_message(const char *text, uint16_t color)
{
    snprintf(s.message, sizeof(s.message), "%s", text);
    s.message_color = color;
}

/* ---------- Steps ---------- */

static void start_targets(void)
{
    s.mode = MODE_TARGET;
    s.target = 0;
    s.trail_count = 0;
}

static void finish_targets(void)
{
    float scr_x[NUM_FIT_TARGETS], scr_y[NUM_FIT_TARGETS];
    for (int i = 0; i < NUM_FIT_TARGETS; ++i) {
        scr_x[i] = (float)s_target_x[i];
        scr_y[i] = (float)s_target_y[i];
    }
    if (!calibration_fit(s.raw_x, s.raw_y, scr_x, scr_y, NUM_FIT_TARGETS, s.m)) {
        set_message("COULD NOT COMPUTE - TRY AGAIN", CYD_RED);
        start_targets();
        return;
    }

    /* Check the fit against the center target, which was not used to compute it. */
    int c = NUM_TARGETS - 1;
    float px = s.m[0] * s.raw_x[c] + s.m[1] * s.raw_y[c] + s.m[2];
    float py = s.m[3] * s.raw_x[c] + s.m[4] * s.raw_y[c] + s.m[5];
    float err = hypotf(px - s_target_x[c], py - s_target_y[c]);
    ESP_LOGI(TAG, "Fit m = [%.4f %.4f %.1f; %.4f %.4f %.1f], center error %.1f px",
             s.m[0], s.m[1], s.m[2], s.m[3], s.m[4], s.m[5], err);
    if (err > MAX_CHECK_ERROR) {
        set_message("NOT ACCURATE ENOUGH - TRY AGAIN", CYD_RED);
        start_targets();
        return;
    }

    cyd_touch_set_calibration(s.m);
    s.mode = MODE_TEST;
    s.trail_count = 0;
}

static bool in_button(int x, int y, int bx)
{
    return x >= bx && x < bx + BTN_W && y >= BTN_Y && y < BTN_Y + BTN_H;
}

/* ---------- Main loop ---------- */

void app_main(void)
{
    ESP_ERROR_CHECK(cyd_display_init());
    ESP_ERROR_CHECK(cyd_touch_init());
    ESP_ERROR_CHECK(cyd_button_init());

    start_targets();
    set_message("", CYD_GREY);
    redraw();

    float sum_x = 0, sum_y = 0;
    int samples = 0;
    bool was_pressed = false;
    int64_t button_since = -1;
    int64_t last_draw = 0;

    for (;;) {
        int64_t now = esp_timer_get_time();

        /* Hold BOOT to leave without saving. */
        if (cyd_button_pressed()) {
            if (button_since < 0) button_since = now;
            if (now - button_since > LONG_PRESS_US) cyd_platform_return_to_launcher();
        } else {
            button_since = -1;
        }

        if (s.mode == MODE_TARGET) {
            int rx, ry;
            bool pressed = cyd_touch_read_raw(&rx, &ry);
            if (pressed) {
                sum_x += rx;
                sum_y += ry;
                samples++;
            } else if (was_pressed) {
                /* Released: accept the average of a steady press. */
                if (samples >= MIN_SAMPLES) {
                    s.raw_x[s.target] = sum_x / samples;
                    s.raw_y[s.target] = sum_y / samples;
                    ESP_LOGI(TAG, "Target %d raw (%.0f, %.0f)", s.target, s.raw_x[s.target], s.raw_y[s.target]);
                    set_message("", CYD_GREY);
                    if (++s.target == NUM_TARGETS) {
                        finish_targets();
                    }
                } else {
                    set_message("HOLD A LITTLE LONGER", CYD_AMBER);
                }
                redraw();
                sum_x = sum_y = 0;
                samples = 0;
                vTaskDelay(pdMS_TO_TICKS(250)); /* ignore bounce after release */
            }
            was_pressed = pressed;
        } else if (s.mode == MODE_TEST) {
            int x, y;
            bool pressed = cyd_touch_read(&x, &y);
            if (pressed && !was_pressed && in_button(x, y, SAVE_X)) {
                cyd_touch_cal_t cal = {.magic = CYD_TOUCH_CAL_MAGIC};
                memcpy(cal.m, s.m, sizeof(cal.m));
                if (cyd_platform_set_touch_calibration(&cal) == ESP_OK) {
                    s.mode = MODE_SAVED;
                    redraw();
                    vTaskDelay(pdMS_TO_TICKS(1500));
                    cyd_platform_return_to_launcher();
                }
                set_message("SAVING FAILED - TRY AGAIN", CYD_RED);
                start_targets();
                redraw();
            } else if (pressed && !was_pressed && in_button(x, y, REDO_X)) {
                set_message("", CYD_GREY);
                start_targets();
                redraw();
            } else if (pressed && y < BTN_Y - 4) {
                if (s.trail_count == TRAIL_LEN) {
                    memmove(s.trail_x, s.trail_x + 1, sizeof(int) * (TRAIL_LEN - 1));
                    memmove(s.trail_y, s.trail_y + 1, sizeof(int) * (TRAIL_LEN - 1));
                    s.trail_count--;
                }
                s.trail_x[s.trail_count] = x;
                s.trail_y[s.trail_count] = y;
                s.trail_count++;
                if (now - last_draw > 40000) {
                    last_draw = now;
                    redraw();
                }
            }
            was_pressed = pressed;
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}
