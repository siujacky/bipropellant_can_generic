# PhaseMap Wizard — User Guide

The PhaseMap Wizard identifies which GPIO pins drive which motor phase (U/V/W,
high-side/low-side) on an unknown hoverboard controller board.  It also
detects the three Hall-sensor pins.  When it completes successfully it prints a
TOML snippet ready to drop into the `profiles/` library.

---

## Safety Checklist — Read Before You Start

**Do not skip this section.  The wizard drives gate-driver inputs HIGH.
Without the dummy load the motor winding acts as a short through the active FET
and the FET will be destroyed.**

### Required equipment

| Item | Specification |
|------|---------------|
| Current-limited bench PSU | Set to board operating voltage (typically 36 V).  **Current limit 0.5 A or less.** |
| Dummy-load resistors | One 10 Ω / 5 W resistor per phase output (3 total for a single-motor board, 6 for dual-motor).  Wire-wound ceramic, not carbon film. |
| Jumper wires | Adequate for the phase connectors (XT60 or JST-PH depending on board) |
| Serial terminal | Any 115200-baud terminal (minicom, PuTTY, screen) |

### Dummy-load wiring diagram

```
PSU+ ─────────────────────────────────┐
                                       │
                                  BOARD POWER IN

PSU- ──── Current-limit set ───────────┐
                                       │ (common ground)
┌──────────────────────────────────────┴──────────────────────────────────┐
│                              BOARD                                       │
│  Phase U out ──── 10 Ω 5 W ──── PSU-/GND                                │
│  Phase V out ──── 10 Ω 5 W ──── PSU-/GND                                │
│  Phase W out ──── 10 Ω 5 W ──── PSU-/GND                                │
│                                                                          │
│  (Repeat for phases_left U/V/W if dual-motor board)                     │
└──────────────────────────────────────────────────────────────────────────┘
```

**Motor windings must be disconnected.**  The dummy resistors replace the
motor.  If the motor connector is left plugged in, a mis-identified phase pin
will drive current through a live winding and may spin the motor unexpectedly
or destroy the winding insulation.

### Pre-start checklist

- [ ] Motor connector physically disconnected from board
- [ ] One 10 Ω resistor connected from each phase output to PSU negative rail
- [ ] PSU current limit set to ≤ 0.5 A before powering on
- [ ] PSU voltage set to board rated voltage (check silkscreen or schematics — typically 24 V or 36 V)
- [ ] Serial terminal connected at 115200 baud 8N1
- [ ] Board powered and firmware boot messages visible in terminal

---

## Guided Mode — Step-by-Step Walkthrough

Guided mode is recommended for first use.  The operator observes each probe
and confirms whether the dummy-load resistor got warm / lit an ammeter reading.

### Step 1 — Start the wizard

Type in the serial terminal:

```
Vstart g
```

Expected response:

```
[PhaseMap] === PhaseMap Wizard ===
[PhaseMap] Mode: GUIDED
[PhaseMap] Found N low-side candidate pins.
[PhaseMap] SAFETY: Connect a current-limiting dummy load (e.g. 10 ohm resistor)
[PhaseMap]         between each phase output and ground before proceeding.
[PhaseMap] Type exactly: CONFIRM DUMMY LOAD
```

### Step 2 — Confirm dummy load

Type (including the capital letters and spaces, exactly):

```
CONFIRM DUMMY LOAD
```

If you mistype the phrase the wizard says "Incorrect phrase" and waits again.
This is intentional — it guards against accidental entry.

### Step 3 — Probe each pin

For each candidate pin the wizard prints a message like:

```
[PhaseMap] Probing PB0 (TIM8_CH2N) ...
[PhaseMap]   Gate is HIGH. Did you see/hear the motor respond? [y/n]:
```

While the gate is HIGH (up to 20 ms), check your bench PSU ammeter:
- If the current rose (typically 50–200 mA through a 10 Ω load at 36 V), type **y**.
- If the current did not move, type **n**.

The wizard records your answer, drives all pins LOW again, waits 150 ms for the
power FET to cool, and moves to the next candidate.

**Other interactive keys during probing:**

| Key | Effect |
|-----|--------|
| `y` | Confirm — this pin drove current (gate active) |
| `n` | Reject — no response seen |
| `s` | Skip — move to next pin without recording a result |
| `q` | Quit / abort — drives all pins LOW and stops |

### Step 4 — Hall sensor rotation

After all low-side pins are probed the wizard switches to Hall detection:

```
[PhaseMap] Low-side probe complete. Now probing hall sensors.
[PhaseMap] Slowly rotate the wheel 1/6 turn (60 degrees) in 5 seconds.
```

Slowly rotate **one** motor by hand approximately 60 degrees within the 5-second
window.  The wizard samples all potential Hall-pin candidates at 10 Hz.  Any pin
that changed state during the window is reported as a Hall candidate.

### Step 5 — Read the TOML output

When the wizard completes it prints the profile to the serial terminal:

```
# PhaseMap Wizard output — paste into profiles/*.toml
[meta]
name = "phasemap-detected"
confidence = 0.8

[chip]
family = "STM32F1"

[power]
self_hold = "PA..."

[phases_right]
timer = "TIM1"
u_high = "PA8"
u_low  = "PB13"
...
dead_time_ns = 833

[phases_left]
...

[halls_right]
hall_a = "PC6"
hall_b = "PC7"
hall_c = "PC8"
```

Copy the printed block from `# PhaseMap Wizard output` to the last line
into a new file, for example `profiles/my-board-detected.toml`.

---

## Auto ADC Mode

Auto mode uses battery voltage droop on the ADC to detect whether a FET fired.
No operator interaction is needed during the probe sequence; it runs fully
unattended after the safety confirmation.

```
Vstart a
CONFIRM DUMMY LOAD
```

The wizard first scans ADC channels 0–15 to locate the battery voltage divider
(it looks for a ratio in the 6.5 %–12 % range consistent with an 8×–15× divider).
Then it probes each pin and records the voltage droop.  A droop > 50 mV is
treated as a confirmed gate.  A droop > 2000 mV triggers an emergency abort
(FET fault or dummy-load fault).

Auto mode is faster but less robust on boards where the battery ADC channel is
not in the expected ratio window.  Fall back to Guided mode if Auto produces no
confirmations.

---

## Serial Command Reference

All commands are sent as a line (terminated with Enter / CR) in the ASCII
protocol terminal.  The protocol must be unlocked first if your firmware has
`asciiProtocolUnlocked = 0` (type `unlockASCII` and press Enter).

| Command | Description |
|---------|-------------|
| `Vstart g` | Start Guided mode |
| `Vstart a` | Start Auto ADC mode |
| `Vabort` | Emergency stop — drives all candidate gate pins LOW immediately |
| `Vstatus` | Print current wizard state and confirmed-probe count |

**Interactive single-key input during probing** (no Enter needed — the wizard
reads characters directly from its internal ring buffer):

| Key | Wizard state it applies to | Effect |
|-----|--------------------------|--------|
| `y` | PM_PROBE_LOWSIDE (Guided) | Confirm pin |
| `n` | PM_PROBE_LOWSIDE (Guided) | Reject pin |
| `s` | PM_PROBE_LOWSIDE (Guided) | Skip pin |
| `q` | Any active state | Abort wizard |

---

## CAN Bus Command Reference

The PhaseMap Wizard can be fully controlled over CAN without a serial console.
All IDs are relative to the board's `board_base` address:

```
board_base = CAN_BaseID_CMD + (CAN_BoardID * 0x10)
```

Default with `CAN_BoardID=0` and `CAN_BaseID_CMD=0x100`:
`board_base = 0x100`.

### Command frames (host → board)

| ID | Name | DLC | data[0] | data[1..7] | Description |
|----|------|-----|---------|-----------|-------------|
| `board_base + 0x20` | PHASEMAP_START | 1 | `0`=GUIDED, `1`=AUTO | — | Start the wizard |
| `board_base + 0x21` | PHASEMAP_CONFIRM | 0 | — | — | Send "CONFIRM DUMMY LOAD" to safety gate |
| `board_base + 0x22` | PHASEMAP_CHAR | 1 | ASCII char (`y`/`n`/`s`/`q`) | — | Feed one char to wizard input |
| `board_base + 0x23` | PHASEMAP_ABORT | 0 | — | — | Emergency abort (drives all pins LOW) |

### Status frames (board → host)

The board transmits a status frame after every command it receives, and
optionally after state transitions.

| ID | Name | DLC | Payload | Description |
|----|------|-----|---------|-------------|
| `board_base + 0x30` | PHASEMAP_STATE | 3 | `[0]`=state enum, `[1]`=confirmed count, `[2]`=confirmed count | Current wizard state |
| `board_base + 0x31` | PHASEMAP_PROBE | 6 | `[0]`=port, `[1]`=pin, `[2]`=confirmed, `[3..5]`=droop_mv LE | Most recent confirmed probe result |

**State enum values (`data[0]` of PHASEMAP_STATE):**

| Value | Name |
|-------|------|
| 0 | PM_IDLE |
| 1 | PM_SAFETY_GATE |
| 2 | PM_SCAN_ADC |
| 3 | PM_PROBE_LOWSIDE |
| 4 | PM_PROBE_HALL |
| 5 | PM_BUILD_PROFILE |
| 6 | PM_DONE |
| 7 | PM_ABORTED |

### Example CAN sequence (Guided mode, using `can-utils` on Linux)

```bash
# Default CAN_BoardID=0, CAN_BaseID_CMD=0x100 → board_base=0x100

# 1. Start wizard in Guided mode
cansend can0 120#00

# 2. Confirm dummy load (replaces typing "CONFIRM DUMMY LOAD" on serial)
cansend can0 121#

# 3. Confirm first pin (y = 0x79)
cansend can0 122#79

# 4. Reject second pin (n = 0x6E)
cansend can0 122#6E

# 5. Abort if needed
cansend can0 123#

# Monitor status frames
candump can0 | grep -E "12[01]#"
```

---

## Interpreting and Saving the TOML Output

### Understanding the output fields

```toml
[meta]
name = "phasemap-detected"
confidence = 0.8        # < 1.0 means "wizard result, not hand-verified"
```

`confidence = 0.8` is a marker that the profile was generated by the wizard.
After you verify the motor spins correctly with this profile, raise confidence
to `1.0` and rename the profile to match your board (e.g. `my-board-v1`).

```toml
[phases_right]
timer = "TIM1"
u_high = "PA8"    # TIM1_CH1  — high-side U-phase FET gate
u_low  = "PB13"   # TIM1_CH1N — low-side U-phase FET gate (probed by wizard)
v_high = "PA9"    # TIM1_CH2
v_low  = "PB14"   # TIM1_CH2N (probed)
w_high = "PA10"   # TIM1_CH3
w_low  = "PB15"   # TIM1_CH3N (probed)
dead_time_ns = 833
```

The wizard probes **low-side** (`CHxN`) pins only — these are the complementary
timer outputs that drive the bottom FETs.  The high-side pins are inferred from
the silicon alternate-function table (`g_af_validity`); they should be correct
but verify them against your board's schematic before running with real current.

`UNDETECTED` in any slot means no confirmed probe was recorded for that phase.
This can happen if:
- The dummy-load resistor was not connected to that phase's output
- The gate driver is not powered (some boards have a separate 12 V gate-driver rail)
- The pin is not a TIM1/TIM8 complementary output on this silicon family

### Saving to the profiles library

```bash
# In the repo root:
cp /tmp/phasemap-output.toml profiles/my-board-phasemap.toml

# Run the codegen to regenerate board_table.c:
make generate   # or: python -m stm32_auto_detect emit-c-board-table --family STM32F1 -o generated/
make
```

After flashing the new firmware, select the profile via serial:

```
# Find the table index for your new profile:
# (board_table_count is printed at boot, or use Vstatus on a later version)
# Then force-select it (index 3 in this example):
board_select 3
```

Or via CAN (board select command, `board_base + 0x04`):

```bash
# Select index 3 (little-endian uint16):
cansend can0 104#0300
```

Power-cycle the board.  The new profile is now active.

---

## Safety Architecture Summary

Three independent safety lines protect against FET latch-up during probing:

1. **Complement lockout** — Before any candidate pin goes HIGH, ALL other
   candidate pins are configured as GPIO_OUTPUT_PP and driven LOW in a single
   atomic block (interrupts disabled, direct register writes).  The PWM timer
   peripheral is never initialised during the wizard.

2. **SysTick hard limit** — `MAX_PROBE_MS = 20 ms`.  The SysTick ISR
   (`SysTick_Handler` calls `phasemap_systick_isr()`) checks the elapsed time
   every millisecond.  If the deadline passes with any pin still HIGH it drives
   all candidates LOW via `GPIOx->BRR` (direct register, no HAL) and sets the
   abort flag.  The main loop also calls `phasemap_systick_isr()` as a
   belt-and-suspenders check.

3. **Current watchdog (Auto mode only)** — After each probe, if the battery
   ADC droop exceeds 2000 mV, `phasemap_abort()` is called immediately.
   This catches a failed dummy-load or a shorted FET before it can thermally
   run away.

The self-hold pin (`ACTIVE.self_hold`) is **never probed**.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| "no low-side candidates found" | Board profile has no TIM1/TIM8 CHxN entries in `g_af_validity` | Check that `generated/board_af_validity_stm32f1.c` is up to date and the correct family build is flashed |
| All probes come back "not confirmed" | Dummy-load resistors not connected, or gate-driver power rail missing | Verify resistors with a meter; check 12 V gate-driver supply |
| Abort immediately after confirming dummy load | `pm_abort` already set from a previous run | Power-cycle the board to clear; or call `Vabort` then `Vstart g` |
| Auto mode finds no battery ADC channel | Battery divider ratio is outside 6.5–12 % window | Set `s_vsupply_mv` in phasemap.c to match your supply voltage, or use Guided mode |
| TOML shows `u_high = "UNKNOWN"` | High-side inference failed (no matching non-complementary TIM pin in g_af_validity) | This is a codegen gap; look up the high-side pin in the board schematic and fill it in manually |
| PSU current trips during probe | Dummy-load resistor value too low, or motor winding still connected | Check resistors (≥ 10 Ω), disconnect motor |
| CAN PHASEMAP_START not acknowledged | CAN IDs have not been re-initialised after changing `CAN_BoardID` | Power-cycle the board after changing the board ID |
