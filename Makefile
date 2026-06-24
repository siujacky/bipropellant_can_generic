######################################
# target
######################################
TARGET = hover

######################################
# building variables
######################################
# debug build?
DEBUG = 1
# optimization
OPT = -Og

# Build path
BUILD_DIR = build

######################################
# source
######################################
# C sources
C_SOURCES =  \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c \
Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
src/system_stm32f1xx.c \
src/setup.c \
src/control.c \
src/main.c \
src/bldc.c \
src/comms.c \
src/sensorcoms.c \
src/softwareserial.c \
src/hallinterrupts.c \
src/flashaccess.c \
src/stm32f1xx_it.c \
src/pid.c \
src/deadreckoner.c \
src/hbprotocol/protocol.c \
src/hbprotocol/machine_protocol.c \
src/hbprotocol/ascii_protocol.c \
src/hbprotocol/cobsr.c \
src/ascii_proto_funcs.c \
src/protocolfunctions.c \
src/BLDC_controller_data.c \
src/BLDC_controller.c \
src/mcp2515.c \
src/software_spi.c \
src/can_bus.c \
src/board_select.c \
src/board_override.c \
generated/board_table_stm32f1.c \
generated/board_af_validity_stm32f1.c \
src/phasemap.c \
src/uart_hdsel.c \
src/chip_detect.c


# ASM sources
ASM_SOURCES =  \
startup_stm32f103xe.s

#######################################
# binaries
#######################################
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
AR = $(PREFIX)ar
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

#######################################
# CFLAGS
#######################################
# cpu
CPU = -mcpu=cortex-m3

# fpu
# NONE for Cortex-M0/M0+/M3

# float-abi


# mcu
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# macros for gcc
# AS defines
AS_DEFS =

# C defines
C_DEFS =  \
-DUSE_HAL_DRIVER \
-DSTM32F103xE


# AS includes
AS_INCLUDES =

# C includes
C_INCLUDES =  \
-Iinc \
-Igenerated \
-Isrc/hbprotocol \
-IDrivers/STM32F1xx_HAL_Driver/Inc \
-IDrivers/STM32F1xx_HAL_Driver/Inc/Legacy \
-IDrivers/CMSIS/Device/ST/STM32F1xx/Include \
-IDrivers/CMSIS/Include


# compile gcc flags
ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

# -fcommon: this firmware predates GCC 10's -fno-common default and relies on
# merged tentative definitions (rtP, hdma_i2c2_*) across translation units.
# Without it modern arm-none-eabi-gcc (>=10) fails to link with "multiple
# definition" errors. (PlatformIO's older toolchain defaulted to -fcommon.)
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections -std=gnu11

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif


# Generate dependency information
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)"


#######################################
# LDFLAGS
#######################################
# link script
LDSCRIPT = STM32F103RCTx_FLASH.ld

# libraries
LIBS = -lc -lm -lnosys
LIBDIR =
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBDIR) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

# default action: build all
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin


#######################################
# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c inc/config.h Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s inc/config.h Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir -p $@

format:
	find src/ inc/ -iname '*.h' -o -iname '*.c' | xargs clang-format -i
#######################################
# clean up
#######################################
clean:
	-rm -fR .dep $(BUILD_DIR)

flash:
	st-flash --reset write $(BUILD_DIR)/$(TARGET).bin 0x8000000

unlock:
	openocd -f interface/stlink-v2.cfg -f target/stm32f1x.cfg -c init -c "reset halt" -c "stm32f1x unlock 0"

#######################################
# dependencies
#######################################
-include $(shell mkdir .dep 2>/dev/null) $(wildcard .dep/*)

# =============================================================================
# M5 scaffold: per-family smoke builds (motor-disabled)
#
# Each env builds a minimal smoke .bin that:
#   - boots (clock init via system_<fam>.c, startup_<fam>.s)
#   - runs board_select/override (with the family-filtered board_table)
#   - links hal_motor_<fam>.c (MOTOR-DISABLED stub — no phase PWM init)
#
# Motor-disabled smoke only — NOT for production use.
# Verify clock tree and peripheral init on hardware before enabling motors.
#
# Usage:
#   make env-gd32f1      # GD32F130 smoke .bin (Cortex-M3)
#   make env-gd32e2      # GD32E230 smoke .bin (Cortex-M23)
#   make env-mm32spin0x  # MM32SPIN05 smoke .bin (Cortex-M0)
#   make env-all         # all three smoke builds
#   make env-clean       # remove all smoke build artefacts
# =============================================================================

# ---------------------------------------------------------------------------
# Smoke-build common sources (shared by all non-STM32F1 envs)
# ---------------------------------------------------------------------------
SMOKE_COMMON_SRCS = \
	src/board_select.c \
	src/board_override.c \
	src/main_smoke.c

SMOKE_COMMON_INC = \
	-Iinc/smoke_hal \
	-Iinc \
	-Igenerated \
	-Isrc/hbprotocol

SMOKE_COMMON_DEFS = \
	-DUSE_HAL_DRIVER

SMOKE_COMMON_FLAGS = \
	-mthumb \
	-std=gnu11 \
	-Wall \
	-Og \
	-fdata-sections \
	-ffunction-sections

SMOKE_LDFLAGS_COMMON = \
	-specs=nano.specs \
	-lc -lm -lnosys \
	-Wl,--gc-sections

# ---------------------------------------------------------------------------
# env:gd32f1 — GD32F130 (Cortex-M3, BOARD_FAMILY_GD32F1)
# ---------------------------------------------------------------------------
GD32F1_BUILD = build/env-gd32f1
GD32F1_TARGET = hover-gd32f1

GD32F1_SRCS = \
	$(SMOKE_COMMON_SRCS) \
	src/system_gd32f1.c \
	src/hal_motor_gd32f1.c \
	generated/board_table_gd32f1.c \
	generated/board_af_validity_gd32f1.c

GD32F1_ASM = startup_gd32f1.s

GD32F1_DEFS = $(SMOKE_COMMON_DEFS) -DBUILD_FAMILY_GD32F1

GD32F1_CFLAGS = $(SMOKE_COMMON_FLAGS) -mcpu=cortex-m3 $(GD32F1_DEFS) $(SMOKE_COMMON_INC)

GD32F1_OBJS = \
	$(addprefix $(GD32F1_BUILD)/,$(notdir $(GD32F1_SRCS:.c=.o))) \
	$(GD32F1_BUILD)/$(notdir $(GD32F1_ASM:.s=.o))

vpath %.c $(sort $(dir $(GD32F1_SRCS)))
vpath %.s .

$(GD32F1_BUILD)/%.o: %.c | $(GD32F1_BUILD)
	$(CC) -c $(GD32F1_CFLAGS) $< -o $@

$(GD32F1_BUILD)/%.o: %.s | $(GD32F1_BUILD)
	$(AS) -c $(GD32F1_CFLAGS) $< -o $@

$(GD32F1_BUILD)/$(GD32F1_TARGET).elf: $(GD32F1_OBJS)
	$(CC) $(GD32F1_OBJS) -mcpu=cortex-m3 -mthumb \
		$(SMOKE_LDFLAGS_COMMON) \
		-T gd32f1_flash.ld \
		-Wl,-Map=$(GD32F1_BUILD)/$(GD32F1_TARGET).map,--cref \
		-o $@
	$(SZ) $@

$(GD32F1_BUILD)/$(GD32F1_TARGET).bin: $(GD32F1_BUILD)/$(GD32F1_TARGET).elf | $(GD32F1_BUILD)
	$(BIN) $< $@

$(GD32F1_BUILD):
	mkdir -p $@

env-gd32f1: $(GD32F1_BUILD)/$(GD32F1_TARGET).bin
	@echo "OK: env-gd32f1 smoke .bin built: $<"
	@echo "    Motor-disabled smoke only — bench-validate before enabling motors."

# ---------------------------------------------------------------------------
# env:gd32e2 — GD32E230 (Cortex-M23, BOARD_FAMILY_GD32E2)
# ---------------------------------------------------------------------------
GD32E2_BUILD = build/env-gd32e2
GD32E2_TARGET = hover-gd32e2

GD32E2_SRCS = \
	$(SMOKE_COMMON_SRCS) \
	src/system_gd32e2.c \
	src/hal_motor_gd32e2.c \
	generated/board_table_gd32e2.c \
	generated/board_af_validity_gd32e2.c

GD32E2_ASM = startup_gd32e2.s

GD32E2_DEFS = $(SMOKE_COMMON_DEFS) -DBUILD_FAMILY_GD32E2

# NOTE: Cortex-M23 requires -mcpu=cortex-m23 (NOT cortex-m3)
GD32E2_CFLAGS = $(SMOKE_COMMON_FLAGS) -mcpu=cortex-m23 $(GD32E2_DEFS) $(SMOKE_COMMON_INC)

GD32E2_OBJS = \
	$(addprefix $(GD32E2_BUILD)/,$(notdir $(GD32E2_SRCS:.c=.o))) \
	$(GD32E2_BUILD)/$(notdir $(GD32E2_ASM:.s=.o))

vpath %.c $(sort $(dir $(GD32E2_SRCS)))

$(GD32E2_BUILD)/%.o: %.c | $(GD32E2_BUILD)
	$(CC) -c $(GD32E2_CFLAGS) $< -o $@

$(GD32E2_BUILD)/%.o: %.s | $(GD32E2_BUILD)
	$(AS) -c $(GD32E2_CFLAGS) $< -o $@

$(GD32E2_BUILD)/$(GD32E2_TARGET).elf: $(GD32E2_OBJS)
	$(CC) $(GD32E2_OBJS) -mcpu=cortex-m23 -mthumb \
		$(SMOKE_LDFLAGS_COMMON) \
		-T gd32e2_flash.ld \
		-Wl,-Map=$(GD32E2_BUILD)/$(GD32E2_TARGET).map,--cref \
		-o $@
	$(SZ) $@

$(GD32E2_BUILD)/$(GD32E2_TARGET).bin: $(GD32E2_BUILD)/$(GD32E2_TARGET).elf | $(GD32E2_BUILD)
	$(BIN) $< $@

$(GD32E2_BUILD):
	mkdir -p $@

env-gd32e2: $(GD32E2_BUILD)/$(GD32E2_TARGET).bin
	@echo "OK: env-gd32e2 smoke .bin built: $<"
	@echo "    Motor-disabled smoke only — bench-validate before enabling motors."
	@echo "    NOTE: Cortex-M23 — use -mcpu=cortex-m23 toolchain flag."

# ---------------------------------------------------------------------------
# env:mm32spin0x — MM32SPIN05 (Cortex-M0, BOARD_FAMILY_MM32SPIN0X)
# ---------------------------------------------------------------------------
MM32_BUILD = build/env-mm32spin0x
MM32_TARGET = hover-mm32spin0x

MM32_SRCS = \
	$(SMOKE_COMMON_SRCS) \
	src/system_mm32spin0x.c \
	src/hal_motor_mm32spin0x.c \
	generated/board_table_mm32spin0x.c \
	generated/board_af_validity_mm32spin0x.c

MM32_ASM = startup_mm32spin0x.s

MM32_DEFS = $(SMOKE_COMMON_DEFS) -DBUILD_FAMILY_MM32SPIN0X

# NOTE: MM32SPIN0x is Cortex-M0 — use -mcpu=cortex-m0 (NOT cortex-m3)
MM32_CFLAGS = $(SMOKE_COMMON_FLAGS) -mcpu=cortex-m0 $(MM32_DEFS) $(SMOKE_COMMON_INC)

MM32_OBJS = \
	$(addprefix $(MM32_BUILD)/,$(notdir $(MM32_SRCS:.c=.o))) \
	$(MM32_BUILD)/$(notdir $(MM32_ASM:.s=.o))

vpath %.c $(sort $(dir $(MM32_SRCS)))

$(MM32_BUILD)/%.o: %.c | $(MM32_BUILD)
	$(CC) -c $(MM32_CFLAGS) $< -o $@

$(MM32_BUILD)/%.o: %.s | $(MM32_BUILD)
	$(AS) -c $(MM32_CFLAGS) $< -o $@

$(MM32_BUILD)/$(MM32_TARGET).elf: $(MM32_OBJS)
	$(CC) $(MM32_OBJS) -mcpu=cortex-m0 -mthumb \
		$(SMOKE_LDFLAGS_COMMON) \
		-T mm32spin0x_flash.ld \
		-Wl,-Map=$(MM32_BUILD)/$(MM32_TARGET).map,--cref \
		-o $@
	$(SZ) $@

$(MM32_BUILD)/$(MM32_TARGET).bin: $(MM32_BUILD)/$(MM32_TARGET).elf | $(MM32_BUILD)
	$(BIN) $< $@

$(MM32_BUILD):
	mkdir -p $@

env-mm32spin0x: $(MM32_BUILD)/$(MM32_TARGET).bin
	@echo "OK: env-mm32spin0x smoke .bin built: $<"
	@echo "    Motor-disabled smoke only — bench-validate before enabling motors."
	@echo "    NOTE: Cortex-M0 (HIGHEST RISK family) — verify RCC register map before use."

# ---------------------------------------------------------------------------
# env-all: build all three smoke envs
# ---------------------------------------------------------------------------
env-all: env-gd32f1 env-gd32e2 env-mm32spin0x
	@echo "All smoke builds complete."

# ---------------------------------------------------------------------------
# env-clean: remove all smoke build artefacts
# ---------------------------------------------------------------------------
env-clean:
	-rm -fR build/env-gd32f1 build/env-gd32e2 build/env-mm32spin0x

# *** EOF ***
