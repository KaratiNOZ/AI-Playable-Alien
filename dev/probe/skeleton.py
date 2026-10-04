# skeleton.py <pid> [npc hex] [layer] — скелет персонажа (Havok hkaPose слоя анимации): кости, родители, позы
#   поза слоя = *(персонаж+0x3f0+0x230+слой*4): +0 hkaSkeleton*, +4 локальная поза, +0x10 поза модели (hkQsTransform по 0x30:
#   сдвиг, поворот-кватернион, масштаб), +0x1c флаги костей. hkaSkeleton: +8 имя, +0xc родители (int16), +0x18 кости
#   (hkaBone по 8 байт: имя, lockTranslation), +0x1c их число.
import ctypes, struct, sys, re
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1])
base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
def cstr(a):
    b = rd(a, 64)
    return b.split(b'\0')[0].decode('ascii', 'replace') if b else '?'
log = open(__file__.rsplit('\\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read()
npc = int(sys.argv[2], 16) if len(sys.argv) > 2 else int(re.findall(r'alien found ([0-9A-F]{8})', log)[-1], 16)
layer = int(sys.argv[3]) if len(sys.argv) > 3 else 0
anim = npc + 0x3f0
pose = u32(anim + 0x230 + layer * 4)
skel = u32(pose)
print('npc %08X anim %08X layer %d pose %08X skel %08X "%s"' % (npc, anim, layer, pose, skel, cstr(u32(skel + 8))))
par = u32(skel + 0xc); npar = u32(skel + 0x10)
bones = u32(skel + 0x18); nb = u32(skel + 0x1c)
local = u32(pose + 4); model = u32(pose + 0x10); flags = u32(pose + 0x1c)
print('parents %08X (%d)  bones %08X (%d)  local %08X (%d)  model %08X (%d)  flags %08X' % (par, npar, bones, nb, local, u32(pose + 8), model, u32(pose + 0x14), flags))
pp = struct.unpack('<%dh' % nb, rd(par, nb * 2))
for i in range(nb):
    name = cstr(u32(bones + i * 8) & ~1)
    m = struct.unpack('<12f', rd(model + i * 0x30, 0x30))
    fl = u32(flags + i * 4)
    print('%3d %-24s parent %3d  model t %.2f %.2f %.2f  s %.2f  fl %X' % (i, name, pp[i], m[0], m[1], m[2], m[8], fl))
