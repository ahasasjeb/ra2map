# -*- coding: utf-8 -*-
"""Find the CRC algorithm used by Mental Omega's expandmo99.mix."""
import binascii
import struct

path = r'D:\Program Files (x86)\Mental Omega\expandmo99.mix'
with open(path, 'rb') as f:
    data = f.read()

flags, count, body = struct.unpack_from('<IHI', data, 0)
entries = []
off = 10
for i in range(count):
    fid, foff, fsize = struct.unpack_from('<III', data, off)
    entries.append((fid, foff, fsize, i))
    off += 12

names = ['rulesmo.ini', 'artmo.ini', 'aimo.ini', 'evamo.ini', 'thememo.ini', 'soundmo.ini',
         'Rulesmo.ini', 'RULESMO.INI', 'RulesMO.ini']

entry_ids = {e[0] for e in entries}
print("All entry IDs:", [hex(e[0]) for e in entries])

for n in names:
    crcs = {
        'upper': binascii.crc32(n.upper().encode()) & 0xFFFFFFFF,
        'lower': binascii.crc32(n.lower().encode()) & 0xFFFFFFFF,
        'as-is': binascii.crc32(n.encode()) & 0xFFFFFFFF,
    }
    found = None
    for algo, crc in crcs.items():
        if crc in entry_ids:
            for fid, foff, fsize, idx in entries:
                if fid == crc:
                    found = (algo, idx, foff, fsize)
                    break
            break
    if found:
        print(f"{n}: FOUND via {found[0]} CRC, entry {found[1]}, off={found[2]}, size={found[3]}")
    else:
        print(f"{n}: NOT found. upper={crcs['upper']:#010x} lower={crcs['lower']:#010x} as-is={crcs['as-is']:#010x}")

# The body_size issue: MO sets body_size=25903 but real data is 11MB.
# XCC library likely rejects entries whose offset+size > body_size.
# Let's check which entries are "valid" per body_size
print("\n--- Entry validity per body_size ---")
for fid, foff, fsize, idx in entries:
    valid = (foff + fsize <= body) and fsize != 0xFFFFFFFF
    print(f"  entry {idx}: id={fid:#010x} off={foff} size={fsize} valid={valid}")
