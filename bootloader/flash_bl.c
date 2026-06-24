/* flash_bl.c — STM32F103 flash programming (no HAL).
 *
 * SAFETY NOTES:
 *  - flash_erase_page() and flash_write_page() refuse to operate below
 *    APP_FLASH_START (0x08002000) — the bootloader cannot erase itself.
 *  - If power is lost mid-page-write the page may be corrupt; the device
 *    will need reflashing via ST-Link.  The per-page CRC check in main_bl.c
 *    minimises this window by verifying before committing the next page.
 */

#include "flash_bl.h"
#include "device_regs.h"

/* Keys from STM32F103 reference manual section 3.3.2 */
#define FLASH_KEY1  0x45670123UL
#define FLASH_KEY2  0xCDEF89ABUL

/* Wait until flash is not busy */
static inline void flash_wait(void)
{
    while (FLASH_SR & FLASH_SR_BSY);
}

void flash_unlock(void)
{
    if (FLASH_CR & FLASH_CR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
}

void flash_lock(void)
{
    FLASH_CR |= FLASH_CR_LOCK;
}

void flash_erase_page(uint32_t addr)
{
    /* Safety: never erase the bootloader */
    if (addr < APP_FLASH_START) return;

    flash_wait();
    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR  = addr;
    FLASH_CR |= FLASH_CR_STRT;
    flash_wait();
    FLASH_CR &= ~FLASH_CR_PER;
}

void flash_write_halfword(uint32_t addr, uint16_t data)
{
    flash_wait();
    FLASH_CR |= FLASH_CR_PG;
    *((volatile uint16_t *)addr) = data;
    flash_wait();
    FLASH_CR &= ~FLASH_CR_PG;
}

void flash_write_page(uint32_t dst, const uint8_t *src, uint32_t len)
{
    if (dst < APP_FLASH_START) return;

    flash_erase_page(dst);

    /* Write halfword-by-halfword */
    uint32_t i = 0;
    while (i < len) {
        uint16_t hw;
        uint8_t lo = src[i++];
        uint8_t hi = (i < len) ? src[i++] : 0xFF;
        hw = (uint16_t)(lo | ((uint16_t)hi << 8));
        flash_write_halfword(dst, hw);
        dst += 2;
    }

    /* Pad remainder of 1KB page with 0xFFFF if len < FLASH_PAGE_SIZE */
    while (i < FLASH_PAGE_SIZE) {
        flash_write_halfword(dst, 0xFFFF);
        dst += 2;
        i   += 2;
    }
}
