/* bootloader_request.h — API for the running application to trigger a
 * programmatic jump back to the CAN+UART bootloader.
 *
 * Usage from anywhere in the firmware:
 *
 *   #include "bootloader_request.h"
 *   bootloader_request_reset();   // does not return
 *
 * What it does:
 *   1. Writes BKP_DR1 = 0xB001 ("boot-one") to the STM32 backup register.
 *      Backup registers survive NVIC_SystemReset() (they are in the RTC/backup
 *      power domain, kept alive by VBAT or VDD standby).
 *   2. Calls NVIC_SystemReset().
 *
 * After the reset the bootloader reads the flag, clears it, and waits 30 seconds
 * for a CAN or UART upload tool to connect — instead of the normal 500 ms — so
 * there is no race condition between the reset and the upload tool starting.
 *
 * If the upload tool does not connect within 30 s, the bootloader jumps to the
 * application as normal.
 */

#pragma once
#include "stm32f1xx_hal.h"

/* Write the BKP magic and reset into the bootloader.  Never returns. */
static inline __attribute__((noreturn)) void bootloader_request_reset(void)
{
    /* Enable the PWR and BKP peripheral clocks on APB1 */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();

    /* Disable backup domain write protection (required to write BKP registers) */
    HAL_PWR_EnableBkUpAccess();

    /* Write the "enter bootloader" magic — bootloader reads this on startup */
    BKP->DR1 = 0xB001U;

    /* Data Synchronisation Barrier: ensure the write completes before reset */
    __DSB();

    /* Reset — BKP register survives, bootloader sees the flag */
    NVIC_SystemReset();

    /* Unreachable — suppress compiler warning */
    for (;;) {}
}

/* Enter the STM32 ROM DFU bootloader directly via USB.
 *
 * Windows shows "DEVICE_DESCRIPTOR_FAILURE" / VID_0000:PID_0002 when the
 * STM32's USB D+ pullup is active but the firmware doesn't implement USB.
 * Calling this function resets into the factory ROM bootloader, which
 * enumerates as VID_0483:PID_DF11 (STM32 DFU mode).
 *
 * Windows: install STM32CubeProgrammer (recommended) or use Zadig to
 *   assign WinUSB, then flash with dfu-util.
 * Linux/Mac: use dfu-util directly (usually installed by default).
 *
 * cmd-line: python3 tools/can_flash.py -d can0 --enter-dfu
 *           or serial: RD (type in the UART terminal)
 *
 * Only works if the board has a USB connector on PA11/PA12.
 * If the board has no USB, the device reboots normally. */
static inline __attribute__((noreturn)) void bootloader_request_dfu(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    BKP->DR1 = 0x0000U;           /* clear "our bootloader" flag */
    BKP->DR2 = 0xDFD0U;           /* "DFU-Device-0" — triggers ROM DFU jump */
    __DSB();
    NVIC_SystemReset();
    for (;;) {}
}
