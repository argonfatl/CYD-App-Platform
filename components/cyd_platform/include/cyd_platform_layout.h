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

/* Stored at the start of the cydcfg partition. Erased flash (0xFF) means defaults. */
typedef struct {
    uint32_t magic;
    uint8_t keep_app_on_power_on; /* 1 = power-on boots the installed app; anything else = launcher */
    uint8_t reserved[3];
} cyd_cfg_t;
