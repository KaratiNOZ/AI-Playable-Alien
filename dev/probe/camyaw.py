# camyaw.py <pid> <npc hex> — курс тела персонажа и курс текущей камеры (блок 1), относительный угол камеры (вправо — плюс)
import ctypes, struct, sys, re, math
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); npc = int(sys.argv[2], 16)
base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
R = lambda va: va - 0x400000 + base
G = u32(u32(R(0x016f0c88)) + 0x58)
cam = u32(G + 0x1d8)
x, y, z, w = struct.unpack('<4f', rd(cam + 0x2c, 16))
fx, fz = 2 * (x * z + w * y), 1 - 2 * (x * x + y * y)
cy = math.atan2(fx, fz)
by = struct.unpack('<f', rd(npc + 0x174, 4))[0]
rel = math.remainder(cy - by, 2 * math.pi)
print('body %.0f  camera %.0f  camera relative to body %.0f deg' % (math.degrees(by), math.degrees(cy), math.degrees(rel)))
