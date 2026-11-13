"""Minimal Amiga OFS/FFS .adf extractor: unadf.py IMAGE OUTDIR"""
import os, struct, sys

img = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
ffs = img[3] & 1
BS = 512


def blk(n):
    return img[n * BS:(n + 1) * BS]


def L(b, i):
    return struct.unpack_from('>l', b, i * 4)[0]


def name(b):
    n = b[432]
    return b[433:433 + n].decode('latin-1')


def read_file(b):
    size = struct.unpack_from('>L', b, 324)[0]
    data, hdr = bytearray(), b
    while True:                                   # data block pointers, stored from the end of the table
        for i in range(77, 5, -1):
            p = L(hdr, i)
            if p == 0:
                break
            d = blk(p)
            data += d if ffs else d[24:24 + struct.unpack_from('>L', d, 12)[0]]
        ext = L(hdr, 126)
        if ext == 0:
            break
        hdr = blk(ext)
    return bytes(data[:size])


count = 0


def walk(b, path):
    global count
    os.makedirs(path, exist_ok=True)
    for i in range(6, 78):
        p = L(b, i)
        while p:
            e = blk(p)
            nm = name(e).replace('/', '_').replace('\\', '_').replace(':', '_')
            st = L(e, 127)
            if st == 2:
                walk(e, os.path.join(path, nm))
            elif st == -3:
                with open(os.path.join(path, nm), 'wb') as f:
                    f.write(read_file(e))
                count += 1
            p = L(e, 124)


root = blk(len(img) // BS // 2)
print('volume', name(root), 'FFS' if ffs else 'OFS')
walk(root, out)
print(count, 'files ->', out)
