import ctypes, struct, sys
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
import subprocess
pid = int(sys.argv[1]); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
R = lambda va: va - 0x400000 + base
act = R(0x01707a48)
for i in range(18):
    e = u32(act + i * 0xB0)
    if not e: continue
    name = rd(e + 0x10, 64).split(b'\0')[0].decode('ascii', 'replace')
    vt = u32(e) - base + 0x400000
    x, y, z = struct.unpack('<3f', rd(e + 0x70, 12))
    print('%2d %08X vt %08X %-40s pos %.1f %.1f %.1f yaw %.2f  +64->%08X' % (i, e, vt, name, x, y, z, f32(e + 0x174), u32(e + 0x64)))
print('--- details')
pl = None
for i in range(18):
    e = u32(act + i * 0xB0)
    if not e: continue
    try:
        typ = u32(u32(e + 0x64) + 4)
        tgt = u32(u32(u32(e + 0x34a8) + 0x10) + 0x42c)
    except Exception as ex:
        typ = tgt = -1
    print('%08X type %s target %08X  +34a8 %08X +34f0 %08X +11a0 %08X' % (e, typ, tgt, u32(e+0x34a8), u32(e+0x34f0), u32(e+0x11a0)))
