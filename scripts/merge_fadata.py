# -*- coding: utf-8 -*-
"""Merge MO-specific FAData.ini sections into the v2.0 FAData.ini.

The v2.0 source already supports most FAData sections (IgnoreRA2, EventsRA2,
ActionsRA2, Debug, etc.). MO adds: [Sides] (extended factions), [FA2Ext],
[ScriptsRA2], [VehicleVoxelTurrets], [BuildingVoxelTurrets], [UseSetNEWURBAN],
[UseSetLUNAR], [UseSetDESERT], [Filenames], and a few others.

This script merges those MO-only sections into the v2.0 FAData.ini without
destroying v2.0's own additions (ForceIsoPalettePrefix, etc.).
"""
import re
from pathlib import Path

ROOT = Path(r"E:\zaxiang6\CNC_TS_and_RA2_Mission_Editor")
MO_DATA = ROOT / "心理终结地图编辑器" / "fadata.ini"
V20_DATA = ROOT / "MissionEditor" / "data" / "FinalAlert2" / "FAData.ini"
OUT = ROOT / "dist" / "FinalAlert2YR" / "FAData.ini"
MO_OUT = ROOT / "心理终结地图编辑器" / "fadata.ini"


def parse_sections(text: str) -> dict[str, str]:
    """Parse ini into ordered dict of section_name -> raw section text (including header)."""
    sections: dict[str, str] = {}
    current_name = None
    current_lines: list[str] = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]") and "\n" not in stripped:
            if current_name is not None:
                sections[current_name] = "\n".join(current_lines)
            current_name = stripped
            current_lines = [line]
        else:
            if current_name is not None:
                current_lines.append(line)
            # else: pre-section content (comments) - ignore for merge
    if current_name is not None:
        sections[current_name] = "\n".join(current_lines)
    return sections


def main() -> None:
    mo_text = MO_DATA.read_text(encoding="utf-8-sig")
    v20_text = V20_DATA.read_text(encoding="utf-8-sig")

    mo_secs = parse_sections(mo_text)
    v20_secs = parse_sections(v20_text)

    print(f"MO sections: {len(mo_secs)}")
    print(f"v2.0 sections: {len(v20_secs)}")

    # Sections to merge from MO (that v2.0 doesn't have, or MO has extra data)
    mo_only = [s for s in mo_secs if s not in v20_secs]
    print(f"\nMO-only sections to add: {mo_only}")

    # Also check [Sides] - MO has extended sides, v2.0 doesn't have this section
    # For sections that exist in both, prefer v2.0's version (it may have v2.0-specific data)
    # but for [Sides], [FA2Ext], [ScriptsRA2] we MUST use MO's version

    # Build merged: start with v2.0, then append MO-only sections
    merged_lines = [v20_text.rstrip()]

    for sec_name in mo_only:
        print(f"  Adding: {sec_name}")
        merged_lines.append("")
        merged_lines.append(mo_secs[sec_name].strip())

    merged = "\n".join(merged_lines) + "\n"

    # Write as UTF-8 with BOM
    OUT.write_bytes(b"\xef\xbb\xbf" + merged.encode("utf-8"))
    print(f"\nMerged FAData.ini written to {OUT}")
    print(f"  Size: {OUT.stat().st_size} bytes")

    # Also copy to MO directory
    MO_OUT.write_bytes(b"\xef\xbb\xbf" + merged.encode("utf-8"))
    print(f"Also written to {MO_OUT}")


if __name__ == "__main__":
    main()
