/*
 * Minimal CYD App Platform support for Arduino-ESP32 apps.
 * Copy this header into your sketch folder.
 *
 *   cydReturnToLauncher()      boot the launcher (call it from your Exit action)
 *   cydGetTouchCalibration()   read the calibration saved by the Touch Setup app
 *   cydMapTouch()              convert raw XPT2046 readings to screen pixels
 *
 * The layout below must match components/cyd_platform/include/cyd_platform_layout.h.
 */
#pragma once

#include <Arduino.h>
#include <math.h>
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

/*
 * Touch calibration: x = m[0]*rawX + m[1]*rawY + m[2], y = m[3]*rawX + m[4]*rawY + m[5].
 * rawX is the 12-bit XPT2046 result of command 0xD0 (X position), rawY of 0x90 (Y position),
 * with the 320x240 screen in landscape.
 */
struct CYDTouchCalibration {
    float m[6];
};

/** Read the saved calibration. Returns false if Touch Setup has not been run. */
inline bool cydGetTouchCalibration(CYDTouchCalibration &out)
{
    struct {
        uint32_t magic;
        uint8_t keep_app_on_power_on;
        uint8_t reserved[3];
        uint32_t touch_magic;
        float m[6];
    } cfg;
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "cydcfg");
    if (part == nullptr || esp_partition_read(part, 0, &cfg, sizeof(cfg)) != ESP_OK) {
        return false;
    }
    if (cfg.magic != 0x43594443u || cfg.touch_magic != 0x31414354u) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (!isfinite(cfg.m[i])) return false;
        out.m[i] = cfg.m[i];
    }
    return true;
}

/** Map raw readings to screen pixels, clamped to 320x240. */
inline void cydMapTouch(const CYDTouchCalibration &cal, int rawX, int rawY, int &x, int &y)
{
    float fx = cal.m[0] * rawX + cal.m[1] * rawY + cal.m[2];
    float fy = cal.m[3] * rawX + cal.m[4] * rawY + cal.m[5];
    x = constrain((int)lroundf(fx), 0, 319);
    y = constrain((int)lroundf(fy), 0, 239);
}
