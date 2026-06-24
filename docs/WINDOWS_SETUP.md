# Windows Setup Guide

## Why Windows shows "DEVICE_DESCRIPTOR_FAILURE"

When you plug an STM32 into Windows you may see this in Device Manager:

```
Device:  USB\VID_0000&PID_0002
Driver:  usb.inf  (BADDEVICE.Dev.NT)
Matching Device ID: USB\DEVICE_DESCRIPTOR_FAILURE
```

**What this means:** Windows detected the USB D+ pullup on the STM32 (it knows
a device is present) but the device didn't respond to the standard "Get Device
Descriptor" request. The firmware doesn't implement USB, so the STM32 ignores it.

**Why it happens with this firmware:** `bipropellant_can_generic` uses
CAN (via MCP2515 SPI) and UART for communication — not USB. If the board has
a USB connector on PA11/PA12 (like a Blue Pill or Black Pill dev board), Windows
sees the hardware but gets no response.

---

## Immediate Fix — Option A: STM32CubeProgrammer (recommended)

1. Download and install **STM32CubeProgrammer** from STMicroelectronics:
   https://www.st.com/en/development-tools/stm32cubeprog.html
   (free, requires account, includes ST-Link + USB DFU drivers)

2. Once installed, the ST-Link and USB DFU devices work automatically.

3. To flash via **ST-Link**: connect the ST-Link/V2 to SWD pins, open
   STM32CubeProgrammer, select "ST-Link" interface, connect, and flash.

4. To flash via **USB DFU**: see the "Enter USB DFU mode" section below.

---

## Immediate Fix — Option B: Zadig + dfu-util

If you just want USB DFU without installing STM32CubeProgrammer:

1. Download **Zadig**: https://zadig.akeo.ie/
2. In Zadig: `Options → List All Devices`
3. Select `STM32 BOOTLOADER` or `Unknown Device (VID_0000 PID_0002)` from the list
4. Set driver to **WinUSB** and click "Install Driver"
5. Install **dfu-util**: `winget install dfu-util` or download from http://dfu-util.sourceforge.net/
6. Flash: `dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin`

---

## How to enter USB DFU mode

The STM32 has a factory ROM bootloader (at 0x1FFFF000) that handles USB and
appears as **VID_0483:PID_DF11** — which STM32CubeProgrammer and dfu-util recognise.

### Method 1: BOOT0 pin (hardware)
Hold **BOOT0** HIGH while powering on the board.
- On Blue Pill: bridge the BOOT0 jumper to 3.3V
- On a custom board: check the schematic for the BOOT0 pin
The board enumerates as VID_0483:PID_DF11 automatically.

### Method 2: From our UART bootloader menu
Open a serial terminal (115200 baud, 8N1) on PA9/PA10, press any key within 500 ms,
select **[7]**: `Enter USB DFU (ROM bootloader, VID_0483:PID_DF11)`.

### Method 3: From the running application (serial command)
Type `RD` in the UART terminal. The application writes the BKP_DR2 magic flag
and resets into the ROM DFU bootloader.

### Method 4: From the application code
```c
#include "bootloader_request.h"
bootloader_request_dfu();   // does not return
```

### Method 5: Via CAN
```bash
python3 tools/can_flash.py -d can0 --enter-dfu
# Device will reappear as VID_0483:PID_DF11
```

---

## Flash via USB DFU (after entering DFU mode)

### Using STM32CubeProgrammer
1. Open STM32CubeProgrammer
2. Select **USB** interface (not ST-Link)
3. Click **Connect** (device must be in DFU mode)
4. Click **Open file**, select `build/hover.bin`
5. Set start address to `0x08002000` (after the CAN+UART bootloader)
6. Click **Download**

### Using dfu-util
```bash
# Install driver first via Zadig (WinUSB for VID_0483:PID_DF11)
dfu-util -l                              # list DFU devices
dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin
```

---

## Normal flashing workflow without USB (recommended)

Use the **ST-Link/V2** programmer via SWD pins and ST-Link tools:

**One-time bootloader flash (when you have ST-Link):**
```bash
# Linux / Raspberry Pi:
st-flash write bootloader/bootloader.bin 0x08000000
st-flash write build/hover.bin 0x08002000
```

**Subsequent updates (no programmer needed):**
```bash
# Via CAN (from Pi):
python3 tools/can_flash.py -d can0 -f build/hover.bin

# Via UART (USB-serial adapter on PA9/PA10):
python3 uart-updater.py -p /dev/ttyUSB0 -f build/hover.bin

# Via USB DFU (Windows, BOOT0 pin):
dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin
```

---

## Detect what's connected (detect_chip.py)

```bash
# Shows ST-Link version + connected chip info:
python3 tools/detect_chip.py

# With CAN: also shows bootloader banner + chip DevID from firmware:
python3 tools/detect_chip.py -d can0
```

---

## STM32F4xx (F401, F411) — will this firmware work?

**Partially yes, with one important caveat (same answer for F401 and F411).**

| Chip | DevID | Flash | SRAM | MHz | TIM8? | Native CAN | Windows USB |
|---|---|---|---|---|---|---|---|
| **STM32G474CEU6** | **0x0469** | **512 KB** | **128 KB** | **170** | **✅** | **FDCAN FD** | Driver-free |
| STM32F103RCT6 (hoverboard) | 0x0414 | 256 KB | 48 KB | 72 | ✅ | via MCP2515 | Needs Zadig |
| STM32F401CCU6 (Black Pill v1) | 0x0423 | 256 KB | 64 KB | 84 | ❌ | via MCP2515 | Driver-free |
| STM32F401CEU6 | 0x0433 | 512 KB | 96 KB | 84 | ❌ | via MCP2515 | Driver-free |
| STM32F411CEU6 (Black Pill v2) | 0x0431 | 512 KB | 128 KB | 100 | ❌ | via MCP2515 | Driver-free |

| Feature | F103 | F401/F411 | **G474** | Notes |
|---|---|---|---|---|
| Bootloader (CAN+UART+USB DFU) | ✅ | ✅ | ✅ | G4 ROM DFU at 0x1FFF0000 — same as F4 |
| chip_detect.c | ✅ | ✅ | ✅ | G474 DevID 0x0469 added |
| uart_hdsel, PhaseMap, detect | ✅ | ✅ | ✅ | HAL-based, family-agnostic |
| Single-motor drive (TIM1) | ✅ | ✅ | ✅ | TIM1 pin mapping identical on all |
| **Dual-motor (TIM1+TIM8)** | ✅ | ❌ | **✅** | G474 has TIM8 — same as F103! |
| Native CAN (no MCP2515) | ❌ | ❌ | **✅** | G474 FDCAN1/2, CAN FD + 2.0B compat |
| USB on Windows | Needs Zadig | Driver-free | **Driver-free** | G4 HSI48 = crystal-free USB |
| HRTIM dead-time | ❌ | ❌ | **✅** | 184 ps resolution for FOC |
| On-chip current sense OpAmps | ❌ | ❌ | **✅** | 3× OpAmps, no external ICs needed |

**TIM1 and TIM8 pin mapping is identical on F103, G474:**
```
Right motor TIM1: PA8(CH1)  PA9(CH2)  PA10(CH3)  high-side
                  PB13(CH1N) PB14(CH2N) PB15(CH3N) low-side
Left motor  TIM8: PC6(CH1)  PC7(CH2)  PC8(CH3)    high-side
                  PA7(CH1N)  PB0(CH2N)  PB1(CH3N)  low-side
```
Existing hoverboard wiring drops straight in on G474.

**G474 CAN FD advantage:** Instead of MCP2515 (SPI overhead, 500 kbps max), use the
native FDCAN peripheral with a TJA1042 or SIT65HVD230 transceiver. Backwards compatible
with the existing bipropellant CAN protocol at 500 kbps; future upgrade path to 2 Mbit/s+.
See `src/hal_motor_stm32g4.c` for the dual-motor scaffold and FDCAN integration notes.

**To build for G474:** create a Makefile env with `-DBOARD_FAMILY_STM32G4 -DSTM32G474xx`
+ stm32g4xx HAL + startup_stm32g474xe.s + STM32G474CETx_FLASH.ld.

**G4 variants covered:**
- G4 Cat.2 (G431/G441): DevID 0x0468 — no TIM8, limited
- **G4 Cat.3 (G474/G484): DevID 0x0469** — full TIM1+TIM8+TIM20+FDCAN+HRTIM
- G4 Cat.4 (G491/G4A1): DevID 0x0479 — single FDCAN, no HRTIM
