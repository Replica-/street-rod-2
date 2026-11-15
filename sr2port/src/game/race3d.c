/* ENHANCED (R1.2): the 3D race view's hooks in road_frame.
 *
 * In 3D mode road_frame still draws the original road and mirror (so every side effect of the 2D renderer stays),
 * then race3d_overlays replaces the overlay step: the view rectangle (without the mirror) is filled with colour 0
 * and the overlays drawn, the result saved, the view filled with colour 15 and the overlays drawn again. Pixels that
 * come out the same both times belong to an overlay (or the mirror); the rest is see-through, and the 3D view shows
 * there (ega_set_page_mask). The start-light slide's state is put back between the two passes. */
#include "game/race3d.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "game/race_int.h"
#include "platform/ega.h"
#include "platform/shadow.h"
#include "r3d/r3d.h"

#define MODE DSS(DS_road_mode)

#define VIEW_Y0 18                  /* race view rows (race_render.md 1.3) */
#define VIEW_Y1 101
#define MIRROR_Y0 20                /* the mirror box (mirror_draw_backdrop): x 240..319 (byte 30 on), rows 20..44, */
#define MIRROR_Y1 44                /* and its stem x 272..287 (bytes 34, 35) in rows 18, 19 */
#define MIRROR_B0 30
#define MIRROR_STEM_B0 34

static u8 masks[3][320 * 200];
static u8 saved[4][(VIEW_Y1 - VIEW_Y0 + 1) * EGA_BYTES_PER_LINE];

bool race3d_on(void)
{
    if (!r3d_active() || r3d_classic()) return false;
    return MODE == 0 || MODE == 1 || MODE == 2 || MODE == 4 || MODE == 8 || MODE == 0x10;
}

static u32 page_base(void) { return (u32)DSS(DS_back_page) * 0x2000u; }

#define GLASS_X0 243                /* R1.5: the mirror glass inside its frame */
#define GLASS_X1 317
#define GLASS_Y0 22
#define GLASS_Y1 42

static void fill_view(int colour)
{
    u32 base = page_base();
    for (int p = 0; p < 4; p++) {
        u8 v = (colour >> p & 1) ? 0xFF : 0x00, *pl = ega_plane(p);
        for (int y = VIEW_Y0; y <= VIEW_Y1; y++) {
            u8 *row = pl + ((base + (u32)y * EGA_BYTES_PER_LINE) & 0xFFFF);
            if (y < MIRROR_Y0) {                                 /* above the box only the stem x 272..287 */
                memset(row, v, MIRROR_STEM_B0);
                memset(row + MIRROR_STEM_B0 + 2, v, EGA_BYTES_PER_LINE - MIRROR_STEM_B0 - 2);
            } else memset(row, v, (size_t)(y <= MIRROR_Y1 ? MIRROR_B0 : EGA_BYTES_PER_LINE));
            if (y >= GLASS_Y0 && y <= GLASS_Y1)                  /* the glass, pixel by pixel (frame kept) */
                for (int x = GLASS_X0; x <= GLASS_X1; x++) {
                    u8 bit = (u8)(0x80 >> (x & 7));
                    row[x >> 3] = (u8)((row[x >> 3] & ~bit) | (v & bit));
                }
        }
    }
}

static void save_view(void)
{
    u32 base = page_base();
    for (int p = 0; p < 4; p++)
        memcpy(saved[p], ega_plane(p) + ((base + VIEW_Y0 * EGA_BYTES_PER_LINE) & 0xFFFF), sizeof saved[p]);
}

static void build_mask(u8 *mask)
{
    u32 base = page_base();
    memset(mask, 0, 320 * 200);
    for (int y = VIEW_Y0; y <= VIEW_Y1; y++) {
        const u8 *now[4], *old[4];
        for (int p = 0; p < 4; p++) {
            now[p] = ega_plane(p) + ((base + (u32)y * EGA_BYTES_PER_LINE) & 0xFFFF);
            old[p] = saved[p] + (y - VIEW_Y0) * EGA_BYTES_PER_LINE;
        }
        for (int b = 0; b < EGA_BYTES_PER_LINE; b++) {
            u8 diff = (u8)((now[0][b] ^ old[0][b]) | (now[1][b] ^ old[1][b]) | (now[2][b] ^ old[2][b]) |
                           (now[3][b] ^ old[3][b]));
            if (!diff) continue;
            for (int bit = 0; bit < 8; bit++)
                if (diff & (0x80 >> bit)) mask[y * 320 + b * 8 + bit] = 1;
        }
    }
}

/* ---------------------------------------------------------------- sprites (R1.4) */

#define PIC_MAX (320 * 200)
static u32 pic_px[PIC_MAX];

/* the 16 live colours (0xFFRRGGBB) of the palette registers; --amiga: with the cars' own paint, as the screen shows */
static void live_palette(u32 rgb[16])
{
    u8 regs[16];
    for (int i = 0; i < 16; i++) {
        u8 r = regs[i] = ega_get_palette_reg(i);
        rgb[i] = ega_palette[(r & 7) | (r >> 1 & 8)] | 0xFF000000u;
    }
    if (sh_enabled()) sh_paint_regs(rgb, regs);
}

/* A RAM bitmap descriptor (video.h: planes one after the other, stride bytes per row, mask 1 = transparent)
 * as BGRA through the current palette registers. */
static bool decode_pic(FarPtr d, int *pw, int *ph)
{
    if (!d.seg && !d.off) return false;
    int w = desc_w(d), h = desc_h(d), st = desc_stride(d);
    if (w <= 0 || h <= 0 || w * h > PIC_MAX || st <= 0 || desc_type(d) == DESC_TYPE_EGA) return false;
    FarPtr pl = desc_planes(d), mk = desc_mask(d);
    u32 base = lin(pl.seg, pl.off), mbase = lin(mk.seg, mk.off), plane = (u32)st * (u32)h;
    bool has_mask = mk.seg || mk.off;
    if (base + 4 * plane > MEM_SIZE || (has_mask && mbase + plane > MEM_SIZE)) return false;
    u32 rgb[16];
    live_palette(rgb);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            int c = 0;
            for (int k = 0; k < 4; k++)
                if (mem[base + k * plane + o] & bit) c |= 1 << k;
            bool clear = has_mask ? (mem[mbase + o] & bit) != 0 : c == 0;
            pic_px[y * w + x] = clear ? 0 : rgb[c];
        }
    *pw = w;
    *ph = h;
    return true;
}

static FarPtr opp_pic(int s) { return ds_far((u16)(DS_opp_sprites + 4 * s)); }

/* The opponent (opponent_draw af98 has just run in the 2D frame and chosen view DS:A7D4 and picture DS:B198):
 * the largest picture of the same kind (side 0, rear quarters 1 / 3, rear 2 for 2 and 4..8), sized in the world
 * from the original's own size at its distance (screen px = 4 * world / z), smoothed. */
static void opp_sprite(R3dSprite *o)
{
    static float ew[13], eh[13];
    memset(o, 0, sizeof *o);
    if (!DSB(DS_racing) || MODE == 2) return;
    s16 s = DSS(DS_opp_sprite), d = DSS(DS_opp_last_d);
    if (s < 0 || s > 12) return;                           /* not chosen */
    int kind = (s == 0 || s == 1 || s == 3) ? s : s >= 9 ? 9 : 2;    /* 9..12: front views, behind us (mirror) */
    int w, h;
    FarPtr cur = ds_far(DS_opp_pic);
    if (desc_w(cur) > 0) {
        int z = d / 8;
        if (s == 0) z += 0x10;
        if (z < -2 && s == 2) z += 8;
        float sc;                                          /* world units per frame pixel at that depth */
        if (s >= 9) sc = (float)(z < 0 ? -z : z);          /* road_proj_x_mirror: no x4 */
        else sc = (float)((z < 0 ? -z : z) + 4) / 4.0f;
        float ww = desc_w(cur) * sc, hh = desc_h(cur) * sc;
        if (ew[kind] <= 0) { ew[kind] = ww; eh[kind] = hh; }
        else { ew[kind] += (ww - ew[kind]) * 0.3f; eh[kind] += (hh - eh[kind]) * 0.3f; }
    }
    /* the Aqueduct's pictures are the original's rotated bank variants (opp_sprite_aq): use the one it chose */
    if (ew[kind] <= 0 || !decode_pic(MODE == 8 ? cur : opp_pic(kind), &w, &h)) return;
    r3d_sprite_upload(0, pic_px, w, h);
    o->vis = true;
    o->slot = 0;
    o->pos = (float)DSS(DS_opp_seg) + (float)DSS(DS_opp_seg_frac64) / 64.0f;
    o->lat = (float)DSS(DS_opp_lat);
    o->h = eh[kind];
    o->w = eh[kind] * (float)w / (float)h;                 /* the big picture's own aspect */
    o->along = s == 0;
}

/* ---------------------------------------------------------------- track access */

/* The drag strip (modes 2, 4; road_set_seg_ptrs 056a) reads its flags from L2 / R2 / C2[seg] (0x74 entries) and
 * the geometry from seg + 0xC0, flat. */
#define DRAG_N 0x74
static bool drag(void) { return (MODE & 6) != 0; }
static int geo(int i) { return drag() ? (i + 0xC0 < R3D_TRACK_N ? i + 0xC0 : R3D_TRACK_N - 1) : i; }
static u16 flag_l(int i) { return drag() ? (i < DRAG_N ? TRKW(TRK_L2, i) : 0) : TRKW(TRK_L, i); }
static u16 flag_r(int i) { return drag() ? (i < DRAG_N ? TRKW(TRK_R2, i) : 0) : TRKW(TRK_R, i); }
static u16 flag_c(int i) { return drag() ? (i < DRAG_N ? TRKW(TRK_C2, i) : 0) : TRKW(TRK_C, i); }
static s16 bank_l(int i) { return drag() ? 0 : TRKS(TRK_BL, i); }
static s16 bank_r(int i) { return drag() ? 0 : TRKS(TRK_BR, i); }

/* ---------------------------------------------------------------- scenery (R1.4) */

/* SR2_SHP format-A set at DS:8172[type]: {u16 n_lods; LodRec far *recs}; LodRec = 16 bytes: 6 x {u8 colour, u8 count}
 * (count 0 ends), far spans at +12; span = {s8 dx, s8 dy, s8 w}: pixels dx..dx+w on row dy (shape_draw 4154). */
typedef struct { u32 spans; int n, x0, x1, y0, y1; } LodBox;

static bool lod_box(u32 recs, int l, LodBox *b)
{
    u32 r = recs + 16u * (u32)l;
    if (r + 16 > MEM_SIZE) return false;
    b->spans = lin(mem[r + 14] | mem[r + 15] << 8, mem[r + 12] | mem[r + 13] << 8);
    b->n = 0;
    for (int k = 0; k < 6 && mem[r + 2 * k + 1]; k++) b->n += mem[r + 2 * k + 1];
    if (b->n <= 0 || b->spans + 3u * (u32)b->n > MEM_SIZE) return false;
    b->x0 = b->y0 = 1000;
    b->x1 = b->y1 = -1000;
    for (int i = 0; i < b->n; i++) {
        const u8 *s = mem + b->spans + 3 * i;
        int dx = (s8)s[0], dy = (s8)s[1], w = (s8)s[2];
        if (dx < b->x0) b->x0 = dx;
        if (dx + w > b->x1) b->x1 = dx + w;
        if (dy < b->y0) b->y0 = dy;
        if (dy > b->y1) b->y1 = dy;
    }
    return b->x1 >= b->x0 && b->y1 >= b->y0;
}

/* the largest LOD as a picture, with its world scale: the original uses LOD l at depth row 9 - l (scenery_draw_shape
 * 5158), row k at about z = 8k + 4, so world height = pixel height * z / 4; the median over the LODs */
static void upload_shape(int type, const u32 *rgb)
{
    FarPtr set = ds_far((u16)(DS_shp_a_ptrs + 4 * type));
    if (!set.seg && !set.off) return;
    u32 sa = lin(set.seg, set.off);
    if (sa + 6 > MEM_SIZE) return;
    int n = mem[sa] | mem[sa + 1] << 8;
    if (n < 1 || n > 12) return;
    u32 recs = lin(mem[sa + 4] | mem[sa + 5] << 8, mem[sa + 2] | mem[sa + 3] << 8);
    LodBox best = { 0 }, b;
    float worlds[12];
    int nw = 0, big = -1;
    for (int l = 0; l < n; l++) {
        if (!lod_box(recs, l, &b)) continue;
        int h = b.y1 - b.y0 + 1, row = 9 - l < 1 ? 1 : 9 - l;
        worlds[nw++] = (float)h * (float)(8 * row + 4) / 4.0f;
        if (big < 0 || h > best.y1 - best.y0 + 1) { best = b; big = l; }
    }
    if (big < 0 || nw == 0) return;
    for (int i = 1; i < nw; i++)                          /* median of the world heights */
        for (int j = i; j > 0 && worlds[j] < worlds[j - 1]; j--) { float t = worlds[j]; worlds[j] = worlds[j - 1]; worlds[j - 1] = t; }
    float world_h = worlds[nw / 2];
    int w = best.x1 - best.x0 + 1, h = best.y1 - best.y0 + 1;
    if (w * h > PIC_MAX) return;
    memset(pic_px, 0, (size_t)w * h * sizeof(u32));
    u32 r = recs + 16u * (u32)big;
    int span = 0;
    for (int k = 0; k < 6 && mem[r + 2 * k + 1]; k++) {
        u32 c = rgb[mem[r + 2 * k] & 15];
        for (int m = mem[r + 2 * k + 1]; m > 0; m--, span++) {
            const u8 *s = mem + best.spans + 3 * span;
            int dx = (s8)s[0], dy = (s8)s[1], sw = (s8)s[2];
            for (int x = dx; x <= dx + sw; x++) pic_px[(dy - best.y0) * w + (x - best.x0)] = c;
        }
    }
    r3d_shape_upload(type, pic_px, w, h, -best.x0, -best.y0, world_h / (float)h);
    if (SDL_getenv("SR_R3D_TRACE") && mem[0] != 0xFF) {             /* developer aid: once per shape and mode */
        static int seen[R3D_SHAPES][0x20];
        if (!seen[type][MODE & 0x1F]++) fprintf(stderr, "r3d shape %d: %dx%d lods %d scale %.1f\n", type, w, h, n, (double)(world_h / (float)h));
    }
}

/* Sign pictures (scenery_draw_sign_picture 52e2): kind 1 = the location-sign sheet in page 0 rows 100.. , kind 2 / 3 =
 * the two town billboards (DS:96AE); size 6 (the nearest) = sub-rectangle DS:7F02[6] {srcx, w, h}, used at depth row
 * 3 (z ~ 28: 7 world units per pixel). Uploaded as shapes 23.. with the origin where the original anchors them. */
#define SIGN_BILL2 23
#define SIGN_BILL3 24
#define SIGN_LOC 25                 /* centred (banner) */
#define SIGN_RIGHT 26               /* right edge at x (left corner sign, align 1) */
#define SIGN_LEFT 27                /* left edge at x (right corner sign) */

static void upload_signs(const u32 *rgb)
{
    u16 e = (u16)(DS_sign_sizes + 6 * 6);
    int sx = DSS(e), w = DSS((u16)(e + 2)), h = DSS((u16)(e + 4));
    if (w <= 0 || h <= 0 || w > 320 || h > 100) return;
    static u32 crop[320 * 100];
    for (int k = 0; k < 2; k++) {                                  /* billboards */
        int pw, ph;
        if (!decode_pic(ds_far((u16)(DS_billboard_pics + 4 * k)), &pw, &ph)) continue;
        if (sx + w > pw || h > ph) continue;
        for (int y = 0; y < h; y++) memcpy(crop + y * w, pic_px + y * pw + sx, (size_t)w * sizeof(u32));
        r3d_shape_upload(SIGN_BILL2 + k, crop, w, h, w / 2, h / 2, 7.0f);
    }
    FarPtr pg = desc_planes(ds_far(DS_page0_ptr));                  /* location signs: VGA page 0, rows 100.. */
    u32 base = (u32)(pg.seg - 0xA000) * 16u + pg.off;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 o = (base + (u32)(100 + y) * EGA_BYTES_PER_LINE + (u32)((sx + x) >> 3)) & 0xFFFF;
            u8 bit = (u8)(0x80 >> ((sx + x) & 7));
            int c = 0;
            for (int pl = 0; pl < 4; pl++) if (ega_plane(pl)[o] & bit) c |= 1 << pl;
            crop[y * w + x] = rgb[c];
        }
    r3d_shape_upload(SIGN_LOC, crop, w, h, w / 2, h / 2, 7.0f);
    r3d_shape_upload(SIGN_RIGHT, crop, w, h, w, h / 2, 7.0f);
    r3d_shape_upload(SIGN_LEFT, crop, w, h, 0, h / 2, 7.0f);
}

static void road_edges(u16 c, int *xl, int *xr)
{
    unsigned cm = c & 0xE0;
    if (cm == 0) { *xl = 0x5DC; *xr = 0xCE4; }
    else if (cm & 0x80) { *xl = 0x7D0; *xr = 0xAF0; }
    else if (cm & 0x20) { *xl = *xr = 0xCE4; }
    else { *xl = *xr = 0x5DC; }
}

#define MAX_OBJECTS 12000
static R3dObject objs[MAX_OBJECTS];

/* front_objects (6a32) for every segment, in world lateral units (the original's screen-x sums are linear in the
 * world x of one row). Simplified: fast machine (cpu > 3), bush groups as one bush, no sign pictures. */
static int build_objects(void)
{
    int n = 0, mode = MODE, w = mode == 0x10 ? 200 : 400;
    if (mode == 8) return 0;                                   /* the Aqueduct: its own obstacles (scene.c), no road scenery */
    int xol = 0x5DC - w, xor_ = 0xCE4 + w, xfl = -(w - 0x177) * 4, xfr = (w + 0x339) * 4;
    int t4 = mode == 0x10 ? 0x15 : 4, u = mode == 0x10 ? 0x16 : 0xB;
#define ADDL(xx, t, sd, lf) do { if (n < MAX_OBJECTS) { objs[n].seg = (int16_t)i; objs[n].x = (int16_t)(xx); \
        objs[n].lift = (int16_t)(lf); objs[n].type = (uint8_t)(t); objs[n].side = (int8_t)(sd); n++; } } while (0)
#define ADD(xx, t, sd) ADDL(xx, t, sd, 0)
    for (int i = 0; i < R3D_TRACK_N; i++) {
        u16 L = flag_l(i), R = flag_r(i), C = flag_c(i);
        bool cross = C & 1, flat_l = bank_l(i) == 0, flat_r = bank_r(i) == 0;
        int xl, xr;
        road_edges(C, &xl, &xr);
        if (mode != 0x10 && (L & 0x20)) ADD(xl, 0, -1);
        if (L & 0x100) {
            if (cross) ADD(xl, 4, -1);
            else if (!(mode == 0x10 && !flat_l)) ADD((L & 0x200) ? 2 * xol - xl : xol, t4, -1);
        }
        if (!(mode & 7) && (L & 0x800)) ADD(xl, 3, 1);
        if (!(mode & 7) && (L & 0x1000)) ADD(xl, 6, 1);
        if (!(mode & 6) && (L & 1) && cross) ADDL(xl, SIGN_RIGHT, 1, 50);
        if (!(mode & 6) && (L & 1) && !cross && flat_l) ADD(xl, 1, 1);
        if (mode == 0x10 && (L & 0x10)) ADD(xl - 150, 8, 1);
        if ((mode & 7) || flat_l) {
            if (L & 0x40) ADD(xol, 0xA, -1);
            if (L & 0x80) ADD((!cross && (L & 2)) ? xfl : xol, u, -1);
        }
        if ((mode & 7) || flat_r) {
            if (R & 0x40) ADD(xor_, 0xA, 1);
            if (R & 0x80) ADD((!cross && (R & 2)) ? xfr : xor_ + ((xor_ - xr) >> 1), u, 1);
        }
        if (!cross) {
            if ((mode & 1) || flat_l) { if (L & 4) ADD(xl - 2, 7, -1); if (L & 8) ADD(xl - 2, 8, -1); }
            if ((mode & 1) || flat_r) { if (R & 4) ADD(xr + 2, 7, 1); if (R & 8) ADD(xr + 2, 8, 1); }
        } else {
            if (L & 0x40) ADD(xl, 2, 1);
            if (R & 0x40) ADD(xr, 2, -1);
        }
        if (!cross && (C & 2) && (mode != 0x10 || flat_r) && !(mode & 6)) ADD(xor_ - ((xr - xl) >> 3), 9, 1);
        if (mode != 0x10 && (R & 0x20)) ADD(xr, 0, 1);
        if (R & 0x100) {
            if (cross) ADD(xr, 4, 1);
            else if (!(mode == 0x10 && !flat_r)) ADD((R & 0x200) ? 2 * xor_ - xr : xor_, t4, 1);
        }
        if (R & 0x2000) ADD(xr, 0x12, 1);
        if (R & 0x4000) ADD(xr, 0x13, 1);
        if (R & 0x800) ADD(xr, 3, 1);
        if (R & 0x1000) ADD(xr, 6, -1);
        if (!(mode & 6) && (R & 1) && cross) ADDL(xr, SIGN_LEFT, 1, 50);
        if (!(mode & 6) && (R & 1) && !cross && flat_r) ADD(xr, 1, -1);
        if ((C & 0xF000) && !(mode & 0x1E)) {                  /* gantry sign (banner 55ce), at the bar 400 up; ENHANCED: none over the Aqueduct */
            int mid = (xl + xr) >> 1;
            if (C & 0x1000) ADDL(mid, 0x0D, 1, 400);
            else if (!(C & 0x2000)) {
                if (!cross) ADDL(mid, (C & 0x4000) ? 0x0C : 0x0E, 1, 400);
                else ADDL(mid, (C & 0x4000) ? SIGN_BILL2 : SIGN_BILL3, 1, 400);
            } else ADDL(mid, SIGN_LOC, 1, 400);
        }
        if (mode == 0x10 && (R & 0x10)) ADD(xr + 150, 8, -1);
        if (mode == 0x10 && (C & 4)) {
            s16 x6 = TRKS(TRK_LANE, geo(i));
            ADD(xl + ((x6 * (xr - xl)) >> 4), 7, x6 < 9 ? -1 : 1);
        }
    }
#undef ADD
    return n;
}

static void push_scenery(void)
{
    static u32 last_sum;
    static int last_mode = -1;
    u32 sum = 2166136261u;
    for (int i = 0; i < R3D_TRACK_N; i++)
        sum = (sum ^ (flag_l(i) | (u32)flag_r(i) << 16) ^ flag_c(i)) * 16777619u;
    u32 rgb[16];
    live_palette(rgb);
    for (int t = 0; t < 23; t++) upload_shape(t, rgb);          /* cheap; hashed by r3d */
    if (!(MODE & 0x1E)) upload_signs(rgb);
    if (sum == last_sum && MODE == last_mode) return;
    last_sum = sum;
    last_mode = MODE;
    int nobj = build_objects();
    r3d_objects_set(objs, nobj);
    if (SDL_getenv("SR_R3D_TRACE")) {                              /* developer aid */
        int cnt[R3D_SHAPES] = { 0 };
        for (int i = 0; i < nobj; i++) cnt[objs[i].type]++;
        fprintf(stderr, "r3d objects mode %d:", MODE);
        for (int t = 0; t < R3D_SHAPES; t++) if (cnt[t]) fprintf(stderr, " %d:%d", t, cnt[t]);
        fprintf(stderr, "\n");
    }
}

/* The drag strip's start lights (road_draw_start_tree aab0): the panel picture DS:9A40 with its lamps composed in,
 * nearest size = columns 0x48.. (56 x 87), at segment start_seg + 4 between the road edges; world size from the
 * middle size (40 x 69 at rows 2 / 3, z ~ 20: 5 world units per pixel). */
static void tree_sprite(R3dSprite *o)
{
    memset(o, 0, sizeof *o);
    if (!drag()) return;
    int w, h;
    if (!decode_pic(ds_far(DS_start_panel_pic), &w, &h)) return;
    int x0 = 0x48, cw = 0x38, ch = 0x57;
    if (x0 + cw > w) { x0 = 0; cw = w; }
    if (ch > h) ch = h;
    static u32 crop[0x80 * 0x60];
    if (cw * ch > (int)(sizeof crop / sizeof crop[0])) return;
    for (int y = 0; y < ch; y++) memcpy(crop + y * cw, pic_px + y * w + x0, (size_t)cw * sizeof(u32));
    r3d_sprite_upload(1, crop, cw, ch);
    o->vis = true;
    o->slot = 1;
    o->pos = (float)(DSS(DS_start_seg) + 4);
    o->lat = 2400.0f;
    o->h = 69.0f * 5.0f;
    o->w = o->h * (float)cw / (float)ch;
}

#define AQ_N 0x172                  /* Aqueduct flags 3726:4E62 */

/* The sky the original scrolls in (road_load_graphics 37ce keeps it in page 1's rows below the view): normal
 * roads = the 52-row sky at rows 102..153 above the first 20 rows of the horizon panorama (rows 163..182, horizon
 * at its row 12: road_draw_sky 3e66); Mulholland = one panorama, rows 110..199, horizon near row 154
 * (road_draw_sky_mulholland 3cca); the Aqueduct has a night sky of its own (none here). */
static void push_sky(void)
{
    static u32 sky[320 * 90];
    if (MODE == 8) { r3d_sky_upload(NULL, 0, 0, 0); return; }   /* (the gradient: colour 0, scene.c) */
    FarPtr pg = desc_planes(ds_far(DS_page1_ptr));
    u32 base = (u32)(pg.seg - 0xA000) * 16u + pg.off;
    u32 rgb[16];
    live_palette(rgb);
    int rows[90], n = 0, horizon;
    if (MODE == 0x10) { for (int y = 110; y < 200; y++) rows[n++] = y; horizon = 44; }
    else {
        for (int y = 102; y < 154; y++) rows[n++] = y;
        for (int y = 163; y < 183; y++) rows[n++] = y;
        horizon = 64;
    }
    for (int k = 0; k < n; k++)
        for (int x = 0; x < 320; x++) {
            u32 o = (base + (u32)rows[k] * EGA_BYTES_PER_LINE + (u32)(x >> 3)) & 0xFFFF;
            u8 bit = (u8)(0x80 >> (x & 7));
            int c = 0;
            for (int pl = 0; pl < 4; pl++) if (ega_plane(pl)[o] & bit) c |= 1 << pl;
            sky[k * 320 + x] = rgb[c];
        }
    /* ENHANCED: the sky's own colour (the commonest of the top row) see-through down each column to the first
     * other colour (a skyline in the sky's colour, e.g. black hills at night, stays): the gradient shows there */
    int cnt[16] = { 0 }, top = 0;
    u32 key[16];
    for (int x = 0; x < 320; x++)
        for (int c = 0; c < 16; c++)
            if (sky[x] == rgb[c]) { cnt[c]++; break; }
    for (int c = 1; c < 16; c++) if (cnt[c] > cnt[top]) top = c;
    for (int c = 0; c < 16; c++) key[c] = rgb[c];
    for (int x = 0; x < 320; x++)                          /* (specks, the game's stars, do not end a column) */
        for (int k = 0; k < n; k++) {
            int solid = 0;
            while (solid < 4 && k + solid < n && sky[(k + solid) * 320 + x] != key[top]) solid++;
            if (solid >= 4 || (solid && k + solid >= n)) break;
            if (sky[k * 320 + x] == key[top]) sky[k * 320 + x] &= 0x00FFFFFFu;
        }
    r3d_sky_base(key[top] & 0xFFFFFFu);
    r3d_sky_upload(sky, 320, n, horizon);
}

static void push_track(void)
{
    static R3dTrack t;
    bool aq = MODE == 8;
    for (int i = 0; i < R3D_TRACK_N; i++) {
        if (aq) {                                          /* aq_project_front: straight, flat (heading only) */
            t.yaw[i] = t.hgt[i] = 0;
            t.l[i] = t.r[i] = 0;
            t.c[i] = i < AQ_N ? TRKW(TRK_AQ, i) : 0;
            t.bl[i] = t.br[i] = 0;
            continue;
        }
        t.yaw[i] = TRKS(TRK_YAW, geo(i));
        t.hgt[i] = drag() ? 0 : TRKS(TRK_HGT, i);
        t.l[i] = flag_l(i);
        t.r[i] = flag_r(i);
        t.c[i] = flag_c(i);
        t.bl[i] = bank_l(i);
        t.br[i] = bank_r(i);
    }
    t.wall_phase = drag() ? DSS(DS_rail_phase_seg) : -1;
    t.aqueduct = aq;
    r3d_track_set(&t);
}

/* aq_project_front 18ec: the channel as the original projects it this frame (the deformation a094 set) */
static void aq_view(R3dView *v)
{
    v->aq_fl = (float)(0x5C8 + DSS(DS_aq_def_xl));
    v->aq_fr = (float)(0xCF8 + DSS(DS_aq_def_xr));
    v->aq_tl = (float)(0x438 + DSS(DS_aq_def_top_xl));
    v->aq_tr = (float)(0xE88 + DSS(DS_aq_def_top_xr));
    v->aq_th = (float)(2000 - (0x67C + DSS(DS_aq_def_top_h)));
    v->aq_floor = (float)-DSS(DS_aq_def_floor_h);
    /* the roll in degrees (DS:9E70, 0 = level; the sin / cos at DS:A540 / BA90 are borrowed by the opponent's rotated
     * picture, rot_alloc), turned the way the original turns its picture (rotate_about_centre 0379) */
    v->roll = -(float)DSS(DS_rot_enable) * 0.017453293f;
    v->height = 0;
    if (v->cam_x < 0x564) v->cam_x = 0x564;                /* road_proj_x's mode-8 clamp */
    if (v->cam_x > 0xD5C) v->cam_x = 0xD5C;
}

static void push_view(void)
{
    push_track();
    R3dView v;
    v.pos = (float)DSS(DS_seg) + (float)DSS(DS_substep) / 64.0f;
    v.cam_x = (float)DSS(DS_cam_x);
    v.cam_y = (float)DSS(DS_cam_y);
    v.heading = (float)DSS(DS_heading);
    v.height = (float)sub16(DSS(DS_cur_height), TRKS(TRK_HGT, DSS(DS_seg)));   /* 0 on the road; a fall off the edge */
    v.opp_pos = (float)DSS(DS_opp_seg) + (float)(DSS(DS_opp_seg_frac64) & 0x3F) / 64.0f;
    v.opp_lat = (float)DSS(DS_opp_lat);
    v.mode = MODE;
    v.aq_fl = v.aq_fr = v.aq_tl = v.aq_tr = v.aq_th = v.aq_floor = v.roll = 0;
    if (drag()) v.height = 0;                              /* flat; the HGT pointer is not advanced there */
    if (MODE == 8) aq_view(&v);
    v.squeal = DSW(DS_squeal) != 0;
    u8 regs[16];
    for (int i = 0; i < 16; i++) {
        u8 r = regs[i] = ega_get_palette_reg(i);
        v.pal[i] = ega_palette[(r & 7) | (r >> 1 & 8)];
    }
    if (sh_enabled()) sh_paint_regs(v.pal, regs);         /* ENHANCED (--amiga): the cars' own colours, as the screen */
    push_sky();
    push_scenery();
    opp_sprite(&v.opp);
    tree_sprite(&v.tree);
    r3d_view_push(&v);
}

void race3d_overlays(void (*draw_overlays)(void))
{
    s16 latch = DSS(DS_light_latch), active = DSS(DS_start_active), starter = DSS(DS_starter_seg);
    fill_view(0);
    draw_overlays();
    save_view();
    DSS(DS_light_latch) = latch;
    DSS(DS_start_active) = active;
    DSS(DS_starter_seg) = starter;
    fill_view(15);
    draw_overlays();
    int page = DSS(DS_back_page);
    if (page >= 0 && page < 3) {
        build_mask(masks[page]);
        ega_set_page_mask(page, masks[page]);
    }
    push_view();
}
