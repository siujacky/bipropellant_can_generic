/* chip_detect.c — MCU self-identification via DBGMCU_IDCODE + FLASHSIZE.
 *
 * DBGMCU_IDCODE (0xE0042000) is part of the ARM CoreSight architecture and
 * is always accessible in normal code — no JTAG/SWD connection required.
 * FLASHSIZE (0x1FFFF7E0) is in the option-byte / system-memory region and
 * is also always readable on STM32F1xx (RM0008, section 30.6).
 *
 * For GD32 clones, DBGMCU_IDCODE reports their own device IDs (0xB641 for
 * GD32F103, etc.).  The flash-size register may or may not be present at the
 * same address on all clones; we read it and accept whatever value appears.
 */

#include "chip_detect.h"
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Known device IDs (DBGMCU_IDCODE bits [11:0]).
 * Sources: RM0008 (STM32F1xx), GD32 product selector, MM32 datasheets.
 * --------------------------------------------------------------------------- */
typedef struct { uint16_t dev_id; uint16_t sram_kb; const char *part; const char *density; } dev_entry_t;

static const dev_entry_t k_devices[] = {
    /* STM32F1 family */
    { 0x0412,  6, "STM32F103x4/x6",    "low"    },  /* 32KB flash */
    { 0x0410, 20, "STM32F103x8/xB",    "medium" },  /* 128KB flash */
    { 0x0414, 48, "STM32F103xC/D/E",   "high"   },  /* 256-512KB flash */
    { 0x0418, 64, "STM32F105/F107",    "conn"   },  /* connectivity */
    { 0x0430, 96, "STM32F103xF/G",     "XL"     },  /* 768KB-1MB */
    { 0x0411, 20, "STM32F2xx",         "F2"     },
    { 0x0413, 192,"STM32F405/F407",    "F4"     },
    { 0x0419, 256,"STM32F42x/F43x",   "F4"     },
    { 0x0423,  64,"STM32F401xB/xC",    "F4"     },  /* 256KB, 64KB SRAM, Black Pill v1 */
    { 0x0433,  96,"STM32F401xD/xE",    "F4"     },  /* 384KB, 96KB SRAM */
    { 0x0421, 128,"STM32F446",         "F4"     },
    { 0x0431, 128,"STM32F411xC/xE",    "F4"     },  /* Black Pill v2 */
    { 0x0434, 256,"STM32F469/F479",    "F4"     },
    { 0x0441, 128,"STM32F412",         "F4"     },
    { 0x0449, 256,"STM32F7xx",         "F7"     },
    /* STM32G4 — important: G474/G484 have TIM1+TIM8+TIM20, native CAN FD */
    { 0x0468,  32,"STM32G431/G441",   "G4-Cat2"},  /* Cat.2: no TIM8, limited */
    { 0x0469, 128,"STM32G474/G484",   "G4-Cat3"},  /* Cat.3: TIM1+TIM8+TIM20+FDCAN, HRTIM */
    { 0x0479,  96,"STM32G491/G4A1",   "G4-Cat4"},  /* Cat.4: single FDCAN, no HRTIM */
    /* GD32 (some share IDs with STM32 for drop-in compatibility) */
    { 0xA641, 20, "GD32F103 (med)",    "medium" },
    { 0xB641, 48, "GD32F103 (high)",   "high"   },
    { 0xB642, 48, "GD32F130",          "high"   },
    /* MM32 */
    { 0xCC68,  8, "MM32SPIN0x",        "SPIN"   },
};
#define K_DEVICES_COUNT ((uint32_t)(sizeof(k_devices)/sizeof(k_devices[0])))

/* UID and flash-size register addresses (STM32F1xx, RM0008 sections 30.x) */
#define DBGMCU_IDCODE_ADDR  0xE0042000UL
#define FLASHSIZE_ADDR      0x1FFFF7E0UL
#define UID_ADDR            0x1FFFF7E8UL

void chip_detect(chip_info_t *info)
{
    uint32_t idcode   = *(volatile uint32_t *)DBGMCU_IDCODE_ADDR;
    info->dev_id      = (uint16_t)(idcode & 0x0FFFU);
    info->rev_id      = (uint16_t)(idcode >> 16);
    info->flash_kb    = *(volatile uint16_t *)FLASHSIZE_ADDR;

    /* Look up in the device table */
    info->part    = "Unknown";
    info->density = "unknown";
    info->sram_kb = 0;
    for (uint32_t i = 0; i < K_DEVICES_COUNT; i++) {
        if (k_devices[i].dev_id == info->dev_id) {
            info->part    = k_devices[i].part;
            info->density = k_devices[i].density;
            info->sram_kb = k_devices[i].sram_kb;
            break;
        }
    }

    /* 96-bit UID (three 32-bit words, little-endian on the bus) */
    volatile uint32_t *uid = (volatile uint32_t *)UID_ADDR;
    info->uid[0] = uid[0];
    info->uid[1] = uid[1];
    info->uid[2] = uid[2];
}

void chip_info_str(const chip_info_t *info, char *buf, uint16_t buflen)
{
    snprintf(buf, buflen,
             "Chip: %s  DevID=0x%04X  Rev=0x%04X  Flash=%uKB  SRAM=%uKB  "
             "UID=%08lX-%08lX-%08lX",
             info->part,
             (unsigned)info->dev_id,
             (unsigned)info->rev_id,
             (unsigned)info->flash_kb,
             (unsigned)info->sram_kb,
             (unsigned long)info->uid[0],
             (unsigned long)info->uid[1],
             (unsigned long)info->uid[2]);
}
