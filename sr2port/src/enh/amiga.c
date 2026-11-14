/* ENHANCED (--amiga): pictures of the Amiga release shown in place of the PC ones, in full colour.
 *
 * The Amiga files are the user's own (Game/amiga, extracted from the .adf disks with tools/unadf.py; never committed).
 * Picture libraries disk2/lib.p, disk1/libstart.p = the PC SR2 LIB directory, big-endian, their own run packing;
 * palettes = 32-colour tables in disk1/STREET_ROD from 0x3C904, 64 bytes apart, depth 6 = extra half-bright
 * (tools/amilib.py, PLAN E7). Which Amiga picture stands for which PC picture: enh/amiga_map.h.
 *
 * How: pic_get tells which bitmap holds which picture id; when such a bitmap is blitted onto a screen page, the blitted
 * rectangle (its current pixels = the reference, so composed backdrops like the garage work too) and the Amiga
 * picture's same rectangle go to the frame composer (ega_add_backdrop), which shows the Amiga pixel wherever the
 * screen still shows the reference. Everything drawn on top stays the game's. The palette table comes with the map. */
#include "enh/amiga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/ega.h"
#include "platform/video.h"

#include "enh/amiga_map.h"

#define NMAP ((int)(sizeof amiga_map / sizeof amiga_map[0]))
#define NPAL 10

static bool on;
static uint8_t *lib[2], *exe;
static size_t lib_n[2], exe_n;
static uint32_t pal_rgb[NPAL][64];
static struct { uint8_t *idx; int w, h; bool tried; } dec[NMAP];   /* decoded Amiga pictures (colour indices) */

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

/* picture idx of a library as colour indices 0..63 (malloc'd), its size in *w, *h */
static uint8_t *amiga_picture(const uint8_t *L, size_t ln, int idx, int *pw, int *ph)
{
    if (!L || ln < 2) return NULL;
    int n = be16(L);
    size_t base = 2 + (size_t)n * 28, r = 2 + (size_t)idx * 28;
    if (idx < 0 || idx >= n || base > ln) return NULL;
    int w = be16(L + r), h = be16(L + r + 2), depth = L[r + 4];
    const uint8_t *tok = L + r + 6;
    size_t off = base + be32(L + r + 22), size = (size_t)be16(L + r + 26);
    if (w <= 0 || h <= 0 || depth < 1 || depth > 6 || off + size > ln) return NULL;
    int bpr = (w + 7) / 8;
    size_t raw = (size_t)bpr * h * depth, o = 0, i = 0;
    uint8_t *px = calloc(raw, 1), *out = malloc((size_t)w * h);
    if (!px || !out) { free(px); free(out); return NULL; }
    const uint8_t *src = L + off;
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
    size_t plane = (size_t)bpr * h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = 0;
            for (int p = 0; p < depth; p++)
                if (px[p * plane + (size_t)y * bpr + (x >> 3)] & (0x80 >> (x & 7))) c |= 1 << p;
            out[y * w + x] = (uint8_t)c;
        }
    free(px);
    *pw = w;
    *ph = h;
    return out;
}

bool amiga_init(bool enable)
{
    on = enable;
    if (!on) return false;
    lib[0] = load("amiga/disk2/lib.p", &lib_n[0]);
    lib[1] = load("amiga/disk1/libstart.p", &lib_n[1]);
    exe = load("amiga/disk1/STREET_ROD", &exe_n);
    if (!lib[0] || !exe || exe_n < 0x3C904 + 64 * NPAL) {
        fprintf(stderr, "amiga: Game/amiga (disk2/lib.p, disk1/STREET_ROD) missing; the PC pictures stay "
                        "(extract the .adf disks with tools/unadf.py)\n");
        on = false;
        return false;
    }
    for (int k = 0; k < NPAL; k++)
        for (int c = 0; c < 32; c++) {
            int v = be16(exe + 0x3C904 + 64 * k + 2 * c);
            uint32_t R = (uint32_t)(v >> 8 & 15) * 17, G = (uint32_t)(v >> 4 & 15) * 17, B = (uint32_t)(v & 15) * 17;
            pal_rgb[k][c] = 0xFF000000u | R << 16 | G << 8 | B;
            pal_rgb[k][c + 32] = 0xFF000000u | (R / 2) << 16 | (G / 2) << 8 | (B / 2);    /* extra half-bright */
        }
    return true;
}

/* ---------------------------------------------------------------- which bitmap holds which picture */

#define TRACK 64
static struct { u16 seg, off; s16 id; } track[TRACK];
static int track_next;

void amiga_note_pic(FarPtr bmp, s16 id)
{
    if (!on) return;
    for (int i = 0; i < TRACK; i++)
        if (track[i].seg == bmp.seg && track[i].off == bmp.off) { track[i].id = id; return; }
    track[track_next].seg = bmp.seg;
    track[track_next].off = bmp.off;
    track[track_next].id = id;
    track_next = (track_next + 1) % TRACK;
}

static int map_of(FarPtr bmp)
{
    for (int i = 0; i < TRACK; i++)
        if (track[i].seg == bmp.seg && track[i].off == bmp.off)
            for (int m = 0; m < NMAP; m++)
                if (amiga_map[m].pc_id == track[i].id) return m;
    return -1;
}

/* ---------------------------------------------------------------- blits onto the screen */

void amiga_on_blit(FarPtr src, FarPtr dst, const Rect *r, bool masked)
{
    if (!on || !r || desc_type(dst) != DESC_TYPE_EGA || desc_type(src) == DESC_TYPE_EGA) return;
    int m = map_of(src);
    if (m < 0) return;
    if (!dec[m].tried) {
        dec[m].tried = true;
        int l = amiga_map[m].lib;
        dec[m].idx = amiga_picture(lib[l], lib_n[l], amiga_map[m].index, &dec[m].w, &dec[m].h);
    }
    int sw = desc_w(src), sh = desc_h(src);
    if (!dec[m].idx || dec[m].w != sw || dec[m].h != sh) return;
    int w = r->w, h = r->h, sx = r->sx, sy = r->sy;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || sx + w > sw || sy + h > sh) return;
    FarPtr pl = desc_planes(src), mk = desc_mask(src);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(src), plane = st * (u32)sh;
    bool has_mask = masked && (mk.seg || mk.off);
    if (b + 4 * plane > MEM_SIZE || (has_mask && mb + plane > MEM_SIZE)) return;
    uint8_t *ref = malloc((size_t)w * h);
    uint32_t *rgb = malloc((size_t)w * h * sizeof(uint32_t));
    if (!ref || !rgb) { free(ref); free(rgb); return; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 o = (u32)(sy + y) * st + (u32)((sx + x) >> 3);
            u8 bit = (u8)(0x80 >> ((sx + x) & 7));
            int c = 0;
            for (int p = 0; p < 4; p++) if (mem[b + p * plane + o] & bit) c |= 1 << p;
            bool clear = has_mask && (mem[mb + o] & bit) != 0;
            ref[y * w + x] = clear ? 0xFF : (uint8_t)c;               /* 0xFF: not part of the picture */
            rgb[y * w + x] = pal_rgb[amiga_map[m].pal % NPAL][dec[m].idx[(sy + y) * sw + sx + x] & 63];
        }
    ega_add_backdrop(ref, rgb, r->dx, r->dy, w, h);                     /* takes ownership */
}
