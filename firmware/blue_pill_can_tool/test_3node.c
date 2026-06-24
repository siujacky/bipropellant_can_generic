/* test_3node.c — 3-node CAN bus performance tester.
 * Blue Pill runs bxCAN (PB8/PB9) + MCP2515 (PA4-PA7/SPI1) simultaneously.
 * Tests:
 *   Phase A: Pico2 TX → bxCAN RX + MCP2515 RX
 *   Phase B: bxCAN TX  → Pico2 RX + MCP2515 RX
 *   Phase C: MCP2515 TX → Pico2 RX + bxCAN RX
 *
 * Results broadcast as CAN frame ID=0x7FE, DLC=8:
 *   [0] = phase (A/B/C)
 *   [1] = sent count
 *   [2] = rx1 count (bxCAN or Pico2 proxy)
 *   [3] = rx2 count (MCP2515 or bxCAN)
 *   [4] = tx_success
 *   [5-7] = reserved
 */
#include <stdint.h>
#include "device_regs.h"
#include "bxcan.h"
#include "mcp2515.h"

/* Shared counters — readable via SWD */
volatile uint32_t g_phase_a_bxcan_rx  = 0;  /* Pico2→Blue Pill bxCAN */
volatile uint32_t g_phase_a_mcp_rx    = 0;  /* Pico2→Blue Pill MCP2515 */
volatile uint32_t g_phase_b_pico_seen = 0;  /* bxCAN→Pico2 (frames Pico2 ACKed) */
volatile uint32_t g_phase_b_mcp_rx    = 0;  /* bxCAN→MCP2515 */
volatile uint32_t g_phase_c_pico_seen = 0;  /* MCP2515→Pico2 */
volatile uint32_t g_phase_c_bxcan_rx  = 0;  /* MCP2515→bxCAN */
volatile uint32_t g_test_phase        = 0;

static void delay_ms(uint32_t ms) {
    while (ms--) { volatile uint32_t c=2667; while(c--); }
}

void test_3node_init(void) {
    /* Enable LED (PC13 active-low) */
    RCC_APB2ENR |= (1U<<4);
    GPIOC->CRH = (GPIOC->CRH & ~(0xFU<<20)) | (0x3U<<20);
    GPIOC->BSRR = (1U<<13);

    /* Init bxCAN — Standard Mode (osm=0: auto-retry) */
    bxcan_app_init(250000, 0);

    /* Init MCP2515 — Standard Mode (OSM=0) */
    mcp2515_init(MCP_BRATE_250K);
    mcp2515_enter_normal();   /* opens bus */
    delay_ms(10);
}

void test_3node_run(uint32_t frames_per_phase) {
    uint8_t data[8];
    uint32_t id; uint8_t dlc; int ext;

    /* ── Phase A: receive only (Pico2 sends externally) ── */
    g_test_phase = 'A';
    GPIOC->BRR = (1U<<13);  /* LED on */
    delay_ms(100);

    /* Poll both RX for 3 seconds */
    uint32_t end = 0;
    for (uint32_t ms=0; ms<3000; ms++) {
        /* bxCAN RX */
        if (bxcan_app_rx_available()) {
            if (bxcan_app_rx(&id, data, &dlc, &ext))
                if ((id & 0x700) == 0x700) /* frames from Pico2 on 0x7xx */
                    g_phase_a_bxcan_rx++;
        }
        /* MCP2515 RX */
        {
            uint32_t mcp_id; uint8_t mcp_dlc; int mcp_ext;
            if (mcp2515_rx_available()) {
                if (mcp2515_rx(&mcp_id, data, (uint8_t*)&mcp_dlc, &mcp_ext))
                    if ((mcp_id & 0x700) == 0x700)
                        g_phase_a_mcp_rx++;
            }
        }
        volatile uint32_t c=2667; while(c--); /* 1ms */
    }
    GPIOC->BSRR = (1U<<13);

    /* ── Phase B: bxCAN TX, both others receive ── */
    g_test_phase = 'B';
    for (uint32_t i=0; i<frames_per_phase; i++) {
        uint8_t tx[8] = {(uint8_t)i,0xBB,0,0,0,0,0,0};
        bxcan_app_tx(0x111, tx, 8, 0);
        delay_ms(20);
        /* Check MCP2515 RX (bxCAN TX → MCP2515 RX on same bus) */
        {
            uint32_t mid; uint8_t mdlc; int mext;
            if (mcp2515_rx_available())
                if (mcp2515_rx(&mid, data, (uint8_t*)&mdlc, &mext))
                    if (mid == 0x111)
                        g_phase_b_mcp_rx++;
        }
        GPIOC->ODR ^= (1U<<13);  /* toggle LED */
    }

    /* ── Phase C: MCP2515 TX, both others receive ── */
    g_test_phase = 'C';
    for (uint32_t i=0; i<frames_per_phase; i++) {
        uint8_t tx[8] = {(uint8_t)i,0xCC,0,0,0,0,0,0};
        mcp2515_tx(0x222, tx, 8, 0);
        delay_ms(20);
        /* Check bxCAN RX (MCP2515 TX → bxCAN RX) */
        if (bxcan_app_rx_available())
            if (bxcan_app_rx(&id, data, &dlc, &ext))
                if (id == 0x222)
                    g_phase_c_bxcan_rx++;
        GPIOC->ODR ^= (1U<<13);
    }

    g_test_phase = 'D';  /* done */
}

int main(void) {
    test_3node_init();
    delay_ms(500);
    test_3node_run(20);  /* 20 frames per phase */
    while(1) {
        GPIOC->BRR = (1U<<13); delay_ms(200);
        GPIOC->BSRR = (1U<<13); delay_ms(200);
    }
}
