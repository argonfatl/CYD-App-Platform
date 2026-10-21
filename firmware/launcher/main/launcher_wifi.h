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

/* Current IPv4 address as text, or "" when not connected. */
void launcher_wifi_ip(char *out, int len);

/*
 * If Wi-Fi is configured and the clock is not set yet, connects in the background
 * and sets the clock from NTP. Call once at startup, from the main task.
 */
void launcher_clock_start(void);

/* True once the clock has been set (from NTP now or before the last restart). */
bool launcher_clock_valid(void);

/* Start Wi-Fi in station mode and wait for an IP address. Also starts NTP clock sync. */
esp_err_t launcher_wifi_connect(int timeout_ms);
