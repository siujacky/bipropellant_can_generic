/* usart_bl.c — USART1 driver for the bootloader (no HAL, direct registers).
 *
 * STM32F103 USART1: APB2 peripheral, base 0x40013800.
 * Pins: PA9 = TX (AF push-pull, 50 MHz), PA10 = RX (floating input).
 * 8 MHz HSI, 115200 baud: USARTDIV = 4.340 → BRR = (4<<4)|5 = 0x0045.
 * 8N1, no parity, no flow control, no interrupts (polling only).
 */

#include "usart_bl.h"
#include "device_regs.h"

/* ---- USART1 register definitions ---- */
#define USART1_BASE  0x40013800UL
#define USART1_SR   (*(volatile uint32_t *)(USART1_BASE + 0x00))
#define USART1_DR   (*(volatile uint32_t *)(USART1_BASE + 0x04))
#define USART1_BRR  (*(volatile uint32_t *)(USART1_BASE + 0x08))
#define USART1_CR1  (*(volatile uint32_t *)(USART1_BASE + 0x0C))

#define USART_SR_RXNE  (1U << 5)   /* RX data register not empty */
#define USART_SR_TXE   (1U << 7)   /* TX data register empty     */
#define USART_SR_TC    (1U << 6)   /* Transmission complete       */
#define USART_CR1_UE   (1U << 13)  /* USART enable                */
#define USART_CR1_TE   (1U << 3)   /* Transmitter enable          */
#define USART_CR1_RE   (1U << 2)   /* Receiver enable             */

/* ---- RCC USART1 clock enable ---- */
#define RCC_APB2ENR_USART1EN  (1U << 14)

/* GPIO AF push-pull output for TX (50 MHz = mode 0b11, cnf 0b10) */
#define GPIO_MODE_OUT_50_AFPP  0xBU  /* CNF=10 (AF PP), MODE=11 (50MHz) */

/* Approximate 1-ms spin at 8 MHz (-O2).  Used inside timeout loops. */
static inline void spin_1ms(void)
{
    volatile uint32_t c = 2667;
    while (c--)
        ;
}

void usart_init(void)
{
    /* Enable GPIOA and USART1 clocks on APB2 */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* PA9 = TX: AF push-pull 50 MHz (CRH bits [7:4] = 0b1011 = 0xB) */
    GPIOA->CRH &= ~(0xFU << 4);
    GPIOA->CRH |=  (GPIO_MODE_OUT_50_AFPP << 4);

    /* PA10 = RX: floating input (CRH bits [11:8] = 0b0100 = 0x4) */
    GPIOA->CRH &= ~(0xFU << 8);
    GPIOA->CRH |=  ((GPIO_MODE_IN | (GPIO_CNF_FLOAT << 2)) << 8);

    /* Configure USART1: 115200 @ 8 MHz HSI.
     * USARTDIV = 8 000 000 / (16 × 115 200) = 4.340...
     * Mantissa = 4, Fraction = round(0.340 × 16) = 5
     * BRR = (4 << 4) | 5 = 0x0045
     */
    USART1_BRR = 0x0045U;

    /* Enable USART, TX, RX */
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

int usart_rx_ready(void)
{
    return (USART1_SR & USART_SR_RXNE) ? 1 : 0;
}

uint8_t usart_rx_byte(void)
{
    while (!(USART1_SR & USART_SR_RXNE))
        ;
    return (uint8_t)(USART1_DR & 0xFFU);
}

void usart_tx_byte(uint8_t b)
{
    while (!(USART1_SR & USART_SR_TXE))
        ;
    USART1_DR = b;
}

void usart_tx(const uint8_t *buf, uint8_t len)
{
    for (uint8_t i = 0; i < len; i++)
        usart_tx_byte(buf[i]);
}

int usart_rx_buf(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t t = timeout_ms;
        while (!(USART1_SR & USART_SR_RXNE)) {
            spin_1ms();
            if (--t == 0)
                return 0;  /* timeout */
        }
        buf[i] = (uint8_t)(USART1_DR & 0xFFU);
    }
    return 1;
}
