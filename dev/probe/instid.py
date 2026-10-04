# instid.py <dump.txt> <GUID сущности-экземпляра, напр. 52-25-C7-53> — все пути от корня уровня до этой сущности (по дампу
# cathodedump) и ID экземпляра составной по формуле CathodeLib (PathUtils.GenerateCompositeInstanceID + ShortGuid.Combine).
import re, struct, sys
from collections import defaultdict
dump, target = sys.argv[1], sys.argv[2]
def g2u(s): return struct.unpack('<I', bytes(int(x, 16) for x in s.split('-')))[0]
def u2g(v): return '-'.join('%02X' % b for b in struct.pack('<I', v))
INSTANCE = 1257266174
def combine(a, b):  # a.Combine(b): a — this, b — guid2
    if b == 0: return a
    if a == 0: return b
    x = struct.unpack('<Q', struct.pack('<I', a) + struct.pack('<I', b))[0]
    M = (1 << 64) - 1
    x = ((~x) + (x * 262144)) & M
    x = ((x ^ (x >> 31)) * 21) & M
    x = ((x ^ (x >> 11)) * 65) & M
    return ((x >> 22) ^ x) & 0xFFFFFFFF
def instance_id(path):  # path: [g_root_child, ..., g_target] — guid'ы сущностей от корня
    values = list(reversed(path)) + [INSTANCE]
    v = values[0]
    for i in range(len(values) - 1):
        v = combine(values[i + 1], v)
    return v
comp_of = {}                  # путь составной -> её guid
parents = defaultdict(list)   # путь составной -> [(путь родительской составной, guid сущности-экземпляра)]
where = defaultdict(list)     # guid сущности -> [путь составной, где она лежит]
cur = None
for line in open(dump, encoding='utf-8', errors='replace'):
    m = re.match(r'== composite (.+) ([0-9A-F]{2}(?:-[0-9A-F]{2}){3})', line)
    if m:
        cur = m.group(1); comp_of[cur] = g2u(m.group(2)); continue
    m = re.match(r"\s+fn ([0-9A-F]{2}(?:-[0-9A-F]{2}){3}) '[^']*' : composite (\S+)", line)
    if m and cur:
        parents[m.group(2)].append((cur, g2u(m.group(1))))
    m = re.match(r"\s+fn ([0-9A-F]{2}(?:-[0-9A-F]{2}){3}) ", line)
    if m and cur:
        where[g2u(m.group(1))].append(cur)
t = g2u(target)
def paths_to(comp, depth=0):
    if depth > 30: return []
    if not parents.get(comp):
        return [[]]
    out = []
    for par, eg in parents[comp]:
        for p in paths_to(par, depth + 1):
            out.append(p + [eg])
    return out
for comp in where.get(t, []):
    for p in paths_to(comp):
        full = p + [t]
        print('%s  path %s  -> instance id %s (%08X)' % (comp, ' / '.join(u2g(x) for x in full), u2g(instance_id(full)), instance_id(full)))
