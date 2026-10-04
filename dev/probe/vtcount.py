# vtcount.py <pid> <vtVA...> — сколько раз каждая vtable встречается в памяти игры (выровнено по 4) и первые адреса
import ctypes, struct, sys
from ctypes import wintypes as W
k32 = ctypes.WinDLL('kernel32', use_last_error=True); psapi = ctypes.WinDLL('psapi')
pid = int(sys.argv[1])
h = k32.OpenProcess(0x0410, False, pid)
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw[:got.value] if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else b''
mods = (ctypes.c_void_p * 1024)(); need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
base = mods[0]
R = lambda va: va - 0x400000 + base
vts = {R(int(x, 16)): x for x in sys.argv[2:]}
pats = {v: struct.pack('<I', v) for v in vts}
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', W.DWORD),
                ('RegionSize', ctypes.c_size_t), ('State', W.DWORD), ('Protect', W.DWORD), ('Type', W.DWORD)]
k32.VirtualQueryEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t
hits = {v: [] for v in vts}
a = 0x10000; m = MBI()
while a < 0xFFFF0000 and k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
    start = m.BaseAddress or 0; size = m.RegionSize
    if m.State == 0x1000 and m.Protect in (0x04, 0x40) and m.Type in (0x20000, 0x40000):
        off = 0
        while off < size:
            n = min(16 << 20, size - off)
            b = rd(start + off, n)
            for v, p in pats.items():
                i = b.find(p)
                while i >= 0:
                    if i % 4 == 0: hits[v].append(start + off + i)
                    i = b.find(p, i + 4)
            off += n
    a = start + size
for v, name in vts.items():
    print('%s: %d  %s' % (name, len(hits[v]), ' '.join('%08X' % x for x in hits[v][:12])))
