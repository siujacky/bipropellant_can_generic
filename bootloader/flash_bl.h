/* flash_bl.h — STM32F103 flash programming API for the bootloader.
 * No HAL. Uses direct register access via device_regs.h.
 * Only operates on addresses >= APP_FLASH_START (0x08002000) to protect
 * the bootloader itself.
 */

#ifndef FLASH_BL_H
#define FLASH_BL_H

#include <stdint.h>

#define APP_FLASH_START  0x08002000UL
#define FLASH_PAGE_SIZE  1024U          /* STM32F103 medium-density: 1 KB pages */

/* Unlock the flash for programming/erasing */
void flash_unlock(void);

/* Lock the flash (set LOCK bit) */
void flash_lock(void);

/* Erase a single 1KB page containing 'addr'.
 * SAFETY: refuses to erase below APP_FLASH_START. */
void flash_erase_page(uint32_t addr);

/* Write a single 16-bit halfword to 'addr' (must be 2-byte aligned).
 * Flash must already be unlocked and the page erased. */
void flash_write_halfword(uint32_t addr, uint16_t data);

/* Erase and program a full page.
 *   dst  — flash destination address (must be page-aligned, >= APP_FLASH_START)
 *   src  — pointer to source data in RAM
 *   len  — number of bytes to write (padded to even; remainder filled 0xFF)
 */
void flash_write_page(uint32_t dst, const uint8_t *src, uint32_t len);

#endif /* FLASH_BL_H */
