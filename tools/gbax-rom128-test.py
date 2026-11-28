#!/usr/bin/env python3
"""Build an original sparse 128 MiB data cart for native CI."""
import pathlib
import struct
import sys

source=pathlib.Path(sys.argv[1]).read_bytes()
assert source[0xa0:0xb0]==b"GBAX RAM64  GBAX"
with open(sys.argv[2],"wb") as f:
    f.write(source)
    f.truncate(0x08000000)
    for offset,value in [(0x01fffffc,0x11223344),(0x02000000,0x55667788),
                         (0x04000000,0x99aabbcc),(0x06000000,0xdeadbeef),
                         (0x07fffffc,0x12345678)]:
        f.seek(offset)
        f.write(struct.pack("<I",value))
