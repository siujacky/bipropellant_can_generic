"""Stage 4 Strategy B — emit the generated C board table from board profiles.

This module realizes the embedded-board-table codegen for the
bipropellant_can_generic universal firmware (see docs/PLAN.md sections
"Embedded board table", "Codegen", "Runtime override").

It is SELF-CONTAINED: it reads the profile TOMLs and af_table JSONs directly
(tomllib / json) and does NOT import the stm32_auto_detect package. Field names
are taken verbatim from the pydantic schema in stm32_auto_detect/profile.py so
the generated struct mirrors it EXACTLY:

  - GpioPin            -> gpio_pin_t {port, pin}, sentinel {0xFF,0xFF} for None
  - AdcChannel         -> adc_chan_t {adc_unit, channel, on_pin, divider,
                          shunt_resistor_milliohms}   (names match schema)
  - PhaseOutputs       -> phase_out_t {timer_id, u_high..w_low, dead_time_ns}
  - IndicatorPins      -> led_red/led_green/led_orange/led_blue + buzzer
                          (ENUMERATED colors, NO single 'led')
  - InputPins          -> button(+active_low), charger(+active_low)
  - Sensing            -> battery_voltage, dc_link_voltage, left/right phase
                          current, mcu_temp_channel (schema
                          mcu_temperature_channel; 0xFF = unset)

Style: template-free string building (like codegen/klipper_config.py). No Jinja2.
"""

from __future__ import annotations

import hashlib
import io
import json
import tomllib
from pathlib import Path

# --------------------------------------------------------------------------
# CANONICAL FAMILY MAP — single source of truth.
#   ChipFamily enum value -> (BOARD_FAMILY C define, af_table json filename)
# AT32F4 reuses the STM32F1 bin (stm32f103 AF table + STM32F1 HAL/ld).
# --------------------------------------------------------------------------
FAMILY_MAP: dict[str, tuple[str, str]] = {
    "STM32F1": ("BOARD_FAMILY_STM32F1", "stm32f103.json"),
    "GD32F1": ("BOARD_FAMILY_GD32F1", "gd32f130.json"),
    "GD32E2": ("BOARD_FAMILY_GD32E2", "gd32e230.json"),
    "MM32SPIN0X": ("BOARD_FAMILY_MM32SPIN0X", "mm32spin0x.json"),
    # AT32F4 is binary/AF-compatible with STM32F1 and reuses its bin.
    "AT32F4": ("BOARD_FAMILY_STM32F1", "stm32f103.json"),
}

# All ChipFamily values that map onto a single BOARD_FAMILY token.
# Used both for filtering rows and for the bin-assertion.
FAMILY_DEFINE_TO_ENUMS: dict[str, list[str]] = {}
for _enum, (_define, _af) in FAMILY_MAP.items():
    FAMILY_DEFINE_TO_ENUMS.setdefault(_define, []).append(_enum)

# Timer string -> numeric id. TIMER0 is the GD32 alias for TIM1.
TIMER_ID_MAP: dict[str, int] = {
    "TIM1": 1,
    "TIM8": 8,
    "TIMER0": 1,
}

# Port letter -> index. A=0 .. H=7.
_PORT_INDEX = {ch: i for i, ch in enumerate("ABCDEFGH")}

GPIO_SENTINEL = "{0xFF, 0xFF}"

# Numeric ChipFamily enum values emitted into the table's `family` field.
# Mirrors the C enum board_family_t in the header.
_FAMILY_ENUM_VALUE = {
    "BOARD_FAMILY_STM32F1": 0,
    "BOARD_FAMILY_GD32F1": 1,
    "BOARD_FAMILY_GD32F3": 2,
    "BOARD_FAMILY_GD32E2": 3,
    "BOARD_FAMILY_MM32SPIN0X": 4,
}


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------
def _pin_to_c(pin_str: str | None) -> str:
    """Emit a gpio_pin_t initializer for a 'PA10'-style string or None."""
    if not pin_str:
        return GPIO_SENTINEL
    s = pin_str.strip().upper()
    if s.startswith("P"):
        s = s[1:]
    port_ch = s[0]
    if port_ch not in _PORT_INDEX:
        raise ValueError(f"Unknown GPIO port in {pin_str!r}")
    num = int(s[1:])
    if not (0 <= num <= 15):
        raise ValueError(f"GPIO pin out of range in {pin_str!r}")
    return f"{{{_PORT_INDEX[port_ch]}, {num}}}"


def _gpio_field(d: dict | None, key: str) -> str:
    """Pull a 'PA10' string out of a TOML table and convert."""
    if not d:
        return GPIO_SENTINEL
    return _pin_to_c(d.get(key))


def _timer_id(timer_str: str | None) -> int:
    if not timer_str:
        return 0
    if timer_str not in TIMER_ID_MAP:
        raise ValueError(f"Unknown timer string {timer_str!r}; not in TIMER_ID_MAP")
    return TIMER_ID_MAP[timer_str]


def _on_pin_to_c(on_pin) -> str:
    """on_pin may be a 'PA1' string or a {port,pin} dict (both seen in TOML)."""
    if on_pin is None:
        return GPIO_SENTINEL
    if isinstance(on_pin, str):
        return _pin_to_c(on_pin)
    if isinstance(on_pin, dict):
        port = on_pin["port"].upper()
        if port not in _PORT_INDEX:
            raise ValueError(f"Unknown port in on_pin {on_pin!r}")
        return f"{{{_PORT_INDEX[port]}, {int(on_pin['pin'])}}}"
    raise ValueError(f"Unexpected on_pin shape {on_pin!r}")


def _adc_to_c(adc: dict | None) -> str:
    """adc_chan_t initializer: {adc_unit, channel, on_pin, divider, shunt}."""
    if not adc:
        return "{0, 0xFF, {0xFF, 0xFF}, 0.0f, 0.0f}"
    adc_unit = int(adc.get("adc_unit", 1))
    channel = int(adc["channel"])
    on_pin = _on_pin_to_c(adc.get("on_pin"))
    divider = float(adc.get("divider") or 0.0)
    shunt = float(adc.get("shunt_resistor_milliohms") or 0.0)
    return f"{{{adc_unit}, {channel}, {on_pin}, {divider!r}f, {shunt!r}f}}"


def _halls_to_c(h: dict | None) -> str:
    return (
        f"{{{_gpio_field(h, 'hall_a')}, "
        f"{_gpio_field(h, 'hall_b')}, "
        f"{_gpio_field(h, 'hall_c')}}}"
    )


def _phases_to_c(p: dict | None) -> str:
    if not p:
        return "{0, " + ", ".join([GPIO_SENTINEL] * 6) + ", 0}"
    timer_id = _timer_id(p.get("timer"))
    pins = ", ".join(
        _gpio_field(p, k) for k in ("u_high", "u_low", "v_high", "v_low", "w_high", "w_low")
    )
    dead = int(p.get("dead_time_ns", 1000))
    return f"{{{timer_id}, {pins}, {dead}}}"


def _c_str(s: str | None, maxlen: int) -> str:
    """C string literal, truncated (without the terminating NUL) to maxlen-1."""
    s = (s or "")[: maxlen - 1]
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{s}"'


def _phases_has_pin(p: dict | None) -> bool:
    if not p:
        return False
    return any(p.get(k) for k in ("u_high", "u_low", "v_high", "v_low", "w_high", "w_low"))


def _fingerprint(profile: dict) -> int:
    """Truncated (32-bit) SHA-256 of the pin slots.

    Mirrors the `fingerprint` CLI command (stm32_auto_detect __main__.py:146):
    hashes only chip.family + the same ordered pin slots, ignoring meta. The
    firmware cannot self-compute this; it is host/CLI parity only.
    """
    items: list[tuple[str, str]] = []

    def add(slot: str, pin: str | None) -> None:
        items.append((slot, pin or ""))

    chip = profile.get("chip", {})
    fam = chip.get("family", "UNKNOWN")
    add("chip.family", None if fam == "UNKNOWN" else fam)
    add("power.self_hold", profile.get("power", {}).get("self_hold"))
    for side in ("left", "right"):
        h = profile.get(f"halls_{side}", {})
        for k in ("hall_a", "hall_b", "hall_c"):
            add(f"halls_{side}.{k}", h.get(k))
        p = profile.get(f"phases_{side}", {})
        for k in ("u_high", "u_low", "v_high", "v_low", "w_high", "w_low"):
            add(f"phases_{side}.{k}", p.get(k))
    ind = profile.get("indicators", {})
    for k in ("led_red", "led_green", "led_orange", "led_blue", "buzzer"):
        add(f"indicators.{k}", ind.get(k))
    inp = profile.get("inputs", {})
    add("inputs.button", inp.get("button"))
    add("inputs.charger_detect", inp.get("charger_detect"))

    canonical = "\n".join(f"{slot}={value}" for slot, value in sorted(items))
    digest = hashlib.sha256(canonical.encode("utf-8")).digest()
    return int.from_bytes(digest[:4], "big")


# --------------------------------------------------------------------------
# Profile loading + family binning
# --------------------------------------------------------------------------
def load_profiles(profiles_dir: str | Path) -> list[dict]:
    """Load every *.toml profile, sorted by filename for deterministic output.

    Each dict is annotated with `_path` (source filename stem)."""
    profiles_dir = Path(profiles_dir)
    out: list[dict] = []
    for path in sorted(profiles_dir.glob("*.toml")):
        data = tomllib.loads(path.read_text(encoding="utf-8"))
        data["_path"] = path.stem
        out.append(data)
    return out


def assert_all_binned(profiles: list[dict]) -> None:
    """Fail if any profile's chip.family is not in FAMILY_MAP."""
    unmapped = []
    for p in profiles:
        fam = p.get("chip", {}).get("family", "UNKNOWN")
        if fam not in FAMILY_MAP:
            unmapped.append((p.get("_path", "?"), fam))
    if unmapped:
        raise AssertionError(
            "Unmapped profiles (chip.family not in FAMILY_MAP): " + repr(unmapped)
        )


def filter_family(profiles: list[dict], family: str) -> list[dict]:
    """Rows whose ChipFamily bins to the BOARD_FAMILY define for `family`.

    `family` may be a ChipFamily token (e.g. 'STM32F1') or a BOARD_FAMILY
    define ('BOARD_FAMILY_STM32F1').
    """
    define = _resolve_define(family)
    enums = set(FAMILY_DEFINE_TO_ENUMS[define])
    return [p for p in profiles if p.get("chip", {}).get("family") in enums]


def _resolve_define(family: str) -> str:
    if family in FAMILY_MAP:
        return FAMILY_MAP[family][0]
    if family in FAMILY_DEFINE_TO_ENUMS:
        return family
    raise ValueError(f"Unknown family token {family!r}")


def _resolve_af_filename(family: str) -> str:
    """The af_table json filename for a family token or BOARD_FAMILY define."""
    if family in FAMILY_MAP:
        return FAMILY_MAP[family][1]
    if family in FAMILY_DEFINE_TO_ENUMS:
        # pick any enum that maps to this define; they share the af file
        enum = FAMILY_DEFINE_TO_ENUMS[family][0]
        return FAMILY_MAP[enum][1]
    raise ValueError(f"Unknown family token {family!r}")


# --------------------------------------------------------------------------
# Header: board_table.h
# --------------------------------------------------------------------------
def emit_c_board_table_header() -> str:
    out = io.StringIO()
    out.write("/* GENERATED by codegen/c_board_table.py — DO NOT EDIT. */\n")
    out.write("/* Mirrors the pydantic Profile schema in stm32_auto_detect/profile.py. */\n")
    out.write("#ifndef BOARD_TABLE_H\n#define BOARD_TABLE_H\n\n")
    out.write("#include <stdint.h>\n\n")

    out.write("/* ChipFamily enum (subset that maps to a BOARD_FAMILY bin). */\n")
    out.write("typedef enum {\n")
    out.write("    BOARD_FAMILY_STM32F1   = 0,\n")
    out.write("    BOARD_FAMILY_GD32F1    = 1,\n")
    out.write("    BOARD_FAMILY_GD32F3    = 2,\n")
    out.write("    BOARD_FAMILY_GD32E2    = 3,\n")
    out.write("    BOARD_FAMILY_MM32SPIN0X = 4,\n")
    out.write("} board_family_t;\n\n")

    out.write("/* port: 0=A .. 7=H; pin 0-15; sentinel {0xFF,0xFF} = unset. */\n")
    out.write("typedef struct { uint8_t port; uint8_t pin; } gpio_pin_t;\n\n")

    out.write("/* names match schema: shunt_resistor_milliohms, nested on_pin. */\n")
    out.write("typedef struct {\n")
    out.write("    uint8_t    adc_unit;\n")
    out.write("    uint8_t    channel;\n")
    out.write("    gpio_pin_t on_pin;\n")
    out.write("    float      divider;\n")
    out.write("    float      shunt_resistor_milliohms;\n")
    out.write("} adc_chan_t;\n\n")

    out.write("/* timer_id from string map: TIM1->1, TIM8->8, TIMER0->1 (GD32 alias). */\n")
    out.write("typedef struct {\n")
    out.write("    uint8_t    timer_id;\n")
    out.write("    gpio_pin_t u_high, u_low, v_high, v_low, w_high, w_low;\n")
    out.write("    uint16_t   dead_time_ns;\n")
    out.write("} phase_out_t;\n\n")

    out.write("typedef struct { gpio_pin_t hall_a, hall_b, hall_c; } halls_t;\n\n")

    out.write("typedef struct {\n")
    out.write("    uint8_t     family;          /* board_family_t */\n")
    out.write("    char        part_hint[24];   /* e.g. \"GD32F130C8T6\" — best-effort narrowing */\n")
    out.write("    char        uid[25];         /* RETAINED but EMPTY for all current rows; NOT a live selector */\n")
    out.write("    uint32_t    fingerprint;     /* SHA-256-trunc of pin slots; host/CLI parity only */\n")
    out.write("    uint8_t     motor_count;     /* 2 if phases_left present else 1 */\n")
    out.write("    gpio_pin_t  self_hold;\n")
    out.write("    uint8_t     self_hold_active_high;\n")
    out.write("    halls_t     halls_left, halls_right;\n")
    out.write("    phase_out_t phases_left, phases_right;   /* phases_left all-sentinel when single-motor */\n")
    out.write("    gpio_pin_t  led_red, led_green, led_orange, led_blue, buzzer;  /* ENUMERATED, no single 'led' */\n")
    out.write("    gpio_pin_t  button;\n")
    out.write("    uint8_t     button_active_low;\n")
    out.write("    gpio_pin_t  charger;\n")
    out.write("    uint8_t     charger_active_low;\n")
    out.write("    adc_chan_t  battery_voltage, dc_link_voltage, left_phase_current, right_phase_current;\n")
    out.write("    uint8_t     mcu_temp_channel; /* schema mcu_temperature_channel; 0xFF = unset */\n")
    out.write("    char        name[48];\n")
    out.write("} board_profile_t;\n\n")

    out.write("extern const board_profile_t g_board_table[];\n")
    out.write("extern const uint16_t        g_board_table_count;\n")
    out.write("extern const uint16_t        g_default_board_index;  /* points at a MOTOR-DISABLED safe row */\n\n")

    out.write("/* g_af_validity[] — pin -> legal {peripheral, channel, af} for the silicon-fixed check. */\n")
    out.write("typedef struct {\n")
    out.write("    gpio_pin_t pin;\n")
    out.write("    char       peripheral[12];\n")
    out.write("    char       channel[8];\n")
    out.write("    int16_t    af;       /* alternate-function number; -1 if not applicable (STM32F1 remap) */\n")
    out.write("} af_validity_t;\n\n")
    out.write("extern const af_validity_t g_af_validity[];\n")
    out.write("extern const uint16_t      g_af_validity_count;\n\n")

    out.write("/* Result enums for board_select / board_override (see runtime_override). */\n")
    out.write("typedef enum {\n")
    out.write("    BOARD_OVERRIDE_APPLIED_LIVE = 0,\n")
    out.write("    BOARD_OVERRIDE_PENDING_REBOOT,\n")
    out.write("    BOARD_OVERRIDE_REJECTED_SILICON,\n")
    out.write("    BOARD_OVERRIDE_REJECTED_CONFLICT,\n")
    out.write("    BOARD_OVERRIDE_REJECTED_EXTI,\n")
    out.write("} board_override_result_t;\n\n")

    out.write("typedef enum {\n")
    out.write("    BOARD_SELECT_DEFAULT_SAFE = 0,\n")
    out.write("    BOARD_SELECT_STORED_INDEX,\n")
    out.write("    BOARD_SELECT_SERIAL_FORCED,\n")
    out.write("} board_select_mode_t;\n\n")

    out.write("#endif /* BOARD_TABLE_H */\n")
    return out.getvalue()


# --------------------------------------------------------------------------
# Table: board_table_<family>.c
# --------------------------------------------------------------------------
def _emit_row(p: dict) -> str:
    chip = p.get("chip", {})
    fam = chip["family"]
    define = FAMILY_MAP[fam][0]
    fam_val = _FAMILY_ENUM_VALUE[define]

    power = p.get("power", {})
    ind = p.get("indicators", {})
    inp = p.get("inputs", {})
    sens = p.get("sensing", {})
    meta = p.get("meta", {})

    pl = p.get("phases_left")
    pr = p.get("phases_right")
    motor_count = 2 if _phases_has_pin(pl) else 1

    mcu_temp = sens.get("mcu_temperature_channel")
    mcu_temp_c = "0xFF" if mcu_temp is None else str(int(mcu_temp))

    o = io.StringIO()
    o.write("    {\n")
    o.write(f"        .family = {define},\n")
    o.write(f"        .part_hint = {_c_str(chip.get('part_hint'), 24)},\n")
    o.write(f"        .uid = {_c_str(chip.get('uid'), 25)},\n")
    o.write(f"        .fingerprint = 0x{_fingerprint(p):08X}u,\n")
    o.write(f"        .motor_count = {motor_count},\n")
    o.write(f"        .self_hold = {_gpio_field(power, 'self_hold')},\n")
    o.write(f"        .self_hold_active_high = {1 if power.get('self_hold_active_high', True) else 0},\n")
    o.write(f"        .halls_left = {_halls_to_c(p.get('halls_left'))},\n")
    o.write(f"        .halls_right = {_halls_to_c(p.get('halls_right'))},\n")
    o.write(f"        .phases_left = {_phases_to_c(pl)},\n")
    o.write(f"        .phases_right = {_phases_to_c(pr)},\n")
    o.write(f"        .led_red = {_gpio_field(ind, 'led_red')},\n")
    o.write(f"        .led_green = {_gpio_field(ind, 'led_green')},\n")
    o.write(f"        .led_orange = {_gpio_field(ind, 'led_orange')},\n")
    o.write(f"        .led_blue = {_gpio_field(ind, 'led_blue')},\n")
    o.write(f"        .buzzer = {_gpio_field(ind, 'buzzer')},\n")
    o.write(f"        .button = {_gpio_field(inp, 'button')},\n")
    o.write(f"        .button_active_low = {1 if inp.get('button_active_low', True) else 0},\n")
    o.write(f"        .charger = {_gpio_field(inp, 'charger_detect')},\n")
    o.write(f"        .charger_active_low = {1 if inp.get('charger_active_low', True) else 0},\n")
    o.write(f"        .battery_voltage = {_adc_to_c(sens.get('battery_voltage'))},\n")
    o.write(f"        .dc_link_voltage = {_adc_to_c(sens.get('dc_link_voltage'))},\n")
    o.write(f"        .left_phase_current = {_adc_to_c(sens.get('left_phase_current'))},\n")
    o.write(f"        .right_phase_current = {_adc_to_c(sens.get('right_phase_current'))},\n")
    o.write(f"        .mcu_temp_channel = {mcu_temp_c},\n")
    o.write(f"        .name = {_c_str(meta.get('name'), 48)},\n")
    o.write("    },\n")
    _ = fam_val  # family stored via define symbol; numeric value documented above
    return o.getvalue()


def _emit_safe_default_row(define: str = "BOARD_FAMILY_STM32F1") -> str:
    """A MOTOR-DISABLED safe default row: no phases, no halls, no IO.

    g_default_board_index points here so a fresh / unconfigured unit cannot
    energize wrong phase pins. motor_count=1 but both phase blocks are all
    sentinel so nothing drives. The .family is tagged with the TARGET family's
    define so boot logic that gates on row.family == BOARD_FAMILY sees a
    consistent value on the fallback row too.
    """
    o = io.StringIO()
    o.write("    {\n")
    o.write(f"        .family = {define},\n")
    o.write('        .part_hint = "",\n')
    o.write('        .uid = "",\n')
    o.write("        .fingerprint = 0x00000000u,\n")
    o.write("        .motor_count = 1,\n")
    o.write(f"        .self_hold = {GPIO_SENTINEL},\n")
    o.write("        .self_hold_active_high = 1,\n")
    o.write(f"        .halls_left = {_halls_to_c(None)},\n")
    o.write(f"        .halls_right = {_halls_to_c(None)},\n")
    o.write(f"        .phases_left = {_phases_to_c(None)},\n")
    o.write(f"        .phases_right = {_phases_to_c(None)},\n")
    o.write(f"        .led_red = {GPIO_SENTINEL},\n")
    o.write(f"        .led_green = {GPIO_SENTINEL},\n")
    o.write(f"        .led_orange = {GPIO_SENTINEL},\n")
    o.write(f"        .led_blue = {GPIO_SENTINEL},\n")
    o.write(f"        .buzzer = {GPIO_SENTINEL},\n")
    o.write(f"        .button = {GPIO_SENTINEL},\n")
    o.write("        .button_active_low = 1,\n")
    o.write(f"        .charger = {GPIO_SENTINEL},\n")
    o.write("        .charger_active_low = 1,\n")
    o.write(f"        .battery_voltage = {_adc_to_c(None)},\n")
    o.write(f"        .dc_link_voltage = {_adc_to_c(None)},\n")
    o.write(f"        .left_phase_current = {_adc_to_c(None)},\n")
    o.write(f"        .right_phase_current = {_adc_to_c(None)},\n")
    o.write("        .mcu_temp_channel = 0xFF,\n")
    o.write('        .name = "SAFE-DEFAULT (motor disabled)",\n')
    o.write("    },\n")
    return o.getvalue()


def emit_c_board_table(profiles: list[dict], family: str) -> str:
    """board_table_<family>.c: the family-filtered const g_board_table[].

    Row 0 is always the MOTOR-DISABLED safe default; g_default_board_index = 0.
    The family-matching profile rows follow.
    """
    assert_all_binned(profiles)
    define = _resolve_define(family)
    rows = filter_family(profiles, family)

    out = io.StringIO()
    out.write("/* GENERATED by codegen/c_board_table.py — DO NOT EDIT. */\n")
    out.write(f"/* Family bin: {define}.  Rows: {len(rows)} (+1 safe default). */\n")
    out.write('#include "board_table.h"\n\n')
    out.write("const board_profile_t g_board_table[] = {\n")
    out.write("    /* index 0: MOTOR-DISABLED safe default (g_default_board_index) */\n")
    out.write(_emit_safe_default_row(define))
    for p in rows:
        out.write(f"    /* {p.get('_path', '')} */\n")
        out.write(_emit_row(p))
    out.write("};\n\n")
    out.write("const uint16_t g_board_table_count = sizeof(g_board_table) / sizeof(g_board_table[0]);\n")
    out.write("const uint16_t g_default_board_index = 0u;  /* MOTOR-DISABLED safe row */\n")
    return out.getvalue()


# --------------------------------------------------------------------------
# AF validity: board_af_validity_<family>.c
# --------------------------------------------------------------------------
def _load_af_table(family: str, af_dir: str | Path) -> dict:
    fname = _resolve_af_filename(family)
    return json.loads((Path(af_dir) / fname).read_text(encoding="utf-8"))


def emit_c_af_validity(family: str, af_dir: str | Path) -> str:
    """board_af_validity_<family>.c: g_af_validity[] from the family af_table."""
    define = _resolve_define(family)
    table = _load_af_table(family, af_dir)

    out = io.StringIO()
    out.write("/* GENERATED by codegen/c_board_table.py — DO NOT EDIT. */\n")
    out.write(f"/* Silicon AF validity for {define} (from {_resolve_af_filename(family)}). */\n")
    out.write('#include "board_table.h"\n\n')
    out.write("const af_validity_t g_af_validity[] = {\n")

    for pin in sorted(k for k in table if k != "_meta"):
        pin_c = _pin_to_c(pin)
        for entry in table[pin]:
            peripheral = entry.get("peripheral", "")
            channel = entry.get("channel", "")
            # STM32F1 entries carry 'remap' not 'af'; map a missing af to -1.
            af = entry.get("af")
            af_c = str(int(af)) if af is not None else "-1"
            out.write(
                f"    {{{pin_c}, {_c_str(peripheral, 12)}, "
                f"{_c_str(channel, 8)}, {af_c}}},\n"
            )

    out.write("};\n\n")
    out.write("const uint16_t g_af_validity_count = sizeof(g_af_validity) / sizeof(g_af_validity[0]);\n")
    return out.getvalue()
