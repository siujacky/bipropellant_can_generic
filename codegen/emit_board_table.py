"""CLI: emit the generated C board table for one (or all) chip families.

Usage:
    python -m codegen.emit_board_table --family STM32F1 \
        --profiles ../profiles --out ../generated
    python -m codegen.emit_board_table --all-families \
        --profiles ../profiles --out ../generated

Writes, into <out>/:
    board_table.h                       (family-independent struct defs; once)
    board_table_<family>.c              (family-filtered g_board_table[])
    board_af_validity_<family>.c        (g_af_validity[] from the af_table)

Reads profiles/*.toml + codegen/data/af_tables/*.json directly (no
stm32_auto_detect import). Template-free, mirrors the codebase style.
"""

from __future__ import annotations

import argparse
from pathlib import Path

try:  # allow `python -m codegen.emit_board_table` and direct execution
    from codegen.c_board_table import (
        FAMILY_MAP,
        assert_all_binned,
        emit_c_af_validity,
        emit_c_board_table,
        emit_c_board_table_header,
        load_profiles,
    )
except ModuleNotFoundError:  # pragma: no cover - direct-script fallback
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from codegen.c_board_table import (  # type: ignore[no-redef]
        FAMILY_MAP,
        assert_all_binned,
        emit_c_af_validity,
        emit_c_board_table,
        emit_c_board_table_header,
        load_profiles,
    )

# The four firmware bins (deduped BOARD_FAMILY targets). AT32F4 folds into
# STM32F1, so we drive codegen off the ChipFamily token whose bin is unique.
TARGET_FAMILIES = ["STM32F1", "GD32F1", "GD32E2", "MM32SPIN0X"]


def _family_tag(family: str) -> str:
    """Lowercase filename tag for a family token, e.g. 'STM32F1' -> 'stm32f1'."""
    return family.lower()


def emit_family(family: str, profiles: list[dict], af_dir: Path, out_dir: Path) -> list[Path]:
    written: list[Path] = []
    tag = _family_tag(family)

    table_c = emit_c_board_table(profiles, family)
    table_path = out_dir / f"board_table_{tag}.c"
    table_path.write_text(table_c, encoding="utf-8")
    written.append(table_path)

    af_c = emit_c_af_validity(family, af_dir)
    af_path = out_dir / f"board_af_validity_{tag}.c"
    af_path.write_text(af_c, encoding="utf-8")
    written.append(af_path)

    return written


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Emit generated C board table(s).")
    ap.add_argument("--family", help="Chip family token, e.g. STM32F1.")
    ap.add_argument("--all-families", action="store_true", help="Emit all four families.")
    ap.add_argument("--profiles", required=True, help="Directory of *.toml profiles.")
    ap.add_argument(
        "--af-tables",
        default=None,
        help="Directory of af_table JSONs (default: <this>/data/af_tables).",
    )
    ap.add_argument("--out", required=True, help="Output directory for generated C.")
    args = ap.parse_args(argv)

    if not args.family and not args.all_families:
        ap.error("specify --family <F> or --all-families")
    if args.family and args.family not in FAMILY_MAP:
        ap.error(f"unknown --family {args.family!r}; known: {sorted(FAMILY_MAP)}")

    profiles_dir = Path(args.profiles)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    af_dir = (
        Path(args.af_tables)
        if args.af_tables
        else Path(__file__).resolve().parent / "data" / "af_tables"
    )

    profiles = load_profiles(profiles_dir)
    assert_all_binned(profiles)  # FAIL the build if any of the 24 is unmapped

    # board_table.h is family-independent — write it once.
    header_path = out_dir / "board_table.h"
    header_path.write_text(emit_c_board_table_header(), encoding="utf-8")
    written = [header_path]

    families = TARGET_FAMILIES if args.all_families else [args.family]
    for fam in families:
        written += emit_family(fam, profiles, af_dir, out_dir)

    for p in written:
        print(p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
