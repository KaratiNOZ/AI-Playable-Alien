import ctypes, struct, sys, math, time
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
i32 = lambda a: struct.unpack('<i', rd(a, 4))[0]
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
v3 = lambda a: struct.unpack('<3f', rd(a, 12))
R = lambda va: va - 0x400000 + base
pl = u32(u32(u32(u32(R(0x016f0c88)) + 8) + 0x80))
npc = int(sys.argv[2], 16)
mgr = u32(u32(npc + 0x34f0) + 0x24)
for k in range(int(sys.argv[3]) if len(sys.argv) > 3 else 5):
    aim = u32(pl + 0x1100); yaw = f32(pl + 0x174)
    f = v3(aim + 0x60) if aim else (0, 0, 0)
    print('yaw %.2f -> (%.2f,%.2f)  aim+60 (%.2f %.2f %.2f)  rrA %d rrP %d pendA %08X pendP %08X AV %d %d FV %d %d locks %d %d %d %d' % (
        yaw, math.sin(yaw), math.cos(yaw), f[0], f[1], f[2], i32(mgr+0x28), i32(mgr+0x2c), u32(mgr+0x18), u32(mgr+0x20),
        i32(mgr+0x40), i32(mgr+0x44), i32(mgr+0x48), i32(mgr+0x4c), i32(mgr+0x30), i32(mgr+0x34), i32(mgr+0x38), i32(mgr+0x3c)))
    time.sleep(0.1)
