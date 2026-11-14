"""The picture set for --amiga, made once into one folder of ready files: python tools/amiga_export.py [OUT]

Reads the PC libraries (Game/LIB1-3) and the user's Amiga release (Game/amiga, the .adf disks extracted with
tools/unadf.py) and writes OUT (default gfx, next to the executable in a package; game data, never committed):

  pic_<id>.bmp      every picture the game draws, by its id (LIB1 #i = i + 1, LIB2 #i = 1000 + i, LIB3 #i = 2000 + i):
                    the Amiga version where there is one (tools/amiga_map.h; 32-bit, full colour with its palette
                    table, same size as the PC picture), else the PC picture (8-bit, the game's colour indices with
                    the default palette; the game draws it in its own colours)
  car_<m>.bmp       the Amiga body of car model m (0..21), alpha 0 = see-through (Amiga colour 0, as the Amiga draws
                    its cars: the background and the windows), 254 = paint (recoloured to the car's paint at run
                    time, keeping its brightness), 255 = other
  car_<m>_<piece>.bmp  the customisation pieces (roof = chopped roof, rbump / fbump = stripped bumpers), same coding
  cars.txt          per model: the size of the body and where each piece goes on it ("<m> <piece> <x> <y>")

32-bit BMPs have BI_BITFIELDS with an alpha mask; all load with SDL_LoadBMP.
"""
import os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amilib, srlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, 'Game/amiga/disk2/lib.p')
LIBSTART = os.path.join(ROOT, 'Game/amiga/disk1/libstart.p')
MAP_H = os.path.join(ROOT, 'tools/amiga_map.h')

# the 22 car bodies in model order, then their pieces (roof, rear bumper, front bumper: the ones the model has)
CAR_BODY = [181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 232, 236, 239, 243, 247, 251, 255, 259, 263]
CAR_AFTER = CAR_BODY[1:] + [264]
# the pieces each PC model has (SR2_DATA overlays block: roof, rear bumper, front bumper; -1 = none)
PC_HAS = {10: ('rbump', 'fbump'), 12: ('roof', 'rbump'), 14: ('roof', 'rbump'), 21: ()}
CAR_PAL = 7
PAINT = set(range(1, 10)) | set(range(33, 42))     # table 7: the red ramp 810..F98 and its half-bright copies
# PC pictures that are part of a full Amiga screen: the PC id -> (x, y, w, h) cut from the Amiga picture
CROP = {3: (40, 40, 256, 97)}                       # the California Dreams logo, drawn at (40, 40) on the PC


def write_bmp(path, w, h, argb):
    """32-bit top-down BMP, BI_BITFIELDS, masks R G B A."""
    row = w * 4
    data = b''.join(struct.pack('<%dI' % w, *argb[y * w:(y + 1) * w]) for y in range(h))
    info = struct.pack('<IiiHHIIiiII', 108, w, -h, 1, 32, 3, len(data), 2835, 2835, 0, 0)
    info += struct.pack('<IIII', 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    info += b'BGRs' + bytes(36) + bytes(12)
    head = b'BM' + struct.pack('<IHHI', 14 + len(info) + len(data), 0, 0, 14 + len(info))
    with open(path, 'wb') as f:
        f.write(head + info + data)


def write_bmp8(path, w, h, idx, pal):
    """8-bit bottom-up BMP with a 16-entry colour table (the colour indices kept)."""
    pad = (-w) % 4
    data = b''.join(bytes(idx[y * w:(y + 1) * w]) + bytes(pad) for y in range(h - 1, -1, -1))
    table = b''.join(bytes((c[2], c[1], c[0], 0)) for c in pal)
    info = struct.pack('<IiiHHIIiiII', 40, w, h, 1, 8, 0, len(data), 2835, 2835, len(pal), 0)
    head = b'BM' + struct.pack('<IHHI', 14 + len(info) + len(table) + len(data), 0, 0, 14 + len(info) + len(table))
    with open(path, 'wb') as f:
        f.write(head + info + table + data)


def rgb(c):
    return (c[0] << 16) | (c[1] << 8) | c[2]


def decode(pics, i):
    p = pics[i]
    return p['w'], p['h'], list(amilib.indices(p, amilib.unpack(p)))


def place(B, aw, ah, P, pw, ph):
    best = (-1, 0, 0)
    for y in range(ah - ph + 1):
        for x in range(aw - pw + 1):
            sc = 0
            for j in range(ph):
                base = (y + j) * aw + x
                for i in range(pw):
                    q = P[j * pw + i]
                    if q and q == B[base + i]:
                        sc += 1
            if sc > best[0]:
                best = (sc, x, y)
    return best[1], best[2]


def car_argb(I, w, h, pal):
    clear = bytearray(1 if c == 0 else 0 for c in I)       # colour 0 is see-through (windows too), as on the Amiga
    out = []
    for i, c in enumerate(I):
        if clear[i]:
            out.append(0)
        else:
            a = 254 if (c & 63) in PAINT else 255
            out.append((a << 24) | rgb(pal[c % len(pal)]))
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'gfx')
    os.makedirs(out, exist_ok=True)
    libs = [amilib.read(LIB), amilib.read(LIBSTART)]
    amiga = re.findall(r'\{ (\d+), (\d), (\d+), (\d+) \}', open(MAP_H).read())
    have = {int(a[0]) for a in amiga}
    n = 0
    pcpal = srlib.game_palette(0x1DC8) if os.path.exists(os.path.join(ROOT, 'work/SR2_unp.exe')) else srlib.EGA16
    for lib, base in (('LIB1', 1), ('LIB2', 1000), ('LIB3', 2000)):     # the PC pictures without an Amiga version
        for p in srlib.read_lib(os.path.join(ROOT, 'Game', lib)):
            pid = base + p['index']
            if pid in have or p['w'] <= 0 or p['h'] <= 0:
                continue
            idx = srlib.to_indices(p, srlib.unpack(p['data'], p['tokens'], p['raw'], p['sr2']))
            write_bmp8(os.path.join(out, 'pic_%d.bmp' % pid), p['w'], p['h'], idx, pcpal)
            n += 1
    for pid, lib, idx, pal in amiga:
        w, h, I = decode(libs[int(lib)], int(idx))
        if int(pid) in CROP:
            cx, cy, cw, ch = CROP[int(pid)]
            I = [I[(cy + y) * w + cx + x] for y in range(ch) for x in range(cw)]
            w, h = cw, ch
        p = amilib.exe_palette(int(pal), libs[int(lib)][int(idx)]['depth'])
        write_bmp(os.path.join(out, 'pic_%s.bmp' % pid), w, h, [0xFF000000 | rgb(p[c % len(p)]) for c in I])
        n += 1
    pal = amilib.exe_palette(CAR_PAL, 6)
    lines = []
    for m, body in enumerate(CAR_BODY):
        aw, ah, B = decode(libs[0], body)
        write_bmp(os.path.join(out, 'car_%d.bmp' % m), aw, ah, car_argb(B, aw, ah, pal))
        roles = PC_HAS.get(m, ('roof', 'rbump', 'fbump'))
        npieces = CAR_AFTER[m] - body - 1
        if len(roles) == npieces:
            for k, role in enumerate(roles):
                pw, ph, P = decode(libs[0], body + 1 + k)
                x, y = place(B, aw, ah, P, pw, ph)
                write_bmp(os.path.join(out, 'car_%d_%s.bmp' % (m, role)), pw, ph, car_argb(P, pw, ph, pal))
                lines.append('%d %s %d %d' % (m, role, x, y))
        n += 1
    with open(os.path.join(out, 'cars.txt'), 'w', newline='\n') as f:
        f.write('# model piece x y (where the piece goes on car_<model>.bmp)\n' + '\n'.join(lines) + '\n')
    print('%d pictures and cars -> %s' % (n, out))


if __name__ == '__main__':
    main()
