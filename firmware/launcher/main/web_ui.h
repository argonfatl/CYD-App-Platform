#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * Small web page served by the launcher while the GET APPS screen is open.
 * It lists available apps, lets the user add or remove GitHub repositories,
 * and can ask the device to download an app to the SD card.
 */
esp_err_t web_ui_start(void);
void web_ui_stop(void);

/* If the page asked to install an app, copies its id and returns true (once per request). */
bool web_ui_take_install_request(char id[32]);

/* Text shown on the page while the device is busy, e.g. "Installing Hello 40%". */
void web_ui_set_activity(const char *text);
