/*
 * CYD App Platform API for apps and the launcher.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Version of this API. Apps can record it in manifest.json as "sdk". */
#define CYD_PLATFORM_SDK_VERSION 1

/**
 * Select the launcher (factory partition) as the boot target and restart.
 * Returns only if switching failed.
 */
esp_err_t cyd_platform_return_to_launcher(void);

/** True when power-on/RST keeps booting the installed app instead of the launcher. */
bool cyd_platform_get_keep_app_on_power_on(void);

/** Change the power-on policy. Used by the launcher settings. */
esp_err_t cyd_platform_set_keep_app_on_power_on(bool keep);

#ifdef __cplusplus
}
#endif
