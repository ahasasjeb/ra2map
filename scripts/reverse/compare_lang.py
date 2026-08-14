# -*- coding: utf-8 -*-
"""Compare MO vs v2.0 FALanguage.ini to diagnose language/font issues."""
import sys

files = {
    "MO (converted)": r"E:\zaxiang6\CNC_TS_and_RA2_Mission_Editor\心理终结地图编辑器\falanguage.ini",
    "dist v2.0": r"E:\zaxiang6\CNC_TS_and_RA2_Mission_Editor\dist\FinalAlert2YR\FALanguage.ini",
    "git original": r"E:\zaxiang6\CNC_TS_and_RA2_Mission_Editor\MissionEditor\data\FinalAlert2\FALanguage.ini",
}

for label, path in files.items():
    print(f"\n=== {label} ===")
    print(f"Path: {path}")
    try:
        raw = open(path, "rb").read()
    except FileNotFoundError:
        print("  NOT FOUND")
        continue
    # detect encoding
    if raw.startswith(b"\xef\xbb\xbf"):
        enc = "utf-8-sig"
        print("  Encoding: UTF-8 with BOM")
    elif raw.startswith(b"\xff\xfe"):
        enc = "utf-16-le"
        print("  Encoding: UTF-16LE")
    else:
        # try utf-8 then gbk
        try:
            raw.decode("utf-8")
            enc = "utf-8"
            print("  Encoding: UTF-8 (no BOM)")
        except:
            enc = "gbk"
            print("  Encoding: GBK/GB2312 (ANSI)")
    text = raw.decode(enc if enc != "utf-8-sig" else "utf-8")
    lines = text.splitlines()
    secs = [l for l in lines if l.startswith("[")]
    print(f"  Total lines: {len(lines)}, sections: {len(secs)}")
    has_chinese = "[Chinese-Strings]" in text
    has_languages = "[Languages]" in text
    print(f"  Has [Chinese-Strings]: {has_chinese}")
    print(f"  Has [Languages]: {has_languages}")
    if has_languages:
        for l in lines:
            if l.startswith("[Languages]"):
                idx = lines.index(l)
                print(f"  [Languages] block:")
                for ll in lines[idx:idx+6]:
                    print(f"    {ll}")
                break
    # Check font references
    font_lines = [l for l in lines if "FONT" in l.upper() and "=" in l]
    if font_lines:
        print(f"  Font lines: {len(font_lines)}")
        for fl in font_lines[:3]:
            print(f"    {fl}")
