# lookslots.py <pid> [раз] [пауза] — у всех персонажей: вторичные слоты взгляда (0xf..0x12) и поля объекта взгляда
import ctypes, struct, sys, re, time
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)  # база — из лога мода
n = int(sys.argv[2]) if len(sys.argv) > 2 else 1
pause = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, k):
    buf = ctypes.create_string_buffer(k); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, k, ctypes.byref(got)) else None
def u32(a):
    b = rd(a, 4)
    return struct.unpack('<I', b)[0] if b else 0
R = lambda va: va - 0x400000 + base
act = R(0x01707a48)
for it in range(n):
    for i in range(18):
        e = u32(act + i * 0xB0)
        if not e: continue
        name = (rd(e + 0x10, 24) or b'').split(b'\0')[0].decode('ascii', 'replace')
        t = u32(u32(e + 0x64) + 4) if u32(e + 0x64) else -1
        prim = u32(e + 0x36c)
        pt = rd(prim + 0x1a, 1)[0] if prim else -1
        types = []
        for s in range(6):
            st = u32(e + 0x370 + s * 8)
            ty = struct.unpack('<H', rd(st + 0x1e, 2))[0] if st else 0
            types.append(ty)
            if 0xf <= ty <= 0x12:
                d = rd(st, 0x180)
                f = lambda o: struct.unpack_from('<f', d, o)[0]
                print('%-14s t%d prim %2d slot%d type 0x%X  tgt %.1f %.1f %.1f  mtx %.1f %.1f %.1f  theta %.3f phi %.3f dist %.2f coords %08X tree %08X fl8 %02X fl42 %02X c8 %08X  +2c8 %02X +29cc %02X' % (
                    name, t, pt, s, ty, f(0x30), f(0x34), f(0x38), f(0xa0), f(0xa4), f(0xa8), f(0x174), f(0xb4), f(0x64),
                    struct.unpack_from('<I', d, 0x170)[0], struct.unpack_from('<I', d, 0x14)[0], d[8], d[0x42],
                    struct.unpack_from('<I', d, 0xc8)[0], rd(e + 0x2c8, 1)[0], rd(e + 0x29cc, 1)[0]))
        if it == 0: print('   %-14s t%d prim %d slots %s' % (name, t, pt, ' '.join('%X' % x for x in types)))
    if it + 1 < n: time.sleep(pause); print('---')
