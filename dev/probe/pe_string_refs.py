"""Find raw x86 string references in a local PE, including unanalyzed code."""
import runpy
import struct
import sys
from pathlib import Path

g = runpy.run_path(str(Path(__file__).with_name('pe_light_types.py')))
b, va = g['b'], g['va']
for name in sys.argv[1:]:
    start = 0
    while (s := b.find(name.encode('ascii') + b'\0', start)) >= 0:
        start = s + 1
        print(name, f'string {va(s):08x}')
        pos = 0
        pat = struct.pack('<I', va(s))
        while (r := b.find(pat, pos)) >= 0:
            pos = r + 1
            if 0x400000 <= va(r) < 0x11b0000:
                boundary = b.rfind(b'\xcc\xcc', max(0, r - 40000), r)
                f = boundary + 2
                while b[f] == 0xcc:
                    f += 1
                print(f'  ref {va(r):08x} possible entry {va(f):08x}')
