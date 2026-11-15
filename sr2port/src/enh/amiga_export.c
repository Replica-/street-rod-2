/* ENHANCED: the picture set of the Amiga graphics (gfx/), made at the first start from the Amiga disks in the game folder
 * (SR2AMIGA_DISK1.adf, SR2AMIGA_DISK2.adf) and the PC libraries (LIB1-3): the C version of tools/amiga_export.py with
 * tools/unadf.py, amilib.py and srlib.py (see there for the formats; this file follows them line by line). No Python
 * needed. The PC pictures' default palette is the game's own (DS:1DC8 of the loaded SR.EXE). */
#include "enh/amiga_export.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "mem.h"

#include "../../../tools/amiga_map.h"

/* ---------------------------------------------------------------- ADF (tools/unadf.py) */

typedef struct { const Uint8 *img; size_t n; bool ffs; } Adf;

static const Uint8 *blk(const Adf *a, Sint32 b) { return (size_t)(b + 1) * 512 <= a->n && b >= 0 ? a->img + (size_t)b * 512 : NULL; }
static Sint32 L(const Uint8 *b, int i) { const Uint8 *p = b + 4 * i; return (Sint32)((Uint32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

static Uint8 *adf_read_file(const Adf *a, const Uint8 *hdr, size_t *out_n)
{
    size_t size = (Uint32)L(hdr, 81), n = 0;                 /* byte 324 */
    Uint8 *data = malloc(size + 512);
    if (!data) return NULL;
    for (;;) {
        for (int i = 77; i > 5; i--) {
            Sint32 p = L(hdr, i);
            if (p == 0) break;
            const Uint8 *d = blk(a, p);
            if (!d) { free(data); return NULL; }
            size_t k = a->ffs ? 512 : (Uint32)L(d, 3);
            if (!a->ffs && k > 488) k = 488;
            if (n + k > size + 512) k = size + 512 - n;
            memcpy(data + n, a->ffs ? d : d + 24, k);
            n += k;
        }
        Sint32 ext = L(hdr, 126);
        if (ext == 0 || !(hdr = blk(a, ext))) break;
    }
    *out_n = n < size ? n : size;
    return data;
}

/* the first file called `name` (any case) anywhere below directory block b */
static Uint8 *adf_find(const Adf *a, const Uint8 *b, const char *name, size_t *n, int depth)
{
    if (depth > 8) return NULL;
    for (int i = 6; i < 78; i++)
        for (Sint32 p = L(b, i), guard = 0; p && guard < 1000; guard++) {
            const Uint8 *e = blk(a, p);
            if (!e) break;
            char nm[32];
            int len = e[432] < 31 ? e[432] : 31;
            memcpy(nm, e + 433, (size_t)len);
            nm[len] = 0;
            Sint32 st = L(e, 127);
            Uint8 *r = NULL;
            if (st == 2) r = adf_find(a, e, name, n, depth + 1);
            else if (st == -3 && SDL_strcasecmp(nm, name) == 0) r = adf_read_file(a, e, n);
            if (r) return r;
            p = L(e, 124);
        }
    return NULL;
}

static Uint8 *adf_file(const char *adf_path, const char *name, size_t *n)
{
    size_t sz;
    Uint8 *img = SDL_LoadFile(adf_path, &sz);
    if (!img || sz < 1024) { SDL_free(img); return NULL; }
    Adf a = { img, sz, (img[3] & 1) != 0 };
    const Uint8 *root = blk(&a, (Sint32)(sz / 512 / 2));
    Uint8 *r = root ? adf_find(&a, root, name, n, 0) : NULL;
    SDL_free(img);
    return r;
}

/* ---------------------------------------------------------------- Amiga libraries (tools/amilib.py) */

typedef struct { int w, h, depth; const Uint8 *tokens, *data; int size; } APic;
typedef struct { APic *pic; int n; } ALib;

static const Uint8 RUN[16][2] = { { 0, 1 }, { 255, 1 }, { 0, 2 }, { 0, 3 }, { 255, 2 }, { 0, 4 }, { 0, 5 }, { 255, 3 },
                                  { 0, 6 }, { 255, 4 }, { 0, 7 }, { 255, 5 }, { 0, 8 }, { 255, 6 }, { 0, 9 }, { 255, 7 } };

static int be16(const Uint8 *p) { return p[0] << 8 | p[1]; }
static Uint32 be32(const Uint8 *p) { return (Uint32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static bool alib_read(ALib *l, const Uint8 *d, size_t n)
{
    if (n < 2) return false;
    l->n = be16(d);
    size_t base = 2 + (size_t)l->n * 28;
    if (base > n) return false;
    l->pic = calloc((size_t)l->n, sizeof *l->pic);
    for (int i = 0; i < l->n; i++) {
        const Uint8 *p = d + 2 + i * 28;
        APic *q = &l->pic[i];
        q->w = be16(p); q->h = be16(p + 2); q->depth = p[4];
        q->tokens = p + 6;
        size_t off = base + be32(p + 22);
        q->size = be16(p + 26);
        if (off > n) { q->size = 0; off = n; }
        if (off + (size_t)q->size > n) q->size = (int)(n - off);
        q->data = d + off;
    }
    return true;
}

/* the colour indices of picture i (w * h ints), *w, *h, *depth */
static int *adecode(const ALib *l, int i, int *w, int *h, int *depth)
{
    const APic *q = &l->pic[i];
    int bpr = (q->w + 7) / 8;
    size_t need = (size_t)bpr * q->h * q->depth, n = 0;
    Uint8 *raw = calloc(need + 16, 1);
    int tok[256];
    for (int k = 0; k < 256; k++) tok[k] = -1;
    for (int k = 0; k < 16; k++) if (q->tokens[k]) tok[q->tokens[k]] = k;
    for (int s = 0; s < q->size;) {
        Uint8 b = q->data[s++];
        int v, c;
        if (b == 0x00 || b == 0xFF) { int m = s < q->size ? q->data[s] : 0; v = b; c = m ? m + 1 : 1; s++; }
        else if (tok[b] >= 0) { v = RUN[tok[b]][0]; c = RUN[tok[b]][1]; }
        else { v = b; c = 1; }
        for (; c > 0 && n < need; c--) raw[n++] = (Uint8)v;
    }
    int *idx = calloc((size_t)q->w * q->h + 1, sizeof *idx);
    for (int pl = 0; pl < q->depth; pl++)
        for (int y = 0; y < q->h; y++) {
            const Uint8 *row = raw + ((size_t)pl * q->h + y) * bpr;
            for (int x = 0; x < q->w; x++)
                if (row[x / 8] & (0x80 >> (x % 8))) idx[y * q->w + x] |= 1 << pl;
        }
    free(raw);
    *w = q->w; *h = q->h;
    if (depth) *depth = q->depth;
    return idx;
}

static Uint8 *street_rod;                   /* the Amiga exe (palette tables) */
static size_t street_rod_n;

/* exe_palette(n, depth): 1 << depth colours, 0xRRGGBB */
static int exe_palette(int t, int depth, Uint32 *pal)
{
    Uint32 p[64];
    for (int k = 0; k < 32; k++) {
        size_t o = 0x3C904 + 64 * (size_t)t + 2 * k;
        int x = o + 1 < street_rod_n ? be16(street_rod + o) : 0;
        int r = (x >> 8 & 15) * 17, g = (x >> 4 & 15) * 17, b = (x & 15) * 17;
        p[k] = (Uint32)(r << 16 | g << 8 | b);
        p[k + 32] = (Uint32)((r / 2) << 16 | (g / 2) << 8 | b / 2);
    }
    int n = 1 << depth;
    if (n > 64) n = 64;
    memcpy(pal, p, sizeof(Uint32) * (size_t)n);
    return n;
}

static int pmod(int c, int n) { return ((c % n) + n) % n; }

/* ---------------------------------------------------------------- PC libraries (tools/srlib.py) */

static const int RUN_COUNT[16] = { 1, 1, 2, 3, 4, 2, 3, 5, 6, 6, 4, 7, 7, 5, 8, 8 };
static const Uint8 RUN_VALUE[16] = { 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0x00 };
static const Uint32 EGA16[16] = { 0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                                  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };

static int le16(const Uint8 *p) { return p[0] | p[1] << 8; }
static Uint32 le32(const Uint8 *p) { return p[0] | p[1] << 8 | (Uint32)p[2] << 16 | (Uint32)p[3] << 24; }

/* ---------------------------------------------------------------- BMP files */

static bool write_bmp(const char *path, int w, int h, const Uint32 *argb)
{
    size_t data = (size_t)w * h * 4, hdr = 14 + 108;
    Uint8 *f = calloc(hdr + data, 1);
    if (!f) return false;
    f[0] = 'B'; f[1] = 'M';
    SDL_memcpy(f + 2, &(Uint32){ (Uint32)(hdr + data) }, 4);
    SDL_memcpy(f + 10, &(Uint32){ (Uint32)hdr }, 4);
    Uint8 *i = f + 14;
    Uint32 v[] = { 108, (Uint32)w, (Uint32)-h };
    SDL_memcpy(i, v, 12);
    i[12] = 1; i[14] = 32; i[16] = 3;
    SDL_memcpy(i + 20, &(Uint32){ (Uint32)data }, 4);
    SDL_memcpy(i + 24, &(Uint32){ 2835 }, 4);
    SDL_memcpy(i + 28, &(Uint32){ 2835 }, 4);
    Uint32 masks[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 };
    SDL_memcpy(i + 40, masks, 16);
    memcpy(i + 56, "BGRs", 4);
    SDL_memcpy(f + hdr, argb, data);                  /* little-endian ARGB words = B G R A bytes */
    bool ok = SDL_SaveFile(path, f, hdr + data);
    free(f);
    return ok;
}

static bool write_bmp8(const char *path, int w, int h, const Uint8 *idx, const Uint32 *pal)
{
    int pad = (4 - w % 4) % 4;
    size_t data = (size_t)(w + pad) * h, hdr = 14 + 40 + 64;
    Uint8 *f = calloc(hdr + data, 1);
    if (!f) return false;
    f[0] = 'B'; f[1] = 'M';
    SDL_memcpy(f + 2, &(Uint32){ (Uint32)(hdr + data) }, 4);
    SDL_memcpy(f + 10, &(Uint32){ (Uint32)hdr }, 4);
    Uint8 *i = f + 14;
    Uint32 v[] = { 40, (Uint32)w, (Uint32)h };
    SDL_memcpy(i, v, 12);
    i[12] = 1; i[14] = 8;
    SDL_memcpy(i + 20, &(Uint32){ (Uint32)data }, 4);
    SDL_memcpy(i + 24, &(Uint32){ 2835 }, 4);
    SDL_memcpy(i + 28, &(Uint32){ 2835 }, 4);
    SDL_memcpy(i + 32, &(Uint32){ 16 }, 4);
    for (int k = 0; k < 16; k++) {
        Uint8 *c = f + 54 + 4 * k;
        c[0] = (Uint8)pal[k]; c[1] = (Uint8)(pal[k] >> 8); c[2] = (Uint8)(pal[k] >> 16);
    }
    for (int y = 0; y < h; y++) memcpy(f + hdr + (size_t)(h - 1 - y) * (w + pad), idx + (size_t)y * w, (size_t)w);
    bool ok = SDL_SaveFile(path, f, hdr + data);
    free(f);
    return ok;
}

/* ---------------------------------------------------------------- the export (tools/amiga_export.py) */

static const int CAR_BODY[22] = { 181, 185, 189, 193, 197, 201, 205, 209, 213, 247, 221, 225, 229, 232, 236, 239, 243,
                                  217, 251, 255, 259, 263 };
static const int WHEELS[3] = { 137, 135, 136 };
#define CAR_PAL 7
#define PAINT_ROWS 16
#define STRAY 0xFF00CCu
static bool is_paint(int c) { return (c >= 1 && c <= 9) || (c >= 33 && c <= 41); }
static bool is_paint_pic(int id) { return id == 2011 || id == 2012 || id == 2019; }
static bool is_clock(int id) { return id == 2059 || id == 2025; }
static bool is_crop_wheel(int id)
{
    static const int ids[] = { 2072, 2073, 2074, 2021, 2023, 2024, 2077, 2076, 2075 };
    for (size_t k = 0; k < SDL_arraysize(ids); k++) if (ids[k] == id) return true;
    return false;
}
static const struct { int k; Uint32 rgb; } HAND_SKIN[] = {
    { 6, 0xFAD2AA }, { 7, 0xECBE94 }, { 8, 0xDAA87E }, { 9, 0xC49068 }, { 11, 0xAA7854 }, { 12, 0x8E6242 },
    { 39, 0x966C4C }, { 40, 0x886042 }, { 41, 0x785238 }, { 43, 0x6E4A32 }, { 44, 0x583A26 },
};

static char out_dir[1024];

static bool save32(const char *name, int w, int h, const Uint32 *px)
{
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", out_dir, name);
    return write_bmp(path, w, h, px);
}

static Uint32 *car_argb(const int *I, int w, int h, const Uint32 *pal)
{
    Uint32 *o = malloc(sizeof *o * (size_t)w * h);
    for (int i = 0; i < w * h; i++)
        o[i] = I[i] == 0 ? 0 : (Uint32)(is_paint(I[i] & 63) ? 254 : 255) << 24 | pal[pmod(I[i], 64)];
    return o;
}

static void place(const int *B, int aw, int ah, const int *P, int pw, int ph, int *bx, int *by)
{
    int best = -1;
    *bx = *by = 0;
    for (int y = 0; y <= ah - ph; y++)
        for (int x = 0; x <= aw - pw; x++) {
            int sc = 0;
            for (int j = 0; j < ph; j++) {
                const int *b = B + (y + j) * aw + x, *p = P + j * pw;
                for (int i = 0; i < pw; i++) sc += p[i] && p[i] == b[i];
            }
            if (sc > best) { best = sc; *bx = x; *by = y; }
        }
}

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* arches(): the two wheel arches (centre x, apex y); returns 0 or 2 */
static int arches(const int *B, int w, int h, int found[2][2])
{
    int *low = malloc(sizeof *low * (size_t)w), *body = malloc(sizeof *body * (size_t)w), nb = 0;
    for (int x = 0; x < w; x++) {
        low[x] = -1;
        for (int y = 0; y < h; y++) if (B[y * w + x]) low[x] = y;
        if (low[x] >= 0) body[nb++] = low[x];
    }
    int res = 0;
    if (nb) {
        qsort(body, (size_t)nb, sizeof *body, cmp_int);
        int sill = body[nb * 3 / 4];
        int runs[256][3], nr = 0;                       /* (length, x0, x1) */
        for (int x = 0; x < w;) {
            if (low[x] >= 0 && low[x] < sill - 5) {
                int x0 = x;
                while (x < w && low[x] >= 0 && low[x] < sill - 5) x++;
                if (x0 > 0 && x < w && nr < 256) { runs[nr][0] = x - x0; runs[nr][1] = x0; runs[nr][2] = x; nr++; }
            } else x++;
        }
        /* the two largest by (length, x0, x1) descending, then left to right */
        for (int a = 0; a < nr; a++)
            for (int b = a + 1; b < nr; b++) {
                bool gt = runs[b][0] != runs[a][0] ? runs[b][0] > runs[a][0]
                        : runs[b][1] != runs[a][1] ? runs[b][1] > runs[a][1] : runs[b][2] > runs[a][2];
                if (gt) for (int k = 0; k < 3; k++) { int t = runs[a][k]; runs[a][k] = runs[b][k]; runs[b][k] = t; }
            }
        if (nr >= 2) {
            int s0 = runs[0][1] < runs[1][1] ? 0 : 1, s1 = 1 - s0;
            int sel[2] = { s0, s1 };
            for (int k = 0; k < 2; k++) {
                int a = runs[sel[k]][1], b = runs[sel[k]][2], m = low[a];
                for (int x = a; x < b; x++) if (low[x] < m) m = low[x];
                found[k][0] = (a + b) / 2; found[k][1] = m;
            }
            if (!(found[0][0] < w * 0.08 || found[1][0] > w * 0.92 || found[1][0] - found[0][0] < w * 0.4)) res = 2;
        }
    }
    free(low);
    free(body);
    return res;
}

static bool export_pc(const char *game_dir, const char *lib, int base, const bool *have, const Uint32 *pcpal, int *count)
{
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", game_dir, lib);
    size_t n;
    Uint8 *d = SDL_LoadFile(path, &n);
    if (!d) {                                           /* any case */
        int c = 0;
        char **e = SDL_GlobDirectory(game_dir, NULL, 0, &c);
        for (int i = 0; e && i < c && !d; i++)
            if (SDL_strcasecmp(e[i], lib) == 0) { SDL_snprintf(path, sizeof path, "%s/%s", game_dir, e[i]); d = SDL_LoadFile(path, &n); }
        SDL_free(e);
    }
    if (!d || n < 4) { SDL_free(d); return false; }
    int cnt = le16(d), rec = 30;
    if ((size_t)(3 + cnt * 30) <= n && (size_t)(3 + cnt * 28) <= n && d[2 + cnt * 30] != 0xBC && d[2 + cnt * 28] == 0xBC) rec = 28;
    size_t dir_end = 2 + (size_t)cnt * rec;
    for (int i = 0; i < cnt; i++) {
        const Uint8 *p = d + 2 + i * rec;
        int w, h, raw, size;
        const Uint8 *tokens;
        Uint32 off;
        if (rec == 30) { w = le16(p); h = le16(p + 2); raw = le16(p + 4); tokens = p + 7; off = le32(p + 24); size = le16(p + 28); }
        else { w = le16(p); h = le16(p + 2); raw = w * h * p[4] / 8; tokens = p + 6; off = le32(p + 22); size = le16(p + 26); }
        int pid = base + i;
        char name[32];
        SDL_snprintf(name, sizeof name, "pic_%d.bmp", pid);
        if (pid == 1149) {                              /* HIDE: Burger's man by the door (the Amiga picture has him) */
            Uint32 *z = calloc((size_t)w * h + 1, 4);
            save32(name, w, h, z);
            free(z);
            (*count)++;
            continue;
        }
        if ((pid < 3000 && have[pid]) || w <= 0 || h <= 0) continue;
        size_t at = dir_end + off;
        if (at + (size_t)size + 1 > n || d[at] != 0xBC) continue;
        const Uint8 *src = d + at + 1;
        int ns = size - 1, tok[256];
        for (int k = 0; k < 256; k++) tok[k] = -1;
        for (int k = 0; k < 16; k++) if (tokens[k]) tok[tokens[k]] = k;
        Uint8 *pix = calloc((size_t)raw + 16, 1);
        int o = 0;
        for (int s = 0; s < ns && o < raw;) {
            Uint8 b = src[s++];
            if (b == 0x00 || b == 0xFF) {
                int m = s < ns ? src[s] : 0;
                if (rec == 28 && m == 0) pix[o++] = b == 0 ? tokens[0] : tokens[1];
                else for (int c = 0; c < m + 1 && o < raw; c++) pix[o++] = b;
                s++;
            } else if (tok[b] >= 0) for (int c = 0; c < RUN_COUNT[tok[b]] && o < raw; c++) pix[o++] = RUN_VALUE[tok[b]];
            else pix[o++] = b;
        }
        int bpr = w / 8;
        Uint8 *idx = calloc((size_t)w * h, 1);
        for (int pl = 0; pl < 4; pl++)
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    size_t q = (size_t)pl * bpr * h + (size_t)y * bpr + x / 8;
                    if (q < (size_t)raw && pix[q] & (0x80 >> (x % 8))) idx[y * w + x] |= (Uint8)(1 << pl);
                }
        char path2[1200];
        SDL_snprintf(path2, sizeof path2, "%s/%s", out_dir, name);
        write_bmp8(path2, w, h, idx, pcpal);
        free(idx);
        free(pix);
        (*count)++;
    }
    SDL_free(d);
    return true;
}

bool amiga_export_run(const char *game_dir, const char *out, char *err, size_t errn)
{
    char p1[1200], p2[1200];
    SDL_snprintf(p1, sizeof p1, "%s/SR2AMIGA_DISK1.adf", game_dir);
    SDL_snprintf(p2, sizeof p2, "%s/SR2AMIGA_DISK2.adf", game_dir);
    size_t n_lib = 0, n_start = 0;
    Uint8 *lib = adf_file(p2, "lib.p", &n_lib), *start = adf_file(p1, "libstart.p", &n_start);
    street_rod = adf_file(p1, "STREET_ROD", &street_rod_n);
    ALib libs[2] = { { 0 }, { 0 } };
    if (!lib || !start || !street_rod || !alib_read(&libs[0], lib, n_lib) || !alib_read(&libs[1], start, n_start)) {
        SDL_snprintf(err, errn, "the Amiga disks (%s) are missing or not readable", "SR2AMIGA_DISK1/2.adf");
        free(lib); free(start); free(street_rod); street_rod = NULL;
        return false;
    }
    SDL_strlcpy(out_dir, out, sizeof out_dir);
    SDL_CreateDirectory(out_dir);
    int count = 0;
    static bool have[3000];
    memset(have, 0, sizeof have);
    for (size_t k = 0; k < SDL_arraysize(amiga_map); k++) if (amiga_map[k].pc_id < 3000) have[amiga_map[k].pc_id] = true;

    /* the PC pictures without an Amiga version, in the game's default palette (DS:1DC8) */
    Uint32 pcpal[16];
    const Uint8 *regs = mp(DGROUP, 0x1DC8);
    for (int k = 0; k < 16; k++) pcpal[k] = EGA16[regs[k] & 15];
    if (!export_pc(game_dir, "LIB1", 1, have, pcpal, &count) || !export_pc(game_dir, "LIB2", 1000, have, pcpal, &count) ||
        !export_pc(game_dir, "LIB3", 2000, have, pcpal, &count)) {
        SDL_snprintf(err, errn, "the PC libraries (%s) are missing", "LIB1-3");
        return false;
    }

    /* the Amiga pictures */
    for (size_t k = 0; k < SDL_arraysize(amiga_map); k++) {
        int pid = amiga_map[k].pc_id, li = amiga_map[k].lib, ix = amiga_map[k].index;
        if (ix >= libs[li].n) continue;
        int w, h, depth;
        int *I = adecode(&libs[li], ix, &w, &h, &depth);
        Uint32 p[64];
        int np = exe_palette(amiga_map[k].pal, depth, p);
        if (is_clock(pid)) {
            int *J = malloc(sizeof *J * (size_t)w * h);
            for (int i = 0; i < w * h; i++) {
                Uint32 c = p[pmod(I[i], np)];
                bool red = (c >> 16 & 255) > 150 && (c >> 8 & 255) < 110;
                J[i] = red ? I[(h - 1 - i / w) * w + w - 1 - i % w] : I[i];
            }
            free(I);
            I = J;
        }
        int cx = 0, cy = 0, cw = 0, ch = 0, cut_black = -1;
        bool crop = true;
        if (pid == 3) { cx = 40; cy = 40; cw = 256; ch = 97; cut_black = 95; }
        else if (is_crop_wheel(pid)) { cx = 8; cy = 0; cw = 184; ch = 30; }
        else if (pid == 2059) { cx = 4; cy = 0; cw = 40; ch = 25; }
        else if (pid == 2025) { cx = 0; cy = -1; cw = 40; ch = 33; }
        else crop = false;
        if (crop) {
            int *J = malloc(sizeof *J * (size_t)cw * ch);
            for (int y = 0; y < ch; y++)
                for (int x = 0; x < cw; x++)
                    J[y * cw + x] = cy + y >= 0 && cy + y < h && cx + x >= 0 && cx + x < w ? I[(cy + y) * w + cx + x] : -1;
            if (cut_black >= 0) for (int y = cut_black; y < ch; y++) for (int x = 0; x < cw; x++) J[y * cw + x] = 0;
            if (pid == 3)                               /* PASTE: its "presents", where the PC has it */
                for (int y = 0; y < 11; y++)
                    for (int x = 0; x < 68; x++) J[(84 + y) * cw + 180 + x] = I[(148 + y) * w + 210 + x];
            free(I);
            I = J; w = cw; h = ch;
        }
        if (is_paint_pic(pid))
            for (int i = 0; i < w * h; i++) {
                int c = I[i];
                if (i / w >= PAINT_ROWS && i % w && ((c & 31) == 6 || (c & 31) == 7 || p[pmod(c, np)] == STRAY)) I[i] = I[i - 1];
            }
        Uint32 *px = malloc(sizeof *px * (size_t)w * h);
        for (int i = 0; i < w * h; i++) {
            int c = I[i];
            Uint32 a = is_paint_pic(pid) && i / w < PAINT_ROWS && c >= 0 && ((c & 31) == 6 || (c & 31) == 7) ? 0xFE000000u : 0xFF000000u;
            px[i] = c < 0 ? 0 : a | p[pmod(c, np)];
        }
        char name[32];
        SDL_snprintf(name, sizeof name, "pic_%d.bmp", pid);
        save32(name, w, h, px);
        free(px);
        free(I);
        count++;
    }

    /* the driver's hands */
    {
        int w, h, depth;
        free(adecode(&libs[0], 70, &w, &h, &depth));
        Uint32 hp[64];
        int np = exe_palette(1, depth, hp);
        for (size_t k = 0; k < SDL_arraysize(HAND_SKIN); k++) if (HAND_SKIN[k].k < np) hp[HAND_SKIN[k].k] = HAND_SKIN[k].rgb;
        static const char *names[4] = { "hand_l.bmp", "hand_r.bmp", "arm_l.bmp", "arm_r.bmp" };
        for (int k = 0; k < 4; k++) {
            int *I = adecode(&libs[0], 79 + k, &w, &h, NULL);
            Uint32 *px = malloc(sizeof *px * (size_t)w * h);
            for (int i = 0; i < w * h; i++) px[i] = I[i] ? 0xFF000000u | hp[pmod(I[i], np)] : 0;
            save32(names[k], w, h, px);
            free(px);
            free(I);
            count++;
        }
    }

    /* the cars, their pieces and wheel arches, the wheels */
    Uint32 pal[64];
    exe_palette(CAR_PAL, 6, pal);
    char *lines = calloc(64 * 1024, 1);
    size_t ln = 0;
    int sorted[23];
    memcpy(sorted, CAR_BODY, sizeof CAR_BODY);
    sorted[22] = 264;
    qsort(sorted, 22, sizeof *sorted, cmp_int);
    for (int m = 0; m < 22; m++) {
        int body = CAR_BODY[m], after = 264;
        for (int k = 0; k < 22; k++) if (sorted[k] == body) { after = sorted[k + 1]; break; }
        int aw, ah;
        int *B = adecode(&libs[0], body, &aw, &ah, NULL);
        Uint32 *px = car_argb(B, aw, ah, pal);
        char name[48];
        SDL_snprintf(name, sizeof name, "car_%d.bmp", m);
        save32(name, aw, ah, px);
        free(px);
        static const char *all[3] = { "roof", "rbump", "fbump" }, *m10[2] = { "rbump", "fbump" }, *m12[2] = { "roof", "rbump" };
        const char *const *roles = all;
        int nroles = 3;
        if (m == 10) { roles = m10; nroles = 2; }
        else if (m == 12 || m == 14) { roles = m12; nroles = 2; }
        else if (m == 21) nroles = 0;
        if (nroles == after - body - 1)
            for (int k = 0; k < nroles; k++) {
                int pw, ph, x, y;
                int *P = adecode(&libs[0], body + 1 + k, &pw, &ph, NULL);
                place(B, aw, ah, P, pw, ph, &x, &y);
                Uint32 *pp = car_argb(P, pw, ph, pal);
                SDL_snprintf(name, sizeof name, "car_%d_%s.bmp", m, roles[k]);
                save32(name, pw, ph, pp);
                free(pp);
                free(P);
                ln += (size_t)SDL_snprintf(lines + ln, 64 * 1024 - ln, "%d %s %d %d\n", m, roles[k], x, y);
            }
        free(B);
        count++;
    }
    for (int m = 0; m < 22; m++) {
        int aw, ah, f[2][2];
        int *B = adecode(&libs[0], CAR_BODY[m], &aw, &ah, NULL);
        if (arches(B, aw, ah, f) == 2)
            for (int k = 0; k < 2; k++) ln += (size_t)SDL_snprintf(lines + ln, 64 * 1024 - ln, "%d wheel%d %d %d\n", m, k, f[k][0], f[k][1]);
        free(B);
    }
    for (int t = 0; t < 3; t++) {
        int sw, sh;
        int *S = adecode(&libs[0], WHEELS[t], &sw, &sh, NULL);
        int cw = sw / 4;
        int *W = malloc(sizeof *W * (size_t)cw * sh);
        for (int f = 0; f < 4; f++) {
            for (int y = 0; y < sh; y++) for (int x = 0; x < cw; x++) W[y * cw + x] = S[y * sw + f * cw + x];
            Uint32 *px = car_argb(W, cw, sh, pal);
            char name[32];
            SDL_snprintf(name, sizeof name, "wheel_%d_%d.bmp", t, f);
            save32(name, cw, sh, px);
            free(px);
        }
        free(W);
        free(S);
    }
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/cars.txt", out_dir);
    FILE *fp = fopen(path, "wb");
    bool ok = fp != NULL;
    if (fp) {
        fputs("# model piece x y (where the piece goes on car_<model>.bmp; wheel0 / wheel1: centre x, arch top y)\n", fp);
        fwrite(lines, 1, ln, fp);
        fclose(fp);
    }
    free(lines);
    free(libs[0].pic); free(libs[1].pic);
    free(lib); free(start); free(street_rod); street_rod = NULL;
    if (!ok) SDL_snprintf(err, errn, "could not write %s", path);
    else fprintf(stderr, "amiga: %d pictures and cars -> %s\n", count, out_dir);
    return ok;
}
