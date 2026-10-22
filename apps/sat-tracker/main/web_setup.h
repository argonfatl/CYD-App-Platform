#pragma once

#include <stdbool.h>
#include "esp_err.h"

/*
 * Settings web page for Satellites. It runs only in setup mode, when no satellites
 * are loaded, so Wi-Fi and the HTTP server do not compete with them for memory.
 * The page edits /apps/sat-tracker/config.json and the app's NVS settings.
 */
esp_err_t web_setup_start(void);

/* True once the page has saved new settings; the app then restarts to apply them. */
bool web_setup_saved(void);
