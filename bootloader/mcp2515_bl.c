/* mcp2515_bl.c — Software SPI + MCP2515 minimal driver.
 * No HAL, no CMSIS device headers beyond CMSIS core.
 * Runs at 8 MHz HSI; delay loops calibrated for 8 MHz.
 */

#include "mcp2515_bl.h"
#include "device_regs.h"

/* -----------------------------------------------------------------------
 * Pin helpers — direct register manipulation
 * CS=PB11, SCK=PB10, MOSI=PA2, MISO=PA3
 * ----------------------------------------------------------------------- */

/* BSRR: bits [15:0] set, bits [31:16] clear */
#define CS_HIGH()    GPIOB->BSRR = (1U << 11)
#define CS_LOW()     GPIOB->BSRR = (1U << (11 + 16))
#define SCK_HIGH()   GPIOB->BSRR = (1U << 10)
#define SCK_LOW()    GPIOB->BSRR = (1U << (10 + 16))
#define MOSI_HIGH()  GPIOA->BSRR = (1U << 2)
#define MOSI_LOW()   GPIOA->BSRR = (1U << (2 + 16))
#define MISO_READ()  ((GPIOA->IDR >> 3) & 1U)

/* Short delay — at 8 MHz each loop ~3 cycles; ~4 loops ≈ 1.5µs for SPI */
static void spi_delay(void)
{
    volatile uint32_t i = 4;
    while (i--);
}

/* -----------------------------------------------------------------------
 * GPIO initialisation
 * CRL controls pins 0-7, CRH controls pins 8-15.
 * Each pin: 4 bits = [CNF1:CNF0:MODE1:MODE0]
 * Output push-pull 50MHz = CNF=00, MODE=11 → 0x3
 * Floating input          = CNF=01, MODE=00 → 0x4
 * ----------------------------------------------------------------------- */
static void gpio_init(void)
{
    /* Enable clocks for GPIOA and GPIOB */
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;

    /* PA2 (MOSI) — output push-pull 50MHz
     * CRL pin2 field = bits [11:8] */
    GPIOA->CRL &= ~(0xFU << 8);
    GPIOA->CRL |=  (0x3U << 8);   /* MODE=11 (50MHz out), CNF=00 (PP) */

    /* PA3 (MISO) — floating input
     * CRL pin3 field = bits [15:12] */
    GPIOA->CRL &= ~(0xFU << 12);
    GPIOA->CRL |=  (0x4U << 12);  /* MODE=00 (input), CNF=01 (float) */

    /* PB10 (SCK) — output push-pull 50MHz
     * CRH pin10 field = bits [11:8] */
    GPIOB->CRH &= ~(0xFU << 8);
    GPIOB->CRH |=  (0x3U << 8);

    /* PB11 (CS) — output push-pull 50MHz
     * CRH pin11 field = bits [15:12] */
    GPIOB->CRH &= ~(0xFU << 12);
    GPIOB->CRH |=  (0x3U << 12);

    /* Initial states */
    CS_HIGH();
    SCK_LOW();
    MOSI_LOW();
}

/* -----------------------------------------------------------------------
 * Bit-bang SPI — mode 0 (CPOL=0, CPHA=0), MSB first
 * ----------------------------------------------------------------------- */
static uint8_t spi_xfer(uint8_t byte)
{
    uint8_t result = 0;
    for (int i = 7; i >= 0; i--) {
        if (byte & (1U << i))
            MOSI_HIGH();
        else
            MOSI_LOW();
        spi_delay();
        SCK_HIGH();
        spi_delay();
        if (MISO_READ())
            result |= (1U << i);
        SCK_LOW();
    }
    return result;
}

/* -----------------------------------------------------------------------
 * MCP2515 register access
 * ----------------------------------------------------------------------- */
static void mcp_write_reg(uint8_t addr, uint8_t val)
{
    CS_LOW();
    spi_xfer(MCP_WRITE);
    spi_xfer(addr);
    spi_xfer(val);
    CS_HIGH();
}

static uint8_t mcp_read_reg(uint8_t addr)
{
    CS_LOW();
    spi_xfer(MCP_READ);
    spi_xfer(addr);
    uint8_t val = spi_xfer(0x00);
    CS_HIGH();
    return val;
}

/* Delay ~1ms at 8 MHz (8000 cycles / ~3 cycles per loop ≈ 2667 iterations) */
static void delay_ms(uint32_t ms)
{
    while (ms--) {
        volatile uint32_t c = 2667;
        while (c--);
    }
}

/* -----------------------------------------------------------------------
 * Public: mcp2515_init
 * Sequence: RESET → wait 10ms → config mode → CNF → filters → normal mode
 * ----------------------------------------------------------------------- */
void mcp2515_init(void)
{
    gpio_init();

    /* Hardware reset via SPI */
    CS_LOW();
    spi_xfer(MCP_RESET);
    CS_HIGH();
    delay_ms(10);

    /* Should now be in config mode (CANSTAT = 0x80) */
    /* 500 kbps at 8 MHz crystal:
     * TQ = 2 × (BRP+1) / Fosc; BRP=0 → TQ = 250ns @ 8MHz
     * CNF1 = 0x00  (BRP=0, SJW=1TQ)
     * CNF2 = 0x90  (BTLMODE=1, SAM=0, PHSEG1=2TQ, PRSEG=1TQ) → 0b10010000
     * CNF3 = 0x02  (PHSEG2=3TQ, WAKFIL=0, SOF=0) — wait, spec says CNF3=0x82?
     *   Actually: CNF3=0x82 = 0b10000010 → SOF=1(CLKOUT), PHSEG2=3TQ
     *   Using the values specified in the task: CNF1=0x00, CNF2=0x90, CNF3=0x02
     *   Per jsphuebner stm32-CANBootloader reference: CNF3=0x02 gives PHSEG2=3TQ
     *   Task specifies 0x82 so we honour that exactly.
     */
    mcp_write_reg(MCP_CNF1, 0x00);
    mcp_write_reg(MCP_CNF2, 0x90);
    mcp_write_reg(MCP_CNF3, 0x82);

    /* Accept all frames into RXB0 (RXM=11), enable rollover to RXB1 (BUKT=1)
     * RXB0CTRL = 0x64 = 0b01100100: RXM=11 (accept all), BUKT=1 */
    mcp_write_reg(MCP_RXB0CTRL, 0x64);

    /* Disable all interrupts (we poll CANINTF) */
    mcp_write_reg(MCP_CANINTE, 0x00);

    /* Switch to normal mode */
    mcp_write_reg(MCP_CANCTRL, MCP_MODE_NORMAL);

    /* Wait for normal mode to be active */
    delay_ms(1);
}

/* -----------------------------------------------------------------------
 * Public: mcp2515_tx — transmit a CAN frame via TXB0
 * Standard 11-bit ID, data 0-8 bytes.
 * ----------------------------------------------------------------------- */
void mcp2515_tx(uint16_t id, const uint8_t *data, uint8_t len)
{
    if (len > 8) len = 8;

    /* SIDH = id[10:3], SIDL = id[2:0] << 5 */
    mcp_write_reg(MCP_TXB0SIDH, (uint8_t)(id >> 3));
    mcp_write_reg(MCP_TXB0SIDH + 1, (uint8_t)((id & 0x7U) << 5)); /* SIDL */
    mcp_write_reg(MCP_TXB0SIDH + 2, 0x00); /* EID8 */
    mcp_write_reg(MCP_TXB0SIDH + 3, 0x00); /* EID0 */
    mcp_write_reg(MCP_TXB0SIDH + 4, len);  /* DLC */

    for (uint8_t i = 0; i < len; i++) {
        mcp_write_reg(MCP_TXB0SIDH + 5 + i, data[i]);
    }

    /* Request transmit */
    CS_LOW();
    spi_xfer(MCP_RTS_TXB0);
    CS_HIGH();
}

/* -----------------------------------------------------------------------
 * Public: mcp2515_rx_available — returns 1 if RXB0 has a frame
 * ----------------------------------------------------------------------- */
int mcp2515_rx_available(void)
{
    return (mcp_read_reg(MCP_CANINTF) & MCP_CANINTF_RX0IF) ? 1 : 0;
}

/* -----------------------------------------------------------------------
 * Public: mcp2515_rx — read a frame from RXB0
 * Returns 1 on success, 0 if no frame available.
 * ----------------------------------------------------------------------- */
int mcp2515_rx(uint16_t *id_out, uint8_t *data_out, uint8_t *len_out)
{
    if (!(mcp_read_reg(MCP_CANINTF) & MCP_CANINTF_RX0IF))
        return 0;

    uint8_t sidh = mcp_read_reg(MCP_RXB0SIDH);
    uint8_t sidl = mcp_read_reg(MCP_RXB0SIDH + 1);
    /* Skip EID8, EID0 */
    uint8_t dlc  = mcp_read_reg(MCP_RXB0SIDH + 4) & 0x0F;

    if (id_out)
        *id_out = ((uint16_t)sidh << 3) | ((sidl >> 5) & 0x7U);
    if (len_out)
        *len_out = dlc;

    if (dlc > 8) dlc = 8;
    if (data_out) {
        for (uint8_t i = 0; i < dlc; i++) {
            data_out[i] = mcp_read_reg(MCP_RXB0SIDH + 5 + i);
        }
    }

    /* Clear RX0IF to release the buffer */
    mcp_write_reg(MCP_CANINTF, 0x00);

    return 1;
}
