/* mcp2515_bl.h — Minimal MCP2515 CAN controller driver for STM32F103 bootloader.
 * Software SPI (bit-bang) — no HAL, no peripheral SPI.
 *
 * Hardcoded pins:
 *   CS   = PB11   SCK  = PB10
 *   MOSI = PA2    MISO = PA3
 * Crystal: 8 MHz → 500 kbps (CNF1=0x00, CNF2=0x90, CNF3=0x82)
 */

#ifndef MCP2515_BL_H
#define MCP2515_BL_H

#include <stdint.h>

/* MCP2515 SPI commands */
#define MCP_RESET       0xC0
#define MCP_READ        0x03
#define MCP_WRITE       0x02
#define MCP_RTS_TXB0    0x81   /* Request-to-send TXB0 */
#define MCP_READ_RX0    0x90   /* Read RX buffer 0, starting at RXB0SIDH */

/* MCP2515 register addresses */
#define MCP_CANSTAT     0x0E
#define MCP_CANCTRL     0x0F
#define MCP_CNF3        0x28
#define MCP_CNF2        0x29
#define MCP_CNF1        0x2A
#define MCP_CANINTE     0x2B
#define MCP_CANINTF     0x2C
#define MCP_RXB0CTRL    0x60
#define MCP_RXB0SIDH    0x61
#define MCP_TXB0CTRL    0x30
#define MCP_TXB0SIDH    0x31

/* CANCTRL modes */
#define MCP_MODE_NORMAL     0x00
#define MCP_MODE_CONFIG     0x80
#define MCP_MODE_MASK       0xE0

/* CANINTF bits */
#define MCP_CANINTF_RX0IF   0x01
#define MCP_CANINTF_RX1IF   0x02

/* TXB0 registers */
#define MCP_TXB0SIDL    0x32
#define MCP_TXB0EID8    0x33
#define MCP_TXB0EID0    0x34
#define MCP_TXB0DLC     0x35
#define MCP_TXB0D0      0x36
#define MCP_TXCTRL_TXREQ 0x08

/* RXB1 registers */
#define MCP_RXB1SIDH    0x71

/* SPI bit-modify command */
#define MCP_BIT_MODIFY  0x05

/* Public API */
void     mcp2515_init(void);
void     mcp2515_tx(uint16_t id, const uint8_t *data, uint8_t len);
int      mcp2515_rx_available(void);
int      mcp2515_rx(uint16_t *id_out, uint8_t *data_out, uint8_t *len_out);

#endif /* MCP2515_BL_H */
