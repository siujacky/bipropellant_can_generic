#include "can_bus.h"
#include "config.h"
#include "comms.h"

// Force output regardless of debug_out flag (RAW output, no protocol framing)
void forceLog(char *message) {
    consoleLogRaw(message);
}

#ifdef ENABLE_CAN_BUS

#include "mcp2515.h"
#include "setup.h"
#include "protocolfunctions.h"
#include "flashcontent.h"
#include "control_structures.h"
#include "hbprotocol/protocol.h"  // For CONTROL_TYPE_* defines
#include <string.h>
#include <stdio.h>

#ifdef SOFTWARE_SERIAL
#include "softwareserial.h"
#endif

// Communication mode flags
volatile bool can_mode_active = false;
volatile bool usart_mode_active = false;
static bool software_serial_ready = false;  // Track if PA13/PA14 is initialized

// Check if Software Serial (PA13/PA14) is initialized
bool CAN_IsSoftwareSerialReady(void) {
    return software_serial_ready;
}

// Board ID configuration (default 0)
#ifndef CAN_BOARD_ID
#define CAN_BOARD_ID 0
#endif

// CAN ID offsets for command types
#define CAN_CMD_PWM_OFFSET       0x00
#define CAN_CMD_SPEED_OFFSET     0x01
#define CAN_CMD_POSITION_OFFSET  0x02
#define CAN_CMD_ENABLE_OFFSET    0x03
#define CAN_STATUS_SPEED_OFFSET  0x00
#define CAN_STATUS_POS_OFFSET    0x01
#define CAN_STATUS_BATT_OFFSET   0x02
#define CAN_STATUS_BOOTUP_OFFSET 0x0F  // Boot announcement message

static uint32_t can_cmd_pwm;
static uint32_t can_cmd_speed;
static uint32_t can_cmd_position;
static uint32_t can_cmd_enable;
static uint32_t can_status_speed;
static uint32_t can_status_pos;
static uint32_t can_status_batt;
static uint32_t can_status_bootup;

// External flash content
extern FLASH_CONTENT FlashContent;

// External USART handles
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern void USART2_IT_init(void);
extern void USART3_IT_init(void);

// CAN receive buffer
static CAN_Frame rx_frame;

// CAN statistics
static struct {
    uint32_t rx_total;
    uint32_t rx_pwm;
    uint32_t rx_speed;
    uint32_t rx_position;
    uint32_t rx_enable;
    uint32_t rx_unknown;
    uint32_t tx_total;
    uint32_t tx_status_speed;
    uint32_t tx_status_pos;
    uint32_t tx_status_batt;
    uint32_t errors;
    uint32_t last_error_flags;
} can_stats = {0};

// Initialize CAN IDs based on flash-stored settings
static void CAN_Init_IDs(void) {
    // Use flash-stored values if available, otherwise use defaults
    uint8_t board_id = FlashContent.CAN_BoardID & 0x0F;  // Limit to 0-15
    uint32_t cmd_base = FlashContent.CAN_BaseID_CMD;
    uint32_t status_base = FlashContent.CAN_BaseID_STATUS;
    
    // Calculate offset based on board ID
    uint32_t offset = board_id * 0x10;
    
    // Command IDs
    can_cmd_pwm = cmd_base + offset + CAN_CMD_PWM_OFFSET;
    can_cmd_speed = cmd_base + offset + CAN_CMD_SPEED_OFFSET;
    can_cmd_position = cmd_base + offset + CAN_CMD_POSITION_OFFSET;
    can_cmd_enable = cmd_base + offset + CAN_CMD_ENABLE_OFFSET;
    
    // Status IDs
    can_status_speed = status_base + offset + CAN_STATUS_SPEED_OFFSET;
    can_status_pos = status_base + offset + CAN_STATUS_POS_OFFSET;
    can_status_batt = status_base + offset + CAN_STATUS_BATT_OFFSET;
    can_status_bootup = status_base + offset + CAN_STATUS_BOOTUP_OFFSET;
}

// Function to detect MCP2515 and initialize CAN bus
bool CAN_AutoDetectAndInit(void) {
#ifdef ENABLE_CAN_BUS
    char msg[128];
    
    forceLog("\r\n=== CAN Bus Initialization ===\r\n");
    forceLog("Detecting MCP2515 module...\r\n");
    
    // Initialize CAN IDs based on board ID
    CAN_Init_IDs();
    
    // Try to detect MCP2515
    if (MCP2515_Detect()) {
        forceLog("MCP2515 DETECTED!\r\n");
        
        // MCP2515 detected - initialize CAN bus
        sprintf(msg, "Initializing CAN at %d kbps...\r\n", 
                CAN_SPEED == MCP2515_SPEED_125KBPS ? 125 :
                CAN_SPEED == MCP2515_SPEED_250KBPS ? 250 :
                CAN_SPEED == MCP2515_SPEED_500KBPS ? 500 : 1000);
        forceLog(msg);
        
        if (MCP2515_Init(CAN_SPEED)) {
            forceLog("CAN bus initialized successfully\r\n");
            can_mode_active = true;
            usart_mode_active = false;
            
            // Reset statistics
            memset(&can_stats, 0, sizeof(can_stats));

            // Now that CAN is working, enable software serial on PA13/PA14
            // This disables SWD but CAN is confirmed working
            #if defined(SOFTWARE_SERIAL) && defined(SOFTWARE_SERIAL_DEFERRED_INIT)
            forceLog("Enabling software serial on PA13/PA14...\r\n");
            SoftwareSerialInit();
            software_serial_ready = true;  // Mark Software Serial as ready
            
            // Test PA13 output - blink it a few times to verify it's working
            for(int i = 0; i < 10; i++) {
                HAL_GPIO_WritePin(SOFTWARE_SERIAL_TX_PORT, SOFTWARE_SERIAL_TX_PIN, 1);
                for(volatile int j = 0; j < 50000; j++);  // ~5ms
                HAL_GPIO_WritePin(SOFTWARE_SERIAL_TX_PORT, SOFTWARE_SERIAL_TX_PIN, 0);
                for(volatile int j = 0; j < 50000; j++);  // ~5ms
            }
            forceLog("PA13 test blink complete\r\n");

            // Run SPI diagnostic NOW (after serial is ready)
            {
                char dmsg[100];
                forceLog("\r\n=== SPI DIAGNOSTIC ===\r\n");

                // Single byte reads
                uint8_t cnf1 = MCP2515_ReadRegister(MCP2515_CNF1);
                uint8_t cnf2 = MCP2515_ReadRegister(MCP2515_CNF2);
                uint8_t cnf3 = MCP2515_ReadRegister(MCP2515_CNF3);
                sprintf(dmsg, "Single: CNF1=0x%02X CNF2=0x%02X CNF3=0x%02X\r\n", cnf1, cnf2, cnf3);
                forceLog(dmsg);

                // Multi-byte read of same registers
                uint8_t multi[3];
                extern void SoftSPI_CS_Low(void);
                extern void SoftSPI_CS_High(void);
                extern uint8_t SoftSPI_Transfer(uint8_t data);
                SoftSPI_CS_Low();
                SoftSPI_Transfer(0x03);  // READ
                SoftSPI_Transfer(0x28);  // CNF3 addr
                multi[0] = SoftSPI_Transfer(0xFF);
                multi[1] = SoftSPI_Transfer(0xFF);
                multi[2] = SoftSPI_Transfer(0xFF);
                SoftSPI_CS_High();
                sprintf(dmsg, "Multi:  CNF3=0x%02X CNF2=0x%02X CNF1=0x%02X\r\n", multi[0], multi[1], multi[2]);
                forceLog(dmsg);

                // Check CANSTAT
                uint8_t canstat = MCP2515_ReadRegister(MCP2515_CANSTAT);
                sprintf(dmsg, "CANSTAT=0x%02X (0x00=normal)\r\n", canstat);
                forceLog(dmsg);
                forceLog("======================\r\n\r\n");
            }
            #endif

            forceLog("CAN controller mode ACTIVE\r\n");
            forceLog("NOTE: All debug messages will now appear automatically.\r\n");
            forceLog("      No need to send 'E' or unlock commands.\r\n\r\n");
            return true;
        } else {
            forceLog("ERROR: CAN initialization failed\r\n");
        }
    } else {
        forceLog("MCP2515 NOT DETECTED\r\n");
    }
    
    // MCP2515 not detected or initialization failed
    can_mode_active = false;
    
    // Fall back to USART mode
    CAN_Fallback_To_USART();
    return false;
#else
    // CAN not enabled - use USART by default
    can_mode_active = false;
    CAN_Fallback_To_USART();
    return false;
#endif
}

// Fall back to USART2/USART3 mode
void CAN_Fallback_To_USART(void) {
    usart_mode_active = true;

#ifdef SERIAL_USART2_IT
    USART2_IT_init();
#endif

#ifdef SERIAL_USART3_IT
    USART3_IT_init();
#endif
}

// Process incoming CAN messages
void CAN_ProcessMessages(void) {
#ifdef ENABLE_CAN_BUS
    if (!can_mode_active) {
        return;
    }
    
    char msg[150];
    
    // Check if CAN message available
    while (MCP2515_Available()) {
        if (MCP2515_ReceiveFrame(&rx_frame)) {
            // Sanity check: DLC must be 0-8, ID must be valid
            // Invalid frames indicate CAN bus corruption
            if (rx_frame.dlc > 8) {
                can_stats.errors++;
                sprintf(msg, "[CAN CORRUPT] DLC=%d (max 8) ID=0x%08X - IGNORING\r\n",
                        rx_frame.dlc, (unsigned int)rx_frame.id);
                forceLog(msg);
                continue;  // Skip this corrupt frame
            }

            // Also filter obviously corrupt extended IDs
            if (rx_frame.id > 0x7FF && !rx_frame.extended) {
                can_stats.errors++;
                sprintf(msg, "[CAN CORRUPT] Std ID=0x%08X > 0x7FF - IGNORING\r\n",
                        (unsigned int)rx_frame.id);
                forceLog(msg);
                continue;
            }

            can_stats.rx_total++;

            // Log all data in a single call to avoid USART FIFO overflow.
            // Per-byte forceLog() at 250kbps (frame every ~0.4ms) caused
            // up to 9 USART transmissions per frame — faster than drain rate.
            {
                uint8_t off = (uint8_t)sprintf(msg, "[CAN RX] ID:0x%03X DLC:%d Data:",
                        (unsigned int)rx_frame.id, rx_frame.dlc);
                for (uint8_t i = 0; i < rx_frame.dlc && i < 8; i++) {
                    off += (uint8_t)sprintf(msg + off, " %02X", rx_frame.data[i]);
                }
                forceLog(msg);
            }
            
            // Process CAN message based on ID (with board offset)
            if (rx_frame.id == can_cmd_pwm) {
                can_stats.rx_pwm++;
                forceLog(" [PWM CMD]");

                // PWM Command Frame
                if (rx_frame.dlc >= 8) {
                    // Parse PWM values using memcpy (little-endian, same as ESP32)
                    int32_t pwm1, pwm2;
                    memcpy(&pwm1, &rx_frame.data[0], 4);
                    memcpy(&pwm2, &rx_frame.data[4], 4);

                    // Sanity check: PWM should be -1000 to 1000
                    if (pwm1 < -1000 || pwm1 > 1000 || pwm2 < -1000 || pwm2 > 1000) {
                        sprintf(msg, "\r\n  !!! PWM OUT OF RANGE: %d, %d (expect -1000 to 1000) - REJECTED !!!\r\n",
                                (int)pwm1, (int)pwm2);
                        forceLog(msg);
                        forceLog("  Check CAN bus wiring, termination, and crystal frequency!\r\n");
                    } else {
                        // Calculate percentage and direction
                        int pct1 = (pwm1 * 100) / 1000;  // -100% to +100%
                        int pct2 = (pwm2 * 100) / 1000;
                        const char* dir1 = (pwm1 > 0) ? "FWD" : (pwm1 < 0) ? "REV" : "STOP";
                        const char* dir2 = (pwm2 > 0) ? "FWD" : (pwm2 < 0) ? "REV" : "STOP";

                        sprintf(msg, "\r\n  >>> MOTOR1: %d%% %s | MOTOR2: %d%% %s <<<\r\n",
                                pct1 < 0 ? -pct1 : pct1, dir1,
                                pct2 < 0 ? -pct2 : pct2, dir2);
                        forceLog(msg);

                        // Set motor PWM values and switch to PWM control mode
                        extern int control_type;
                        extern uint8_t enable;
                        extern volatile uint32_t input_timeout_counter;
                        extern uint32_t inactivity_timeout_counter;

                        PWMData.pwm[0] = pwm1;
                        PWMData.pwm[1] = pwm2;
                        control_type = CONTROL_TYPE_PWM;  // CRITICAL: Enable PWM mode
                        enable = 1;                        // Ensure motors are enabled
                        input_timeout_counter = 0;         // Reset input timeout
                        inactivity_timeout_counter = 0;    // Reset inactivity timeout (prevents beep after 8 min)
                    }
                }
            }
            else if (rx_frame.id == can_cmd_speed) {
                can_stats.rx_speed++;
                forceLog(" [SPEED CMD]");
                
                // Speed Command Frame
                if (rx_frame.dlc >= 8) {
                    // Parse speed values using memcpy (little-endian, same as ESP32)
                    int32_t speed1, speed2;
                    memcpy(&speed1, &rx_frame.data[0], 4);
                    memcpy(&speed2, &rx_frame.data[4], 4);

                    // Bounds check: limit to reasonable speed range (-5000 to 5000 mm/s = 18 km/h)
                    #define MAX_SPEED_MM_S 5000
                    if (speed1 < -MAX_SPEED_MM_S) speed1 = -MAX_SPEED_MM_S;
                    if (speed1 > MAX_SPEED_MM_S) speed1 = MAX_SPEED_MM_S;
                    if (speed2 < -MAX_SPEED_MM_S) speed2 = -MAX_SPEED_MM_S;
                    if (speed2 > MAX_SPEED_MM_S) speed2 = MAX_SPEED_MM_S;

                    sprintf(msg, " SPD1:%d SPD2:%d mm/s", (int)speed1, (int)speed2);
                    forceLog(msg);

                    // Set motor speed values and switch to Speed control mode
                    extern int control_type;
                    extern uint8_t enable;
                    extern volatile uint32_t input_timeout_counter;
                    extern uint32_t inactivity_timeout_counter;

                    SpeedData.wanted_speed_mm_per_sec[0] = speed1;
                    SpeedData.wanted_speed_mm_per_sec[1] = speed2;
                    control_type = CONTROL_TYPE_SPEED;  // Enable Speed mode
                    enable = 1;
                    input_timeout_counter = 0;
                    inactivity_timeout_counter = 0;    // Reset inactivity timeout
                }
            }
            else if (rx_frame.id == can_cmd_position) {
                can_stats.rx_position++;
                forceLog(" [POS CMD]");
                
                // Position Command Frame
                if (rx_frame.dlc >= 8) {
                    // Parse position values using memcpy (little-endian, same as ESP32)
                    int32_t pos1, pos2;
                    memcpy(&pos1, &rx_frame.data[0], 4);
                    memcpy(&pos2, &rx_frame.data[4], 4);

                    sprintf(msg, " POS1:%d POS2:%d mm", (int)pos1, (int)pos2);
                    forceLog(msg);

                    // Set motor position values and switch to Position control mode
                    extern int control_type;
                    extern uint8_t enable;
                    extern volatile uint32_t input_timeout_counter;
                    extern uint32_t inactivity_timeout_counter;

                    PosnData.wanted_posn_mm[0] = pos1;
                    PosnData.wanted_posn_mm[1] = pos2;
                    control_type = CONTROL_TYPE_POSITION;  // Enable Position mode
                    enable = 1;
                    input_timeout_counter = 0;
                    inactivity_timeout_counter = 0;    // Reset inactivity timeout
                }
            }
            else if (rx_frame.id == can_cmd_enable) {
                can_stats.rx_enable++;
                forceLog(" [ENABLE CMD]");

                // Enable/Disable Command
                if (rx_frame.dlc >= 1) {
                    uint8_t en_val = rx_frame.data[0];
                    sprintf(msg, " EN:%d", en_val);
                    forceLog(msg);

                    extern uint8_t enable;
                    extern volatile uint32_t input_timeout_counter;
                    extern uint32_t inactivity_timeout_counter;

                    if (en_val) {
                        enable = 1;
                        input_timeout_counter = 0;
                        inactivity_timeout_counter = 0;    // Reset inactivity timeout
                        forceLog(" -> Motors ENABLED\r\n");
                    } else {
                        enable = 0;
                        PWMData.pwm[0] = 0;
                        PWMData.pwm[1] = 0;
                        SpeedData.wanted_speed_mm_per_sec[0] = 0;
                        SpeedData.wanted_speed_mm_per_sec[1] = 0;
                        forceLog(" -> Motors DISABLED\r\n");
                    }
                }
            } else {
                can_stats.rx_unknown++;
                forceLog(" [UNKNOWN]");
            }
            
            forceLog("\r\n");
        }
    }
#endif
}

// Send status via CAN bus
void CAN_SendStatus(void) {
#ifdef ENABLE_CAN_BUS
    if (!can_mode_active) {
        return;
    }

    // Get current values from actual data structures
    extern volatile HALL_DATA_STRUCT HallData[2];
    extern volatile ELECTRICAL_PARAMS electrical_measurements;
    extern int pwms[2];  // Current motor PWM values

    int32_t speedL = HallData[0].HallSpeed_mm_per_s;
    int32_t speedR = HallData[1].HallSpeed_mm_per_s;
    // Clamp position before packing into CAN frame.
    // HallPosn_mm is a running accumulator; on encoder wrap (e.g. >100km
    // travel) it overflows int32_t and produces a discontinuous jump on
    // the CAN bus that confuses odometry on the receiver side.
    // Clamp to ±100,000,000 mm (±100 km) — practical rover range.
#define POS_CLAMP_MM 100000000L
    int32_t posL = HallData[0].HallPosn_mm;
    int32_t posR = HallData[1].HallPosn_mm;
    if (posL >  POS_CLAMP_MM) posL =  POS_CLAMP_MM;
    if (posL < -POS_CLAMP_MM) posL = -POS_CLAMP_MM;
    if (posR >  POS_CLAMP_MM) posR =  POS_CLAMP_MM;
    if (posR < -POS_CLAMP_MM) posR = -POS_CLAMP_MM;
    int16_t batVoltage = (int16_t)(electrical_measurements.batteryVoltage * 100.0f);  // Convert V to centivolts
    int16_t boardTemp = (int16_t)electrical_measurements.board_temp_deg_c;

    CAN_Frame tx_frame;
    char msg[150];
    static uint8_t verbose_counter = 0;
    static uint8_t motor_status_counter = 0;

    // Status Frame 1: Speed (ID with board offset)
    tx_frame.id = can_status_speed;
    tx_frame.extended = false;
    tx_frame.rtr = false;
    tx_frame.dlc = 8;

    // Pack speed values (int32_t, little endian)
    tx_frame.data[0] = (uint8_t)(speedL & 0xFF);
    tx_frame.data[1] = (uint8_t)((speedL >> 8) & 0xFF);
    tx_frame.data[2] = (uint8_t)((speedL >> 16) & 0xFF);
    tx_frame.data[3] = (uint8_t)((speedL >> 24) & 0xFF);

    tx_frame.data[4] = (uint8_t)(speedR & 0xFF);
    tx_frame.data[5] = (uint8_t)((speedR >> 8) & 0xFF);
    tx_frame.data[6] = (uint8_t)((speedR >> 16) & 0xFF);
    tx_frame.data[7] = (uint8_t)((speedR >> 24) & 0xFF);

    if (MCP2515_SendFrame(&tx_frame)) {
        can_stats.tx_total++;
        can_stats.tx_status_speed++;

        if (verbose_counter == 0) {
            sprintf(msg, "[CAN TX] Speed: L=%d R=%d mm/s\r\n",
                    (int)speedL, (int)speedR);
            forceLog(msg);
        }
    }

    // Status Frame 2: Position (ID with board offset)
    tx_frame.id = can_status_pos;
    tx_frame.data[0] = (uint8_t)(posL & 0xFF);
    tx_frame.data[1] = (uint8_t)((posL >> 8) & 0xFF);
    tx_frame.data[2] = (uint8_t)((posL >> 16) & 0xFF);
    tx_frame.data[3] = (uint8_t)((posL >> 24) & 0xFF);

    tx_frame.data[4] = (uint8_t)(posR & 0xFF);
    tx_frame.data[5] = (uint8_t)((posR >> 8) & 0xFF);
    tx_frame.data[6] = (uint8_t)((posR >> 16) & 0xFF);
    tx_frame.data[7] = (uint8_t)((posR >> 24) & 0xFF);

    if (MCP2515_SendFrame(&tx_frame)) {
        can_stats.tx_total++;
        can_stats.tx_status_pos++;

        if (verbose_counter == 0) {
            sprintf(msg, "[CAN TX] Position: L=%d R=%d mm\r\n",
                    (int)posL, (int)posR);
            forceLog(msg);
        }
    }

    // Status Frame 3: Battery & Temperature (ID with board offset)
    tx_frame.id = can_status_batt;
    tx_frame.dlc = 4;
    tx_frame.data[0] = (uint8_t)(batVoltage & 0xFF);
    tx_frame.data[1] = (uint8_t)((batVoltage >> 8) & 0xFF);
    tx_frame.data[2] = (uint8_t)(boardTemp & 0xFF);
    tx_frame.data[3] = (uint8_t)((boardTemp >> 8) & 0xFF);

    if (MCP2515_SendFrame(&tx_frame)) {
        can_stats.tx_total++;
        can_stats.tx_status_batt++;

        if (verbose_counter == 0) {
            sprintf(msg, "[CAN TX] Battery: %d cV, Temp: %d C\r\n",
                    (int)batVoltage, (int)boardTemp);
            forceLog(msg);
        }
    }

    // Print verbose TX info every 10 cycles (1 second at 100ms rate)
    verbose_counter = (verbose_counter + 1) % 10;

    // Print motor status every 5 cycles (500ms) if motors are active
    motor_status_counter = (motor_status_counter + 1) % 5;
    if (motor_status_counter == 0 && (pwms[0] != 0 || pwms[1] != 0)) {
        int pct1 = (pwms[0] * 100) / 1000;
        int pct2 = (pwms[1] * 100) / 1000;
        const char* dir1 = (pwms[0] > 0) ? "FWD" : (pwms[0] < 0) ? "REV" : "---";
        const char* dir2 = (pwms[1] > 0) ? "FWD" : (pwms[1] < 0) ? "REV" : "---";

        sprintf(msg, "[MOTOR] M1: %3d%% %-3s (%4d) | M2: %3d%% %-3s (%4d) | SPD: L=%d R=%d mm/s\r\n",
                pct1 < 0 ? -pct1 : pct1, dir1, pwms[0],
                pct2 < 0 ? -pct2 : pct2, dir2, pwms[1],
                (int)speedL, (int)speedR);
        forceLog(msg);
    }
#endif
}

// Get current CAN IDs for display/debugging
void CAN_GetCurrentIDs(uint32_t *cmd_ids, uint32_t *status_ids) {
    if (cmd_ids) {
        cmd_ids[0] = can_cmd_pwm;
        cmd_ids[1] = can_cmd_speed;
        cmd_ids[2] = can_cmd_position;
        cmd_ids[3] = can_cmd_enable;
    }
    
    if (status_ids) {
        status_ids[0] = can_status_speed;
        status_ids[1] = can_status_pos;
        status_ids[2] = can_status_batt;
        status_ids[3] = can_status_bootup;
    }
}

// Send boot announcement over CAN bus
void CAN_SendBootAnnouncement(void) {
#ifdef ENABLE_CAN_BUS
    if (!can_mode_active) {
        return;
    }
    
    char msg[100];
    
    CAN_Frame tx_frame;
    tx_frame.id = can_status_bootup;
    tx_frame.extended = false;
    tx_frame.rtr = false;
    tx_frame.dlc = 8;
    
    // Boot message format:
    // [0-1]: Board ID (uint16_t)
    // [2-3]: Reserved for battery voltage (set to 0 at boot)
    // [4-5]: Reserved for temperature (set to 0 at boot)
    // [6]: Firmware version major
    // [7]: Firmware version minor
    
    uint16_t board_id = FlashContent.CAN_BoardID;
    
    tx_frame.data[0] = (uint8_t)(board_id & 0xFF);
    tx_frame.data[1] = (uint8_t)((board_id >> 8) & 0xFF);
    tx_frame.data[2] = 0;  // Battery voltage not available at boot
    tx_frame.data[3] = 0;
    tx_frame.data[4] = 0;  // Temperature not available at boot
    tx_frame.data[5] = 0;
    tx_frame.data[6] = 1;  // Firmware version major
    tx_frame.data[7] = 25; // Firmware version minor

    if (MCP2515_SendFrame(&tx_frame)) {
        can_stats.tx_total++;
        sprintf(msg, "[CAN BOOT] Board ID:%d ONLINE - FW v1.25\r\n", board_id);
        forceLog(msg);
    }
#endif
}

// Print current CAN configuration to console
void CAN_PrintConfiguration(void) {
    char msg[128];
    
    sprintf(msg, "\r\n=== CAN Configuration ===\r\n");
    forceLog(msg);
    
    sprintf(msg, "Board ID: %d\r\n", FlashContent.CAN_BoardID);
    forceLog(msg);
    
    // Print CAN bus speed
    const char* speed_str;
    switch(CAN_SPEED) {
        case MCP2515_SPEED_125KBPS: speed_str = "125 kbps"; break;
        case MCP2515_SPEED_250KBPS: speed_str = "250 kbps"; break;
        case MCP2515_SPEED_500KBPS: speed_str = "500 kbps"; break;
        case MCP2515_SPEED_1MBPS:   speed_str = "1 Mbps"; break;
        default: speed_str = "Unknown"; break;
    }
    sprintf(msg, "CAN Speed: %s\r\n", speed_str);
    forceLog(msg);
    
    sprintf(msg, "Base CMD ID: 0x%03X\r\n", (unsigned int)FlashContent.CAN_BaseID_CMD);
    forceLog(msg);
    
    sprintf(msg, "Base STATUS ID: 0x%03X\r\n", (unsigned int)FlashContent.CAN_BaseID_STATUS);
    forceLog(msg);
    
    sprintf(msg, "\r\nCommand IDs:\r\n");
    forceLog(msg);
    sprintf(msg, "  PWM:      0x%03X\r\n", (unsigned int)can_cmd_pwm);
    forceLog(msg);
    sprintf(msg, "  Speed:    0x%03X\r\n", (unsigned int)can_cmd_speed);
    forceLog(msg);
    sprintf(msg, "  Position: 0x%03X\r\n", (unsigned int)can_cmd_position);
    forceLog(msg);
    sprintf(msg, "  Enable:   0x%03X\r\n", (unsigned int)can_cmd_enable);
    forceLog(msg);
    
    sprintf(msg, "\r\nStatus IDs:\r\n");
    forceLog(msg);
    sprintf(msg, "  Speed:    0x%03X\r\n", (unsigned int)can_status_speed);
    forceLog(msg);
    sprintf(msg, "  Position: 0x%03X\r\n", (unsigned int)can_status_pos);
    forceLog(msg);
    sprintf(msg, "  Battery:  0x%03X\r\n", (unsigned int)can_status_batt);
    forceLog(msg);
    sprintf(msg, "  Boot:     0x%03X\r\n", (unsigned int)can_status_bootup);
    forceLog(msg);
    
    sprintf(msg, "========================\r\n\r\n");
    forceLog(msg);
}

// Print CAN statistics and diagnostics
void CAN_PrintStatistics(void) {
    char msg[128];
    
    sprintf(msg, "\r\n=== CAN Statistics ===\r\n");
    forceLog(msg);
    
    sprintf(msg, "RX Total:     %lu\r\n", can_stats.rx_total);
    forceLog(msg);
    sprintf(msg, "  PWM:        %lu\r\n", can_stats.rx_pwm);
    forceLog(msg);
    sprintf(msg, "  Speed:      %lu\r\n", can_stats.rx_speed);
    forceLog(msg);
    sprintf(msg, "  Position:   %lu\r\n", can_stats.rx_position);
    forceLog(msg);
    sprintf(msg, "  Enable:     %lu\r\n", can_stats.rx_enable);
    forceLog(msg);
    sprintf(msg, "  Unknown:    %lu\r\n", can_stats.rx_unknown);
    forceLog(msg);
    
    sprintf(msg, "\r\nTX Total:     %lu\r\n", can_stats.tx_total);
    forceLog(msg);
    sprintf(msg, "  Speed:      %lu\r\n", can_stats.tx_status_speed);
    forceLog(msg);
    sprintf(msg, "  Position:   %lu\r\n", can_stats.tx_status_pos);
    forceLog(msg);
    sprintf(msg, "  Battery:    %lu\r\n", can_stats.tx_status_batt);
    forceLog(msg);
    
    sprintf(msg, "\r\nErrors:       %lu\r\n", can_stats.errors);
    forceLog(msg);
    
    if (can_stats.last_error_flags) {
        sprintf(msg, "Last Error Flags: 0x%02lX\r\n", (unsigned long)can_stats.last_error_flags);
        forceLog(msg);
    }

    // Verify SPI is still working by reading CNF registers
    uint8_t cnf1 = MCP2515_ReadRegister(MCP2515_CNF1);
    uint8_t cnf2 = MCP2515_ReadRegister(MCP2515_CNF2);
    uint8_t cnf3 = MCP2515_ReadRegister(MCP2515_CNF3);
    // Expected CNF values depend on CAN_SPEED (8MHz crystal).
    // Old check hardcoded 500kbps values (0x00,0x90,0x82) — always
    // reported MISMATCH when deployed at 250kbps.
    uint8_t exp1, exp2, exp3;
    switch (CAN_SPEED) {
        case MCP2515_SPEED_125KBPS: exp1=0x01; exp2=0x9E; exp3=0x03; break;
        case MCP2515_SPEED_250KBPS: exp1=0x00; exp2=0x9E; exp3=0x03; break;
        case MCP2515_SPEED_500KBPS: exp1=0x00; exp2=0x90; exp3=0x02; break;
        case MCP2515_SPEED_1MBPS:   exp1=0x00; exp2=0x80; exp3=0x00; break;
        default:                    exp1=0xFF; exp2=0xFF; exp3=0xFF; break;
    }
    const char *cnf_ok = (cnf1==exp1 && cnf2==exp2 && cnf3==exp3) ? "OK" : "MISMATCH!";
    sprintf(msg, "SPI Check: CNF1=0x%02X CNF2=0x%02X CNF3=0x%02X %s\r\n",
            cnf1, cnf2, cnf3, cnf_ok);
    forceLog(msg);

    sprintf(msg, "======================\r\n");
    forceLog(msg);
}

// Reset CAN statistics
void CAN_ResetStatistics(void) {
    memset(&can_stats, 0, sizeof(can_stats));
    forceLog("CAN statistics reset\r\n");
}

// Check MCP2515 error flags
void CAN_CheckErrors(void) {
#ifdef ENABLE_CAN_BUS
    if (!can_mode_active) {
        return;
    }

    uint8_t error_flags = MCP2515_GetErrorFlags();

    if (error_flags != 0) {
        char msg[120];
        can_stats.errors++;
        can_stats.last_error_flags = error_flags;

        // Read error counters (TEC at 0x1C, REC at 0x1D)
        uint8_t tec = MCP2515_ReadRegister(0x1C);  // Transmit Error Counter
        uint8_t rec = MCP2515_ReadRegister(0x1D);  // Receive Error Counter

        sprintf(msg, "[CAN ERROR] Flags:0x%02X TEC:%d REC:%d", error_flags, tec, rec);
        forceLog(msg);

        // Correct bit positions per MCP2515 datasheet
        if (error_flags & 0x80) forceLog(" RX1OVR");
        if (error_flags & 0x40) forceLog(" RX0OVR");
        if (error_flags & 0x20) forceLog(" TXBO");
        if (error_flags & 0x10) forceLog(" TXEP");
        if (error_flags & 0x08) forceLog(" RXEP");
        if (error_flags & 0x04) forceLog(" TXWAR");
        if (error_flags & 0x02) forceLog(" RXWAR");
        if (error_flags & 0x01) forceLog(" EWARN");

        forceLog("\r\n");

        // Clear error flags
        MCP2515_WriteRegister(MCP2515_EFLG, 0x00);
    }
#endif
}

#else // !ENABLE_CAN_BUS

// Stub implementations for non-CAN builds
volatile bool can_mode_active = false;
volatile bool usart_mode_active = false;

bool CAN_IsSoftwareSerialReady(void) { return false; }
bool CAN_AutoDetectAndInit(void) { return false; }
void CAN_Fallback_To_USART(void) {}
void CAN_ProcessMessages(void) {}
void CAN_SendStatus(void) {}
void CAN_SendBootAnnouncement(void) {}
void CAN_GetCurrentIDs(uint32_t *cmd_ids, uint32_t *status_ids) { (void)cmd_ids; (void)status_ids; }
void CAN_PrintConfiguration(void) {}
void CAN_PrintStatistics(void) {}
void CAN_ResetStatistics(void) {}
void CAN_CheckErrors(void) {}

#endif // ENABLE_CAN_BUS
