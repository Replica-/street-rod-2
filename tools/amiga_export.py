"""The picture set for --amiga, made once into one folder of ready files: python tools/amiga_export.py [OUT]

Reads the PC libraries (Game/LIB1-3) and the user's Amiga release (Game/amiga, the .adf disks extracted with
tools/unadf.py) and writes OUT (default gfx, next to the executable in a package; game data, never committed):

  pic_<id>.bmp      every picture the game draws, by its id (LIB1 #i = i + 1, LIB2 #i = 1000 + i, LIB3 #i = 2000 + i):
                    the Amiga version where there is one (tools/amiga_map.h; 32-bit, full colour with its palette
                    table, same size as the PC picture), else the PC picture (8-bit, the game's colour indices with
                    the default palette; the game draws it in its own colours); HIDE: 32-bit see-through (not shown)
  car_<m>.bmp       the Amiga body of car model m (0..21), alpha 0 = see-through (Amiga colour 0, as the Amiga draws
                    its cars: the background and the windows), 254 = paint (recoloured to the car's paint at run
                    time, keeping its brightness), 255 = other
  car_<m>_<piece>.bmp  the customisation pieces (roof = chopped roof, rbump / fbump = stripped bumpers), same coding
  cars.txt          per model: the size of the body and where each piece goes on it ("<m> <piece> <x> <y>")
  hands_<w>.bmp     the driver's hands over the dashboard (320 x 90, alpha 0 = none) with steering wheel picture w
                    (0 straight, 1 turned left, 2 turned right)

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
CAR_BODY = [181, 185, 189, 193, 197, 201, 205, 209, 213, 247, 221, 225, 229, 232, 236, 239, 243, 217, 251, 255, 259, 263]   # 9 / 17 by their PC pictures
_S = sorted(CAR_BODY) + [264]
CAR_AFTER = [_S[_S.index(b) + 1] for b in CAR_BODY]   # the next body in the library: the pieces lie between
# the pieces each PC model has (SR2_DATA overlays block: roof, rear bumper, front bumper; -1 = none)
PC_HAS = {10: ('rbump', 'fbump'), 12: ('roof', 'rbump'), 14: ('roof', 'rbump'), 21: ()}
CAR_PAL = 7
# the Amiga wheel sheets (4 turning frames side by side) for the PC tyre types DS:4D30 (1050-1053, 1014-1017, 1084-1087)
WHEELS = [137, 135, 136]
PAINT = set(range(1, 10)) | set(range(33, 42))     # table 7: the red ramp 810..F98 and its half-bright copies
# PC pictures that are part of a full Amiga screen: the PC id -> (x, y, w, h) cut from the Amiga picture
CROP = {3: (40, 40, 256, 97)}                       # the California Dreams logo, drawn at (40, 40) on the PC
# the steering wheel's lower part (straight, turned left, turned right) of the three dashboards: the Amiga's 192 columns
# lie at x 72 of the dashboard, the PC's 184 at x 80
CROP.update({p: (8, 0, 184, 30) for p in (2072, 2073, 2074, 2021, 2023, 2024, 2077, 2076, 2075)})
# PC pictures drawn over an Amiga picture that already shows them: written see-through (hidden, what is there stays)
# race pictures whose Amiga colours 6 / 7 (and half-bright 38 / 39) are the car's paint (the dashboards' top band):
# alpha 254 = recoloured to the car's paint at run time, as the cars
PAINT_PICS = {2011, 2012, 2019}
# the driver's hands, a layer over the dashboard in front of its needles (hands_<wheel>.bmp, 320 x 90, alpha 0 = none),
# per wheel picture (0 straight, 1 turning left, 2 turning right): the Amiga hand pictures centred at (x, y) of the
# dashboard. #79 / #80 the left / right fist on the rim where it meets the bottom of the screen, #81 the left arm pushing
# the rim up on the left (turning right), #82 the right arm on the right (turning left). Their colours 6, 7, 8, 9, 11,
# 12 and the half-bright ones are one skin ramp, lit to shadow (the Amiga sets 6-9 per screen band)
HANDS = {0: [(79, 85, 71), (80, 260, 71)], 1: [(79, 85, 71), (82, 269, 59)], 2: [(81, 74, 59), (80, 260, 71)]}
HAND_SKIN = {6: (250, 210, 170), 7: (236, 190, 148), 8: (218, 168, 126), 9: (196, 144, 104), 11: (170, 120, 84),
             12: (142, 98, 66), 39: (150, 108, 76), 40: (136, 96, 66), 41: (120, 82, 56), 43: (110, 74, 50),
             44: (88, 58, 38)}
HIDE = {1149}                                      # Burger's Bungalow: the man by the door (the Amiga picture has him)


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


def arches(B, w, h):
    """The two wheel arches of a body (colour 0 = see-through): the column runs where the body's lowest opaque row is
    well above its sill; (centre x, apex y) of the two widest, left to right."""
    low = []
    for x in range(w):
        ys = [y for y in range(h) if B[y * w + x]]
        low.append(max(ys) if ys else -1)
    body = sorted(v for v in low if v >= 0)
    sill = body[len(body) * 3 // 4]
    runs, x = [], 0
    while x < w:
        if 0 <= low[x] < sill - 5:
            x0 = x
            while x < w and 0 <= low[x] < sill - 5:
                x += 1
            if x0 > 0 and x < w:                       # not the overhangs at the ends
                runs.append((x - x0, x0, x))
        else:
            x += 1
    runs = sorted(sorted(runs, reverse=True)[:2], key=lambda r: r[1])
    found = [((a + b) // 2, min(low[a:b])) for _, a, b in runs]
    if len(found) != 2 or found[0][0] < w * 0.08 or found[1][0] > w * 0.92 or found[1][0] - found[0][0] < w * 0.4:
        return []                                       # not two clear arches: the game places the wheels itself
    return found


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
            if pid in HIDE:
                write_bmp(os.path.join(out, 'pic_%d.bmp' % pid), p['w'], p['h'], [0] * (p['w'] * p['h']))
                n += 1
                continue
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
        a = lambda c: 0xFE000000 if int(pid) in PAINT_PICS and (c & 31) in (6, 7) else 0xFF000000
        px = [a(c) | rgb(p[c % len(p)]) for c in I]
        write_bmp(os.path.join(out, 'pic_%s.bmp' % pid), w, h, px)
        n += 1
    hp = list(amilib.exe_palette(1, libs[0][70]['depth']))
    for k, c in HAND_SKIN.items():
        hp[k] = c
    for state, hands in HANDS.items():
        px = [0] * (320 * 90)
        for hand, cx, cy in hands:
            hw, hh, HI = decode(libs[0], hand)
            for y in range(hh):
                for x in range(hw):
                    c = HI[y * hw + x]
                    tx, ty = cx - hw // 2 + x, cy - hh // 2 + y
                    if c and 0 <= tx < 320 and 0 <= ty < 90:
                        px[ty * 320 + tx] = 0xFF000000 | rgb(hp[c % len(hp)])
        write_bmp(os.path.join(out, 'hands_%d.bmp' % state), 320, 90, px)
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
    for m, body in enumerate(CAR_BODY):                # the wheel arches: where the wheels go
        aw, ah, B = decode(libs[0], body)
        for k, (cx, ay) in enumerate(arches(B, aw, ah)):
            lines.append('%d wheel%d %d %d' % (m, k, cx, ay))
    for t, sheet in enumerate(WHEELS):                  # the wheels: 4 turning frames per type
        sw, sh, S = decode(libs[0], sheet)
        cw = sw // 4
        for f in range(4):
            W = [S[y * sw + f * cw + x] for y in range(sh) for x in range(cw)]
            write_bmp(os.path.join(out, 'wheel_%d_%d.bmp' % (t, f)), cw, sh, car_argb(W, cw, sh, pal))
    with open(os.path.join(out, 'cars.txt'), 'w', newline='\n') as f:
        f.write('# model piece x y (where the piece goes on car_<model>.bmp; wheel0 / wheel1: centre x, arch top y)\n' +
                '\n'.join(lines) + '\n')
    print('%d pictures and cars -> %s' % (n, out))


if __name__ == '__main__':
    main()
