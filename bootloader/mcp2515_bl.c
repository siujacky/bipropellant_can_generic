/* mcp2515_bl.c — Hardware SPI1 + MCP2515 for the bootloader.
 * Pins: PA4=CS, PA5=SCK(SPI1_SCK), PA6=MISO(SPI1_MISO), PA7=MOSI(SPI1_MOSI), PB0=INT
 * 250kbps @ 8MHz: CNF1=0x00, CNF2=0x9E, CNF3=0x03 (BRP=0,TQ=250ns,16TQ=250kbps)
 * These pins MATCH the Blue Pill CAN tool app's hardware SPI configuration.
 */
#include "mcp2515_bl.h"
#include "device_regs.h"

/* CS = PA4 (GPIO output) */
#define CS_HIGH()   GPIOA->BSRR = (1U << 4)
#define CS_LOW()    GPIOA->BSRR = (1U << (4 + 16))

/* INT = PB0 (input pull-up, active-low) */
#define INT_READ()  ((GPIOB->IDR) & 1U)

static void delay_ms(uint32_t ms) {
    while (ms--) { volatile uint32_t c = 2667; while (c--); }
}

/* -----------------------------------------------------------------------
 * Hardware SPI1 init + GPIO
 * ----------------------------------------------------------------------- */
static void gpio_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_SPI1EN;

    /* PA4 (CS)  : GPIO output push-pull 50MHz: CRL bits[19:16] */
    GPIOA->CRL = (GPIOA->CRL & ~(0xFU << 16)) | (0x3U << 16);
    /* PA5 (SCK) : SPI1_SCK AF push-pull 50MHz: CRL bits[23:20] */
    GPIOA->CRL = (GPIOA->CRL & ~(0xFU << 20)) | (0xBU << 20);
    /* PA6 (MISO): SPI1_MISO floating input: CRL bits[27:24] */
    GPIOA->CRL = (GPIOA->CRL & ~(0xFU << 24)) | (0x4U << 24);
    /* PA7 (MOSI): SPI1_MOSI AF push-pull 50MHz: CRL bits[31:28] */
    GPIOA->CRL = (GPIOA->CRL & ~(0xFU << 28)) | (0xBU << 28);
    /* PB0 (INT) : input pull-up: CRL bits[3:0] = 0x8 */
    GPIOB->CRL = (GPIOB->CRL & ~(0xFU << 0)) | (0x8U << 0);
    GPIOB->BSRR = (1U << 0);   /* enable pull-up */

    CS_HIGH();

    /* SPI1: SSM=1, SSI=1, SPE=1, BR=001 (2MHz @ 8MHz APB2), MSTR=1, Mode-0 */
    SPI1_CR1 = SPI1_CR1_SSM | SPI1_CR1_SSI | SPI1_CR1_SPE |
               SPI1_CR1_BR_DIV4 | SPI1_CR1_MSTR;
}

/* -----------------------------------------------------------------------
 * Hardware SPI1 transfer
 * ----------------------------------------------------------------------- */
static uint8_t spi_xfer(uint8_t byte) {
    if (SPI1_SR & SPI1_SR_RXNE) { (void)SPI1_DR; }
    while (!(SPI1_SR & SPI1_SR_TXE));
    SPI1_DR = byte;
    while (!(SPI1_SR & SPI1_SR_RXNE));
    return (uint8_t)SPI1_DR;
}

/* -----------------------------------------------------------------------
 * MCP2515 register access
 * ----------------------------------------------------------------------- */
static void mcp_write_reg(uint8_t addr, uint8_t val) {
    CS_LOW(); spi_xfer(MCP_WRITE); spi_xfer(addr); spi_xfer(val); CS_HIGH();
}
static uint8_t mcp_read_reg(uint8_t addr) {
    CS_LOW(); spi_xfer(MCP_READ); spi_xfer(addr);
    uint8_t v = spi_xfer(0x00); CS_HIGH(); return v;
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
void mcp2515_init(void) {
    gpio_init();
    CS_LOW(); spi_xfer(MCP_RESET); CS_HIGH();
    delay_ms(10);
    /* 250kbps @ 8MHz: BRP=0(TQ=250ns), 16TQ/bit */
    mcp_write_reg(MCP_CNF1, 0x00);
    mcp_write_reg(MCP_CNF2, 0x9E);
    mcp_write_reg(MCP_CNF3, 0x03);
    mcp_write_reg(MCP_RXB0CTRL, 0x64);  /* accept all + BUKT */
    mcp_write_reg(MCP_CANINTE,  0x00);
    /* Enter normal mode with OSM (One-Shot Mode, CANCTRL bit 3).
     * OSM = each TX is attempted ONCE; if no ACK the ABTF flag is set and
     * TXREQ clears immediately. Without OSM the MCP2515 retries the first
     * hello indefinitely → floods bus with error frames → Pico 2 gets
     * RXWARN/RXEP and never receives a valid frame.  With OSM the main loop
     * can retry at a controlled 50ms interval instead. */
    mcp_write_reg(MCP_CANCTRL,  MCP_MODE_NORMAL | 0x08U);  /* NORMAL + OSM */
    delay_ms(1);
}

int mcp2515_rx_available(void) {
    return (mcp_read_reg(MCP_CANINTF) & (MCP_CANINTF_RX0IF | MCP_CANINTF_RX1IF)) ? 1 : 0;
}

void mcp2515_tx(uint16_t id, const uint8_t *data, uint8_t len) {
    if (len > 8) len = 8;
    uint32_t timeout = 10000;
    while ((mcp_read_reg(MCP_TXB0CTRL) & MCP_TXCTRL_TXREQ) && --timeout);
    if (!timeout) return;
    mcp_write_reg(MCP_TXB0SIDH, (uint8_t)(id >> 3));
    mcp_write_reg(MCP_TXB0SIDL, (uint8_t)((id & 0x7U) << 5));
    mcp_write_reg(MCP_TXB0EID8, 0x00);
    mcp_write_reg(MCP_TXB0EID0, 0x00);
    mcp_write_reg(MCP_TXB0DLC,  len & 0x0F);
    for (uint8_t i = 0; i < len; i++) mcp_write_reg(MCP_TXB0D0 + i, data[i]);
    CS_LOW(); spi_xfer(MCP_RTS_TXB0); CS_HIGH();
    timeout = 50000;
    while ((mcp_read_reg(MCP_TXB0CTRL) & MCP_TXCTRL_TXREQ) && --timeout);
}

int mcp2515_rx(uint16_t *id_out, uint8_t *data_out, uint8_t *len_out) {
    uint8_t intf = mcp_read_reg(MCP_CANINTF);
    uint8_t base, flag;
    if (intf & MCP_CANINTF_RX0IF) { base = MCP_RXB0SIDH; flag = MCP_CANINTF_RX0IF; }
    else if (intf & MCP_CANINTF_RX1IF) { base = MCP_RXB1SIDH; flag = MCP_CANINTF_RX1IF; }
    else return 0;
    uint8_t sidh = mcp_read_reg(base);
    uint8_t sidl = mcp_read_reg(base + 1);
    uint8_t dlc  = mcp_read_reg(base + 4) & 0x0F;
    if (id_out)  *id_out  = ((uint16_t)sidh << 3) | ((sidl >> 5) & 0x07);
    if (len_out) *len_out = dlc;
    if (data_out && dlc > 0) {
        uint8_t n = (dlc > 8) ? 8 : dlc;
        for (uint8_t i = 0; i < n; i++) data_out[i] = mcp_read_reg(base + 5 + i);
    }
    CS_LOW(); spi_xfer(MCP_BIT_MODIFY); spi_xfer(MCP_CANINTF);
    spi_xfer(flag); spi_xfer(0x00); CS_HIGH();
    return 1;
}
