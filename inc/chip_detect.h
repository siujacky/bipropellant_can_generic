/* chip_detect.h — STM32/GD32/MM32 chip self-identification.
 *
 * Reads DBGMCU_IDCODE (0xE0042000) and the flash-size register (0x1FFFF7E0)
 * to identify the running MCU without a debugger attached — these registers
 * are in normal APB/memory space, not debug-port-only registers.
 *
 * Usage:
 *   chip_info_t info;
 *   chip_detect(&info);
 *   // info.dev_id   = 0x0414  (STM32F103 high-density)
 *   // info.flash_kb = 256
 *   // info.part     = "STM32F103xC/D/E"
 *
 * Typical log line (add to boot splash or STATUS response):
 *   "Chip: STM32F103xC/D/E  DevID=0414  Rev=1000  Flash=256KB  UID=AABBCCDD..."
 */

#pragma once
#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct {
    uint16_t    dev_id;          /* DBGMCU bits [11:0]: device family/density */
    uint16_t    rev_id;          /* DBGMCU bits [31:16]: silicon revision      */
    uint16_t    flash_kb;        /* flash-size register (KB)                   */
    uint16_t    sram_kb;         /* derived from dev_id where known            */
    const char *part;            /* human-readable part name, e.g. "STM32F103xC/D/E" */
    const char *density;         /* "low" / "medium" / "high" / "XL"          */
    uint32_t    uid[3];          /* 96-bit unique device ID                    */
} chip_info_t;

/* Populate *info from on-chip registers.  Always succeeds (returns known
 * values or "Unknown" strings when the device ID is not in the table). */
void chip_detect(chip_info_t *info);

/* Write a single-line description to buf (max buflen bytes).
 * Example: "STM32F103xC/D/E DevID=0x0414 Rev=0x1000 Flash=256KB" */
void chip_info_str(const chip_info_t *info, char *buf, uint16_t buflen);
