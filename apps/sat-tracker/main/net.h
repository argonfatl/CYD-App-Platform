#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connects using the Wi-Fi network saved by the CYD App Platform launcher (NVS "cyd_wifi"). */
esp_err_t net_wifi_start(int timeout_ms);
/* Stops Wi-Fi and frees its memory. */
void net_wifi_stop(void);

/* Sets the clock from NTP. */
esp_err_t net_sync_time(int timeout_ms);

/* Sets the clock from the Date header of an HTTP(S) response (works where NTP is blocked). */
esp_err_t net_time_from_http(const char *url);

/*
 * HTTP(S) GET into a heap buffer (NUL-terminated). The caller frees *out.
 * Fails with ESP_ERR_INVALID_SIZE if the body is larger than max_len.
 */
esp_err_t net_get(const char *url, size_t max_len, char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif
