#include "cyd_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include <string.h>

#include "cyd_platform.h"

#define Z_THRESHOLD 400
#define SAMPLES     4

static const char *TAG = "cyd_touch";

static float s_cal[6];

static void bit_delay(void)
{
    esp_rom_delay_us(1);
}

static uint16_t xpt2046_transfer(uint8_t command)
{
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(CYD_TOUCH_PIN_MOSI, (command >> bit) & 1);
        bit_delay();
        gpio_set_level(CYD_TOUCH_PIN_CLK, 1);
        bit_delay();
        gpio_set_level(CYD_TOUCH_PIN_CLK, 0);
    }
    gpio_set_level(CYD_TOUCH_PIN_MOSI, 0);

    uint16_t value = 0;
    for (int i = 0; i < 16; ++i) {
        gpio_set_level(CYD_TOUCH_PIN_CLK, 1);
        bit_delay();
        value = (uint16_t)((value << 1) | gpio_get_level(CYD_TOUCH_PIN_MISO));
        gpio_set_level(CYD_TOUCH_PIN_CLK, 0);
        bit_delay();
    }
    return (value >> 3) & 0x0FFF; /* 12-bit result after the busy bit */
}

esp_err_t cyd_touch_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << CYD_TOUCH_PIN_CLK) | (1ULL << CYD_TOUCH_PIN_MOSI) |
                        (1ULL << CYD_TOUCH_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&out_cfg), TAG, "Configure touch outputs");

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << CYD_TOUCH_PIN_MISO) | (1ULL << CYD_TOUCH_PIN_IRQ),
        .mode = GPIO_MODE_INPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&in_cfg), TAG, "Configure touch inputs");

    gpio_set_level(CYD_TOUCH_PIN_CS, 1);
    gpio_set_level(CYD_TOUCH_PIN_CLK, 0);

    cyd_touch_cal_t cal;
    if (cyd_platform_get_touch_calibration(&cal)) {
        cyd_touch_set_calibration(cal.m);
        ESP_LOGI(TAG, "Using saved touch calibration");
    } else {
        cyd_touch_get_default_calibration(s_cal);
        ESP_LOGI(TAG, "Touch not calibrated; using defaults");
    }
    return ESP_OK;
}

bool cyd_touch_read_raw(int *raw_x, int *raw_y)
{
    gpio_set_level(CYD_TOUCH_PIN_CS, 0);

    int z1 = xpt2046_transfer(0xB1);
    int z2 = xpt2046_transfer(0xC1);
    int z = z1 + 4095 - z2;

    bool pressed = z >= Z_THRESHOLD;
    int sum_x = 0, sum_y = 0;
    if (pressed) {
        xpt2046_transfer(0xD1); /* discard the first, noisier conversion */
        for (int i = 0; i < SAMPLES; ++i) {
            sum_x += xpt2046_transfer(0xD1);
            sum_y += xpt2046_transfer(0x91);
        }
    }
    xpt2046_transfer(0x90); /* power down, keep PENIRQ enabled */
    gpio_set_level(CYD_TOUCH_PIN_CS, 1);

    if (!pressed) {
        return false;
    }
    *raw_x = sum_x / SAMPLES;
    *raw_y = sum_y / SAMPLES;
    return true;
}

void cyd_touch_get_default_calibration(float m[6])
{
    /* Scale each raw axis onto a screen axis, then apply swap and mirroring. */
    float sx = (float)CYD_LCD_H_RES / (CYD_TOUCH_RAW_X_MAX - CYD_TOUCH_RAW_X_MIN);
    float sy = (float)CYD_LCD_V_RES / (CYD_TOUCH_RAW_Y_MAX - CYD_TOUCH_RAW_Y_MIN);
    float x_from_rx = 0, x_from_ry = 0, x_off, y_from_rx = 0, y_from_ry = 0, y_off;
#if CYD_TOUCH_SWAP_XY
    x_from_ry = sx; x_off = -CYD_TOUCH_RAW_X_MIN * sx;
    y_from_rx = sy; y_off = -CYD_TOUCH_RAW_Y_MIN * sy;
#else
    x_from_rx = sx; x_off = -CYD_TOUCH_RAW_X_MIN * sx;
    y_from_ry = sy; y_off = -CYD_TOUCH_RAW_Y_MIN * sy;
#endif
#if CYD_TOUCH_INVERT_X
    x_from_rx = -x_from_rx; x_from_ry = -x_from_ry; x_off = CYD_LCD_H_RES - 1 - x_off;
#endif
#if CYD_TOUCH_INVERT_Y
    y_from_rx = -y_from_rx; y_from_ry = -y_from_ry; y_off = CYD_LCD_V_RES - 1 - y_off;
#endif
    m[0] = x_from_rx; m[1] = x_from_ry; m[2] = x_off;
    m[3] = y_from_rx; m[4] = y_from_ry; m[5] = y_off;
}

void cyd_touch_set_calibration(const float m[6])
{
    memcpy(s_cal, m, sizeof(s_cal));
}

void cyd_touch_get_calibration(float m[6])
{
    memcpy(m, s_cal, sizeof(s_cal));
}

static int clamp(int v, int max)
{
    if (v < 0) return 0;
    if (v >= max) return max - 1;
    return v;
}

bool cyd_touch_read(int *x, int *y)
{
    int rx, ry;
    if (!cyd_touch_read_raw(&rx, &ry)) {
        return false;
    }
    *x = clamp((int)(s_cal[0] * rx + s_cal[1] * ry + s_cal[2] + 0.5f), CYD_LCD_H_RES);
    *y = clamp((int)(s_cal[3] * rx + s_cal[4] * ry + s_cal[5] + 0.5f), CYD_LCD_V_RES);
    return true;
}

esp_err_t cyd_button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << CYD_BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&cfg);
}

bool cyd_button_pressed(void)
{
    return gpio_get_level(CYD_BUTTON_PIN) == 0;
}
