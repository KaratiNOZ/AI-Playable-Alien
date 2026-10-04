# memdiff.py <pid> <addr hex> <size hex> <out.bin> — снять кусок памяти игры в файл (для сравнения двух состояний)
# memdiff.py diff <a.bin> <b.bin> [base hex] — какие dword'ы различаются (как float и hex)
import ctypes, struct, sys
if sys.argv[1] == 'diff':
    a = open(sys.argv[2], 'rb').read(); b = open(sys.argv[3], 'rb').read()
    base = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
    for o in range(0, min(len(a), len(b)) - 3, 4):
        x, y = a[o:o + 4], b[o:o + 4]
        if x != y:
            fx, fy = struct.unpack('<f', x)[0], struct.unpack('<f', y)[0]
            print('+%04X  %08X %08X   %10.4f %10.4f' % (o, struct.unpack('<I', x)[0], struct.unpack('<I', y)[0], fx, fy))
    sys.exit()
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
pid = int(sys.argv[1]); a = int(sys.argv[2], 16); n = int(sys.argv[3], 16)
h = k32.OpenProcess(0x0410, False, pid)
buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, n, ctypes.byref(got))
open(sys.argv[4], 'wb').write(buf.raw[:got.value])
print('read', got.value)
