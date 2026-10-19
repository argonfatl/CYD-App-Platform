#include <inttypes.h>
#include <stdint.h>

#include "esp_elf.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cyd-platform";

extern const uint8_t clock_elf_start[] asm("_binary_clock_app_elf_start");

void app_main(void)
{
    esp_elf_t elf;
    int ret;

    ESP_LOGI(TAG, "CYD App Platform booted");
    ESP_LOGI(TAG, "Starting embedded Clock ELF prototype");

    ret = esp_elf_init(&elf);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_elf_init failed: %d", ret);
        return;
    }

    ret = esp_elf_relocate(&elf, clock_elf_start);
    if (ret < 0) {
        ESP_LOGE(TAG, "esp_elf_relocate failed: %d", ret);
        esp_elf_deinit(&elf);
        return;
    }

    ESP_LOGI(TAG, "Clock ELF loaded; transferring control to app module");
    esp_elf_request(&elf, 0, 0, NULL);
    ESP_LOGI(TAG, "Clock app returned; unloading ELF");

    esp_elf_deinit(&elf);
    ESP_LOGI(TAG, "Clock ELF unloaded; launcher core remains active");

    for (;;) {
        ESP_LOGI(TAG, "Launcher core alive; free heap: %" PRIu32 " bytes",
                 (uint32_t)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
