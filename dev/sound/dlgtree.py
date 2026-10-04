import struct, sys
B = r'C:\Program Files (x86)\Steam\steamapps\common\Alien Isolation\DATA\SOUND\ENGLISH(US)\DIALOGUE_GENERIC.BNK'
d = open(B,'rb').read()
def fnv(s):
    h = 2166136261
    for c in s.lower().encode():
        h = (h * 16777619) & 0xffffffff
        h ^= c
    return h
names = open('names.txt').read().split()
H = {fnv(n): n for n in names}; H[0] = '*'
def nm(k): return H.get(k, '%08x' % k)
off = int(sys.argv[1], 16); ln = int(sys.argv[2])
p = off + 5
oid, prob, depth = struct.unpack_from('<IBI', d, p); p += 9
args = struct.unpack_from('<%dI' % depth, d, p); p += 4*depth
print('id %08x prob %d depth %d args %s' % (oid, prob, depth, [nm(a) for a in args]))
end = off + 5 + ln
for gt in (0, 1):
    q = p + (depth if gt else 0)
    size = struct.unpack_from('<I', d, q)[0]
    print('grouptypes %d: treesize %d, remaining %d' % (gt, size, end - (q + 5)))
q = p + depth
size = struct.unpack_from('<I', d, q)[0]; mode = d[q+4]; t0 = q + 5
N = size // 12
def node(i):
    k, a, b, w, pr = struct.unpack_from('<IHHHH', d, t0 + 12*i)
    return k, a, b
paths = []
def walk(i, lvl, path):
    k, a, b = node(i)
    if lvl == depth:
        paths.append(path + [k]); return
    for c in range(a, a + b):
        ck, ca, cb = node(c)
        walk(c, lvl + 1, path + [ck])
walk(0, 0, [])
print('mode', mode, 'nodes', N, 'paths', len(paths))
from collections import Counter
for lvl in range(depth):
    cnt = Counter(nm(pp[lvl]) for pp in paths)
    print('arg%d' % lvl, dict(cnt.most_common(40)))
print('---- paths per voice/event')
byv = {}
for pp in paths:
    byv.setdefault(nm(pp[0]), []).append(pp)
for v, pl in byv.items():
    print('VOICE', v, len(pl))
    for pp in pl:
        print('   ', ' | '.join(nm(x) for x in pp[1:8]), ' -> %08x' % pp[8])
