#include "cyd_platform.h"

#include <math.h>
#include <string.h>

#include "cyd_platform_layout.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "cyd_platform";

esp_err_t cyd_platform_return_to_launcher(void)
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory == NULL) {
        ESP_LOGE(TAG, "No factory partition: is the CYD App Platform partition table installed?");
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = esp_ota_set_boot_partition(factory);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Selecting the launcher failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Returning to the launcher");
    esp_restart();
    return ESP_OK; /* not reached */
}

static const esp_partition_t *find_cfg_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    (esp_partition_subtype_t)CYD_CFG_SUBTYPE, "cydcfg");
}

/* Reads the settings; unset or invalid fields come back as erased flash (0xFF). */
static bool read_cfg(cyd_cfg_t *cfg)
{
    memset(cfg, 0xFF, sizeof(*cfg));
    const esp_partition_t *part = find_cfg_partition();
    if (part == NULL || esp_partition_read(part, 0, cfg, sizeof(*cfg)) != ESP_OK) {
        memset(cfg, 0xFF, sizeof(*cfg));
        return false;
    }
    if (cfg->magic != CYD_CFG_MAGIC) {
        memset(cfg, 0xFF, sizeof(*cfg));
    }
    return true;
}

static esp_err_t write_cfg(cyd_cfg_t *cfg)
{
    const esp_partition_t *part = find_cfg_partition();
    if (part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    cfg->magic = CYD_CFG_MAGIC;
    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err == ESP_OK) {
        err = esp_partition_write(part, 0, cfg, sizeof(*cfg));
    }
    return err;
}

bool cyd_platform_get_keep_app_on_power_on(void)
{
    cyd_cfg_t cfg;
    read_cfg(&cfg);
    return cfg.magic == CYD_CFG_MAGIC && cfg.keep_app_on_power_on == 1;
}

esp_err_t cyd_platform_set_keep_app_on_power_on(bool keep)
{
    cyd_cfg_t cfg;
    read_cfg(&cfg);
    cfg.keep_app_on_power_on = keep ? 1 : 0;
    return write_cfg(&cfg);
}

bool cyd_platform_get_touch_calibration(cyd_touch_cal_t *cal)
{
    cyd_cfg_t cfg;
    read_cfg(&cfg);
    if (cfg.magic != CYD_CFG_MAGIC || cfg.touch.magic != CYD_TOUCH_CAL_MAGIC) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (!isfinite(cfg.touch.m[i])) return false;
    }
    *cal = cfg.touch;
    return true;
}

esp_err_t cyd_platform_set_touch_calibration(const cyd_touch_cal_t *cal)
{
    cyd_cfg_t cfg;
    read_cfg(&cfg);
    cfg.touch = *cal;
    cfg.touch.magic = CYD_TOUCH_CAL_MAGIC;
    ESP_LOGI(TAG, "Saving touch calibration");
    return write_cfg(&cfg);
}
