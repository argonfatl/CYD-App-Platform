#pragma once

#include <stddef.h>
#include "esp_err.h"

/* Called for each chunk of a 200 OK response body. Return an error to abort. */
typedef esp_err_t (*http_body_cb_t)(const void *data, int len, void *ctx);

/*
 * HTTPS GET that follows redirects (GitHub release downloads redirect to a CDN)
 * and delivers only the final response body. Returns ESP_ERR_NOT_FOUND for 404,
 * ESP_FAIL for other non-200 statuses, or the callback's error.
 */
esp_err_t http_get(const char *url, http_body_cb_t on_body, void *ctx);

/* Fetch a small text response into buf (NUL-terminated). ESP_ERR_INVALID_SIZE if it does not fit. */
esp_err_t http_get_text(const char *url, char *buf, size_t buf_len);

/* Short human-readable reason for the last http_get failure, e.g. "HTTP 404". */
const char *http_last_error(void);
