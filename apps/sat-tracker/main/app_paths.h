#pragma once

#include "cyd_board.h"

/* Where Satellites keeps its files and settings. */
#define APP_DIR      CYD_SD_MOUNT_POINT "/apps/sat-tracker"
#define CONFIG_PATH  APP_DIR "/config.json"
#define NVS_NS       "sat_tracker"

/*
 * NVS keys in NVS_NS:
 *   tle_<group>  time the TLE cache for <group> was saved (int64, first 10 characters of the group)
 *   tle_try      time of the last download attempt (int64)
 *   sat_filter   filter on the main screens: 0 all, 1 ISS, 2 favorites (u8)
 *   favorites    catalog numbers of favorite satellites, comma separated (string)
 */
