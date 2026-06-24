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

## STM32F411CEU6 — will this firmware work?

**Partially yes, with an important caveat:**

| Feature | STM32F103RCT6 | STM32F411CEU6 | Works on F411? |
|---|---|---|---|
| Bootloader (CAN+UART+USB DFU) | ✅ | ✅ | Yes — different ROM DFU addr (0x1FFF0000) |
| chip_detect.c | ✅ | ✅ | Yes — DevID 0x0431/0x0441 added to table |
| uart_hdsel.c | ✅ | ✅ | Yes — HAL-based, family-agnostic |
| PhaseMap Wizard | ✅ | ✅ | Yes — GPIO/ADC logic is HAL-agnostic |
| Single-motor drive (TIM1) | ✅ | ✅ | Yes — TIM1 complementary outputs on same pins |
| **Dual-motor drive (TIM1+TIM8)** | ✅ | **❌** | **No — STM32F411 has no TIM8** |

**What this means for a hoverboard (dual-motor) controller:**
- Right motor: TIM1 (PA8/PA9/PA10 + PB13/PB14/PB15) works identically on F411
- Left motor: **no complementary timer** on F411 → use an external gate driver with
  built-in dead-time, or run both motors from TIM1 channels 1-3 as a single-motor
  controller

**STM32F411 USB advantage:** The F411 has **USB OTG Full-Speed** which is driver-free
on Windows 10/11 (CDC virtual COM port). No Zadig/CubeProgrammer needed for USB serial.
The bootloader can enumerate as a COM port on Windows without any driver installation
— a significant improvement over the F103.

See `bootloader/hal_motor_stm32f4.c` for the single-advanced-timer motor stub
(same architecture as GD32 single-motor support). To build for STM32F411, create
a new Makefile env with `-DSTM32F411xE` + the F411 HAL + its startup/ld files.
