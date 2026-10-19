#include "cyd_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_rom_sys.h"

#define Z_THRESHOLD 400
#define SAMPLES     4

static const char *TAG = "cyd_touch";

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
    return ESP_OK;
}

static int map_range(int value, int in_min, int in_max, int out_max)
{
    int out = (value - in_min) * out_max / (in_max - in_min);
    if (out < 0) return 0;
    if (out >= out_max) return out_max - 1;
    return out;
}

bool cyd_touch_read(int *x, int *y)
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

    int raw_x = sum_x / SAMPLES;
    int raw_y = sum_y / SAMPLES;
#if CYD_TOUCH_SWAP_XY
    int tmp = raw_x; raw_x = raw_y; raw_y = tmp;
#endif
    int sx = map_range(raw_x, CYD_TOUCH_RAW_X_MIN, CYD_TOUCH_RAW_X_MAX, CYD_LCD_H_RES);
    int sy = map_range(raw_y, CYD_TOUCH_RAW_Y_MIN, CYD_TOUCH_RAW_Y_MAX, CYD_LCD_V_RES);
#if CYD_TOUCH_INVERT_X
    sx = CYD_LCD_H_RES - 1 - sx;
#endif
#if CYD_TOUCH_INVERT_Y
    sy = CYD_LCD_V_RES - 1 - sy;
#endif
    *x = sx;
    *y = sy;
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
