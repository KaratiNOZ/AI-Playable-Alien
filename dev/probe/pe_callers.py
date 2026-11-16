"""Offline direct x86 callers in the local Ghidra executable copy."""
import contextlib
import io
import re
import runpy
import struct
import sys
from pathlib import Path
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path(str(Path(__file__).with_name('pe_light_types.py')))
b, va = g['b'], g['va']
for target in sys.argv[1:]:
    destination = int(target, 16)
    print('CALLERS', target)
    for m in re.finditer(b'\xe8', b):
        i = m.start()
        if i + 5 > len(b):
            continue
        address = va(i)
        if address and address + 5 + struct.unpack_from('<i', b, i + 1)[0] == destination:
            boundary = b.rfind(b'\xcc\xcc', max(0, i - 30000), i) + 2
            while b[boundary] == 0xcc:
                boundary += 1
            print(f'{address:08x} possible-entry {va(boundary):08x}')
