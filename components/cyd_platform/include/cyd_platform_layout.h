/*
 * Flash layout constants shared by the launcher, apps and the bootloader hook.
 * Keep these in sync with firmware/launcher/partitions.csv.
 */
#pragma once

#include <stdint.h>

#define CYD_OTADATA_OFFSET   0xE000u
#define CYD_OTADATA_SIZE     0x2000u
#define CYD_CFG_OFFSET       0x10000u
#define CYD_CFG_SIZE         0x1000u
#define CYD_CFG_SUBTYPE      0x40

#define CYD_CFG_MAGIC        0x43594443u /* "CYDC" */

#define CYD_TOUCH_CAL_MAGIC  0x31414354u /* "TCA1" */

/*
 * Touch calibration: an affine map from raw XPT2046 readings to screen pixels.
 *   x = m[0] * raw_x + m[1] * raw_y + m[2]
 *   y = m[3] * raw_x + m[4] * raw_y + m[5]
 * raw_x is the 12-bit result of command 0xD0 (X position), raw_y of 0x90 (Y position).
 * The map covers scaling, offset, axis swap, and mirroring at once.
 */
typedef struct {
    uint32_t magic; /* CYD_TOUCH_CAL_MAGIC when valid */
    float m[6];
} cyd_touch_cal_t;

/*
 * Stored at the start of the cydcfg partition. Erased flash (0xFF) means defaults,
 * so fields added at the end stay compatible with older layouts.
 */
typedef struct {
    uint32_t magic;
    uint8_t keep_app_on_power_on; /* 1 = power-on boots the installed app; anything else = launcher */
    uint8_t reserved[3];
    cyd_touch_cal_t touch;
} cyd_cfg_t;
