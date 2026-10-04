import ctypes, struct, sys, math
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); npc = int(sys.argv[2], 16); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
i32 = lambda a: struct.unpack('<i', rd(a, 4))[0]
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
v3 = lambda a: struct.unpack('<3f', rd(a, 12))
R = lambda va: va - 0x400000 + base
for va in (0x011becc4, 0x011beca8, 0x011becac, 0x011bbfb8, 0x011c6938, 0x011c6cfc, 0x011becc0, 0x011bbd74):
    print('const %08X = %g' % (va, f32(R(va))))
mgr = u32(u32(npc + 0x34f0) + 0x24); arr = u32(mgr + 8); cnt = u32(mgr + 0xc)
print('mgr %08X cnt %d alienChar %08X' % (mgr, cnt, u32(mgr + 0x14)))
print('pendA %08X pendP %08X rrA %d rrP %d' % (u32(mgr+0x18), u32(mgr+0x20), i32(mgr+0x28), i32(mgr+0x2c)))
print('locks A', i32(mgr+0x30), i32(mgr+0x34), 'B', i32(mgr+0x38), i32(mgr+0x3c))
print('alienVent', i32(mgr+0x40), i32(mgr+0x44), 'flanked', i32(mgr+0x48), i32(mgr+0x4c))
pl = u32(u32(u32(R(0x016f0c88)) + 8) + 0x80); pl = u32(pl)
print('player %08X pos %s yaw %.2f aim %08X' % (pl, ['%.1f' % x for x in v3(pl + 0x70)], f32(pl + 0x174), u32(pl + 0x1100)))
print('npc pos', ['%.1f' % x for x in v3(npc + 0x70)])
for i in range(cnt):
    e = arr + i * 0x30
    dA, dP = u32(e), u32(e + 8)
    vp = v3(u32(e + 0x28))
    la = f32(dA + 0x20) if dA else -1; lp = f32(dP + 0x20) if dP else -1
    npA = (u32(dA + 0x14) - u32(dA + 0x10)) // 12 if dA else 0
    npP = (u32(dP + 0x14) - u32(dP + 0x10)) // 12 if dP else 0
    print('#%2d vent %6.1f %6.1f %6.1f | A ok%d len %6.1f n%2d at %s | P ok%d len %6.1f n%2d at %s' % (
        i, vp[0], vp[1], vp[2], rd(e + 0x2c, 1)[0], la, npA, ['%.1f' % x for x in v3(e + 0x10)],
        rd(e + 0x2d, 1)[0], lp, npP, ['%.1f' % x for x in v3(e + 0x1c)]))
