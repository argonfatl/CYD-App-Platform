#include "cyd_board.h"

#include <stddef.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

#define SAMPLES       16
#define MIN_VALID_MV  2500
#define MAX_VALID_MV  4500

static const char *TAG = "cyd_battery";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_channel;
static int s_smoothed_mv = -1; /* exponential moving average, -1 = no reading yet */

esp_err_t cyd_battery_init(void)
{
    if (s_adc != NULL) return ESP_OK;

    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(CYD_BAT_ADC_GPIO, &unit, &s_channel), TAG, "GPIO is not an ADC pin");
    ESP_RETURN_ON_FALSE(unit == ADC_UNIT_1, ESP_ERR_NOT_SUPPORTED, TAG, "Battery pin must be on ADC1 (ADC2 is used by Wi-Fi)");

    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "ADC unit");

    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg), TAG, "ADC channel");

    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100, /* used only if the chip has no eFuse calibration */
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "No ADC calibration; battery voltage will be approximate");
        s_cali = NULL;
    }
    return ESP_OK;
}

/* Approximate rested single-cell Li-ion curve; the percentage varies under load. */
static int percent_from_mv(int mv)
{
    static const struct { int mv; int percent; } curve[] = {
        {3300, 0},  {3500, 5},  {3600, 10}, {3700, 20}, {3750, 30}, {3800, 40},
        {3850, 50}, {3900, 60}, {3950, 70}, {4000, 80}, {4100, 90}, {4200, 100},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv <= curve[0].mv) return 0;
    for (int i = 1; i < n; ++i) {
        if (mv <= curve[i].mv) {
            int span = curve[i].mv - curve[i - 1].mv;
            return curve[i - 1].percent + ((curve[i].percent - curve[i - 1].percent) * (mv - curve[i - 1].mv) + span / 2) / span;
        }
    }
    return 100;
}

bool cyd_battery_read(int *millivolts, int *percent)
{
    if (s_adc == NULL) return false;

    int sum = 0, count = 0;
    for (int i = 0; i < SAMPLES; ++i) {
        int raw, mv;
        if (adc_oneshot_read(s_adc, s_channel, &raw) != ESP_OK) continue;
        if (s_cali == NULL || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            mv = raw * 3100 / 4095; /* rough full scale at 12 dB attenuation */
        }
        sum += mv;
        count++;
    }
    if (count == 0) return false;

    int mv = (int)((float)sum / count * CYD_BAT_DIVIDER + 0.5f);
    if (mv < MIN_VALID_MV || mv > MAX_VALID_MV) {
        s_smoothed_mv = -1; /* no battery or no divider on this board */
        return false;
    }
    /* Smooth noise and load spikes; jump straight to the first reading. */
    s_smoothed_mv = s_smoothed_mv < 0 ? mv : (s_smoothed_mv * 3 + mv) / 4;

    if (millivolts) *millivolts = s_smoothed_mv;
    if (percent) *percent = percent_from_mv(s_smoothed_mv);
    return true;
}
