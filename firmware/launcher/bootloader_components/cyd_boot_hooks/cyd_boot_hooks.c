/*
 * Bootloader hook: on power-on (which includes the RST/EN button on ESP32),
 * return to the launcher by erasing otadata, unless the user asked to keep
 * the installed app across power cycles. Software resets and crashes are
 * left alone, so apps restart normally.
 */
#include <stdbool.h>
#include <stdint.h>

#include "bootloader_flash_priv.h"
#include "cyd_platform_layout.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "soc/reset_reasons.h"

static const char *TAG = "cyd_boot";

/* Referenced by the bootloader link so this component is kept. */
void bootloader_hooks_include(void)
{
}

static bool region_is_erased(uint32_t offset, uint32_t size)
{
    uint32_t word;
    for (uint32_t pos = 0; pos < size; pos += sizeof(word)) {
        if (bootloader_flash_read(offset + pos, &word, sizeof(word), false) != ESP_OK) {
            return false;
        }
        if (word != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

void bootloader_after_init(void)
{
    if (esp_rom_get_reset_reason(0) != RESET_REASON_CHIP_POWER_ON) {
        return;
    }

    cyd_cfg_t cfg;
    if (bootloader_flash_read(CYD_CFG_OFFSET, &cfg, sizeof(cfg), false) == ESP_OK &&
        cfg.magic == CYD_CFG_MAGIC && cfg.keep_app_on_power_on == 1) {
        ESP_LOGI(TAG, "Power-on: keeping the installed app");
        return;
    }

    /* Each otadata entry is 32 bytes at the start of its 4 KB sector. */
    if (region_is_erased(CYD_OTADATA_OFFSET, 32) &&
        region_is_erased(CYD_OTADATA_OFFSET + 0x1000, 32)) {
        return; /* already booting the factory launcher */
    }

    ESP_LOGI(TAG, "Power-on: returning to the launcher");
    if (bootloader_flash_erase_range(CYD_OTADATA_OFFSET, CYD_OTADATA_SIZE) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset the boot selection");
    }
}
