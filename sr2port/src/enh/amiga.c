/* ENHANCED (--amiga): pictures of the Amiga release shown in place of the PC ones, in full colour.
 *
 * The Amiga files are the user's own (Game/amiga, extracted from the .adf disks with tools/unadf.py; never committed).
 * Picture library disk2/lib.p = the PC SR2 LIB directory, big-endian, its own run packing; palettes = 32-colour
 * tables in disk1/STREET_ROD from 0x3C904, 64 bytes apart, depth 6 = extra half-bright (tools/amilib.py, PLAN E7).
 *
 * Garage: Amiga lib #144 (320 x 190) replaces LIB2 #18. The garage hands over the backdrop it composed (#18 + two
 * overlays) as the reference; the frame composer then shows the Amiga pixel wherever the screen still shows the
 * backdrop, so everything drawn on top (the car, highlights, texts) stays the game's own (ega_set_backdrop). */
#include "enh/amiga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/ega.h"
#include "platform/video.h"

#define GARAGE_PIC 144
#define GARAGE_PAL 7                /* the table that fits (checked 2026-11-14 on lib #114 / #115 / #120 / #144) */
#define GW 320
#define GH 190

static bool on;
static uint32_t garage_rgb[GW * GH];
static bool garage_ok;
static uint8_t garage_ref[GW * GH];

static uint8_t *load(const char *name, size_t *n)
{
    char *path = host_game_path(name, false);
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    host_free(path);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = len > 0 ? malloc((size_t)len) : NULL;
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f);
    if (b) *n = (size_t)len;
    return b;
}

static int be16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* token k -> {value, count}: single 00 / FF, then zero runs 2..9 and FF runs 2..7 interleaved (tools/amilib.py) */
static const uint8_t run_val[16] = { 0, 255, 0, 0, 255, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255 };
static const uint8_t run_cnt[16] = { 1, 1, 2, 3, 2, 4, 5, 3, 6, 4, 7, 5, 8, 6, 9, 7 };

/* picture idx of an Amiga library as 0xAARRGGBB through palette table pal of the exe */
static bool amiga_picture(const uint8_t *lib, size_t ln, const uint8_t *exe, size_t en, int idx, int pal,
                          uint32_t *out, int ow, int oh)
{
    if (ln < 2) return false;
    int n = be16(lib);
    size_t base = 2 + (size_t)n * 28, r = 2 + (size_t)idx * 28;
    if (idx >= n || base > ln) return false;
    int w = be16(lib + r), h = be16(lib + r + 2), depth = lib[r + 4];
    const uint8_t *tok = lib + r + 6;
    size_t off = base + be32(lib + r + 22), size = (size_t)be16(lib + r + 26);
    if (w != ow || h != oh || depth < 1 || depth > 6 || off + size > ln) return false;
    int bpr = (w + 7) / 8;
    size_t raw = (size_t)bpr * h * depth;
    uint8_t *px = calloc(raw, 1);
    if (!px) return false;
    size_t o = 0, i = 0;
    const uint8_t *src = lib + off;
    while (i < size && o < raw) {
        uint8_t b = src[i++];
        int k = -1;
        for (int t = 0; t < 16; t++) if (tok[t] && tok[t] == b) { k = t; break; }
        if ((b == 0 || b == 0xFF) && i < size) {
            size_t c = src[i] ? (size_t)src[i] + 1 : 1;
            i++;
            for (; c && o < raw; c--) px[o++] = b;
        } else if (k >= 0) {
            for (int c = run_cnt[k]; c && o < raw; c--) px[o++] = run_val[k];
        } else px[o++] = b;
    }
    size_t pa = 0x3C904 + 64 * (size_t)pal;
    if (pa + 64 > en) { free(px); return false; }
    uint32_t rgb[64];
    for (int c = 0; c < 32; c++) {
        int v = be16(exe + pa + 2 * c);
        uint32_t R = (uint32_t)(v >> 8 & 15) * 17, G = (uint32_t)(v >> 4 & 15) * 17, B = (uint32_t)(v & 15) * 17;
        rgb[c] = 0xFF000000u | R << 16 | G << 8 | B;
        rgb[c + 32] = 0xFF000000u | (R / 2) << 16 | (G / 2) << 8 | (B / 2);          /* extra half-bright */
    }
    size_t plane = (size_t)bpr * h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = 0;
            for (int p = 0; p < depth; p++)
                if (px[p * plane + (size_t)y * bpr + (x >> 3)] & (0x80 >> (x & 7))) c |= 1 << p;
            out[y * w + x] = rgb[c];
        }
    free(px);
    return true;
}

bool amiga_init(bool enable)
{
    on = enable;
    if (!on) return false;
    size_t ln = 0, en = 0;
    uint8_t *lib = load("amiga/disk2/lib.p", &ln), *exe = load("amiga/disk1/STREET_ROD", &en);
    garage_ok = lib && exe && amiga_picture(lib, ln, exe, en, GARAGE_PIC, GARAGE_PAL, garage_rgb, GW, GH);
    free(lib);
    free(exe);
    if (!garage_ok) fprintf(stderr, "amiga: Game/amiga/disk2/lib.p or disk1/STREET_ROD missing or unreadable; "
                                    "the PC pictures stay (extract the .adf disks with tools/unadf.py)\n");
    return garage_ok;
}

void amiga_garage_backdrop(FarPtr bmp)
{
    if (!on || !garage_ok) return;
    if (desc_w(bmp) != GW || desc_h(bmp) != GH || desc_type(bmp) == DESC_TYPE_EGA) return;
    FarPtr pl = desc_planes(bmp);
    u32 b = lin(pl.seg, pl.off), st = desc_stride(bmp), plane = st * GH;
    if (b + 4 * plane > MEM_SIZE) return;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            int c = 0;
            for (int p = 0; p < 4; p++) if (mem[b + p * plane + o] & bit) c |= 1 << p;
            garage_ref[y * GW + x] = (uint8_t)c;
        }
    ega_set_backdrop(garage_ref, garage_rgb, 0, 0, GW, GH);
}
