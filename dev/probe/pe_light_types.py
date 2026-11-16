"""Offline MSVC x86 RTTI lookup in the local analysis executable."""
import re
import struct
import sys
from pathlib import Path

b = (Path(__file__).resolve().parents[2] / 'build-ghidra/AI.exe').read_bytes()
u16 = lambda o: struct.unpack_from('<H', b, o)[0]
u32 = lambda o: struct.unpack_from('<I', b, o)[0]
pe = u32(0x3c)
opt = pe + 24
base = u32(opt + 28)
sections = []
for i in range(u16(pe + 6)):
    h = opt + u16(pe + 20) + i * 40
    sections.append((u32(h + 20), u32(h + 16), u32(h + 12)))

def va(o):
    return next((base + rva + o - raw for raw, size, rva in sections if raw <= o < raw + size), 0)

def refs(v):
    p = struct.pack('<I', v)
    return [m.start() for m in re.finditer(re.escape(p), b) if m.start() % 4 == 0]

for m in re.finditer(rb'\.\?AV[^\x00]{1,160}\x00', b):
    name = m.group()[:-1].decode('ascii', errors='replace')
    if not any(x.lower() in name.lower() for x in (sys.argv[1:] or ['light', 'torch'])):
        continue
    td = va(m.start() - 8)
    tables = set()
    for r in refs(td):
        col = r - 12
        if col >= 0 and u32(col) == 0 and u32(col + 4) < 0x10000:
            tables.update(va(p + 4) for p in refs(va(col)))
    if tables:
        print(name, ' '.join(f'{v:08x}' for v in sorted(tables)))
