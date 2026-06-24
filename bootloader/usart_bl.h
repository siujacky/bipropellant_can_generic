/* usart_bl.h — USART1 bootloader interface (PA9=TX, PA10=RX).
 *
 * Pins chosen because they do NOT conflict with MCP2515 software-SPI:
 *   MCP2515 uses PA2/PA3/PB10/PB11.  USART1 uses PA9/PA10.
 * PA9/PA10 are phase-PWM in the application but unused in the bootloader.
 *
 * Baud: 115200 @ 8 MHz HSI.  BRR = (4<<4)|5 = 0x0045.
 *
 * Protocol: compatible with jsphuebner/stm32-CANBootloader uart-updater.py.
 *   Trigger: 0xAA byte → enter programming mode.
 *   Then: same page protocol as CAN (page count, pages, CRC32, P/E/D).
 */

#ifndef USART_BL_H
#define USART_BL_H

#include <stdint.h>

/* Initialise USART1 at 115200 baud (no interrupts, polling). */
void usart_init(void);

/* Returns 1 if a byte is waiting in RDR, 0 otherwise (non-blocking). */
int usart_rx_ready(void);

/* Block until a byte is available, then return it. */
uint8_t usart_rx_byte(void);

/* Transmit a single byte (blocks until TXE). */
void usart_tx_byte(uint8_t b);

/* Transmit a buffer. */
void usart_tx(const uint8_t *buf, uint8_t len);

/* Receive exactly len bytes; returns 0 on timeout (timeout_ms each byte). */
int usart_rx_buf(uint8_t *buf, uint32_t len, uint32_t timeout_ms);

#endif /* USART_BL_H */
