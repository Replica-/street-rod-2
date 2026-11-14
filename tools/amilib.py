"""Street Rod 2 Amiga picture libraries (libstart.p, lib.p; Enhanced backlog E7) -> PNG per picture + contact sheet.

  python tools/amilib.py Game/amiga/disk2/lib.p work/amiga/lib

Same directory as the PC SR2 LIB files (tools/srlib.py) but big-endian: u16 count, count x 28-byte records
(u16 w, u16 h, u8 depth = bit planes 1..6, u8 masked, u8 tokens[16], u32 offset from the end of the directory,
u16 size, exact). No 0xBC marker. Packing: 00 n / FF n = n+1 bytes 00 / FF (n = 0: one byte), token k = RUN[k]
(fixed by solving all 457 pictures for exact sizes); other bytes are literal. Rows are padded to bytes, planes
stored one after the other.
Palettes: 32-colour tables in the Amiga exe `STREET_ROD` from 0x3C904, 64 bytes apart, 12-bit 0RGB, colour 0 black;
depth 6 = extra half-bright (colours 32..63 = 0..31 halved). `--pal N` picks table N (default 7, at 0x3CAC4: U-check
2026-11-14 on lib #114/#115/#120 and libstart #34); `--pal ramp` = stand-in colours. Which picture uses which table
is not known yet.

  python tools/amilib.py Game/amiga/disk2/lib.p work/amiga/lib [--pal N|ramp]
"""
import os, struct, sys, zlib

# token k -> (value, count): 0/1 single 00/FF, then zero runs 2..9 and FF runs 2..7 interleaved
RUN = [(0, 1), (255, 1), (0, 2), (0, 3), (255, 2), (0, 4), (0, 5), (255, 3), (0, 6), (255, 4), (0, 7), (255, 5),
       (0, 8), (255, 6), (0, 9), (255, 7)]


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
                         data=d[base + off:base + off + size]))
    return pics


def unpack(pic):
    tok = {t: k for k, t in enumerate(pic['tokens']) if t}
    src, out, i = pic['data'], bytearray(), 0
    while i < len(src):
        b = src[i]; i += 1
        if b in (0x00, 0xFF):
            out += bytes([b]) * (src[i] + 1 if src[i] else 1)
            i += 1
        elif b in tok:
            v, c = RUN[tok[b]]
            out += bytes([v]) * c
        else:
            out.append(b)
    return bytes(out)


def indices(pic, raw):
    w, h, bpr = pic['w'], pic['h'], (pic['w'] + 7) // 8
    idx = bytearray(w * h)
    for pl in range(pic['depth']):
        for y in range(h):
            row = (pl * h + y) * bpr
            for x in range(w):
                if raw[row + x // 8] & (0x80 >> (x % 8)):
                    idx[y * w + x] |= 1 << pl
    return idx


def exe_palette(n, depth, exe='Game/amiga/disk1/STREET_ROD'):
    d = open(exe, 'rb').read()
    pal = [((x >> 8 & 15) * 17, (x >> 4 & 15) * 17, (x & 15) * 17) for x in struct.unpack_from('>32H', d, 0x3C904 + 64 * n)]
    pal += [(r // 2, g // 2, b // 2) for r, g, b in pal]
    return pal[:1 << depth]


def palette(depth):
    import colorsys
    n = 1 << depth
    return [(0, 0, 0)] + [tuple(int(c * 255) for c in colorsys.hsv_to_rgb((k * 0.618) % 1, 0.55, 0.45 + 0.55 * (k % 4) / 3))
                          for k in range(1, n)]


def png(path, w, h, rgb):
    chunk = lambda t, c: struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c))
    rows = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pal_arg = sys.argv[sys.argv.index('--pal') + 1] if '--pal' in sys.argv else '7'
    pics = read(src)
    bad, imgs = 0, []
    for pic in pics:
        raw = unpack(pic)
        if len(raw) != (pic['w'] + 7) // 8 * pic['h'] * pic['depth']:
            bad += 1
        raw += bytes(max(0, (pic['w'] + 7) // 8 * pic['h'] * pic['depth'] - len(raw)))
        pal = palette(pic['depth']) if pal_arg == 'ramp' else exe_palette(int(pal_arg), pic['depth'])
        rgb = bytearray()
        for c in indices(pic, raw):
            rgb += bytes(pal[c])
        png(os.path.join(out, '%03d.png' % pic['index']), pic['w'], pic['h'], rgb)
        imgs.append((pic['w'], pic['h'], rgb))
    # contact sheet: rows of pictures, 640 wide, 4 px gaps, grey background
    W, x, y, rowh, place = 640, 4, 4, 0, []
    for w, h, _ in imgs:
        if x + w > W - 4 and x > 4:
            x, y, rowh = 4, y + rowh + 4, 0
        place.append((x, y)); x += w + 4; rowh = max(rowh, h)
    H = y + rowh + 4
    sheet = bytearray(b'\x40' * (W * H * 3))
    for (px, py), (w, h, rgb) in zip(place, imgs):
        for yy in range(h):
            for xx in range(min(w, W - px)):
                o = ((py + yy) * W + px + xx) * 3
                sheet[o:o + 3] = rgb[(yy * w + xx) * 3:(yy * w + xx) * 3 + 3]
    png(os.path.join(out, 'sheet.png'), W, H, sheet)
    print('%d pictures, %d size mismatches -> %s (sheet.png %dx%d)' % (len(pics), bad, out, W, H))


if __name__ == '__main__':
    main()
