# playlists.py <pid> — плейлисты игры (только чтение памяти): менеджер плейлистов *(G0+0x68) +0x24 — хеш-карта
# {ShortGuid, объект}; у объекта +0x10 id, +0x14 ключ названия, +0x2c записи (уровни), +0x30 число. Менеджер уровней
# *(G0+0x3c): +0x2fd id текущего плейлиста, +0x320 номер записи. Для Main — все записи (дамп по 0x1c байт).
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
def cstr(a, n=96):
    if not a: return ''
    return rd(a, n).split(b'\0')[0].decode('latin1')
mods = (ctypes.c_void_p * 1024)(); need = W.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
base = mods[0]
R = lambda va: va - 0x400000 + base
g0 = u32(R(0x016f0c88)); pm = u32(g0 + 0x68); lm = u32(g0 + 0x3c)
print('base %08X G0 %08X playlists %08X levelmgr %08X' % (base, g0, pm, lm))
print('level manager: playlist "%s" position %d, +0x2dc %d' % (cstr(lm + 0x2fd, 32), u32(lm + 0x320), rd(lm + 0x2dc, 1)[0]))
arr = u32(pm + 0x24); n = u32(pm + 0x28); empty = u32(pm + 0x2c)
print('map %08X n %d empty %08X' % (arr, n, empty))
want = sys.argv[2] if len(sys.argv) > 2 else 'Main'
for i in range(min(n, 256)):
    key = u32(arr + i * 8); o = u32(arr + i * 8 + 4)
    if key == empty or not o: continue
    pid_ = cstr(u32(o + 0x10)); title = cstr(u32(o + 0x14)); cnt = u32(o + 0x30); lv = u32(o + 0x2c)
    first = cstr(u32(lv)) if lv else ''
    print('%08X %-24s title %-22s entries %3d first %s' % (o, pid_, title, cnt, first))
    if pid_ == want:
        print('  raw +0x00..+0x80:', rd(o, 0x80).hex())
        for k in range(min(cnt, 64)):
            e = lv + k * 4
            p = u32(e)
            print('  [%2d] %08X -> %s | %s' % (k, p, cstr(p, 64), rd(p, 0x20).hex() if p else ''))
