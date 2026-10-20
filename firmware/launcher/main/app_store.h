#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Apps available for download to the SD card: the official catalog from this
 * repository's latest release plus apps from GitHub repositories the user added.
 * See docs/adr/0004-app-catalog.md.
 */

#define STORE_MAX_APPS   24
#define STORE_MAX_REPOS  12
#define STORE_REPO_LEN   64

typedef enum {
    STORE_NEW,       /* not on the SD card */
    STORE_UPDATE,    /* on the card with a different version */
    STORE_INSTALLED, /* on the card with this version */
} store_state_t;

typedef struct {
    char id[32];
    char name[32];
    char version[16];
    char description[96];
    char url[200];
    char sha256[65];
    uint32_t size;
    bool official;
    char source[STORE_REPO_LEN + 8]; /* "official" or "github:owner/name" */
    store_state_t state;
    char installed_version[16];
} store_app_t;

typedef struct {
    store_app_t apps[STORE_MAX_APPS];
    int count;
    int failed_sources; /* repositories or catalog that could not be read */
    char last_error[48];
} store_list_t;

typedef void (*store_progress_t)(const char *label, int percent);

/* Call once at startup, before the web interface runs. */
void store_init(void);

/* The list shared with the web interface. Lock while reading or writing it. */
store_list_t *store_list(void);
void store_lock(void);
void store_unlock(void);

/* User repositories ("owner/name"), kept in NVS. */
int store_get_repos(char repos[][STORE_REPO_LEN], int max);
/* Accepts "owner/name" or a github.com URL. ESP_ERR_INVALID_ARG if it does not look like one. */
esp_err_t store_add_repo(const char *repo);
esp_err_t store_remove_repo(const char *repo);

/* Set by the web interface when repositories change; cleared by store_refresh(). */
bool store_repos_changed(void);

/* Fetch the catalog and every user repository and update the shared list. Needs Wi-Fi and the SD card. */
esp_err_t store_refresh(store_progress_t progress);

/* Download, verify, and save an app to /sdcard/apps/<id>/. */
esp_err_t store_install(const store_app_t *app, store_progress_t progress);

/* True for ids made of [a-z0-9_-], 1..31 characters. */
bool store_valid_id(const char *id);
