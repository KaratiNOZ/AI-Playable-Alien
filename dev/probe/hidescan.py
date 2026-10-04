# hidescan.py <pid> [vt_entity_VA vt_iface_VA] — найти в памяти игры объекты сущности по паре vtable (+0 и +0x14) и
# показать их поля. По умолчанию — CUSTOM_HIDING_CONTROLLER (укрытия: шкафчики и т. п.). Только чтение.
import ctypes, struct, sys
from ctypes import wintypes as W
k32 = ctypes.WinDLL('kernel32', use_last_error=True); psapi = ctypes.WinDLL('psapi')
pid = int(sys.argv[1])
h = k32.OpenProcess(0x0410, False, pid)
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw[:got.value] if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else b''
def u32(a):
    b = rd(a, 4); return struct.unpack('<I', b)[0] if len(b) == 4 else 0
def f32(a):
    b = rd(a, 4); return struct.unpack('<f', b)[0] if len(b) == 4 else float('nan')
mods = (ctypes.c_void_p * 1024)(); need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
base = mods[0]
R = lambda va: va - 0x400000 + base
vt = R(int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x013CAD64)
ivt = R(int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x013CA9B4)
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', W.DWORD),
                ('RegionSize', ctypes.c_size_t), ('State', W.DWORD), ('Protect', W.DWORD), ('Type', W.DWORD)]
k32.VirtualQueryEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
pat = struct.pack('<I', vt)
found = []
a = 0x10000
m = MBI()
k32.VirtualQueryEx.restype = ctypes.c_size_t
scanned = 0
while a < 0xFFFF0000 and k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)):
    start = m.BaseAddress or 0
    size = m.RegionSize
    if m.State == 0x1000 and m.Protect in (0x04, 0x40) and m.Type == 0x20000:  # MEM_COMMIT, RW / RWX, MEM_PRIVATE
        off = 0
        while off < size:
            n = min(16 << 20, size - off)
            b = rd(start + off, n); scanned += len(b)
            i = b.find(pat)
            while i >= 0:
                if i % 4 == 0 and i + 0x18 <= len(b) and struct.unpack_from('<I', b, i + 0x14)[0] == ivt:
                    found.append(start + off + i)
                i = b.find(pat, i + 4)
            off += n
    a = start + size
print('base %08X, scanned %d MB, %d objects' % (base, scanned >> 20, len(found)))
for o in found[:60]:
    words = [u32(o + k) for k in range(0, 0x60, 4)]
    print('%08X ' % o + ' '.join('%08X' % w for w in words))
