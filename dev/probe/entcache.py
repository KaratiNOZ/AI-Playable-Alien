# entcache.py <pid> <dump.txt> [N] — проверка «постоянной ссылки на сущность»: из базы экземпляров составных
# (*(EntityManager 0x0174ef40) … таблица CompositeKey: пары {ID экземпляра, ручка}) берём активные экземпляры, у каждого —
# кеш сущностей (ручка obj+4: {vt EntityCacheEntry, ref, n, ptr} -> n записей по 12 байт {guid сущности, объект, 0})
# и сверяем guid'ы с составом его составной по дампу cathodedump. Только чтение.
import ctypes, struct, sys, re
from ctypes import wintypes
k32 = ctypes.WinDLL('kernel32'); psapi = ctypes.WinDLL('psapi')
k32.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid, dump = int(sys.argv[1]), sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 15
h = k32.OpenProcess(0x0410, False, pid)
def rd(a, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    k32.ReadProcessMemory(h, a, buf, n, ctypes.byref(got)); return buf.raw[:got.value]
def u(a):
    d = rd(a, 4); return struct.unpack('<I', d)[0] if len(d) == 4 else 0
mods = (ctypes.c_void_p * 1)(); need = wintypes.DWORD()
psapi.EnumProcessModulesEx(h, mods, ctypes.sizeof(mods), ctypes.byref(need), 3)
VA = lambda a: a - 0x400000 + mods[0]
g2u = lambda s: struct.unpack('<I', bytes(int(x, 16) for x in s.split('-')))[0]
comp_ents, comp_name, ent_name, cur = {}, {}, {}, None
for line in open(dump, encoding='utf-8', errors='replace'):
    m = re.match(r'== composite (.+) ([0-9A-F]{2}(?:-[0-9A-F]{2}){3})', line)
    if m:
        cur = g2u(m.group(2)); comp_name[cur] = m.group(1); comp_ents[cur] = set(); continue
    m = re.match(r"\s+fn ([0-9A-F]{2}(?:-[0-9A-F]{2}){3}) '([^']*)'", line)
    if m and cur is not None:
        g = g2u(m.group(1)); comp_ents[cur].add(g); ent_name.setdefault(g, m.group(2))
def comb(a, b):
    if b == 0: return a
    if a == 0: return b
    x = struct.unpack('<Q', struct.pack('<I', a) + struct.pack('<I', b))[0]; M = (1 << 64) - 1
    x = ((~x) + (x * 262144)) & M; x = ((x ^ (x >> 31)) * 21) & M; x = ((x ^ (x >> 11)) * 65) & M
    return ((x >> 22) ^ x) & 0xFFFFFFFF
CI = VA(0x013E196C)
em = u(VA(0x0174ef40))
# таблица CompositeKey: ищем её ручку (vt MemoryAllocation<CompositeDatabase::CompositeKey> 0x013E1940) рядом с EntityManager
# — проще: адрес таблицы передаётся 4-м аргументом, если известен
tab_handle = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0
if not tab_handle:
    print('pass the CompositeKey handle (e.g. 0D54A8E0) as the 4th argument'); sys.exit()
hv = struct.unpack('<4I', rd(tab_handle, 16))
n, data = hv[2], hv[3] + 4
d = rd(data, n * 8)
shown = 0
good = total = 0
for i in range(0, len(d) // 8):
    iid, hd = struct.unpack('<2I', d[i*8:i*8+8])
    hh = rd(hd, 16)
    if len(hh) < 16: continue
    p = struct.unpack('<4I', hh)[3]
    rec = p - 0x34
    if u(rec + 0x10) != CI: continue
    key, ctype = u(rec), u(rec + 4)
    eh = u(rec + 0x14)
    if not eh: continue
    ev = rd(eh, 16)
    if len(ev) < 16: continue
    _, _, cnt, ep = struct.unpack('<4I', ev)
    if not 0 < cnt < 2000: continue
    cd = rd(ep, cnt * 12)
    guids = [struct.unpack('<I', cd[j*12:j*12+4])[0] for j in range(len(cd) // 12)]
    known = comp_ents.get(ctype, set())
    m = sum(1 for g in guids if g in known)
    if total < 3:  # проверить гипотезы: guid в кеше = Combine(guid сущности, ID экземпляра) или наоборот
        a = {comb(g, iid) for g in known}; b = {comb(iid, g) for g in known}
        c = {comb(g, key) for g in known}; e = {comb(key, g) for g in known}
        print('   hyp comb(g,iid) %d  comb(iid,g) %d  comb(g,key) %d  comb(key,g) %d  of %d' % (
            sum(x in a for x in guids), sum(x in b for x in guids), sum(x in c for x in guids), sum(x in e for x in guids), len(guids)))
        print('   cache sample:', ' '.join('%08X' % x for x in guids[:6]))
    total += 1; good += (m == len(guids))
    if shown < N:
        shown += 1
        print('id %08X  %-28s %-46s cache %3d, in dump %3d' % (iid, ent_name.get(key, '?%08X' % key)[:28],
              comp_name.get(ctype, '?%08X' % ctype)[-46:], len(guids), m))
print('active instances checked: %d, cache fully matches composite: %d' % (total, good))
