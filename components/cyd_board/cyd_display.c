#include "cyd_board.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define STRIPE_ROWS   20
#define STRIPE_PIXELS (CYD_LCD_H_RES * STRIPE_ROWS)
#define NUM_BUFFERS   2

static const char *TAG = "cyd_display";

static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_buffers[NUM_BUFFERS];
static SemaphoreHandle_t s_free_buffers;

static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free_buffers, &woken);
    return woken == pdTRUE;
}

esp_err_t cyd_display_init(void)
{
    if (s_panel != NULL) {
        return ESP_OK;
    }

    for (int i = 0; i < NUM_BUFFERS; ++i) {
        s_buffers[i] = heap_caps_malloc(STRIPE_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
        ESP_RETURN_ON_FALSE(s_buffers[i], ESP_ERR_NO_MEM, TAG, "Allocate stripe buffer");
    }
    s_free_buffers = xSemaphoreCreateCounting(NUM_BUFFERS, NUM_BUFFERS);
    ESP_RETURN_ON_FALSE(s_free_buffers, ESP_ERR_NO_MEM, TAG, "Create semaphore");

    gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << CYD_LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "Configure backlight");

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = CYD_LCD_PIN_SCLK,
        .mosi_io_num = CYD_LCD_PIN_MOSI,
        .miso_io_num = CYD_LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STRIPE_PIXELS * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "Initialize LCD SPI bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CYD_LCD_PIN_DC,
        .cs_gpio_num = CYD_LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = NUM_BUFFERS + 2,
        .on_color_trans_done = on_color_trans_done,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io),
                        TAG, "Create panel IO");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel), TAG, "Create panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "Reset panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "Init panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "Invert colors");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "Landscape");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "Mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Display on");
    ESP_RETURN_ON_ERROR(gpio_set_level(CYD_LCD_PIN_BL, 1), TAG, "Backlight on");

    ESP_LOGI(TAG, "ILI9341 ready (%dx%d, %d-row stripes)", CYD_LCD_H_RES, CYD_LCD_V_RES, STRIPE_ROWS);
    return ESP_OK;
}

esp_err_t cyd_display_render(cyd_draw_fn_t draw, void *ctx)
{
    ESP_RETURN_ON_FALSE(s_panel, ESP_ERR_INVALID_STATE, TAG, "Display not initialized");

    esp_err_t result = ESP_OK;
    int index = 0;
    for (int y0 = 0; y0 < CYD_LCD_V_RES; y0 += STRIPE_ROWS) {
        /* Wait until this buffer is no longer being sent. */
        xSemaphoreTake(s_free_buffers, portMAX_DELAY);

        cyd_canvas_t canvas = {
            .buf = s_buffers[index],
            .y0 = y0,
            .h = (y0 + STRIPE_ROWS <= CYD_LCD_V_RES) ? STRIPE_ROWS : CYD_LCD_V_RES - y0,
        };
        draw(&canvas, ctx);

        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y0, CYD_LCD_H_RES, y0 + canvas.h, canvas.buf);
        if (err != ESP_OK) {
            xSemaphoreGive(s_free_buffers);
            result = err;
        }
        index = (index + 1) % NUM_BUFFERS;
    }

    /* Wait for the last transfers so callers may reuse their data immediately. */
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        xSemaphoreTake(s_free_buffers, portMAX_DELAY);
    }
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        xSemaphoreGive(s_free_buffers);
    }
    return result;
}

static inline uint16_t panel_order(uint16_t color)
{
    /* The ILI9341 expects RGB565 high byte first; the ESP32 is little-endian. */
    return (uint16_t)((color << 8) | (color >> 8));
}

void cyd_fill_rect(cyd_canvas_t *c, int x, int y, int w, int h, uint16_t color)
{
    int x1 = x < 0 ? 0 : x;
    int x2 = x + w > CYD_LCD_H_RES ? CYD_LCD_H_RES : x + w;
    int y1 = y < c->y0 ? c->y0 : y;
    int y2 = y + h > c->y0 + c->h ? c->y0 + c->h : y + h;
    if (x1 >= x2 || y1 >= y2) {
        return;
    }

    const uint16_t px = panel_order(color);
    for (int row = y1; row < y2; ++row) {
        uint16_t *dst = &c->buf[(row - c->y0) * CYD_LCD_H_RES];
        for (int col = x1; col < x2; ++col) {
            dst[col] = px;
        }
    }
}

void cyd_fill(cyd_canvas_t *c, uint16_t color)
{
    cyd_fill_rect(c, 0, c->y0, CYD_LCD_H_RES, c->h, color);
}
