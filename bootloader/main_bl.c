/* main_bl.c — STM32F103 dual-transport bootloader (CAN/MCP2515 + USART1).
 *
 * Two transports in the same 500 ms startup window — first responder wins:
 *   CAN  (via MCP2515 software-SPI): compatible with can-updater.py
 *        TX ID=0x7DE, RX ID=0x7DD
 *   UART (USART1 PA9/PA10, 115200): compatible with uart-updater.py
 *        Trigger byte = 0xAA
 *
 * Both use the same page-based protocol:
 *   ACK 'S' → page count (4B LE) → N × [1024B page + CRC32] → 'P'/'E' → 'D'
 *
 * Bootloader lives at 0x08000000..0x08001FFF (8 KB).
 * Application starts at APP_FLASH_START = 0x08002000.
 *
 * SAFETY:
 *   - Never erases 0x08000000..0x08001FFF (its own code).
 *   - Per-page CRC32 before any flash write — corrupt page → 'E', master resends.
 *   - Up to 8 retries per page; on excess the device locks flash and boots the
 *     existing app rather than bricking itself.
 *   - If no master connects within 500ms, app boots immediately (only ~500ms
 *     delay on every power-on — acceptable for most deployments).
 */

#include <stdint.h>
#include "device_regs.h"
#include "mcp2515_bl.h"
#include "usart_bl.h"
#include "flash_bl.h"

#define CAN_TX_ID    0x7DEU
#define CAN_RX_ID    0x7DDU
#define UART_TRIGGER 0xAAU

/* -----------------------------------------------------------------------
 * Active transport — set in the startup detection phase.
 * ----------------------------------------------------------------------- */
typedef enum { TRANSPORT_NONE, TRANSPORT_CAN, TRANSPORT_UART } transport_t;

/* -----------------------------------------------------------------------
 * Simple delay — 8 MHz HSI, ~1 ms per call at -O2 (~2667 iterations @ 3 cyc)
 * ----------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    while (ms--) {
        volatile uint32_t c = 2667;
        while (c--)
            ;
    }
}

/* -----------------------------------------------------------------------
 * CRC32 — polynomial 0x04C11DB7 (STM32 hardware CRC default / MPEG-2).
 * No bit-reflection (straight MPEG-2) to match stm32-CANBootloader.
 * ----------------------------------------------------------------------- */
static uint32_t crc32_update(uint32_t crc, uint32_t word)
{
    crc ^= word;
    for (int i = 0; i < 32; i++)
        crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
    return crc;
}

static uint32_t crc32_page(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i = 0;
    while (i + 3 < len) {
        uint32_t word = ((uint32_t)data[i])
                      | ((uint32_t)data[i+1] << 8)
                      | ((uint32_t)data[i+2] << 16)
                      | ((uint32_t)data[i+3] << 24);
        crc = crc32_update(crc, __builtin_bswap32(word));
        i += 4;
    }
    if (i < len) {
        uint32_t word = 0, shift = 24;
        while (i < len) { word |= ((uint32_t)data[i++] << shift); shift -= 8; }
        crc = crc32_update(crc, word);
    }
    return crc;
}

/* -----------------------------------------------------------------------
 * Jump to application — disables interrupts, sets VTOR, loads MSP, branches.
 * ----------------------------------------------------------------------- */
static void jump_to_app(void)
{
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    uint32_t app_sp = *(volatile uint32_t *)APP_FLASH_START;
    uint32_t app_pc = *(volatile uint32_t *)(APP_FLASH_START + 4);

    SCB_VTOR = APP_FLASH_START;
    __asm volatile("msr msp, %0\nbx %1\n" : : "r"(app_sp), "r"(app_pc));
    __builtin_unreachable();
}

/* -----------------------------------------------------------------------
 * Transport-agnostic send one byte.
 * ----------------------------------------------------------------------- */
static transport_t g_transport;

static void send_byte(uint8_t b)
{
    if (g_transport == TRANSPORT_CAN) {
        uint8_t buf[1] = { b };
        mcp2515_tx(CAN_TX_ID, buf, 1);
    } else {
        usart_tx_byte(b);
    }
}

/* -----------------------------------------------------------------------
 * Transport-agnostic receive exactly len bytes into buf.
 * Returns 1 on success, 0 on timeout.
 * timeout_ms is per-call (CAN: per-frame; UART: per-byte sequence).
 * ----------------------------------------------------------------------- */
static int recv_bytes(uint8_t *buf, uint32_t want, uint32_t timeout_ms)
{
    if (g_transport == TRANSPORT_CAN) {
        /* CAN frames arrive in 8-byte chunks; reassemble into want bytes. */
        uint32_t got = 0;
        uint32_t polls_per_ms = 267U;
        while (got < want) {
            uint16_t id;
            uint8_t  tmp[8], rx_len;
            uint32_t polls = timeout_ms * polls_per_ms;
            int found = 0;
            while (polls--) {
                if (mcp2515_rx(&id, tmp, &rx_len) && id == CAN_RX_ID) {
                    found = 1;
                    break;
                }
            }
            if (!found) return 0;
            uint8_t take = (rx_len < (uint8_t)(want - got)) ? rx_len : (uint8_t)(want - got);
            for (uint8_t i = 0; i < take; i++)
                buf[got++] = tmp[i];
        }
        return 1;
    } else {
        return usart_rx_buf(buf, want, timeout_ms);
    }
}

/* -----------------------------------------------------------------------
 * Flash page buffer (BSS — zeroed by startup_bl.s).
 * ----------------------------------------------------------------------- */
static uint8_t page_buf[1024];

/* -----------------------------------------------------------------------
 * main — bootloader entry
 * ----------------------------------------------------------------------- */
int main(void)
{
    /* Disable interrupts immediately */
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    /* Init both transports */
    mcp2515_init();
    usart_init();

    /* --- Broadcast device identity on CAN (0x7DE) --- */
    uint8_t advert[8];
    advert[0] = '3'; advert[1] = '1';
    uint32_t uid0 = DESIG_UNIQUE_ID0;
    advert[2] = (uint8_t)(uid0);
    advert[3] = (uint8_t)(uid0 >> 8);
    advert[4] = (uint8_t)(uid0 >> 16);
    advert[5] = (uint8_t)(uid0 >> 24);
    advert[6] = 0x00; advert[7] = 0x00;
    mcp2515_tx(CAN_TX_ID, advert, 8);

    /* --- Poll both transports for 500 ms; first response wins --- */
    g_transport = TRANSPORT_NONE;
    uint8_t rx_buf[8];
    uint8_t rx_len;

    /* Each outer iteration ≈ 1 ms (MCP2515 SPI poll is dominant) */
    for (uint32_t ms = 0; ms < 500 && g_transport == TRANSPORT_NONE; ms++) {
        /* Check UART first: one byte latency is very short */
        if (usart_rx_ready()) {
            uint8_t b = usart_rx_byte();
            if (b == UART_TRIGGER) {
                g_transport = TRANSPORT_UART;
                break;
            }
        }

        /* Check CAN: a matching 0x7DD frame with DESIG_UNIQUE_ID2 */
        uint16_t can_id;
        if (mcp2515_rx(&can_id, rx_buf, &rx_len)) {
            if (can_id == CAN_RX_ID && rx_len >= 4) {
                uint32_t master_uid2 = (uint32_t)rx_buf[0]
                                     | ((uint32_t)rx_buf[1] << 8)
                                     | ((uint32_t)rx_buf[2] << 16)
                                     | ((uint32_t)rx_buf[3] << 24);
                if (master_uid2 == DESIG_UNIQUE_ID2) {
                    g_transport = TRANSPORT_CAN;
                    break;
                }
            }
        }

        /* Approx 1 ms delay (coarse — actual precision not critical here) */
        volatile uint32_t c = 2667;
        while (c--)
            ;
    }

    if (g_transport == TRANSPORT_NONE) {
        /* No master arrived — boot the app */
        jump_to_app();
    }

    /* --- ACK: tell master we're ready --- */
    send_byte('S');

    /* --- Receive page count (4 bytes LE) --- */
    uint8_t pc_buf[4];
    if (!recv_bytes(pc_buf, 4, 500)) jump_to_app();
    uint32_t n_pages = (uint32_t)pc_buf[0]
                     | ((uint32_t)pc_buf[1] << 8)
                     | ((uint32_t)pc_buf[2] << 16)
                     | ((uint32_t)pc_buf[3] << 24);
    if (n_pages == 0 || n_pages > 246) jump_to_app(); /* max 246×1KB = 246KB < 250KB app */

    /* --- Receive and flash pages --- */
    flash_unlock();
    uint32_t flash_addr = APP_FLASH_START;

    for (uint32_t p = 0; p < n_pages; p++) {
        uint8_t retry = 0;
page_retry:;
        /* Receive page data.
         * CAN: 128 frames of 8 bytes; last frame bytes[4..7] = CRC32.
         * UART: 1020 data bytes then 4 CRC bytes (1024 total).
         */
        uint32_t master_crc = 0;

        if (g_transport == TRANSPORT_CAN) {
            uint32_t byte_idx = 0;
            for (uint32_t f = 0; f < 128; f++) {
                uint16_t id;
                uint8_t  tmp[8], rlen;
                /* 2-second frame timeout */
                uint32_t polls = 2000U * 267U;
                int got = 0;
                while (polls--) {
                    if (mcp2515_rx(&id, tmp, &rlen) && id == CAN_RX_ID) { got = 1; break; }
                }
                if (!got) { flash_lock(); jump_to_app(); }

                if (f == 127) {
                    /* Last frame: bytes 0-3 data, bytes 4-7 CRC */
                    for (uint8_t b = 0; b < 4 && byte_idx < 1024; b++)
                        page_buf[byte_idx++] = tmp[b];
                    master_crc = (uint32_t)tmp[4]
                               | ((uint32_t)tmp[5] << 8)
                               | ((uint32_t)tmp[6] << 16)
                               | ((uint32_t)tmp[7] << 24);
                } else {
                    uint8_t take = (rlen < (uint8_t)(1024 - byte_idx)) ? rlen : (uint8_t)(1024 - byte_idx);
                    for (uint8_t b = 0; b < take; b++)
                        page_buf[byte_idx++] = tmp[b];
                }
            }
        } else {
            /* UART: receive 1024 bytes = 1020 data + 4 CRC (same as CAN page).
             * uart-updater.py sends pages exactly like the CAN protocol:
             * 1020 bytes of firmware data followed by 4 bytes CRC32. */
            if (!usart_rx_buf(page_buf, 1020, 5000)) { flash_lock(); jump_to_app(); }
            uint8_t crc_bytes[4];
            if (!usart_rx_buf(crc_bytes, 4, 2000)) { flash_lock(); jump_to_app(); }
            master_crc = (uint32_t)crc_bytes[0]
                       | ((uint32_t)crc_bytes[1] << 8)
                       | ((uint32_t)crc_bytes[2] << 16)
                       | ((uint32_t)crc_bytes[3] << 24);
        }

        /* CRC check over the 1020 data bytes of this page.
         * (Both CAN and UART paths fill page_buf[0..1019] identically.) */
        uint32_t computed_crc = crc32_page(page_buf, 1020);
        if (computed_crc != master_crc) {
            send_byte('E');
            if (++retry < 8) goto page_retry;
            flash_lock();
            jump_to_app(); /* too many retries — preserve existing app */
        }

        /* Write to flash */
        flash_write_page(flash_addr, page_buf, 1020);
        flash_addr += FLASH_PAGE_SIZE;

        send_byte('P');
    }

    flash_lock();
    send_byte('D');
    delay_ms(100);
    jump_to_app();
    return 0; /* unreachable */
}
