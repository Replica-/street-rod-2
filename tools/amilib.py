"""Amiga SR2 picture library (libstart.p / lib.p): SR2 LIB format, big-endian. amilib.py LIB [OUTDIR]"""
import os, struct, sys, collections
sys.path.insert(0, 'tools')
import srlib


def read(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('>H', d, 0)[0]
    base = 2 + n * 28
    pics = []
    for i in range(n):
        p = 2 + i * 28
        w, h, depth, masked = struct.unpack_from('>HHBB', d, p)
        off, size = struct.unpack_from('>IH', d, p + 22)
        pics.append(dict(index=i, w=w, h=h, depth=depth, masked=masked, tokens=d[p + 6:p + 22],
                         raw=((w + 15) // 16 * 2) * h * depth, data=d[base + off:base + off + size + 1]))
    return d, base, pics


d, base, pics = read(sys.argv[1])
print(len(d), 'bytes', len(pics), 'pictures, data at', base, 'BC at base:', hex(d[base]))
stat = collections.Counter()
for pic in pics:
    try:
        tok = {t: k for k, t in enumerate(pic['tokens']) if t}
        src, out, i = pic['data'][1:-1], 0, 0
        assert pic['data'][0] == 0xBC
        while i < len(src):
            b = src[i]; i += 1
            if b in (0, 0xFF):
                out += 1 if src[i] == 0 else src[i] + 1; i += 1
            elif b in tok: out += srlib.RUN_COUNT[tok[b]]
            else: out += 1
        pic['got'] = out
        stat['exact' if out == pic['raw'] else 'short' if out < pic['raw'] else 'long'] += 1
    except Exception as e:
        stat['error ' + str(e)[:30]] += 1
print(dict(stat), 'depths', collections.Counter(p['depth'] for p in pics))
for p in pics[:12] + [p for p in pics if p.get('got') != p['raw']][:8]:
    print(p['index'], '%dx%d d%d m%d raw %d got %s' % (p['w'], p['h'], p['depth'], p['masked'], p['raw'], p.get('got')))
