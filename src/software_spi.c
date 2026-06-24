#include "software_spi.h"
#include "board_active.h"

static uint8_t spi_mode = SPI_MODE0;

// ---------------------------------------------------------------------------
// Runtime pin SOURCE for the software-SPI / MCP2515 lines.
//
// The board_profile_t schema has no dedicated SoftSPI slots, so these module
// bindings stand in for ACTIVE: each is a gpio_pin_t (port 0=A..7=H, pin 0..15)
// that defaults to the sentinel {0xFF,0xFF}. Per the board_active.h contract a
// sentinel slot falls back to the compile-time SOFT_SPI_* default, so the
// default build stays byte-for-byte equivalent to the historical PA2/PA3/PB10/
// PB11/PB12 pinout. SoftSPI_Rebind() mutates these to move the CAN pins live.
// ---------------------------------------------------------------------------
static gpio_pin_t s_mosi = {0xFFu, 0xFFu};
static gpio_pin_t s_miso = {0xFFu, 0xFFu};
static gpio_pin_t s_sck  = {0xFFu, 0xFFu};
static gpio_pin_t s_cs   = {0xFFu, 0xFFu};
static gpio_pin_t s_int  = {0xFFu, 0xFFu};

void SoftSPI_Rebind(gpio_pin_t mosi,
                    gpio_pin_t miso,
                    gpio_pin_t sck,
                    gpio_pin_t cs,
                    gpio_pin_t intp) {
    s_mosi = mosi;
    s_miso = miso;
    s_sck  = sck;
    s_cs   = cs;
    s_int  = intp;
}

void SoftSPI_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Enable GPIO clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // Configure MOSI pin (Output)
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_mosi, SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN);

    // Configure MISO pin (Input) - no pull (external circuit must handle)
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_miso, SOFT_SPI_MISO_PORT, SOFT_SPI_MISO_PIN);

    // Configure SCK pin (Output)
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN);

    // Configure CS pin (Output)
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_cs, SOFT_SPI_CS_PORT, SOFT_SPI_CS_PIN);

    // Configure INT pin (Input with interrupt capability)
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_int, SOFT_SPI_INT_PORT, SOFT_SPI_INT_PIN);

    // Set default pin states
    BOARD_GPIO_SET(s_cs, SOFT_SPI_CS_PORT, SOFT_SPI_CS_PIN);      // CS high (inactive)
    BOARD_GPIO_RESET(s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN); // SCK low
    BOARD_GPIO_RESET(s_mosi, SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN);

    spi_mode = SPI_MODE0;
}

void SoftSPI_Deinit(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // Reconfigure all pins as analog input (low power)
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    BOARD_GPIO_INIT(&GPIO_InitStruct, s_mosi, SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN);
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_miso, SOFT_SPI_MISO_PORT, SOFT_SPI_MISO_PIN);
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_sck,  SOFT_SPI_SCK_PORT,  SOFT_SPI_SCK_PIN);
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_cs,   SOFT_SPI_CS_PORT,   SOFT_SPI_CS_PIN);
    BOARD_GPIO_INIT(&GPIO_InitStruct, s_int,  SOFT_SPI_INT_PORT,  SOFT_SPI_INT_PIN);
}

void SoftSPI_SetMode(uint8_t mode) {
    spi_mode = mode & 0x03;

    // Set clock idle state based on mode
    if (spi_mode == SPI_MODE2 || spi_mode == SPI_MODE3) {
        BOARD_GPIO_SET(s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN);
    } else {
        BOARD_GPIO_RESET(s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN);
    }
}

void SoftSPI_CS_Low(void) {
    BOARD_GPIO_RESET(s_cs, SOFT_SPI_CS_PORT, SOFT_SPI_CS_PIN);  // Direct register: faster
    // MCP2515 needs 100ns CS setup time before first clock
    SoftSPI_Delay();
}

void SoftSPI_CS_High(void) {
    BOARD_GPIO_SET(s_cs, SOFT_SPI_CS_PORT, SOFT_SPI_CS_PIN);  // Direct register: faster
}

// SPI Mode 0 transfer - v1.24: Sample MISO just before falling edge
// v1.22 showed first 4 bits correct, last 4 wrong = timing drift
__attribute__((optimize("-O3")))
uint8_t SoftSPI_Transfer(uint8_t data) {
    uint8_t received = 0;

    // SPI Mode 0: CPOL=0, CPHA=0
    // MCP2515 outputs on falling edge, we sample on rising (or during high)
    // Try sampling LATE in the clock high period for maximum stability

    for (int i = 7; i >= 0; i--) {
        // 1. Set MOSI bit
        if (data & (1 << i)) {
            BOARD_GPIO_SET(s_mosi, SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN);
        } else {
            BOARD_GPIO_RESET(s_mosi, SOFT_SPI_MOSI_PORT, SOFT_SPI_MOSI_PIN);
        }

        // 2. MOSI setup time
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 3. RISING EDGE
        BOARD_GPIO_SET(s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN);

        // 4. Hold clock high - let signal stabilize
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();

        // 5. Sample MISO NOW (late in high period, just before falling)
        if (BOARD_GPIO_READ(s_miso, SOFT_SPI_MISO_PORT, SOFT_SPI_MISO_PIN)) {
            received |= (1 << i);
        }

        // 6. FALLING EDGE - MCP2515 shifts out next bit
        BOARD_GPIO_RESET(s_sck, SOFT_SPI_SCK_PORT, SOFT_SPI_SCK_PIN);

        // 7. Wait for data valid (tV=45ns) + margin
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
        __NOP(); __NOP(); __NOP(); __NOP();
    }

    return received;
}

void SoftSPI_TransferBuffer(uint8_t *txData, uint8_t *rxData, uint16_t length) {
    for (uint16_t i = 0; i < length; i++) {
        rxData[i] = SoftSPI_Transfer(txData[i]);
    }
}
