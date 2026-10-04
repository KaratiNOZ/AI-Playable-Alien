# acts.py <pid> [npc hex] — действия персонажа: контроллер вторичных действий +0x370 (6 слотов) и все указатели персонажа
# на объекты с vtable в диапазоне действий (0x012a0000..0x012c0000)
import ctypes, struct, sys, re
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)  # база — из лога мода
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
def u32(a):
    b = rd(a, 4)
    return struct.unpack('<I', b)[0] if b else 0
R = lambda va: va - 0x400000 + base
V = lambda rt: rt + 0x400000 - base
act = R(0x01707a48)
npc = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
if not npc:
    for i in range(18):
        e = u32(act + i * 0xB0)
        if e and u32(u32(e + 0x64) + 4) == 1: npc = e
print('npc %08X' % npc)
c = npc + 0x370
for i in range(6):
    st = u32(c + i * 8)
    print('slot %d: state %08X vt %08X type %s  want %08X  cb %08X' % (
        i, st, V(u32(st)) if st else 0, (struct.unpack('<H', rd(st + 0x1e, 2))[0] if st else '-'), u32(c + 0x30 + i * 4), V(u32(c + 0x48 + i * 4))))
blob = rd(npc, 0x5000)
lo, hi = R(0x01200000), R(0x01300000)
for off in range(0, 0x5000, 4):
    p = struct.unpack_from('<I', blob, off)[0]
    if lo <= p < hi:
        print('+%04X embedded vt %08X' % (off, V(p)))
    elif 0x10000 < p < 0x7fff0000 and not (npc <= p < npc + 0x5000):
        v = u32(p)
        if lo <= v < hi and (off < 0x400 or off > 0x3000):
            print('+%04X -> %08X vt %08X' % (off, p, V(v)))
