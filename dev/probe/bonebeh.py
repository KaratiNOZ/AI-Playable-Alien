# bonebeh.py <pid> — поведение BoneAttached камеры «из кости» (G+0x1e0): его поля (ищем индекс кости головы)
import ctypes, struct, sys, re
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1])
base = int(re.findall(r'base ([0-9A-F]{8})', open(__file__.rsplit('\\dev', 1)[0].rsplit('/dev', 1)[0] + '/AlienModUI.log', encoding='utf-8', errors='replace').read())[-1], 16)
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
R = lambda va: va - 0x400000 + base
def hname(s):
    h = 0; n = len(s); j = 0
    for i in range(256):
        if j >= n: j = 0
        h = (h + (ord(s[j]) << (i & 31))) & 0xffffffff; j += 1
    return h
HB = hname('BoneAttached')
G = u32(u32(R(0x016f0c88)) + 0x58)
code = u32(G + 0x1e0)
n = u32(code + 0xc0); arr = u32(code + 0xbc)
for i in range(min(n, 64)):
    b = u32(arr + i * 4)
    if b and u32(b + 8) == HB:
        d = rd(b, 0x140)
        print('BoneAttached %08X' % b)
        for o in range(0, 0x140, 16):
            w = struct.unpack_from('<4I', d, o); f = struct.unpack_from('<4f', d, o)
            print('  +%03X  %08X %08X %08X %08X   %s' % (o, *w, ' '.join('%.3g' % x for x in f)))
