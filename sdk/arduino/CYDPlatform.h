/*
 * Minimal CYD App Platform support for Arduino-ESP32 apps.
 * Copy this header into your sketch and call cydReturnToLauncher() from your Exit action.
 */
#pragma once

#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"

#define CYD_PLATFORM_SDK_VERSION 1

/** Boot the CYD App Platform launcher. Returns false only if switching failed. */
inline bool cydReturnToLauncher()
{
    const esp_partition_t *factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (factory == nullptr || esp_ota_set_boot_partition(factory) != ESP_OK) {
        return false;
    }
    ESP.restart();
    return true;
}
