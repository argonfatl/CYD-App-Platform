#include <inttypes.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cyd-platform";

void app_main(void)
{
    ESP_LOGI(TAG, "CYD App Platform booted");
    ESP_LOGI(TAG, "Runtime prototype is not enabled yet.");
    ESP_LOGI(TAG, "Chip: %s, revision: %d",
             CONFIG_IDF_TARGET, (int)esp_get_chip_revision());

    for (;;) {
        ESP_LOGI(TAG, "Launcher core alive; free heap: %" PRIu32 " bytes",
                 (uint32_t)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
