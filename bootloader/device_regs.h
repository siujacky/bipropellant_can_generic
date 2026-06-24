/* device_regs.h — Minimal STM32F103 peripheral register definitions.
 * No HAL, no CMSIS device headers — only what the bootloader needs.
 * Peripheral base addresses from STM32F103 reference manual (RM0008).
 */

#ifndef DEVICE_REGS_H
#define DEVICE_REGS_H

#include <stdint.h>

/* ---- Cortex-M3 SCB (from CMSIS core) ---- */
#define SCB_BASE    0xE000ED00UL
#define SCB_VTOR    (*(volatile uint32_t *)(SCB_BASE + 0x08))

/* ---- RCC ---- */
#define RCC_BASE    0x40021000UL
#define RCC_APB2ENR (*(volatile uint32_t *)(RCC_BASE + 0x18))
#define RCC_APB2ENR_IOPAEN    (1U << 2)
#define RCC_APB2ENR_IOPBEN    (1U << 3)
#define RCC_APB2ENR_USART1EN  (1U << 14)  /* USART1 on APB2 */

/* ---- GPIO ---- */
#define GPIOA_BASE  0x40010800UL
#define GPIOB_BASE  0x40010C00UL

typedef struct {
    volatile uint32_t CRL;   /* offset 0x00 — pins 0-7  */
    volatile uint32_t CRH;   /* offset 0x04 — pins 8-15 */
    volatile uint32_t IDR;   /* offset 0x08 */
    volatile uint32_t ODR;   /* offset 0x0C */
    volatile uint32_t BSRR;  /* offset 0x10 */
    volatile uint32_t BRR;   /* offset 0x14 */
    volatile uint32_t LCKR;  /* offset 0x18 */
} GPIO_TypeDef;

#define GPIOA  ((GPIO_TypeDef *)GPIOA_BASE)
#define GPIOB  ((GPIO_TypeDef *)GPIOB_BASE)

/* GPIO CRL/CRH mode/config values (2+2 bits per pin) */
#define GPIO_MODE_OUT_50  0x3U   /* Output, max speed 50MHz */
#define GPIO_CNF_PP       0x0U   /* Push-pull output        */
#define GPIO_MODE_IN      0x0U   /* Input mode              */
#define GPIO_CNF_FLOAT    0x1U   /* Floating input          */
#define GPIO_CNF_PULL     0x2U   /* Input with pull-up/down */

/* ---- FLASH ---- */
#define FLASH_BASE      0x40022000UL
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00))
#define FLASH_KEYR      (*(volatile uint32_t *)(FLASH_BASE + 0x04))
#define FLASH_SR        (*(volatile uint32_t *)(FLASH_BASE + 0x0C))
#define FLASH_CR        (*(volatile uint32_t *)(FLASH_BASE + 0x10))
#define FLASH_AR        (*(volatile uint32_t *)(FLASH_BASE + 0x14))

#define FLASH_CR_PG     (1U << 0)
#define FLASH_CR_PER    (1U << 1)
#define FLASH_CR_STRT   (1U << 6)
#define FLASH_CR_LOCK   (1U << 7)
#define FLASH_SR_BSY    (1U << 0)
#define FLASH_SR_EOP    (1U << 5)

/* ---- DESIG (Device signature / unique ID) ---- */
#define DESIG_UNIQUE_ID_BASE  0x1FFFF7E8UL
#define DESIG_UNIQUE_ID0  (*(volatile uint32_t *)(DESIG_UNIQUE_ID_BASE + 0x00))
#define DESIG_UNIQUE_ID1  (*(volatile uint32_t *)(DESIG_UNIQUE_ID_BASE + 0x04))
#define DESIG_UNIQUE_ID2  (*(volatile uint32_t *)(DESIG_UNIQUE_ID_BASE + 0x08))

/* ---- NVIC disable-all helper ---- */
#define NVIC_ICER0  (*(volatile uint32_t *)0xE000E180UL)
#define NVIC_ICER1  (*(volatile uint32_t *)0xE000E184UL)
#define NVIC_ICER2  (*(volatile uint32_t *)0xE000E188UL)

#endif /* DEVICE_REGS_H */
