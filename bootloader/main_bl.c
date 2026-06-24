/* main_bl.c — STM32F103 dual-transport bootloader (CAN/MCP2515 + USART1).
 *
 * Features:
 *   - Interactive UART menu (selectable upload address, A/B slot, NVRAM clear)
 *   - Dual-slot A/B boot with persistent NVRAM config (BL_CONFIG_ADDR)
 *   - Selectable upload target: Slot A, Slot B, or custom address
 *   - NVRAM clear (APP_NVRAM_ADDR, 4KB)
 *   - Flash page-size bug fixed: FLASH_HW_PAGE_SIZE=2048 tracked separately
 *     from protocol FLASH_PAGE_SIZE=1024
 *
 * Startup sequence:
 *   1. Disable interrupts.
 *   2. Read bl_config → determine boot_slot.
 *   3. Init MCP2515 + USART1.
 *   4. Broadcast CAN advert (0x7DE).
 *   5. Poll both transports for 500ms:
 *        UART 0xAA  → auto upload to SLOT_A_START (backwards compat)
 *        UART other → uart_menu()
 *        CAN match  → can_upload(SLOT_A_START)
 *        neither    → jump_to_slot(boot_slot)
 */

#include <stdint.h>
#include "device_regs.h"
#include "mcp2515_bl.h"
#include "usart_bl.h"
#include "flash_bl.h"

#define CAN_TX_ID    0x7DEU
#define CAN_RX_ID    0x7DDU
#define UART_TRIGGER 0xAAU

/* ----------------------------------------------------------------------- */
/* Simple delay: 8 MHz HSI, ~1ms per call at -O2                           */
/* ----------------------------------------------------------------------- */
static void delay_ms(uint32_t ms)
{
    while (ms--) {
        volatile uint32_t c = 2667;
        while (c--)
            ;
    }
}

/* ----------------------------------------------------------------------- */
/* CRC32 — polynomial 0x04C11DB7 (MPEG-2, no bit-reflection)               */
/* ----------------------------------------------------------------------- */
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

/* ----------------------------------------------------------------------- */
/* BL config read/write                                                     */
/* ----------------------------------------------------------------------- */
static void bl_config_read(bl_config_t *cfg)
{
    *cfg = *(const volatile bl_config_t *)BL_CONFIG_ADDR;
    if (cfg->magic != BL_CONFIG_MAGIC) {
        cfg->magic     = BL_CONFIG_MAGIC;
        cfg->boot_slot = 0;
    }
}

static void bl_config_write(const bl_config_t *cfg)
{
    flash_unlock();
    flash_erase_page_any(BL_CONFIG_ADDR);
    const uint8_t *p = (const uint8_t *)cfg;
    uint32_t addr = BL_CONFIG_ADDR;
    for (uint32_t i = 0; i < sizeof(bl_config_t); i += 2) {
        uint8_t lo = p[i];
        uint8_t hi = (i + 1 < sizeof(bl_config_t)) ? p[i + 1] : 0xFFU;
        flash_write_halfword(addr, (uint16_t)(lo | ((uint16_t)hi << 8)));
        addr += 2;
    }
    flash_lock();
}

/* ----------------------------------------------------------------------- */
/* Jump to application at arbitrary address                                */
/* ----------------------------------------------------------------------- */
static void jump_to_app_at(uint32_t start)
{
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    uint32_t app_sp = *(volatile uint32_t *)start;
    uint32_t app_pc = *(volatile uint32_t *)(start + 4);

    SCB_VTOR = start;
    __asm volatile("msr msp, %0\nbx %1\n" : : "r"(app_sp), "r"(app_pc));
    __builtin_unreachable();
}

/* Global config — filled at startup */
static bl_config_t g_config;

static void jump_to_slot(uint32_t slot)
{
    uint32_t target = (slot == 1) ? SLOT_B_START : SLOT_A_START;
    jump_to_app_at(target);
}

/* ----------------------------------------------------------------------- */
/* Transport state                                                          */
/* ----------------------------------------------------------------------- */
typedef enum { TRANSPORT_NONE, TRANSPORT_CAN, TRANSPORT_UART } transport_t;
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

static int recv_bytes(uint8_t *buf, uint32_t want, uint32_t timeout_ms)
{
    if (g_transport == TRANSPORT_CAN) {
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

/* ----------------------------------------------------------------------- */
/* Page buffer (BSS — zeroed by startup_bl.s)                              */
/* ----------------------------------------------------------------------- */
static uint8_t page_buf[1024];

/* ----------------------------------------------------------------------- */
/* uart_do_upload — UART page upload to base_addr                          */
/* force=1: use flash_write_page_any (allows bootloader region overwrite)  */
/* ----------------------------------------------------------------------- */
static void uart_do_upload(uint32_t base_addr, int force)
{
    /* Send 'S' to signal ready */
    usart_tx_byte('S');

    /* Receive page count (4 bytes LE) */
    uint8_t pc_buf[4];
    if (!usart_rx_buf(pc_buf, 4, 500)) jump_to_slot(g_config.boot_slot);
    uint32_t n_pages = (uint32_t)pc_buf[0]
                     | ((uint32_t)pc_buf[1] << 8)
                     | ((uint32_t)pc_buf[2] << 16)
                     | ((uint32_t)pc_buf[3] << 24);
    if (n_pages == 0 || n_pages > 246) jump_to_slot(g_config.boot_slot);

    flash_unlock();
    uint32_t flash_addr = base_addr;

    for (uint32_t p = 0; p < n_pages; p++) {
        uint8_t retry = 0;
page_retry:;
        if (!usart_rx_buf(page_buf, 1020, 5000)) {
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }
        uint8_t crc_bytes[4];
        if (!usart_rx_buf(crc_bytes, 4, 2000)) {
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }
        uint32_t master_crc = (uint32_t)crc_bytes[0]
                            | ((uint32_t)crc_bytes[1] << 8)
                            | ((uint32_t)crc_bytes[2] << 16)
                            | ((uint32_t)crc_bytes[3] << 24);

        uint32_t computed_crc = crc32_page(page_buf, 1020);
        if (computed_crc != master_crc) {
            usart_tx_byte('E');
            if (++retry < 8) goto page_retry;
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }

        if (force)
            flash_write_page_any(flash_addr, page_buf, 1020);
        else
            flash_write_page(flash_addr, page_buf, 1020);

        flash_addr += FLASH_PAGE_SIZE;
        usart_tx_byte('P');
    }

    flash_lock();
    usart_tx_byte('D');
    delay_ms(100);
    jump_to_slot(g_config.boot_slot);
}

/* ----------------------------------------------------------------------- */
/* can_upload — CAN page upload to base_addr                               */
/* ----------------------------------------------------------------------- */
static void can_upload(uint32_t base_addr)
{
    send_byte('S');

    uint8_t pc_buf[4];
    if (!recv_bytes(pc_buf, 4, 500)) jump_to_slot(g_config.boot_slot);
    uint32_t n_pages = (uint32_t)pc_buf[0]
                     | ((uint32_t)pc_buf[1] << 8)
                     | ((uint32_t)pc_buf[2] << 16)
                     | ((uint32_t)pc_buf[3] << 24);
    if (n_pages == 0 || n_pages > 246) jump_to_slot(g_config.boot_slot);

    flash_unlock();
    uint32_t flash_addr = base_addr;

    for (uint32_t p = 0; p < n_pages; p++) {
        uint8_t retry = 0;
can_page_retry:;
        uint32_t byte_idx = 0;
        uint32_t master_crc = 0;

        for (uint32_t f = 0; f < 128; f++) {
            uint16_t id;
            uint8_t  tmp[8], rlen;
            uint32_t polls = 2000U * 267U;
            int got = 0;
            while (polls--) {
                if (mcp2515_rx(&id, tmp, &rlen) && id == CAN_RX_ID) { got = 1; break; }
            }
            if (!got) { flash_lock(); jump_to_slot(g_config.boot_slot); }

            if (f == 127) {
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

        uint32_t computed_crc = crc32_page(page_buf, 1020);
        if (computed_crc != master_crc) {
            send_byte('E');
            if (++retry < 8) goto can_page_retry;
            flash_lock();
            jump_to_slot(g_config.boot_slot);
        }

        flash_write_page(flash_addr, page_buf, 1020);
        flash_addr += FLASH_PAGE_SIZE;
        send_byte('P');
    }

    flash_lock();
    send_byte('D');
    delay_ms(100);
    jump_to_slot(g_config.boot_slot);
}

/* ----------------------------------------------------------------------- */
/* uart_menu — interactive UART menu                                       */
/* ----------------------------------------------------------------------- */

/* Read one hex nibble from UART; returns 0..15, or 0xFF on non-hex input */
static uint8_t read_hex_nibble(void)
{
    uint8_t c = usart_rx_byte();
    usart_tx_byte(c);   /* echo */
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    return 0xFF;
}

static void uart_menu(void)
{
    usart_print("\r\n");
    usart_print("===================================\r\n");
    usart_print("  biPropellant CAN Generic BL v1\r\n");
    usart_print("  UID: ");
    usart_print_hex32(DESIG_UNIQUE_ID2);
    usart_print("\r\n");
    if (g_config.boot_slot == 1) {
        usart_print("  Active slot: B (0x08020000)\r\n");
        usart_print("  Next boot:   B\r\n");
    } else {
        usart_print("  Active slot: A (0x08002000)\r\n");
        usart_print("  Next boot:   A\r\n");
    }
    usart_print("===================================\r\n");
    usart_print(" [1] Flash -> Slot A  (primary,   0x08002000)\r\n");
    usart_print(" [2] Flash -> Slot B  (secondary, 0x08020000)\r\n");
    usart_print(" [3] Flash -> custom address\r\n");
    usart_print(" [4] Set next boot: Slot A\r\n");
    usart_print(" [5] Set next boot: Slot B\r\n");
    usart_print(" [6] Clear NVRAM (settings at 0x0803F000)\r\n");
    usart_print(" [0] Boot application\r\n");
    usart_print("===================================\r\n");
    usart_print("Choice [0-6]: ");

    uint8_t ch = usart_rx_byte();
    usart_print("\r\n");

    switch (ch) {
    case '0':
        jump_to_slot(g_config.boot_slot);
        break;

    case '1':
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(SLOT_A_START, 0);
        break;

    case '2':
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(SLOT_B_START, 0);
        usart_print("Set Slot B as next boot? (y/n): ");
        {
            uint8_t yn = usart_rx_byte();
            usart_print("\r\n");
            if (yn == 'y' || yn == 'Y') {
                g_config.boot_slot = 1;
                bl_config_write(&g_config);
                usart_print("Next boot: Slot B\r\n");
            }
        }
        jump_to_slot(g_config.boot_slot);
        break;

    case '3': {
        usart_print("Enter 8-digit hex address: ");
        uint32_t addr = 0;
        int valid = 1;
        for (int i = 0; i < 8; i++) {
            uint8_t nib = read_hex_nibble();
            if (nib == 0xFF) { valid = 0; break; }
            addr = (addr << 4) | nib;
        }
        usart_print("\r\n");
        if (!valid || addr == 0) {
            usart_print("Invalid address.\r\n");
            jump_to_slot(g_config.boot_slot);
        }
        int force = 0;
        if (addr < APP_FLASH_START) {
            usart_print("!!! BOOTLOADER REGION - overwrite bootloader? (YES/no): ");
            /* Expect "YES" followed by CR */
            uint8_t c0 = usart_rx_byte();
            uint8_t c1 = usart_rx_byte();
            uint8_t c2 = usart_rx_byte();
            uint8_t c3 = usart_rx_byte();   /* CR or \n */
            (void)c3;
            usart_print("\r\n");
            if (!((c0 == 'Y' || c0 == 'y') &&
                  (c1 == 'E' || c1 == 'e') &&
                  (c2 == 'S' || c2 == 's'))) {
                usart_print("Cancelled.\r\n");
                jump_to_slot(g_config.boot_slot);
            }
            force = 1;
        }
        usart_print("Ready. Send 0xAA to begin...\r\n");
        while (usart_rx_byte() != UART_TRIGGER)
            ;
        uart_do_upload(addr, force);
        break;
    }

    case '4':
        g_config.boot_slot = 0;
        bl_config_write(&g_config);
        usart_print("Next boot: Slot A\r\n");
        jump_to_slot(0);
        break;

    case '5':
        g_config.boot_slot = 1;
        bl_config_write(&g_config);
        usart_print("Next boot: Slot B\r\n");
        jump_to_slot(1);
        break;

    case '6':
        usart_print("Erasing NVRAM at 0x0803F000 (4KB)...\r\n");
        flash_unlock();
        flash_erase_region(APP_NVRAM_ADDR, APP_NVRAM_ADDR + 4096U - 1U);
        flash_lock();
        usart_print("Done. Boot and settings will reset to defaults.\r\nBooting...\r\n");
        delay_ms(500);
        jump_to_slot(g_config.boot_slot);
        break;

    default:
        usart_print("Invalid.\r\n");
        jump_to_slot(g_config.boot_slot);
        break;
    }
}

/* ----------------------------------------------------------------------- */
/* main — bootloader entry                                                  */
/* ----------------------------------------------------------------------- */
int main(void)
{
    /* 1. Disable all interrupts immediately */
    __asm volatile("cpsid i");
    NVIC_ICER0 = 0xFFFFFFFFUL;
    NVIC_ICER1 = 0xFFFFFFFFUL;
    NVIC_ICER2 = 0xFFFFFFFFUL;

    /* 2. Read BL config; determine boot_slot */
    bl_config_read(&g_config);

    /* 3. Init both transports */
    mcp2515_init();
    usart_init();

    /* 4. Broadcast device identity on CAN (0x7DE) */
    {
        uint8_t advert[8];
        advert[0] = '3'; advert[1] = '1';
        uint32_t uid0 = DESIG_UNIQUE_ID0;
        advert[2] = (uint8_t)(uid0);
        advert[3] = (uint8_t)(uid0 >> 8);
        advert[4] = (uint8_t)(uid0 >> 16);
        advert[5] = (uint8_t)(uid0 >> 24);
        advert[6] = 0x00; advert[7] = 0x00;
        mcp2515_tx(CAN_TX_ID, advert, 8);
    }

    /* 5. Poll both transports for 500 ms; first response wins */
    g_transport = TRANSPORT_NONE;
    uint8_t rx_buf[8];
    uint8_t rx_len;
    uint8_t uart_byte = 0;

    for (uint32_t ms = 0; ms < 500 && g_transport == TRANSPORT_NONE; ms++) {
        /* Check UART first */
        if (usart_rx_ready()) {
            uart_byte = usart_rx_byte();
            if (uart_byte == UART_TRIGGER) {
                g_transport = TRANSPORT_UART;
            } else {
                /* Any other byte → interactive menu */
                g_transport = TRANSPORT_UART;
                uart_menu();  /* does not return */
            }
            break;
        }

        /* Check CAN: matching 0x7DD frame with DESIG_UNIQUE_ID2 */
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

        /* ~1ms delay */
        volatile uint32_t c = 2667;
        while (c--)
            ;
    }

    if (g_transport == TRANSPORT_NONE) {
        /* No master arrived — boot based on config */
        jump_to_slot(g_config.boot_slot);
    }

    if (g_transport == TRANSPORT_CAN) {
        can_upload(SLOT_A_START);
    } else {
        /* UART 0xAA backwards-compat path: go straight to upload on Slot A */
        uart_do_upload(SLOT_A_START, 0);
    }

    return 0; /* unreachable */
}
