# -*- coding: utf-8 -*-
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "MissionEditor" / "MissionEditor.rc"
raw = p.read_bytes()
newline = "\r\n" if b"\r\n" in raw else "\n"
text = raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
old = 'FONT 8, "MS Shell Dlg 2", 0, 0, 1'
new = 'FONT 8, "MS Shell Dlg 2"'
count = text.count(old)
text = text.replace(old, new)
p.write_bytes(text.replace("\n", newline).encode("utf-8"))
print(f"replaced {count} FONT lines")
