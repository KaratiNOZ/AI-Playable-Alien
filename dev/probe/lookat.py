# lookat.py <pid> [npc hex] — найти у Чужого объект взгляда PERSISTENT_ACT_LOOK_AT (vtable VA 0x012af4a0, второй 0x012af500)
# и показать его поля: цель +0x30, look_theta +0x174, look_phi +0xb4, look_distance +0x64, look_coords +0x170
import ctypes, struct, sys, re, time
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)  # база — из лога мода
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
def u32(a):
    b = rd(a, 4)
    return struct.unpack('<I', b)[0] if b else 0
f32 = lambda a: struct.unpack('<f', rd(a, 4))[0]
R = lambda va: va - 0x400000 + base
VTS = {R(0x012af4a0): 'LOOK_AT(A)', R(0x012af500): 'LOOK_AT(B)', R(0x012af538): 'LOOK_AT_IK?(A)', R(0x012af598): 'LOOK_AT_IK?(B)'}
act = R(0x01707a48)
npcs = []
for i in range(18):
    e = u32(act + i * 0xB0)
    if not e: continue
    try: typ = u32(u32(e + 0x64) + 4)
    except Exception: typ = -1
    name = (rd(e + 0x10, 64) or b'').split(b'\0')[0].decode('ascii', 'replace')
    npcs.append((e, typ, name))
target = int(sys.argv[2], 16) if len(sys.argv) > 2 else next((e for e, t, n in npcs if t == 1), 0)
print('chars:', ', '.join('%08X t%d %s' % x for x in npcs))
print('alien %08X' % target)
blob = rd(target, 0x5000)
found = []
for off in range(0, 0x5000, 4):
    p = struct.unpack_from('<I', blob, off)[0]
    if p in VTS:
        found.append((off, target + off, VTS[p], 'embedded'))
    elif 0x10000 < p < 0x7fff0000:
        v = u32(p)
        if v in VTS: found.append((off, p, VTS[v], 'ptr'))
        # один уровень глубже: массивы действий
for off, obj, what, how in found:
    print('+%04X -> %08X %s (%s)' % (off, obj, what, how))
seen = set()
for off, obj, what, how in found:
    if obj in seen: continue
    seen.add(obj)
    d = rd(obj, 0x190)
    fl = lambda o: struct.unpack_from('<f', d, o)[0]
    print('obj %08X  ch(+4) %08X  +14 %08X  tgt(+30) %.2f %.2f %.2f  theta(+174) %.3f  phi(+b4) %.3f  dist(+64) %.3f  coords(+170) %08X  mtx.t(+a0) %.2f %.2f %.2f' % (
        obj, struct.unpack_from('<I', d, 4)[0], struct.unpack_from('<I', d, 0x14)[0], fl(0x30), fl(0x34), fl(0x38), fl(0x174), fl(0xb4), fl(0x64),
        struct.unpack_from('<I', d, 0x170)[0], fl(0xa0), fl(0xa4), fl(0xa8)))
