import ctypes, struct, sys
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
def u32(a):
    r = rd(a, 4); return struct.unpack('<I', r)[0] if r else 0
def f32(a):
    r = rd(a, 4); return struct.unpack('<f', r)[0] if r else 0
VA = lambda rt: rt - 0x170000 if rt else 0
VT_GATE = 0x012e4870 + 0x170000
def desc(n, depth=0):
    if not n: return 'null'
    vt = VA(u32(n))
    s = '%08X vt %08X' % (n, vt)
    if vt == 0x012bf628: s += ' MOVE dist %.0f' % f32(n + 0x10)
    return s
for center in [int(x, 16) for x in sys.argv[2:]]:
    print('=== around %08X' % center)
    raw = rd(center - 0x1000, 0x2000)
    for o in range(0, 0x2000, 4):
        if struct.unpack_from('<I', raw, o)[0] != VT_GATE: continue
        g = center - 0x1000 + o
        ch = u32(g + 0xc)
        f, t = (u32(ch), u32(ch + 4)) if ch else (0, 0)
        tt = u32(u32(t + 0xc) + 4) if t and u32(t + 0xc) else 0
        print('gate %08X val %d | true: %s | grand: %s | false: %s' % (g, rd(g + 0x14, 1)[0], desc(t), desc(tt), desc(f)))
