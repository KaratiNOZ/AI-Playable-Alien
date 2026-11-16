"""Read-only native torch manager inspection; no input or game calls."""
from pathlib import Path
exec(Path(__file__).with_name('camera_posture.py').read_text().split('name = b"FollowBehavior"')[0])

try:
    gg = rd(base + 0x016f0c88 - 0x400000)
    player = rd(rd(rd(gg + 8) + 0x80))
    for label, actor in [('player', player), ('alien', npc)]:
        print(label, f'{actor:08X}')
        tm = rd(actor + 0x1100)
        print(f'  direct torch manager {tm:08X}')
        if tm:
            print('  direct fields', [f'{v:08X}' for v in rd(tm + 0x1500, '<32I')])
            ref = rd(tm + 0x156c)
            if ref:
                obj = rd(ref + 0xc)
                print(f'  source ref={ref:08X} obj={obj:08X} vtVA={rd(obj)-base+0x400000:08X}')
                print('  source object', [f'{v:08X}' for v in rd(obj, '<24I')])
        if label == 'player':
            ctrl = rd(rd(actor + 0x31c) + 0x3c)
            print(f'  controller {ctrl:08X}')
            print('  controller fields', [f'{v:08X}' for v in rd(ctrl, '<68I')])
            torch = rd(ctrl + 0xe8)
            print(f'  torch? {torch:08X}')
            if torch:
                print('  torch fields', [f'{v:08X}' for v in rd(torch + 0x1500, '<32I')])
        for offset in (0x31c, 0x34f0):
            holder = rd(actor + offset)
            print(f'  +{offset:X} = {holder:08X}')
            if not holder:
                continue
            print('  holder', [f'{v:08X}' for v in rd(holder, '<68I')])
            for i, p in enumerate(rd(holder, '<68I')):
                if not (0x10000 < p < 0x7fff0000) or p % 4:
                    continue
                try:
                    if rd(p + 0x1528) == actor:
                        print(f'  TORCH manager {p:08X} holder+{i*4:X}')
                        print('  fields', [f'{v:08X}' for v in rd(p + 0x1500, '<32I')])
                except OSError:
                    pass
    if '--scan' in sys.argv:
        from ctypes import wintypes
        class MBI(c.Structure):
            _fields_ = [('BaseAddress', c.c_void_p), ('AllocationBase', c.c_void_p),
                        ('AllocationProtect', wintypes.DWORD), ('RegionSize', c.c_size_t),
                        ('State', wintypes.DWORD), ('Protect', wintypes.DWORD), ('Type', wintypes.DWORD)]
        k.VirtualQueryEx.argtypes = [c.c_void_p, c.c_void_p, c.POINTER(MBI), c.c_size_t]
        a = 0x10000
        m = MBI()
        pat = struct.pack('<I', player)
        seen = set()
        while a < 0x7fff0000 and k.VirtualQueryEx(h, a, c.byref(m), c.sizeof(m)):
            size = m.RegionSize
            if m.State == 0x1000 and m.Protect in (0x04, 0x40) and m.Type == 0x20000:
                for off in range(0, size, 1 << 20):
                    n = min(1 << 20, size - off)
                    buf = c.create_string_buffer(n)
                    got = c.c_size_t()
                    k.ReadProcessMemory(h, a + off, buf, n, c.byref(got))
                    raw = buf.raw[:got.value]
                    pos = 0
                    while (i := raw.find(pat, pos)) >= 0:
                        pos = i + 4
                        candidate = a + off + i - 0x1528
                        if candidate in seen or candidate % 4 or candidate < 0x10000:
                            continue
                        seen.add(candidate)
                        try:
                            owner, helper = rd(candidate + 0x1528, '<2I')
                            ref = rd(candidate + 0x156c)
                            if owner == player and helper > 0x10000 and rd(candidate + 0x150c) == 10:
                                obj = rd(ref + 0xc) if ref else 0
                                print(f'CANDIDATE {candidate:08X} helper={helper:08X} ref={ref:08X} obj={obj:08X}', flush=True)
                                print('fields', [f'{v:08X}' for v in rd(candidate + 0x1500, '<32I')])
                        except OSError:
                            pass
            a += size
finally:
    k.CloseHandle(h)
