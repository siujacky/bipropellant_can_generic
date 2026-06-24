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
    /* PA6 (MISO): SPI1_MISO floating input (5V tolerant on STM32F103) */
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
    /* Two-step: first enter NORMAL mode (CANCTRL = 0x00, all bytes LOW → always
     * works even at 5V), then enable OSM (bit 3) via a second write (0x08 =
     * 00001000b, bit 3 HIGH — marginal at 5V but works more reliably than the
     * one-step 0x08 write done during the CONFIG→NORMAL transition).
     * With OSM=1: each TX attempt is made ONCE; no bus-flooding on no-ACK. */
    mcp_write_reg(MCP_CANCTRL, MCP_MODE_NORMAL);  /* step 1: enter NORMAL (all zeros) */
    delay_ms(2);                                   /* wait for mode transition */
    /* step 2: set OSM — retry up to 5 times */
    uint8_t canctrl_rb;
    for (int try = 0; try < 5; try++) {
        mcp_write_reg(MCP_CANCTRL, 0x08U);         /* NORMAL mode + OSM (bit 3) */
        delay_ms(1);
        canctrl_rb = mcp_read_reg(MCP_CANCTRL);
        if ((canctrl_rb & 0x08U) == 0x08U) break;  /* OSM set correctly */
    }
    g_bl_canstat = mcp_read_reg(MCP_CANSTAT);
    g_bl_canctrl = canctrl_rb;

    /* Abort all stale TX: SPI RESET (0xC0 MSB=1) may fail at 5V leaving old
     * TXREQ bits set. Write CANCTRL=0x10 (NORMAL+ABAT, only bit4=HIGH works
     * at 5V). After abort, chip returns to NORMAL with all TXREQ bits cleared. */
    mcp_write_reg(MCP_CANCTRL, 0x10U);  /* NORMAL + ABAT */
    delay_ms(5);                         /* wait for all TX to abort */
    mcp_write_reg(MCP_CANCTRL, 0x00U);  /* NORMAL, clear ABAT */
    delay_ms(2);
    /* Re-apply OSM after ABAT cleared it */
    mcp_write_reg(MCP_CANCTRL, 0x08U);
    delay_ms(1);
    canctrl_rb = mcp_read_reg(MCP_CANCTRL);
    g_bl_canctrl = canctrl_rb;   /* update with post-ABAT value */

    /* ---------------------------------------------------------------
     * Internal LOOPBACK self-test (no CANH/CANL wires needed).
     * Put MCP2515 into LOOPBACK mode, transmit one frame, verify it
     * is received internally.  Result in g_bl_loopback:
     *   0xAA = test not run (init fail)
     *   0x01 = LOOPBACK TX+RX OK   → SPI and MCP2515 logic work ✓
     *   0x00 = LOOPBACK TX failed  → SPI write of TXREQ is broken ✗
     * After the test the chip is restored to NORMAL mode.
     * --------------------------------------------------------------- */
    g_bl_loopback = 0xAAU;
    mcp_write_reg(MCP_CANCTRL, 0x40U);   /* LOOPBACK mode */
    delay_ms(2);
    if ((mcp_read_reg(MCP_CANSTAT) & 0xE0U) == 0x40U) {
        /* Abort all pending TX: SPI RESET (0xC0, MSB=1) may fail at 5V leaving
         * stale TXREQ in TXB0/TXB1/TXB2 from a previous session.  Any stale TX
         * would block our TXB1 test frame.  Set CANCTRL.ABAT (bit4=0x10) in
         * LOOPBACK mode: 0x40|0x10=0x50 (bits 4,6 HIGH — both work at 5V). */
        mcp_write_reg(MCP_CANCTRL, 0x50U);   /* LOOPBACK + ABAT */
        delay_ms(2);                           /* wait for abort to complete */
        mcp_write_reg(MCP_CANCTRL, 0x40U);   /* LOOPBACK, clear ABAT */
        delay_ms(1);

        /* Clear any stale RX flags */
        CS_LOW(); spi_xfer(MCP_BIT_MODIFY); spi_xfer(MCP_CANINTF);
        spi_xfer(0x03U); spi_xfer(0x00U); CS_HIGH();
        delay_ms(1);

        /* Clear stale TXREQ in TXB1 by writing 0x00 (all LOW bits — always works
         * at 5V since no HIGH bits needed). ABAT (bit4=0x10) doesn't work at 5V.
         * Writing 0x00 to addr 0x40 (bit6 only) clears TXREQ and priority. */
        mcp_write_reg(MCP_TXB1CTRL, 0x00U);
        delay_ms(2);
        g_bl_txb0ctrl = mcp_read_reg(MCP_TXB1CTRL);  /* should be 0x00 now */

        /* Load test frame into TXB1 */
        mcp_write_reg(MCP_TXB1SIDH, 0x55U);
        mcp_write_reg(MCP_TXB1SIDL, 0x00U);
        mcp_write_reg(MCP_TXB1EID8, 0x00U);
        mcp_write_reg(MCP_TXB1EID0, 0x00U);
        mcp_write_reg(MCP_TXB1DLC,  0x01U);
        mcp_write_reg(MCP_TXB1D0,   0xA5U);
        /* Trigger TX: addr 0x40 (bit6 only ✓), data 0x04 (bit2 = TXREQ ✓) */
        mcp_write_reg(MCP_TXB1CTRL, MCP_TXCTRL_TXREQ);
        /* Wait for internal RX — check BOTH RXB0 and RXB1 (rollover may apply) */
        uint32_t poll = 10000;
        while (poll-- && !(mcp_read_reg(MCP_CANINTF) & 0x03U));
        g_bl_loopback = (mcp_read_reg(MCP_CANINTF) & 0x03U) ? 1U : 0U;
        /* Clear all RX flags */
        CS_LOW(); spi_xfer(MCP_BIT_MODIFY); spi_xfer(MCP_CANINTF);
        spi_xfer(0x03U); spi_xfer(0x00U); CS_HIGH();
    }
    /* Restore NORMAL + OSM */
    mcp_write_reg(MCP_CANCTRL, MCP_MODE_NORMAL);
    delay_ms(2);
    mcp_write_reg(MCP_CANCTRL, 0x08U);
}

volatile uint8_t g_bl_canstat;
volatile uint8_t g_bl_canctrl;
volatile uint8_t g_bl_loopback;

int mcp2515_rx_available(void) {
    return (mcp_read_reg(MCP_CANINTF) & (MCP_CANINTF_RX0IF | MCP_CANINTF_RX1IF)) ? 1 : 0;
}

/* g_bl_txb0ctrl: TXB0CTRL read after first TX attempt.
 * 0x00 = TX success (TXREQ cleared, no error).
 * 0x10 = ABTF (frame attempted, not ACKed, aborted by OSM).
 * 0x08 = TXREQ still set (stuck — OSM may not have cleared it).
 * 0xFF = SPI dead (all reads return 0xFF). */
volatile uint8_t g_bl_txb0ctrl = 0xAA; /* 0xAA = not yet set */
static uint8_t g_bl_tx_done;

void mcp2515_tx(uint16_t id, const uint8_t *data, uint8_t len) {
    if (len > 8) len = 8;
    /* Use TXB1 (addresses 0x40-0x46, bit6 only) not TXB0 (0x30-0x36, bit4+5).
     * At 5V VCC bit4 of address bytes fails → writes to TXB0 (0x3x) land in
     * TEC/REC (0x2x, read-only): frame never loaded, TXREQ never triggered.
     * TXB1 addresses use only bit6 HIGH (confirmed working at 5V). */
    uint32_t timeout = 10000;
    while ((mcp_read_reg(MCP_TXB1CTRL) & MCP_TXCTRL_TXREQ) && --timeout);
    if (!timeout) return;
    mcp_write_reg(MCP_TXB1SIDH, (uint8_t)(id >> 3));
    mcp_write_reg(MCP_TXB1SIDL, (uint8_t)((id & 0x7U) << 5));
    mcp_write_reg(MCP_TXB1EID8, 0x00);
    mcp_write_reg(MCP_TXB1EID0, 0x00);
    mcp_write_reg(MCP_TXB1DLC,  len & 0x0F);
    for (uint8_t i = 0; i < len; i++) mcp_write_reg(MCP_TXB1D0 + i, data[i]);
    mcp_write_reg(MCP_TXB1CTRL, MCP_TXCTRL_TXREQ);  /* 0x40 = bit6 only ✓ */
    timeout = 50000;
    while ((mcp_read_reg(MCP_TXB1CTRL) & MCP_TXCTRL_TXREQ) && --timeout);
    if (!g_bl_tx_done) { g_bl_txb0ctrl = mcp_read_reg(MCP_TXB1CTRL); g_bl_tx_done = 1; }
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
