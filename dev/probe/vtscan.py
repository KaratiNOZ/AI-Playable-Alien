# vtscan.py <pid> <vtVA hex>[,<vtVA>...] [max] — все объекты в памяти игры с такой vtable (поиск dword по всей куче)
import ctypes, struct, sys, re
from ctypes import wintypes
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)  # база — из лога мода
R = lambda va: va - 0x400000 + base
vts = [R(int(x, 16)) for x in sys.argv[2].split(',')]
mx = int(sys.argv[3]) if len(sys.argv) > 3 else 50
h = k32.OpenProcess(0x0410, False, pid)
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', wintypes.DWORD),
                ('RegionSize', ctypes.c_size_t), ('State', wintypes.DWORD), ('Protect', wintypes.DWORD), ('Type', wintypes.DWORD)]
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got))
    return buf.raw[:got.value] if ok or got.value else None
a = 0x10000
pats = {struct.pack('<I', v): v for v in vts}
hits = {v: [] for v in vts}
m = MBI()
while a < 0x7fff0000:
    if not k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)): break
    size = m.RegionSize
    if m.State == 0x1000 and m.Protect in (0x04, 0x02, 0x40, 0x20) and m.Type in (0x20000, 0x1000000, 0x40000):  # RW: куча, образ, mapped
        for o in range(0, size, 1 << 20):
            chunk = rd(a + o, min(1 << 20, size - o))
            if not chunk: continue
            for p, v in pats.items():
                i = chunk.find(p)
                while i >= 0:
                    if i % 4 == 0 and len(hits[v]) < mx: hits[v].append(a + o + i)
                    i = chunk.find(p, i + 4)
    a += size
for v, lst in hits.items():
    print('vt %08X (VA %08X): %d' % (v, v + 0x400000 - base, len(lst)))
    for x in lst: print('  %08X' % x)
