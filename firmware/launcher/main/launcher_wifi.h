#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * Wi-Fi for the launcher. Credentials come from /wifi.json on the SD card:
 *   {"ssid": "MyNetwork", "password": "secret"}
 * They are copied into NVS so the card can be removed afterwards.
 */

/* Import /wifi.json from the SD card into NVS if it exists. */
void launcher_wifi_import_from_sd(void);

bool launcher_wifi_configured(void);

/* Start Wi-Fi in station mode and wait for an IP address. */
esp_err_t launcher_wifi_connect(int timeout_ms);
