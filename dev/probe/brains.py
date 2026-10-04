import ctypes, struct, sys
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
u32 = lambda a: struct.unpack('<I', rd(a, 4))[0]
hs = [int(x, 16) for x in sys.argv[2:]]
br = [u32(e + 0x34a8) for e in hs]
dumps = [rd(b, 0x480) for b in br]
for o in range(0, 0x480, 4):
    vals = [struct.unpack_from('<I', d, o)[0] for d in dumps]
    if len(set(vals)) == 1 and vals[0] and 0x10000 < vals[0] < 0x7fff0000:
        vt = u32(vals[0]) if rd(vals[0], 4) else 0
        print('+%03x SAME %08X (->%08X)' % (o, vals[0], vt))
    elif all(0x10000000 < v < 0x7fff0000 for v in vals):
        vts = []
        for v in vals:
            r = rd(v, 4); vts.append(struct.unpack('<I', r)[0] if r else 0)
        if len(set(vts)) == 1 and 0x400000 + 0x170000 < vts[0] < 0x2000000 + 0x170000:
            print('+%03x DIFF %s  same vt %08X (VA %08X)' % (o, ' '.join('%08X' % v for v in vals), vts[0], vts[0] - 0x170000))
