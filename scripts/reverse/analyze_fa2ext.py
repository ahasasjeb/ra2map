# -*- coding: utf-8 -*-
"""Comprehensive static analysis of FA2Ext.dll and fa2mo.dat.

Extracts: PE info, exports, imports, sections, strings (ASCII+UTF16LE),
and locates the version-detection string "Found Final Alert 2 version 1.02".
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[2]
MO_DIR = ROOT / "心理终结地图编辑器"
FA2EXT = MO_DIR / "FA2Ext.dll"
FA2MO = MO_DIR / "fa2mo.dat"
OUT_DIR = ROOT / "scripts" / "reverse" / "out"
OUT_DIR.mkdir(parents=True, exist_ok=True)


def analyze(path: Path) -> dict:
    pe = pefile.PE(str(path), fast_load=False)
    info: dict = {"path": str(path.relative_to(ROOT)), "size": path.stat().st_size}

    # Header
    info["machine"] = hex(pe.FILE_HEADER.Machine)
    info["timestamp"] = hex(pe.FILE_HEADER.TimeDateStamp)
    info["entrypoint_rva"] = hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    info["entrypoint_va"] = hex(pe.OPTIONAL_HEADER.ImageBase + pe.OPTIONAL_HEADER.AddressOfEntryPoint)
    info["imagebase"] = hex(pe.OPTIONAL_HEADER.ImageBase)

    # Sections
    info["sections"] = [
        {
            "name": s.Name.rstrip(b"\x00").decode("ascii", "replace"),
            "va": hex(s.VirtualAddress),
            "vsize": hex(s.Misc_VirtualSize),
            "raw_off": hex(s.PointerToRawData),
            "raw_size": hex(s.SizeOfRawData),
            "chars": hex(s.Characteristics),
        }
        for s in pe.sections
    ]

    # Exports
    exports = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            name = exp.name.decode("ascii", "replace") if exp.name else f"Ord_{exp.ordinal}"
            exports.append({
                "name": name,
                "ordinal": exp.ordinal,
                "rva": hex(exp.address),
                "va": hex(pe.OPTIONAL_HEADER.ImageBase + exp.address),
            })
    info["exports"] = exports
    info["num_exports"] = len(exports)

    # Imports
    imports = []
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            dll = entry.dll.decode("ascii", "replace")
            funcs = []
            for imp in entry.imports:
                if imp.name:
                    funcs.append(imp.name.decode("ascii", "replace"))
                else:
                    funcs.append(f"Ord_{imp.ordinal}")
            imports.append({"dll": dll, "funcs": funcs})
    info["imports"] = imports

    pe.close()
    return info


def extract_strings(path: Path, min_len: int = 5) -> dict:
    data = path.read_bytes()
    # ASCII
    ascii_re = re.compile(rb"[\x20-\x7e]{%d,}" % min_len)
    ascii_strs = [m.group().decode("ascii") for m in ascii_re.finditer(data)]
    # UTF-16LE (Windows wide strings)
    utf16_re = re.compile(rb"(?:[\x20-\x7e]\x00){%d,}" % min_len)
    utf16_strs = [m.group().decode("utf-16-le") for m in utf16_re.finditer(data)]
    # Dedup preserving order
    seen = set()
    ascii_unique = []
    for s in ascii_strs:
        if s not in seen:
            seen.add(s)
            ascii_unique.append(s)
    seen = set()
    utf16_unique = []
    for s in utf16_strs:
        if s not in seen:
            seen.add(s)
            utf16_unique.append(s)
    return {"ascii": ascii_unique, "utf16": utf16_unique}


def find_string_xrefs(pe: pefile.PE, target: str) -> list[int]:
    """Find RVAs where a string is referenced (as data). Returns RVA list."""
    enc = target.encode("utf-16-le") + b"\x00\x00"
    data = open(pe.__data__).read() if False else None
    raw = Path(pe.__data__).read_bytes() if isinstance(pe.__data__, (str, bytes)) else None
    # pefile stores raw data internally; access via __data__
    try:
        raw = pe.__data__
        if isinstance(raw, memoryview):
            raw = bytes(raw)
    except AttributeError:
        raw = None
    if raw is None:
        return []
    rvas = []
    start = 0
    while True:
        idx = raw.find(enc, start)
        if idx < 0:
            break
        # convert file offset to RVA
        rva = pe.get_rva_from_offset(idx) if idx >= 0 else -1
        rvas.append({"file_off": hex(idx), "rva": hex(rva) if rva >= 0 else None, "encoding": "utf16"})
        start = idx + len(enc)
    # also try ASCII
    enc_a = target.encode("ascii") + b"\x00"
    start = 0
    while True:
        idx = raw.find(enc_a, start)
        if idx < 0:
            break
        rva = pe.get_rva_from_offset(idx) if idx >= 0 else -1
        rvas.append({"file_off": hex(idx), "rva": hex(rva) if rva >= 0 else None, "encoding": "ascii"})
        start = idx + len(enc_a)
    return rvas


def main() -> None:
    results: dict = {}

    print(f"=== Analyzing {FA2EXT.name} ===")
    ext_info = analyze(FA2EXT)
    results["fa2ext"] = ext_info
    print(f"  Machine={ext_info['machine']} TS={ext_info['timestamp']} EP={ext_info['entrypoint_rva']}")
    print(f"  Exports: {ext_info['num_exports']}")
    for e in ext_info["exports"][:70]:
        print(f"    {e['ordinal']:>3} {e['name']} @ {e['va']}")

    print(f"\n=== Analyzing {FA2MO.name} ===")
    mo_info = analyze(FA2MO)
    results["fa2mo"] = mo_info
    print(f"  Machine={mo_info['machine']} TS={mo_info['timestamp']} EP={mo_info['entrypoint_rva']}")
    print(f"  Exports: {mo_info['num_exports']}")

    print("\n=== Extracting strings ===")
    ext_strs = extract_strings(FA2EXT)
    mo_strs = extract_strings(FA2MO)
    results["fa2ext_strings"] = ext_strs
    results["fa2mo_strings"] = mo_strs
    print(f"  FA2Ext: {len(ext_strs['ascii'])} ASCII, {len(ext_strs['utf16'])} UTF16 strings")
    print(f"  fa2mo:  {len(mo_strs['ascii'])} ASCII, {len(mo_strs['utf16'])} UTF16 strings")

    # Locate version detection string
    print("\n=== Locating version-detection strings ===")
    pe_ext = pefile.PE(str(FA2EXT), fast_load=True)
    for target in [
        "Found Final Alert 2 version 1.02",
        "Applying FA2Ext",
        "Final Alert 2",
        "version 1.02",
    ]:
        refs = find_string_xrefs(pe_ext, target)
        if refs:
            print(f"  {target!r}: {refs}")
    pe_ext.close()

    # Filter interesting strings from FA2Ext
    print("\n=== Interesting FA2Ext strings (filtered) ===")
    interesting_patterns = [
        r"FA2Ext", r"Final\s*Alert", r"version", r"1\.02", r"hook", r"Hook",
        r"Patch", r"Apply", r"fadata", r"FAData", r"Sides", r"KeepDefault",
        r"Script", r"Event", r"Action", r"Track", r"Tunnel", r"SHP",
        r"\.ini", r"\.dll", r"\.exe", r"Mental", r"Omega",
    ]
    pat = re.compile("|".join(f"({p})" for p in interesting_patterns), re.IGNORECASE)
    interesting = [s for s in ext_strs["ascii"] + ext_strs["utf16"] if pat.search(s)]
    seen = set()
    for s in interesting:
        if s not in seen:
            seen.add(s)
            print(f"  {s!r}")
    results["fa2ext_interesting"] = sorted(seen)

    out = OUT_DIR / "analysis.json"
    out.write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nFull analysis written to {out}")

    # Also write strings to separate files for grepping
    (OUT_DIR / "fa2ext_ascii.txt").write_text("\n".join(ext_strs["ascii"]), encoding="utf-8")
    (OUT_DIR / "fa2ext_utf16.txt").write_text("\n".join(ext_strs["utf16"]), encoding="utf-8")
    (OUT_DIR / "fa2mo_ascii.txt").write_text("\n".join(mo_strs["ascii"]), encoding="utf-8")
    (OUT_DIR / "fa2mo_utf16.txt").write_text("\n".join(mo_strs["utf16"]), encoding="utf-8")
    print("String dumps written to scripts/reverse/out/")


if __name__ == "__main__":
    main()
