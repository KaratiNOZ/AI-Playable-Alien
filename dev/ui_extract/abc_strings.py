import struct, re, sys
a = open('dev/ui_extract/pickup.abc', 'rb').read()
pos = 0
def u30():
    global pos
    r = 0; s = 0
    while True:
        b = a[pos]; pos += 1
        r |= (b & 0x7f) << s
        if not (b & 0x80): break
        s += 7
    return r
def u16():
    global pos
    v = struct.unpack_from('<H', a, pos)[0]; pos += 2; return v
pos = 4
def pool(kind):
    global pos
    n = u30(); vals = []
    for i in range(n - 1):
        if kind == 'str':
            sz = u30(); vals.append(a[pos:pos+sz].decode('utf-8', 'replace')); pos += sz
        elif kind == 'dbl':
            vals.append(struct.unpack_from('<d', a, pos)[0]); pos += 8
        elif kind == 'ns':
            k = a[pos]; pos += 1; vals.append((k, u30()))
        elif kind == 'nsset':
            c = u30(); vals.append([u30() for _ in range(c)])
        elif kind == 'mn':
            k = a[pos]; pos += 1
            if k in (0x07, 0x0d): vals.append(('QName', u30(), u30()))
            elif k in (0x0f, 0x10): vals.append(('RTQName', u30()))
            elif k in (0x11, 0x12): vals.append(('RTQNameL',))
            elif k in (0x1b, 0x1c): vals.append(('MultinameL', u30()))
            elif k == 0x09: vals.append(('TypeName', u30(), u30()))
            elif k == 0x1d:
                u30(); c = u30(); vals.append(('Multiname', [u30() for _ in range(c)]))
            else: vals.append(('kind', k))
        else:
            r = 0; s = 0
            while True:
                b = a[pos]; pos += 1
                r |= (b & 0x7f) << s
                if not (b & 0x80): break
                s += 7
            if s < 32 and (b & 0x40): r |= -(1 << (s + 7))
            vals.append(r)
    return vals
ints = pool('int'); uints = pool('int'); dbls = pool('dbl'); strs = pool('str')
nss = pool('ns'); nssets = pool('nsset'); mns = pool('mn')
print('strings', len(strs), 'multinames', len(mns), 'pos', pos)
def mnname(i):
    if i == 0: return '*'
    m = mns[i-1]
    if m[0] in ('QName', 'TypeName'):
        ns = m[1]; nm = m[2]
        return (strs[ns-1] + '::' if ns else '') + (strs[nm-1] if nm else '*')
    if m[0] == 'RTQName': return strs[m[1]-1] if m[1] else '*'
    if m[0] == 'MultinameL': return 'L[' + (strs[m[1]-1] if m[1] else '') + ']'
    return str(m)
pat = re.compile(sys.argv[1] if len(sys.argv) > 1 else 'prompt|hold|interact|spinner|legend|key|button|text|visible|progress', re.I)
if len(sys.argv) > 2 and sys.argv[2] == 'mn':
    for i, m in enumerate(mns):
        nm = mnname(i+1)
        if pat.search(nm): print(i+1, nm)
else:
    for i, s in enumerate(strs):
        if pat.search(s): print(i, repr(s))