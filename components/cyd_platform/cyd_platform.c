#include "cyd_platform.h"

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

bool cyd_platform_get_keep_app_on_power_on(void)
{
    const esp_partition_t *part = find_cfg_partition();
    cyd_cfg_t cfg;
    if (part == NULL || esp_partition_read(part, 0, &cfg, sizeof(cfg)) != ESP_OK) {
        return false;
    }
    return cfg.magic == CYD_CFG_MAGIC && cfg.keep_app_on_power_on == 1;
}

esp_err_t cyd_platform_set_keep_app_on_power_on(bool keep)
{
    const esp_partition_t *part = find_cfg_partition();
    if (part == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    cyd_cfg_t cfg;
    memset(&cfg, 0xFF, sizeof(cfg));
    cfg.magic = CYD_CFG_MAGIC;
    cfg.keep_app_on_power_on = keep ? 1 : 0;

    esp_err_t err = esp_partition_erase_range(part, 0, part->size);
    if (err == ESP_OK) {
        err = esp_partition_write(part, 0, &cfg, sizeof(cfg));
    }
    return err;
}
