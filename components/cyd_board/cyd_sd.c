#include "cyd_board.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "cyd_sd";

static sdmmc_card_t *s_card;
static bool s_bus_ready;

esp_err_t cyd_sd_mount(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST;

    if (!s_bus_ready) {
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = CYD_SD_PIN_MOSI,
            .miso_io_num = CYD_SD_PIN_MISO,
            .sclk_io_num = CYD_SD_PIN_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                            TAG, "Initialize SD SPI bus");
        s_bus_ready = true;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = CYD_SD_PIN_CS;
    slot_cfg.host_id = SPI3_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(CYD_SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(err));
        s_card = NULL;
        return err;
    }
    ESP_LOGI(TAG, "SD card mounted at %s", CYD_SD_MOUNT_POINT);
    return ESP_OK;
}

void cyd_sd_unmount(void)
{
    if (s_card != NULL) {
        esp_vfs_fat_sdcard_unmount(CYD_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
}
