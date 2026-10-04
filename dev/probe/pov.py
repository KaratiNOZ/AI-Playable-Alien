# pov.py <pid> — точки зрения (POV) рендера и их зоны: менеджер *(0x01766a00): +0x88 число (байт), +8 массив POV*.
#   POV: +0 матрица 4x4 (из камеры, FUN_00432300), +0x40/+0x80 ещё две, +0xe8 режим (1 — зона как у POV[0], 2 — своя),
#   +0x330 зона (-1 — нет), +0x34a номер точки обзора зон, +0x36b флаги (0x80 — активная, её пишет камера), +0x36c флаги
#   (FUN_0074c870 = pov_zone_matching.cpp). Сравнивает позицию с камерой мода и Амандой.
import ctypes, struct, sys, re
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1])
root = __file__.rsplit('\\dev', 1)[0].rsplit('/dev', 1)[0]
log = open(root + '/AlienModUI.log', encoding='utf-8', errors='replace').read()
base = int(re.findall(r'base ([0-9A-F]{8})', log)[-1], 16)
VA = lambda a: a - 0x400000 + base
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
mgr = u32(VA(0x01766a00))
n = rd(mgr + 0x88, 1)[0]
print('POV manager %08X count %d' % (mgr, n))
G = u32(u32(VA(0x016f0c88)) + 0x58)
cur = u32(G + 0x1d8)
print('camera cur %08X pos %.2f %.2f %.2f' % ((cur,) + struct.unpack('<3f', rd(cur + 0x14, 12))))
for i in range(n):
    p = u32(mgr + 8 + i * 4)
    m = struct.unpack('<16f', rd(p, 64))
    m2 = struct.unpack('<16f', rd(p + 0x40, 64))
    fl = rd(p + 0x36b, 2)
    print('POV %d %08X mode %d zone %08X vp %d flags36b %02X 36c %02X' % (i, p, u32(p + 0xe8), u32(p + 0x330), rd(p + 0x34a, 1)[0], fl[0], fl[1]))
    print('   m0 row3 %.2f %.2f %.2f %.2f | col3 %.2f %.2f %.2f' % (m[12], m[13], m[14], m[15], m[3], m[7], m[11]))
    print('   m1 row3 %.2f %.2f %.2f %.2f | col3 %.2f %.2f %.2f' % (m2[12], m2[13], m2[14], m2[15], m2[3], m2[7], m2[11]))
