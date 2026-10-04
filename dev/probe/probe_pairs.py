import ctypes, struct, subprocess
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
G = u32(0x16f0c88 + S_)
m = u32(G + 0x5c)
print('G %08x mgr %08x count %d list %08x d8 %d' % (G, m, u32(m+0x1c), u32(m+0x18), u8(m+0xd8)))
L = u32(m + 0x18)
for i in range(min(u32(m+0x1c), 40)):
    e = L + i*0x6c
    a, t, c = u32(e+4), u32(e+0x30), u32(e+0x3c)
    cls = lambda ch: u32(u32(ch+0x64)+4) if ch else -1
    print('%2d att %08x(c%d) tgt %08x(c%d) +3c %08x +44 %d +45 %d  tgt cls flags 1c %d 1f %d' % (
        i, a, cls(a), t, cls(t), c, u8(e+0x44), u8(e+0x45), u8(u32(t+0x64)+0x1c) if t else -1, u8(u32(t+0x64)+0x1f) if t else -1))
