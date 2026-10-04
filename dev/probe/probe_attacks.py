import ctypes, struct, subprocess, sys
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
out = subprocess.check_output('tasklist /FI "IMAGENAME eq AI.exe" /FO CSV /NH', shell=True).decode('mbcs')
pid = int(out.split(',')[1].strip('"'))
h = k32.OpenProcess(0x0410, False, pid)
S_ = 0x170000
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got))
    return buf.raw[:got.value] if ok else b''
def u32(a):
    b = rd(a, 4); return struct.unpack('<I', b)[0] if len(b) == 4 else 0
def u8(a):
    b = rd(a, 1); return b[0] if b else -1
def cstr(a, n=64):
    b = rd(a, n).split(b'\0')[0]
    return b.decode('ascii','replace') if b and all(32 <= c < 127 for c in b) else None
ch = int(sys.argv[1], 16)
c = u32(ch + 0x34b4)
n = u8(c + 0x800); T = u32(c + 0x818)
print('chooser %08x n %d table %08x +81c %d +820 %08x +208*4 %08x' % (c, n, T, u32(c+0x81c), u32(c+0x820), u32(c+0x208*4)))
for i in range(n):
    e = T + i*0x74
    raw = rd(e, 0x74)
    strs = []
    for off in range(0, 0x74, 4):
        p = struct.unpack_from('<I', raw, off)[0]
        s = cstr(p) if p > 0x10000 else None
        if s and len(s) > 2: strs.append('+%x:%s' % (off, s))
    print('%2d type %d mask %x flags12 %02x w %d en %d  %s' % (i, u32(e+8), u32(e+0xc), u8(e+0x12), u32(e+0x14), u8(c+100+i*0x50), ' '.join(strs)))
    print('    ' + raw[:0x40].hex())
print('--- blocks at chooser+100')
for i in range(n):
    b = c + 100 + i*0x50
    raw = rd(b, 0x50)
    strs = []
    for off in range(0, 0x50, 4):
        p = struct.unpack_from('<I', raw, off)[0]
        s = cstr(p) if p > 0x10000 else None
        if s and len(s) > 2: strs.append('+%x:%s' % (off, s))
    print('%2d %s %s' % (i, raw.hex(), ' '.join(strs)))
print('--- full entry strings (deep)')
for i in range(n):
    e = T + i*0x74
    raw = rd(e, 0x74)
    for off in range(0x24, 0x74, 4):
        p = struct.unpack_from('<I', raw, off)[0]
        if p > 0x10000:
            r2 = rd(p, 0x40)
            s = cstr(p)
            s2 = cstr(struct.unpack_from('<I', r2, 0)[0]) if len(r2) >= 4 else None
            if s or s2: print('  %d +%x -> %s / *%s' % (i, off, s, s2))
