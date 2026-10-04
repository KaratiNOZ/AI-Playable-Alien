# compinst.py <pid> <dump.txt> <rec hex> [before] [after] — записи пула CATHODE::CompositeInstance вокруг rec (по 0x40 байт:
# +0 GUID сущности-экземпляра в родительской составной, +4 GUID типа составной, +0x10 vtable CompositeInstance,
# +0x14 ручка, +0x18 номер). Имена — из дампа cathodedump (eyes.exe <уровень> "" > dump.txt). Только чтение.
import ctypes, struct, sys, re
from ctypes import wintypes
k32 = ctypes.WinDLL('kernel32')
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid, dump, rec = int(sys.argv[1]), sys.argv[2], int(sys.argv[3], 16)
before = int(sys.argv[4]) if len(sys.argv) > 4 else 8
after = int(sys.argv[5]) if len(sys.argv) > 5 else 8
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    k32.ReadProcessMemory(h, a, buf, n, ctypes.byref(got)); return buf.raw[:got.value]
def g2u(s):  # "AA-BB-CC-DD" -> uint32 (байты по порядку в памяти)
    return struct.unpack('<I', bytes(int(x, 16) for x in s.split('-')))[0]
ent, comp = {}, {}
for line in open(dump, encoding='utf-8', errors='replace'):
    m = re.match(r'== composite (.+) ([0-9A-F]{2}(?:-[0-9A-F]{2}){3})', line)
    if m:
        comp[g2u(m.group(2))] = m.group(1)
        continue
    m = re.match(r"\s+fn ([0-9A-F]{2}(?:-[0-9A-F]{2}){3}) '([^']*)'", line)
    if m:
        ent.setdefault(g2u(m.group(1)), m.group(2))
psapi = ctypes.WinDLL('psapi')
mods = (ctypes.c_void_p * 1)(); need = wintypes.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
CI = 0x013E196C - 0x400000 + mods[0]
for i in range(-before, after + 1):
    a = rec + i * 0x40
    d = rd(a, 0x40)
    if len(d) < 0x40:
        continue
    w = struct.unpack('<16I', d)
    if w[4] != CI:
        print('%08X: (not a CompositeInstance record) %08X %08X' % (a, w[0], w[1]))
        continue
    print('%08X%s %-34s %-48s idx %06X  +10 %08X +18 %08X +1c %08X  id? %08X' % (
        a, ' <<' if i == 0 else '   ', ent.get(w[0], '?%08X' % w[0])[:34], comp.get(w[1], '?%08X' % w[1])[-48:],
        w[6] & 0xffffff, w[8], w[10], w[11], w[9]))
