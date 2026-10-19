#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Launcher self-update from GitHub Releases.
 *
 * The launcher cannot overwrite the factory partition it runs from, so an
 * update is staged in two steps:
 *   1. download cyd-launcher.bin into ota_0, verify its SHA-256, boot it;
 *   2. the new launcher, running from ota_0, copies itself into factory and
 *      boots factory again.
 * If power is lost during step 2, the bootloader falls back to the copy in
 * ota_0, which simply repeats the copy.
 */

typedef void (*self_update_progress_t)(const char *label, int percent);

typedef struct {
    char version[32];
    char sha256[65];
    uint32_t size;
} launcher_release_t;

/* Fetch cyd-launcher.json from the latest release. */
esp_err_t self_update_check(launcher_release_t *out);

/* True if `remote` (vX.Y.Z) is newer than `local`. Non-release local builds count as older. */
bool self_update_is_newer(const char *remote, const char *local);

/* Download and verify the release into ota_0, then select it for the next boot. */
esp_err_t self_update_download(const launcher_release_t *release, self_update_progress_t progress);

/* Called first thing at boot: if this launcher runs from ota_0, install it into factory and reboot. */
void self_update_finish_if_staged(self_update_progress_t progress);
