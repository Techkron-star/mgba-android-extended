#!/usr/bin/env python3
"""Pad a locally assembled original test ROM and fix its header checksum."""
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
rom = bytearray(path.read_bytes())
assert rom[3] == 0xEA
assert rom[0xA0:0xB0] == b"GBAX RAM64  GBAX"
assert rom[0xB2] == 0x96
assert len(rom) <= 32768
rom.extend(bytes(32768-len(rom)))
rom[0xBD] = (-sum(rom[0xA0:0xBD])-0x19) & 0xFF
path.write_bytes(rom)
print(f"Original GBAX diagnostic ROM: {len(rom)} bytes")
