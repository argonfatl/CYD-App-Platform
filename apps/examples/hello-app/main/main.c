/*
 * Example CYD App Platform app: an ordinary ESP-IDF firmware that runs from
 * the ota_0 slot and returns to the launcher on EXIT or a long BOOT press.
 */
#include <stdio.h>

#include "cyd_board.h"
#include "cyd_platform.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EXIT_X 210
#define EXIT_Y 180
#define EXIT_W 96
#define EXIT_H 44

static int s_seconds;

static void draw(cyd_canvas_t *c, void *ctx)
{
    (void)ctx;
    char text[16];
    snprintf(text, sizeof(text), "%02d:%02d", s_seconds / 60, s_seconds % 60);

    cyd_fill(c, CYD_DARK);
    cyd_fill_rect(c, 0, 0, CYD_LCD_H_RES, 4, CYD_GREEN);
    cyd_draw_text(c, "HELLO FROM AN APP", 14, 20, 2, CYD_WHITE);
    cyd_draw_text(c, "RUNNING FROM THE OTA_0 SLOT", 14, 48, 1, CYD_GREY);
    cyd_draw_text(c, text, 60, 90, 6, CYD_GREEN);
    cyd_draw_text(c, "HOLD BOOT OR TAP EXIT", 14, 196, 1, CYD_GREY);
    cyd_fill_rect(c, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, CYD_RED);
    cyd_draw_text(c, "EXIT", EXIT_X + 24, EXIT_Y + 15, 2, CYD_WHITE);
}

void app_main(void)
{
    ESP_ERROR_CHECK(cyd_display_init());
    ESP_ERROR_CHECK(cyd_touch_init());
    ESP_ERROR_CHECK(cyd_button_init());

    int64_t start = esp_timer_get_time();
    int64_t button_since = -1;
    s_seconds = -1;

    for (;;) {
        int seconds = (int)((esp_timer_get_time() - start) / 1000000);
        if (seconds != s_seconds) {
            s_seconds = seconds;
            cyd_display_render(draw, NULL);
        }

        int x, y;
        if (cyd_touch_read(&x, &y) && x >= EXIT_X && x < EXIT_X + EXIT_W &&
            y >= EXIT_Y && y < EXIT_Y + EXIT_H) {
            cyd_platform_return_to_launcher();
        }

        if (cyd_button_pressed()) {
            if (button_since < 0) button_since = esp_timer_get_time();
            if (esp_timer_get_time() - button_since > 700000) cyd_platform_return_to_launcher();
        } else {
            button_since = -1;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
