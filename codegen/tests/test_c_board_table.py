"""Host unit tests for the board-table codegen (parallels the plan's test_plan).

Self-contained: reads the bundled profiles/*.toml + af_tables/*.json directly,
never importing stm32_auto_detect.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from codegen.c_board_table import (
    FAMILY_MAP,
    assert_all_binned,
    emit_c_af_validity,
    emit_c_board_table,
    emit_c_board_table_header,
    filter_family,
    load_profiles,
)

REPO = Path(__file__).resolve().parents[2]
PROFILES_DIR = REPO / "profiles"
AF_DIR = REPO / "codegen" / "data" / "af_tables"


@pytest.fixture(scope="module")
def profiles() -> list[dict]:
    return load_profiles(PROFILES_DIR)


def _row_block(table_c: str, path_stem: str) -> str:
    """Return the C initializer block whose preceding comment names path_stem."""
    marker = f"/* {path_stem} */"
    idx = table_c.index(marker)
    start = table_c.index("{", idx)
    # Find matching close of this row by scanning braces.
    depth = 0
    for i in range(start, len(table_c)):
        if table_c[i] == "{":
            depth += 1
        elif table_c[i] == "}":
            depth -= 1
            if depth == 0:
                return table_c[start : i + 1]
    raise AssertionError("unbalanced braces")


def _field(block: str, name: str) -> str:
    m = re.search(rf"\.{name}\s*=\s*(.+?),\n", block)
    assert m, f"field {name} not found in row block"
    return m.group(1).strip()


# --------------------------------------------------------------------------
# FAMILY_MAP bin assertion: all 24 profiles bin to exactly one family.
# --------------------------------------------------------------------------
def test_all_24_profiles_bin_exactly_once(profiles):
    assert len(profiles) == 24
    assert_all_binned(profiles)  # raises if any unmapped
    # Each profile bins to exactly one BOARD_FAMILY define.
    defines = {p["chip"]["family"]: FAMILY_MAP[p["chip"]["family"]][0] for p in profiles}
    for p in profiles:
        fam = p["chip"]["family"]
        assert fam in FAMILY_MAP, f"{p['_path']} family {fam} unmapped"
    # Count per define matches expectation (safe-default row added at emit time).
    counts = {}
    for p in profiles:
        d = FAMILY_MAP[p["chip"]["family"]][0]
        counts[d] = counts.get(d, 0) + 1
    assert counts["BOARD_FAMILY_STM32F1"] == 3
    assert counts["BOARD_FAMILY_GD32F1"] == 17
    assert counts["BOARD_FAMILY_GD32E2"] == 3
    assert counts["BOARD_FAMILY_MM32SPIN0X"] == 1
    _ = defines


def test_unmapped_family_fails():
    bad = [{"_path": "bogus", "chip": {"family": "LKS32"}}]
    with pytest.raises(AssertionError):
        assert_all_binned(bad)


# --------------------------------------------------------------------------
# efru-v1 round-trip (STM32F1, dual motor).
# --------------------------------------------------------------------------
def test_efru_v1_round_trip(profiles):
    table = emit_c_board_table(profiles, "STM32F1")
    block = _row_block(table, "efru-hoverboard-firmware-hack-v1")
    # buzzer = PC13 -> {2, 13}.
    assert _field(block, "buzzer") == "{2, 13}"
    # led_green = PB2 -> {1, 2} (actual TOML; the buzzer is the PC13 indicator).
    assert _field(block, "led_green") == "{1, 2}"
    # dual-motor: phases_left present.
    assert _field(block, "motor_count") == "2"
    # phases_left timer TIM8 -> 8; phases_right TIM1 -> 1.
    assert _field(block, "phases_left").startswith("{8,")
    assert _field(block, "phases_right").startswith("{1,")


# --------------------------------------------------------------------------
# GD32F130 row (GD32F1, single motor, TIMER0->1, dead_time 833).
# --------------------------------------------------------------------------
def test_gd32f130_row(profiles):
    table = emit_c_board_table(profiles, "GD32F1")
    block = _row_block(table, "robodurden-gen2x-gd32f130-layout1")
    assert _field(block, "motor_count") == "1"
    # single motor: phases_left all-sentinel (timer id 0).
    assert _field(block, "phases_left").startswith("{0,")
    # phases_right uses TIMER0 -> id 1 and dead_time_ns 833.
    pr = _field(block, "phases_right")
    assert pr.startswith("{1,")
    assert pr.rstrip("}").strip().endswith("833")
    # PF1 hall -> port F = 5.
    assert _field(block, "halls_right") == "{{1, 11}, {5, 1}, {2, 14}}"


def test_gd32f130_shunt_and_on_pin(profiles):
    table = emit_c_board_table(profiles, "GD32F1")
    block = _row_block(table, "robodurden-gen2x-gd32f130-layout1")
    # battery_voltage: adc_unit 1, channel 4, on_pin PA4 -> {0,4}, divider 13.0.
    bv = _field(block, "battery_voltage")
    assert bv.startswith("{1, 4, {0, 4}, 13.0f")
    # left_phase_current: channel 6, on_pin PA6 -> {0,6}, shunt 1.0 mOhm.
    lpc = _field(block, "left_phase_current")
    assert lpc.startswith("{1, 6, {0, 6},")
    assert lpc.rstrip("}").strip().endswith("1.0f")


# --------------------------------------------------------------------------
# GD32E2 row + bin isolation.
# --------------------------------------------------------------------------
def test_gd32e2_row_present(profiles):
    table = emit_c_board_table(profiles, "GD32E2")
    block = _row_block(table, "robodurden-gen2x-gd32e230-layout1")
    assert _field(block, "motor_count") == "1"
    # reversed layout: u_high = PB13 -> {1,13}.
    pr = _field(block, "phases_right")
    assert pr.startswith("{1, {1, 13},")
    assert _field(block, "family") == "BOARD_FAMILY_GD32E2"


def test_gd32e2_rows_only_in_gd32e2_emit(profiles):
    gd32e2 = emit_c_board_table(profiles, "GD32E2")
    # The 3 gd32e230 rows appear here.
    for n in (1, 2, 3):
        assert f"/* robodurden-gen2x-gd32e230-layout{n} */" in gd32e2
    # 3 gd32e230 profile rows + the index-0 safe-default row (now tagged with
    # the target family so boot logic can gate on row.family == BOARD_FAMILY).
    assert gd32e2.count(".family = BOARD_FAMILY_GD32E2") == 4

    # And appear in NO other family emit.
    for fam in ("STM32F1", "GD32F1", "MM32SPIN0X"):
        other = emit_c_board_table(profiles, fam)
        assert "gd32e230" not in other
        assert ".family = BOARD_FAMILY_GD32E2" not in other


def test_family_filter_counts(profiles):
    assert len(filter_family(profiles, "STM32F1")) == 3
    assert len(filter_family(profiles, "GD32F1")) == 17
    assert len(filter_family(profiles, "GD32E2")) == 3
    assert len(filter_family(profiles, "MM32SPIN0X")) == 1


# --------------------------------------------------------------------------
# Safe default + structural invariants.
# --------------------------------------------------------------------------
def test_default_index_is_motor_disabled_safe_row(profiles):
    table = emit_c_board_table(profiles, "STM32F1")
    assert "g_default_board_index = 0u" in table
    # Row 0 is the safe default: name present, all phases sentinel.
    assert "SAFE-DEFAULT (motor disabled)" in table
    safe = table[: table.index("/* efru")]
    # both phase blocks all-sentinel -> timer id 0, six {0xFF,0xFF}.
    assert safe.count("{0xFF, 0xFF}") >= 12


def test_header_has_enumerated_leds_and_no_single_led():
    h = emit_c_board_table_header()
    for f in ("led_red", "led_green", "led_orange", "led_blue", "buzzer"):
        assert f in h
    # No standalone single 'led' field.
    assert not re.search(r"gpio_pin_t\s+led\s*;", h)
    # schema-faithful names present.
    assert "shunt_resistor_milliohms" in h
    assert "mcu_temp_channel" in h
    assert "self_hold_active_high" in h
    assert "charger_active_low" in h


# --------------------------------------------------------------------------
# af_validity.
# --------------------------------------------------------------------------
def test_af_validity_gd32f1():
    c = emit_c_af_validity("GD32F1", AF_DIR)
    assert "g_af_validity[]" in c
    # PA8 -> TIMER0 CH0 af 2.
    assert '{{0, 8}, "TIMER0", "CH0", 2}' in c


def test_af_validity_stm32f1_remap_has_minus_one_af():
    # STM32F1 entries carry 'remap', not 'af'; emit -1 for af.
    c = emit_c_af_validity("STM32F1", AF_DIR)
    assert '{{0, 8}, "TIM1", "CH1", -1}' in c
