# memscan.py <pid> <hex dword|VA:hex> [max] — найти в памяти процесса dword (VA:… — адрес из Ghidra, переводится по базе
# AI.exe, взятой из самого процесса) и показать первые 0x40 байт вокруг находок. Только чтение.
import ctypes, struct, sys
from ctypes import wintypes
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi')
k32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid = int(sys.argv[1])
arg = sys.argv[2]
mx = int(sys.argv[3]) if len(sys.argv) > 3 else 50
h = k32.OpenProcess(0x0410, False, pid)

def module_base():
    mods = (ctypes.c_void_p * 1)(); need = wintypes.DWORD()
    psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
    return mods[0]
base = module_base()
val = int(arg[3:], 16) - 0x400000 + base if arg.startswith('VA:') else int(arg, 16)

class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', wintypes.DWORD),
                ('RegionSize', ctypes.c_size_t), ('State', wintypes.DWORD), ('Protect', wintypes.DWORD), ('Type', wintypes.DWORD)]
def regions():
    a = 0x10000
    m = MBI()
    while a < 0xfffe0000:
        if not k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
            break
        b = m.BaseAddress or 0
        if m.State == 0x1000 and (m.Protect & 0xEE) and not (m.Protect & 0x101):
            yield b, m.RegionSize
        a = b + m.RegionSize
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got))
    return buf.raw[:got.value]
pat = struct.pack('<I', val)
hits = []
for b, sz in regions():
    off = 0
    while off < sz:
        n = min(sz - off, 1 << 22)
        d = rd(b + off, n)
        i = d.find(pat)
        while i >= 0:
            if i % 4 == 0:
                hits.append(b + off + i)
            i = d.find(pat, i + 1)
        off += n
    if len(hits) > 20000:
        break
print('base %08X  value %08X: %d hits' % (base, val, len(hits)))
for a in hits[:mx]:
    d = rd(a, 0x40)
    if len(d) == 0x40:
        print('%08X: %s' % (a, ' '.join('%08X' % x for x in struct.unpack('<16I', d))))
