#include "mcp2515.h"
#include "software_spi.h"
#include <string.h>
#include <stdio.h>

// Private function prototypes
static void MCP2515_Select(void);
static void MCP2515_Deselect(void);
static uint8_t MCP2515_SPITransfer(uint8_t data);

// MCP2515 Initialization and Detection
bool MCP2515_Detect(void) {
    // Initialize software SPI
    SoftSPI_Init();
    SoftSPI_SetMode(SPI_MODE0);
    
    // Small delay for MCP2515 to power up
    HAL_Delay(10);
    
    // Try to read CANSTAT register (should be 0x80 after reset - CONFIG mode)
    uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    
    if ((canstat & 0xE0) == 0x80) {
        // CANSTAT indicates CONFIG mode - MCP2515 detected
        return true;
    }
    
    // Try resetting the chip
    MCP2515_Reset();
    HAL_Delay(10);
    
    // Check again
    canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    if ((canstat & 0xE0) == 0x80) {
        return true;
    }
    
    // Try read/write test to verify communication
    MCP2515_WriteRegister(MCP2515_CNF1, 0xAA);
    uint8_t test = MCP2515_ReadRegister(MCP2515_CNF1);
    
    if (test == 0xAA) {
        MCP2515_WriteRegister(MCP2515_CNF1, 0x55);
        test = MCP2515_ReadRegister(MCP2515_CNF1);
        if (test == 0x55) {
            return true;
        }
    }
    
    // No MCP2515 detected - deinitialize SPI
    SoftSPI_Deinit();
    return false;
}

bool MCP2515_Init(uint8_t speed) {
    // Reset MCP2515
    MCP2515_Reset();
    HAL_Delay(10);

    // Set configuration mode
    if (!MCP2515_SetMode(MCP2515_MODE_CONFIG)) {
        return false;
    }

    // Configure bit timing
    if (!MCP2515_SetBitrate(speed)) {
        return false;
    }

    // Configure RX buffers - receive all messages.
    // RXB0CTRL: RXM=11 (accept all) | BUKT=1 (roll overflow into RXB1).
    // Without BUKT=1 a second frame arriving while RXB0 is full is DROPPED
    // (sets RX0OVR) instead of rolling over — silently loses CAN frames.
    MCP2515_WriteRegister(MCP2515_RXB0CTRL, 0x64);  // 0x60 | BUKT(bit2)
    MCP2515_WriteRegister(MCP2515_RXB1CTRL, 0x60);
    MCP2515_WriteRegister(MCP2515_CANINTF, 0x00);
    MCP2515_WriteRegister(MCP2515_CANINTE, 0x03);

    // Set normal mode
    if (!MCP2515_SetMode(MCP2515_MODE_NORMAL)) {
        return false;
    }

    return true;
}

void MCP2515_Reset(void) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_RESET);
    MCP2515_Deselect();
    HAL_Delay(10);
}

bool MCP2515_SetMode(uint8_t mode) {
    MCP2515_ModifyRegister(MCP2515_CANCTRL, 0xE0, mode);
    
    // Verify mode change
    uint32_t timeout = HAL_GetTick() + 100;
    while (HAL_GetTick() < timeout) {
        uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
        if ((canstat & 0xE0) == mode) {
            return true;
        }
        HAL_Delay(1);
    }
    
    return false;
}

bool MCP2515_SetBitrate(uint8_t speed) {
    // Bitrate configuration for 8 MHz crystal
    // CNF1: SJW=1, BRP
    // CNF2: BTLMODE=1, SAM=0, PHSEG1, PRSEG
    // CNF3: SOF=0, WAKFIL=0, PHSEG2
    
    uint8_t cnf1, cnf2, cnf3;
    
    switch (speed) {
        case MCP2515_SPEED_125KBPS:
            cnf1 = 0x01;  // BRP=1, SJW=1
            cnf2 = 0xB1;  // BTLMODE=1, PHSEG1=3, PRSEG=1
            cnf3 = 0x85;  // PHSEG2=5
            break;
            
        case MCP2515_SPEED_250KBPS:
            cnf1 = 0x00;  // BRP=0, SJW=1
            cnf2 = 0xB1;  // BTLMODE=1, PHSEG1=3, PRSEG=1
            cnf3 = 0x85;  // PHSEG2=5
            break;
            
        case MCP2515_SPEED_500KBPS:
            cnf1 = 0x00;  // BRP=0, SJW=1
            cnf2 = 0x90;  // BTLMODE=1, PHSEG1=2, PRSEG=0
            cnf3 = 0x82;  // PHSEG2=2
            break;
            
        case MCP2515_SPEED_1MBPS:
            cnf1 = 0x00;  // BRP=0, SJW=1
            cnf2 = 0x80;  // BTLMODE=1, PHSEG1=1, PRSEG=0
            cnf3 = 0x80;  // PHSEG2=1
            break;
            
        default:
            return false;
    }
    
    MCP2515_WriteRegister(MCP2515_CNF1, cnf1);
    MCP2515_WriteRegister(MCP2515_CNF2, cnf2);
    MCP2515_WriteRegister(MCP2515_CNF3, cnf3);
    
    return true;
}

bool MCP2515_SendFrame(CAN_Frame *frame) {
    // Rotate through TXB0/TXB1/TXB2 so three rapid sends (speed + pos + batt)
    // never silently drop frames due to a still-pending buffer.
    // TXBnCTRL register addresses: TXB0=0x30, TXB1=0x40, TXB2=0x50.
    // TXBnSIDH load addresses:     TXB0=0x31, TXB1=0x41, TXB2=0x51.
    // RTS bitmask:                 TXB0=0x01, TXB1=0x02, TXB2=0x04.
    static uint8_t tx_slot = 0;
    static const uint8_t ctrl_addr[3] = { 0x30, 0x40, 0x50 };
    static const uint8_t sidh_addr[3] = { 0x31, 0x41, 0x51 };
    static const uint8_t rts_mask[3]  = { 0x01, 0x02, 0x04 };

    // Find a free TX buffer (TXREQ = bit2 = 0x04 per DS21801J §2.3).
    // WARNING: bit3 = TXERR (error flag), NOT TXREQ — using 0x08 would
    // treat error-flagged buffers as busy while missing genuinely busy ones.
    uint8_t slot = tx_slot;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        uint8_t ctrl = MCP2515_ReadRegister(ctrl_addr[slot]);
        if (!(ctrl & 0x04)) {   // TXREQ clear — buffer free
            // Also clear any previous TXERR/ABTF to allow reuse
            if (ctrl & 0x18) {
                MCP2515_ModifyRegister(ctrl_addr[slot], 0x18, 0x00);
            }
            break;
        }
        slot = (slot + 1) % 3;
        if (attempt == 2) {
            return false;   // All three buffers busy
        }
    }
    tx_slot = (slot + 1) % 3;  // Advance for next call

    // Build frame header
    uint8_t txbuf[13];
    uint8_t idx = 0;

    if (frame->extended) {
        txbuf[idx++] = (uint8_t)(frame->id >> 21);
        txbuf[idx++] = (uint8_t)(((frame->id >> 13) & 0xE0) |
                                  0x08 |                        // EXIDE bit
                                  ((frame->id >> 16) & 0x03));
        txbuf[idx++] = (uint8_t)(frame->id >> 8);
        txbuf[idx++] = (uint8_t)(frame->id);
    } else {
        txbuf[idx++] = (uint8_t)(frame->id >> 3);
        txbuf[idx++] = (uint8_t)(frame->id << 5);
        txbuf[idx++] = 0x00;
        txbuf[idx++] = 0x00;
    }
    txbuf[idx++] = frame->rtr ? (0x40 | frame->dlc) : frame->dlc;
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
        txbuf[idx++] = frame->data[i];
    }

    // Load TX buffer
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_WRITE);
    MCP2515_SPITransfer(sidh_addr[slot]);
    for (uint8_t i = 0; i < idx; i++) {
        MCP2515_SPITransfer(txbuf[i]);
    }
    MCP2515_Deselect();

    // Request to send
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_RTS | rts_mask[slot]);
    MCP2515_Deselect();

    // Post-RTS: check for immediate TXERR (bus-off or arbitration loss).
    // With OSM not set, MCP2515 auto-retries; poll is just a fast-fail
    // safety net after ~1ms so callers get an error code, not a silent hang.
    {
        volatile uint16_t timeout = 2000;
        while (timeout--) {
            uint8_t ctrl = MCP2515_ReadRegister(ctrl_addr[slot]);
            if (!(ctrl & 0x04)) break;         // TXREQ cleared — sent OK
            if (ctrl & 0x10) {                 // TXERR set — abort
                MCP2515_ModifyRegister(ctrl_addr[slot], 0x08, 0x00); // clear TXREQ
                return false;
            }
        }
    }

    return true;
}

bool MCP2515_ReceiveFrame(CAN_Frame *frame) {
    // Check RX status
    uint8_t status = MCP2515_GetRxStatus();

    if (!(status & 0xC0)) {
        // No message available
        return false;
    }

    uint8_t rxbuf[13];

    // Use "Read RX Buffer" instruction per DS21801J Table 12-2:
    //   0x90 = Read RXB0 from SIDH (RXB0 full, bit6 of RX_STATUS set)
    //   0x96 = Read RXB1 from SIDH (RXB1 full, bit7 set, bit6 clear)
    // Bug was 0x94 (Read RXB0 from data byte 0 — skips header, returns
    // garbage for RXB1 and does NOT auto-clear the RXB1 interrupt flag).
    uint8_t read_cmd = (status & 0x40) ? 0x90 : 0x96;

    MCP2515_Select();
    MCP2515_SPITransfer(read_cmd);
    for (uint8_t i = 0; i < 13; i++) {
        rxbuf[i] = MCP2515_SPITransfer(0xFF);
    }
    MCP2515_Deselect();


    // Parse frame
    if (rxbuf[1] & 0x08) {
        // Extended frame
        frame->extended = true;
        frame->id = ((uint32_t)rxbuf[0] << 21) |
                    ((uint32_t)(rxbuf[1] & 0xE0) << 13) |
                    ((uint32_t)(rxbuf[1] & 0x03) << 16) |
                    ((uint32_t)rxbuf[2] << 8) |
                    rxbuf[3];
    } else {
        // Standard frame
        frame->extended = false;
        frame->id = ((uint32_t)rxbuf[0] << 3) |
                    (rxbuf[1] >> 5);
    }
    
    frame->rtr = (rxbuf[4] & 0x40) ? true : false;
    frame->dlc = rxbuf[4] & 0x0F;
    
    // Copy data
    for (uint8_t i = 0; i < frame->dlc && i < 8; i++) {
        frame->data[i] = rxbuf[5 + i];
    }
    
    // Clear interrupt flag
    if (status & 0x40) {
        MCP2515_ModifyRegister(MCP2515_CANINTF, 0x01, 0x00);  // Clear RXB0 flag
    } else {
        MCP2515_ModifyRegister(MCP2515_CANINTF, 0x02, 0x00);  // Clear RXB1 flag
    }
    
    return true;
}

bool MCP2515_Available(void) {
    uint8_t status = MCP2515_GetRxStatus();
    return (status & 0xC0) != 0;
}

uint8_t MCP2515_ReadRegister(uint8_t address) {
    uint8_t result;
    
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_READ);
    MCP2515_SPITransfer(address);
    result = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();
    
    return result;
}

void MCP2515_WriteRegister(uint8_t address, uint8_t value) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_WRITE);
    MCP2515_SPITransfer(address);
    MCP2515_SPITransfer(value);
    MCP2515_Deselect();
}

void MCP2515_ModifyRegister(uint8_t address, uint8_t mask, uint8_t value) {
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_BIT_MODIFY);
    MCP2515_SPITransfer(address);
    MCP2515_SPITransfer(mask);
    MCP2515_SPITransfer(value);
    MCP2515_Deselect();
}

uint8_t MCP2515_GetStatus(void) {
    uint8_t status;
    
    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_READ_STATUS);
    status = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();
    
    return status;
}

uint8_t MCP2515_GetRxStatus(void) {
    uint8_t status;

    MCP2515_Select();
    MCP2515_SPITransfer(MCP2515_CMD_RX_STATUS);
    status = MCP2515_SPITransfer(0xFF);
    MCP2515_Deselect();

    return status;
}

uint8_t MCP2515_GetErrorFlags(void) {
    return MCP2515_ReadRegister(MCP2515_EFLG);
}

// Loopback test - call AFTER software serial is initialized
void MCP2515_LoopbackTest(void) {
    extern void forceLog(char *message);
    char msg[100];

    forceLog("\r\n=== LOOPBACK TEST ===\r\n");

    // Save current mode
    uint8_t saved_canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
    sprintf(msg, "Current CANSTAT: 0x%02X\r\n", saved_canstat);
    forceLog(msg);

    // Enter config mode first
    if (!MCP2515_SetMode(MCP2515_MODE_CONFIG)) {
        forceLog("Failed to enter config mode!\r\n");
        return;
    }

    // Clear any pending RX
    MCP2515_WriteRegister(MCP2515_CANINTF, 0x00);

    // Enter loopback mode
    if (!MCP2515_SetMode(MCP2515_MODE_LOOPBACK)) {
        forceLog("Failed to enter loopback mode!\r\n");
        MCP2515_SetMode(MCP2515_MODE_NORMAL);
        return;
    }
    forceLog("Loopback mode set, sending test frame...\r\n");

    // Send a test frame (will loop back to RX)
    CAN_Frame test_frame;
    test_frame.id = 0x123;
    test_frame.extended = false;
    test_frame.rtr = false;
    test_frame.dlc = 4;
    test_frame.data[0] = 0xDE;
    test_frame.data[1] = 0xAD;
    test_frame.data[2] = 0xBE;
    test_frame.data[3] = 0xEF;

    if (MCP2515_SendFrame(&test_frame)) {
        forceLog("Test frame sent, waiting for loopback...\r\n");
        HAL_Delay(50);  // Wait for loopback

        // Check if received
        uint8_t rxstat = MCP2515_GetRxStatus();
        sprintf(msg, "RX status after loopback: 0x%02X\r\n", rxstat);
        forceLog(msg);

        if (rxstat & 0xC0) {
            // Read the RX buffer manually
            uint8_t rxbuf[13];
            MCP2515_Select();
            MCP2515_SPITransfer(MCP2515_CMD_READ);
            MCP2515_SPITransfer(0x61);  // RXB0SIDH
            for (int i = 0; i < 13; i++) {
                rxbuf[i] = MCP2515_SPITransfer(0xFF);
            }
            MCP2515_Deselect();

            sprintf(msg, "Loopback RX: %02X %02X %02X %02X %02X | %02X %02X %02X %02X\r\n",
                    rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4],
                    rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8]);
            forceLog(msg);

            // Expected for ID=0x123: SIDH=0x24, SIDL=0x60, data at [5-8]
            if (rxbuf[0] == 0x24 && rxbuf[1] == 0x60 &&
                rxbuf[5] == 0xDE && rxbuf[6] == 0xAD) {
                forceLog("*** LOOPBACK SUCCESS! ***\r\n");
                forceLog("SPI RX buffer reads work correctly.\r\n");
                forceLog("Problem must be CAN bus reception.\r\n");
            } else {
                forceLog("*** LOOPBACK FAILED - DATA MISMATCH! ***\r\n");
                sprintf(msg, "Expected: 24 60 xx xx 04 DE AD BE EF\r\n");
                forceLog(msg);
                forceLog("SPI RX buffer reads are corrupted.\r\n");
            }

            // Clear RX flag
            MCP2515_ModifyRegister(MCP2515_CANINTF, 0x01, 0x00);
        } else {
            forceLog("No loopback message received!\r\n");
        }
    } else {
        forceLog("Failed to send test frame!\r\n");
    }

    forceLog("=== END LOOPBACK TEST ===\r\n");

    // Return to normal mode
    MCP2515_SetMode(MCP2515_MODE_NORMAL);
    forceLog("Returned to normal mode.\r\n\r\n");
}

// Private functions
static void MCP2515_Select(void) {
    SoftSPI_CS_Low();
}

static void MCP2515_Deselect(void) {
    SoftSPI_CS_High();
}

static uint8_t MCP2515_SPITransfer(uint8_t data) {
    return SoftSPI_Transfer(data);
}
