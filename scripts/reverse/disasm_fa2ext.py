# -*- coding: utf-8 -*-
"""Disassemble FA2Ext.dll key functions and extract hook target addresses.

The goal: understand what each FA2Ext hook does so we can re-implement it
in the open-source v2.0 MissionEditor codebase, eliminating the need for
Syringe + FA2Ext.dll entirely.
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

ROOT = Path(__file__).resolve().parents[2]
MO_DIR = ROOT / "心理终结地图编辑器"
FA2EXT = MO_DIR / "FA2Ext.dll"
FA2MO = MO_DIR / "fa2mo.dat"
OUT_DIR = ROOT / "scripts" / "reverse" / "out"
OUT_DIR.mkdir(parents=True, exist_ok=True)

# Export name -> RVA (from analyze step), we re-derive here
KEY_FUNCS = {
    "SyringeHandshake": 0x10001150,
    "MixFile_Open_CheckRAEncrypted": 0x10001ab0,
    "MixFile_Open_CheckRAUnencrypted": 0x10001aa0,
    "MixFile_Open_CheckTD": 0x10001a90,
    "ObjectBrowserControl_Redraw_Sides": 0x10001fc0,
    "ObjectBrowserControl_Redraw_X": 0x10002370,
    "CScriptTypes_OnInitDialog": 0x10004160,
    "sub_468760_RGBColor": 0x100011d0,
    "sub_479CE0_EvaINI": 0x10001700,
    "sub_479CE0_SoundINI": 0x100017a0,
    "sub_479CE0_ThemeINI": 0x10001840,
    "sub_43CE50": 0x100048f0,
    "sub_4463C0": 0x10003f90,
    "sub_4D6500_1": 0x10004270,
    "sub_4D6500_2": 0x100042e0,
    "sub_4D6A10": 0x100043c0,
    "sub_4D6A10_Label": 0x100048c0,
    "sub_4D75D0": 0x10004350,
    "sub_4D8AC0": 0x10003ef0,
    "?fnFA2Ext@@YAHXZ": 0x100011b0,
}


def va_to_offset(pe: pefile.PE, va: int) -> int:
    return pe.get_offset_from_rva(va - pe.OPTIONAL_HEADER.ImageBase)


def disasm_func(pe: pefile.PE, md: Cs, va_start: int, max_bytes: int = 400) -> list[dict]:
    """Disassemble starting at va_start until a ret/retn is hit or max_bytes reached."""
    off = va_to_offset(pe, va_start)
    raw = pe.__data__
    if isinstance(raw, memoryview):
        raw = bytes(raw)
    chunk = raw[off : off + max_bytes]
    insns = []
    for ins in md.disasm(chunk, va_start):
        insns.append({
            "addr": hex(ins.address),
            "bytes": ins.bytes.hex(),
            "mnemonic": ins.mnemonic,
            "op_str": ins.op_str,
        })
        # Stop at function-end rets (but not conditional rets that may be mid-function)
        if ins.mnemonic in ("ret", "retn") and "esp" not in ins.op_str:
            break
        if len(insns) > 80:
            break
    return insns


def extract_imm_refs(insns: list[dict]) -> list[str]:
    """Extract immediate addresses that look like fa2mo.dat refs (0x4xxxxx range)."""
    refs = set()
    for ins in insns:
        ops = ins["op_str"]
        # look for 0x4xxxxxx (fa2mo code/data) or 0x100xxxxx (FA2Ext own)
        import re
        for m in re.finditer(r"0x([0-9a-fA-F]{6,8})", ops):
            val = int(m.group(1), 16)
            if 0x400000 <= val <= 0x7FFFFF:
                refs.add(f"fa2mo:0x{val:08X}")
            elif 0x10000000 <= val <= 0x100FFFFF:
                refs.add(f"fa2ext:0x{val:08X}")
    return sorted(refs)


def main() -> None:
    pe = pefile.PE(str(FA2EXT), fast_load=False)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = False

    print("=== FA2Ext.dll base info ===")
    print(f"  ImageBase = 0x{pe.OPTIONAL_HEADER.ImageBase:08X}")
    print(f"  EntryPoint RVA = 0x{pe.OPTIONAL_HEADER.AddressOfEntryPoint:08X}")

    results = {"imagebase": hex(pe.OPTIONAL_HEADER.ImageBase), "functions": {}}

    print("\n=== Disassembling key functions ===")
    for name, va in KEY_FUNCS.items():
        try:
            insns = disasm_func(pe, md, va, max_bytes=600)
            refs = extract_imm_refs(insns)
            results["functions"][name] = {
                "va": hex(va),
                "instructions": insns,
                "fa2mo_refs": refs,
            }
            print(f"\n--- {name} @ {va:08X} ---")
            print(f"  fa2mo refs: {refs}")
            for ins in insns[:25]:
                print(f"  {ins['addr']}: {ins['bytes']:<12} {ins['mnemonic']} {ins['op_str']}")
            if len(insns) > 25:
                print(f"  ... ({len(insns) - 25} more insns)")
        except Exception as e:
            print(f"  {name}: ERROR {e}")
            results["functions"][name] = {"error": str(e)}

    # Save full disassembly
    out = OUT_DIR / "disasm.json"
    out.write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\nFull disassembly saved to {out}")

    # Also dump .data section to find hook registration table
    print("\n=== Searching .data for hook registration table ===")
    for s in pe.sections:
        sname = s.Name.rstrip(b"\x00").decode("ascii", "replace")
        print(f"  Section {sname}: VA=0x{s.VirtualAddress:08X} VSize=0x{s.Misc_VirtualSize:X} RawOff=0x{s.PointerToRawData:X}")

    # Syringe hooks are typically registered via exported functions that
    # Syringe calls. The "18 hooks" likely correspond to the non-C++ symbol
    # exports. Let's count: 24 exports - 5 C++ internal = 19, minus SyringeHandshake = 18. Matches!
    print("\n=== Hook count verification ===")
    real_hooks = [n for n in KEY_FUNCS if not n.startswith("??") and n != "?fnFA2Ext@@YAHXZ" and n != "SyringeHandshake"]
    print(f"  Real hook functions: {len(real_hooks)}")
    for h in real_hooks:
        print(f"    {h}")

    pe.close()


if __name__ == "__main__":
    main()
