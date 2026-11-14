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

#include <SDL3/SDL.h>
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

static bool car_blit(FarPtr src, int dpage, const Rect *r);

void amiga_on_blit(FarPtr src, FarPtr dst, const Rect *r, bool masked)
{
    if (!on || !r || desc_type(dst) != DESC_TYPE_EGA) return;
    FarPtr dp = desc_planes(dst);
    int dpage = (int)(((u32)(dp.seg - 0xA000) * 16u + dp.off) / 0x2000);
    if (desc_type(src) == DESC_TYPE_EGA) {                            /* page to page: the regions go along */
        FarPtr sp = desc_planes(src);
        int spage = (int)(((u32)(sp.seg - 0xA000) * 16u + sp.off) / 0x2000);
        ega_copy_backdrops(spage, r->sx, r->sy, r->w, r->h, dpage, r->dx, r->dy);
        return;
    }
    if (getenv("SR_AMIGA_TRACE"))                                     /* developer aid: pictures put on a page */
        for (int i = 0; i < TRACK; i++)
            if (track[i].seg == src.seg && track[i].off == src.off)
                fprintf(stderr, "amiga: picture %d %dx%d -> page %d at %d,%d\n", track[i].id, r->w, r->h, dpage, r->dx, r->dy);
    if (car_blit(src, dpage, r)) return;                             /* a fitted Amiga car */
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
    ega_add_backdrop(ref, rgb, dpage, r->dx, r->dy, w, h, BD_STRICT);   /* takes ownership */
}

/* ---------------------------------------------------------------- cars */

/* The 22 car bodies, in model order (SR2_DATA cars: 0 = 1951 Ford Victoria .. 21 = Shelby GT500; checked by eye
 * 2026-11-14, work/amiga_cars.png); each is followed by its roof / bumper / scoop pieces. Drawn in palette table 7,
 * whose entries 1..9 (and their half-bright copies 33..41) are the paint: a red ramp from 810 to F98. */
static const short car_body[22] = { 181, 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225,
                                    229, 232, 236, 239, 243, 247, 251, 255, 259, 263 };
#define CAR_PAL 7
static struct { uint8_t *idx; int w, h; bool tried; } car_dec[22];
/* the customisation pieces after each body, in the order roof, rear bumper, front bumper (the ones the model has) */
#define PIECES 3
static struct { uint8_t *idx; int w, h, x, y; bool tried, ok; } car_piece[22][PIECES];
static struct { uint8_t *idx, *clear; int flags; } car_work[22];   /* the body with this flags' pieces; clear = bg */
static struct { u16 seg, off; int model, w, h; uint8_t *fit; } car_frames[8];   /* fitted body (0xFF = clear) */
static struct { u16 seg, off, bseg, boff; int shift; } car_shifted[8];          /* frames 1..3: base + shift */
static int car_next;

static bool is_paint(int a) { a &= 63; return (a >= 1 && a <= 9) || (a >= 33 && a <= 41); }

static void rgb_to_hsv(uint32_t c, float *h, float *s, float *v)
{
    float r = (float)(c >> 16 & 255) / 255.0f, g = (float)(c >> 8 & 255) / 255.0f, b = (float)(c & 255) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    if (d <= 0) *h = 0;
    else if (mx == r) *h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) *h = (b - r) / d + 2.0f;
    else *h = (r - g) / d + 4.0f;
    *h /= 6.0f;
}

static uint32_t hsv_to_rgb(float h, float s, float v)
{
    float r = v, g = v, b = v;
    if (s > 0) {
        float f6 = h * 6.0f;
        int i = (int)f6;
        float f = f6 - (float)i;
        float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }
    return 0xFF000000u | (uint32_t)(r * 255.0f + 0.5f) << 16 | (uint32_t)(g * 255.0f + 0.5f) << 8 |
           (uint32_t)(b * 255.0f + 0.5f);
}

static u32 live_rgb(int i)
{
    u8 v = ega_get_palette_reg(i & 15);
    return ega_palette[(v & 7) | (v >> 1 & 8)];
}

/* the body's two paint indices: the two most frequent opaque colours of a body (light first) */
static void paint_pair(const uint8_t *px, int n, int *light, int *dark)
{
    int cnt[16] = { 0 };
    for (int i = 0; i < n; i++) if (px[i] != 0xFF) cnt[px[i] & 15]++;
    int a = 0, b = 1;
    if (cnt[b] > cnt[a]) { a = 1; b = 0; }
    for (int i = 2; i < 16; i++) {
        if (cnt[i] > cnt[a]) { b = a; a = i; }
        else if (cnt[i] > cnt[b]) b = i;
    }
    float ha, sa, va, hb, sb, vb;
    rgb_to_hsv(live_rgb(a), &ha, &sa, &va);
    rgb_to_hsv(live_rgb(b), &hb, &sb, &vb);
    *light = va >= vb ? a : b;
    *dark = va >= vb ? b : a;
}

static const short car_after[22] = { 185, 189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229,
                                     232, 236, 239, 243, 247, 251, 255, 259, 263, 264 };

/* where a piece belongs on the body: the position where most of its pixels equal the body's (its unchanged part:
 * pillars, window lines, the body around a bumper) */
static void piece_place(int model, int k)
{
    int aw = car_dec[model].w, ah = car_dec[model].h, pw = car_piece[model][k].w, ph = car_piece[model][k].h;
    const uint8_t *B = car_dec[model].idx, *P = car_piece[model][k].idx;
    int best = -1;
    for (int y = 0; y + ph <= ah; y++)
        for (int x = 0; x + pw <= aw; x++) {
            int sc = 0;
            for (int j = 0; j < ph; j++)
                for (int i = 0; i < pw; i++) {
                    int q = P[j * pw + i];
                    if (q && q == B[(y + j) * aw + x + i]) sc++;
                }
            if (sc > best) { best = sc; car_piece[model][k].x = x; car_piece[model][k].y = y; }
        }
    car_piece[model][k].ok = best > 0;
}

/* car_work[model] = the body with the pieces flags asks for (2 rear bumper stripped, 1 front, 4 chopped roof),
 * background = colour 0 reached from the edge (black inside the car stays black) */
static bool car_build(int model, int flags)
{
    int aw = car_dec[model].w, ah = car_dec[model].h;
    flags &= 7;
    if (car_work[model].idx && car_work[model].flags == flags) return true;
    free(car_work[model].idx);
    free(car_work[model].clear);
    car_work[model].idx = malloc((size_t)aw * ah);
    car_work[model].clear = calloc((size_t)aw * ah, 1);
    if (!car_work[model].idx || !car_work[model].clear) return false;
    memcpy(car_work[model].idx, car_dec[model].idx, (size_t)aw * ah);
    car_work[model].flags = flags;
    int npieces = car_after[model] - car_body[model] - 1;
    static const int role_flag[PIECES] = { 4, 2, 1 };                  /* roof, rear bumper, front bumper */
    int roles[PIECES], nr = 0;
    for (int k = 0; k < PIECES; k++) {
        bool has = true;                                              /* the PC model has this piece (-1 = none) */
        u16 P = (u16)(DS_MODEL_OVERLAYS + 10 * model);
        int pc = DSS((u16)(P + (k == 0 ? 2 : k == 1 ? 6 : 8)));
        if (pc < 0) has = false;
        if (has) roles[nr++] = k;
    }
    if (nr != npieces) nr = 0;                                        /* no clear pairing: no Amiga pieces */
    if (getenv("SR_AMIGA_TRACE")) fprintf(stderr, "amiga: car %d flags %d: %d pieces, %d roles\n", model, flags, npieces, nr);
    for (int n = 0; n < nr; n++) {
        int k = roles[n];
        if (!(flags & role_flag[k])) continue;
        if (!car_piece[model][k].tried) {
            car_piece[model][k].tried = true;
            car_piece[model][k].idx = amiga_picture(lib[0], lib_n[0], car_body[model] + 1 + n,
                                                    &car_piece[model][k].w, &car_piece[model][k].h);
            if (car_piece[model][k].idx && car_piece[model][k].w <= aw && car_piece[model][k].h <= ah) piece_place(model, k);
        }
        if (!car_piece[model][k].ok) continue;
        int pw = car_piece[model][k].w, ph = car_piece[model][k].h, px = car_piece[model][k].x, py = car_piece[model][k].y;
        for (int j = 0; j < ph; j++)
            memcpy(car_work[model].idx + (py + j) * aw + px, car_piece[model][k].idx + j * pw, (size_t)pw);
    }
    uint8_t *cl = car_work[model].clear;                              /* flood fill of colour 0 from the edge */
    int *stack = malloc((size_t)aw * ah * 4 * sizeof(int)), sp = 0;
    if (!stack) return false;
    for (int x = 0; x < aw; x++) { stack[sp++] = x; stack[sp++] = (ah - 1) * aw + x; }
    for (int y = 0; y < ah; y++) { stack[sp++] = y * aw; stack[sp++] = y * aw + aw - 1; }
    while (sp > 0) {
        int i = stack[--sp];
        if (cl[i] || car_work[model].idx[i] != 0) continue;
        cl[i] = 1;
        int x = i % aw, y = i / aw;
        if (x > 0) stack[sp++] = i - 1;
        if (x + 1 < aw) stack[sp++] = i + 1;
        if (y > 0) stack[sp++] = i - aw;
        if (y + 1 < ah) stack[sp++] = i + aw;
    }
    free(stack);
    return true;
}

bool amiga_car_body(FarPtr frame, s16 model, s16 flags)
{
    if (!on || model < 0 || model >= 22 || desc_type(frame) == DESC_TYPE_EGA) return false;
    if (!car_dec[model].tried) {
        car_dec[model].tried = true;
        car_dec[model].idx = amiga_picture(lib[0], lib_n[0], car_body[model], &car_dec[model].w, &car_dec[model].h);
    }
    if (!car_dec[model].idx) return false;
    int w = desc_w(frame), h = desc_h(frame), aw = car_dec[model].w, ah = car_dec[model].h;
    if (!car_build(model, flags)) return false;
    const uint8_t *AI = car_work[model].idx, *CL = car_work[model].clear;
    FarPtr pl = desc_planes(frame), mk = desc_mask(frame);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(frame), plane = st * (u32)h;
    if (!(mk.seg || mk.off) || b + 4 * plane > MEM_SIZE || mb + plane > MEM_SIZE) return false;
    uint8_t *pc = malloc((size_t)w * h), *fit = malloc((size_t)w * h);
    if (!pc || !fit) { free(pc); free(fit); return false; }
    for (int y = 0; y < h; y++)                                       /* the PC body: its paint pair */
        for (int x = 0; x < w; x++) {
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            int c = 0;
            for (int p = 0; p < 4; p++) if (mem[b + p * plane + o] & bit) c |= 1 << p;
            pc[y * w + x] = (mem[mb + o] & bit) ? 0xFF : (uint8_t)c;
        }
    int light, dark;
    paint_pair(pc, w * h, &light, &dark);
    free(pc);
    int ox = (w - aw) / 2, oy = h - ah;                               /* bottom and centre aligned */
    u32 live[16];
    for (int i = 0; i < 16; i++) live[i] = live_rgb(i);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int ax = x - ox, ay = y - oy;
            bool inside = ax >= 0 && ay >= 0 && ax < aw && ay < ah && !CL[ay * aw + ax];
            int a = inside ? AI[ay * aw + ax] : 0;
            uint8_t q = 0xFF;
            if (inside) {
                if (is_paint(a)) {                                    /* the 16-colour version: two shades */
                    float hh, ss, vv;
                    rgb_to_hsv(pal_rgb[CAR_PAL][a & 63], &hh, &ss, &vv);
                    q = (uint8_t)(vv > 0.62f ? light : dark);
                } else {                                              /* the nearest live colour */
                    u32 c = pal_rgb[CAR_PAL][a & 63];
                    long best = -1;
                    for (int i = 0; i < 16; i++) {
                        long d = labs((long)(c >> 16 & 255) - (long)(live[i] >> 16 & 255)) +
                                 labs((long)(c >> 8 & 255) - (long)(live[i] >> 8 & 255)) +
                                 labs((long)(c & 255) - (long)(live[i] & 255));
                        if (best < 0 || d < best) { best = d; q = (uint8_t)i; }
                    }
                }
            }
            fit[y * w + x] = q;
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            for (int p = 0; p < 4; p++) {
                if (q != 0xFF && (q >> p & 1)) mem[b + p * plane + o] |= bit;
                else mem[b + p * plane + o] &= (u8)~bit;
            }
            if (q == 0xFF) mem[mb + o] |= bit; else mem[mb + o] &= (u8)~bit;
        }
    for (int i = 0; i < 8; i++)                                       /* an earlier fitting of this bitmap */
        if (car_frames[i].fit && car_frames[i].seg == frame.seg && car_frames[i].off == frame.off) {
            free(car_frames[i].fit);
            car_frames[i].fit = NULL;
        }
    free(car_frames[car_next].fit);
    car_frames[car_next].seg = frame.seg;
    car_frames[car_next].off = frame.off;
    car_frames[car_next].model = model;
    car_frames[car_next].w = w;
    car_frames[car_next].h = h;
    car_frames[car_next].fit = fit;
    car_next = (car_next + 1) % 8;
    return true;
}

/* a blit of a fitted car frame: the full-colour Amiga body, its paint ramp turned to the car's paint (hue and
 * saturation of the light paint shade, the Amiga's own brightness), where the frame still shows the body */
static bool car_blit(FarPtr src, int dpage, const Rect *r)
{
    int f = -1, shift = 0;
    FarPtr base = src;
    for (int i = 0; i < 8; i++)
        if (car_shifted[i].seg == src.seg && car_shifted[i].off == src.off && (src.seg || src.off)) {
            base.seg = car_shifted[i].bseg;
            base.off = car_shifted[i].boff;
            shift = car_shifted[i].shift;
        }
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == base.seg && car_frames[i].off == base.off) f = i;
    if (f < 0) return false;
    int model = car_frames[f].model, w = car_frames[f].w, h = car_frames[f].h, sw = desc_w(src);
    if (desc_h(src) != h || sw < w || r->sx < 0 || r->sy < 0 || r->sx + r->w > sw || r->sy + r->h > h) return true;
    FarPtr pl = desc_planes(src), mk = desc_mask(src);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(src), plane = st * (u32)h;
    int aw = car_dec[model].w, ah = car_dec[model].h, ox = (w - aw) / 2, oy = h - ah;
    /* the region in frame coordinates: the blitted rect, grown to the whole Amiga body when the whole frame width is
     * blitted from its top row (the parts outside the PC frame are drawn always: 0xFE) */
    int x0 = r->sx, y0 = r->sy, x1 = r->sx + r->w, y1 = r->sy + r->h;
    if (r->sx == 0 && r->w >= w && r->sy == 0) {
        if (ox + shift < x0) x0 = ox + shift;
        if (ox + shift + aw > x1) x1 = ox + shift + aw;
        if (oy < y0) y0 = oy;
    }
    if (r->dx + (x0 - r->sx) < 0) x0 = r->sx - r->dx;                 /* stay on the page */
    if (r->dx + (x1 - r->sx) > 320) x1 = r->sx - r->dx + 320;
    if (r->dy + (y0 - r->sy) < 0) y0 = r->sy - r->dy;
    int RW = x1 - x0, RH = y1 - y0;
    if (RW <= 0 || RH <= 0) return true;
    uint8_t *ref = malloc((size_t)RW * RH);
    uint32_t *rgb = malloc((size_t)RW * RH * sizeof(uint32_t));
    if (!ref || !rgb) { free(ref); free(rgb); return true; }
    int light, dark;
    paint_pair(car_frames[f].fit, w * h, &light, &dark);
    float ph, ps, pv;
    rgb_to_hsv(live_rgb(light), &ph, &ps, &pv);
    for (int y = 0; y < RH; y++)
        for (int x = 0; x < RW; x++) {
            int fx = x0 + x, fy = y0 + y, ax = fx - shift - ox, ay = fy - oy, k = y * RW + x;
            bool amiga = ax >= 0 && ay >= 0 && ax < aw && ay < ah && !car_work[model].clear[ay * aw + ax];
            uint32_t col = 0;
            if (amiga) {
                int a = car_work[model].idx[ay * aw + ax];
                col = pal_rgb[CAR_PAL][a & 63];
                if (is_paint(a)) {                                   /* the paint, with the Amiga's shading */
                    float hh, ss, vv;
                    rgb_to_hsv(col, &hh, &ss, &vv);
                    col = hsv_to_rgb(ph, ps < 0.08f ? 0.0f : (ss < ps ? ss : ps), vv * (0.55f + 0.45f * pv));
                }
            }
            rgb[k] = col;
            bool in_frame = fx >= r->sx && fy >= r->sy && fx < r->sx + r->w && fy < r->sy + r->h;
            if (!in_frame) { ref[k] = amiga ? 0xFE : 0xFF; continue; }
            u32 o = (u32)fy * st + (u32)(fx >> 3);
            u8 bit = (u8)(0x80 >> (fx & 7));
            int c = 0;
            for (int p = 0; p < 4; p++) if (mem[b + p * plane + o] & bit) c |= 1 << p;
            bool clear = (mk.seg || mk.off) && (mem[mb + o] & bit);
            int bx = fx - shift;
            uint8_t q = (bx >= 0 && bx < w) ? car_frames[f].fit[fy * w + bx] : 0xFF;
            ref[k] = (clear || q == 0xFF || c != q || !amiga) ? 0xFF : (uint8_t)c;
        }
    if (getenv("SR_AMIGA_TRACE"))
        fprintf(stderr, "amiga: car %d -> page %d at %d,%d %dx%d, paint %d/%d\n", model, dpage, r->dx + (x0 - r->sx),
                r->dy + (y0 - r->sy), RW, RH, light, dark);
    ega_add_backdrop(ref, rgb, dpage, r->dx + (x0 - r->sx), r->dy + (y0 - r->sy), RW, RH, BD_CAR);
    return true;
}

void amiga_car_shown(FarPtr frame, FarPtr page, int x, int y, int frow0, int rows)
{
    if (!on || desc_type(page) != DESC_TYPE_EGA) return;
    FarPtr dp = desc_planes(page);
    int dpage = (int)(((u32)(dp.seg - 0xA000) * 16u + dp.off) / 0x2000);
    Rect r;
    r.w = (s16)desc_w(frame); r.h = (s16)rows; r.sx = 0; r.sy = (s16)frow0; r.dx = (s16)x; r.dy = (s16)y;
    car_blit(frame, dpage, &r);
}

void amiga_car_shifted(FarPtr frame, FarPtr base, int shift)
{
    static int next;
    if (!on) return;
    for (int i = 0; i < 8; i++)
        if (car_shifted[i].seg == frame.seg && car_shifted[i].off == frame.off) { next = i; break; }
    car_shifted[next].seg = frame.seg; car_shifted[next].off = frame.off;
    car_shifted[next].bseg = base.seg; car_shifted[next].boff = base.off;
    car_shifted[next].shift = shift;
    next = (next + 1) % 8;
}

void amiga_on_sprite(FarPtr sprite, FarPtr page, const Rect *r)
{
    if (!on || !r || desc_type(page) != DESC_TYPE_EGA) return;
    FarPtr dp = desc_planes(page);
    car_blit(sprite, (int)(((u32)(dp.seg - 0xA000) * 16u + dp.off) / 0x2000), r);
}
