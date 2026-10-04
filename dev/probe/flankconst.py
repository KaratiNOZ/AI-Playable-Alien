import ctypes, struct, sys
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); base = 0x570000
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    return buf.raw if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got)) else None
R = lambda va: va - 0x400000 + base
for va, kind in [(0x011becb8,'d'),(0x011bb1a0,'f'),(0x011bbd84,'f'),(0x011bbf78,'f'),(0x011bed34,'f'),(0x011becb0,'f'),(0x011bb27c,'f'),(0x011bb280,'f')]:
    b = rd(R(va), 8)
    print(hex(va), struct.unpack('<'+kind, b[:8 if kind=='d' else 4])[0])
