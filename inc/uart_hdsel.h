/* uart_hdsel.h — Hardware single-wire half-duplex UART for the application.
 *
 * STM32 USART HDSEL mode (CR3 bit 3) connects the internal TX and RX paths
 * so that a single external wire handles both directions:
 *
 *   Full-duplex (normal):          Half-duplex (HDSEL):
 *   TX ──────────────►             PA_TX/RX ─────────────────────── bus
 *   RX ◄──────────────             (PA_RX released — use for anything else)
 *
 * Every byte you transmit is also echoed back on the RX shift register —
 * "you read exactly what you just transmitted."  This enables:
 *   1. Wire verification: send → read echo → compare.  Mismatch = collision
 *      or shorted line (bus arbitration for multi-master setups).
 *   2. Single-wire protocols: LIN, MODBUS RTU over RS-485 single-wire, custom
 *      sensor buses (some IMUs, distance sensors, hoverboard ESC protocols).
 *   3. Reduced pin count when bidirectional comms fit in half-duplex.
 *
 * Hardware notes:
 *   - PA_TX must be AF open-drain when multiple devices share the bus (add a
 *     4k7 pull-up to VCC externally).  Point-to-point can use push-pull.
 *   - The HAL initialises the pin correctly via HAL_HalfDuplex_Init().
 *   - Direction is managed by the USART hardware; no GPIO toggle needed.
 *   - Callers must manage the TX/RX turnaround (complete the transmission
 *     then switch to receive — HAL_UART_Transmit() blocks until TC, so
 *     calling HAL_UART_Receive() right after is safe).
 *
 * Supported ports and their single-wire pins:
 *   UART_HDSEL_1  →  USART1 PA9  (TX/single-wire pin; PA10 released)
 *   UART_HDSEL_2  →  USART2 PA2  (TX/single-wire pin; PA3  released)
 *   UART_HDSEL_3  →  USART3 PB10 (TX/single-wire pin; PB11 released)
 *
 * Typical usage:
 *   uart_hdsel_t h;
 *   uart_hdsel_init(&h, UART_HDSEL_2, 115200);   // USART2, PA2, 115200 baud
 *
 *   uint8_t msg[] = { 0x01, 0x06 };
 *   int err = uart_hdsel_tx_verify(&h, msg, sizeof(msg));
 *   if (err) { // collision or broken wire }
 *
 *   uint8_t resp[4];
 *   uart_hdsel_rx(&h, resp, sizeof(resp), 10);    // 10 ms timeout
 */

#pragma once
#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef enum {
    UART_HDSEL_1 = 1,   /* USART1, PA9  */
    UART_HDSEL_2 = 2,   /* USART2, PA2  */
    UART_HDSEL_3 = 3,   /* USART3, PB10 */
} uart_hdsel_port_t;

typedef struct {
    UART_HandleTypeDef  hal;     /* HAL handle (caller need not inspect) */
    uart_hdsel_port_t   port;
    uint32_t            baud;
} uart_hdsel_t;

/* Initialise a USART in single-wire half-duplex mode.
 * Returns HAL_OK on success, HAL_ERROR if the port is invalid. */
HAL_StatusTypeDef uart_hdsel_init(uart_hdsel_t *h, uart_hdsel_port_t port, uint32_t baud);

/* Transmit len bytes from buf.  Blocks until TC (transmission complete).
 * timeout_ms limits the HAL blocking call (recommend: len * 10 / baud_khz + 2).
 * Returns HAL_OK or a HAL error code. */
HAL_StatusTypeDef uart_hdsel_tx(uart_hdsel_t *h, const uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/* Receive up to len bytes into buf.
 * timeout_ms applies to the whole receive (not per byte).
 * Returns HAL_OK or HAL_TIMEOUT. */
HAL_StatusTypeDef uart_hdsel_rx(uart_hdsel_t *h, uint8_t *buf, uint16_t len, uint32_t timeout_ms);

/* Transmit buf[len] and read back the HDSEL loopback echo; compare byte-by-byte.
 *
 * In HDSEL mode the chip loops every transmitted bit back through the internal
 * RX path, so the receive register holds exactly what was driven on the wire.
 * A mismatch means the line was pulled to a different voltage by another node
 * (collision) or the wire is broken/shorted.
 *
 * tx_timeout_ms: transmit timeout (see uart_hdsel_tx).
 * rx_timeout_ms: per-call receive timeout after each transmitted byte (5 ms
 *   is comfortable at 115200 baud — adjust for other speeds).
 * Returns: 0 if all echoes matched; number of mismatched bytes otherwise. */
int uart_hdsel_tx_verify(uart_hdsel_t *h,
                         const uint8_t *buf, uint16_t len,
                         uint32_t tx_timeout_ms, uint32_t rx_timeout_ms);
