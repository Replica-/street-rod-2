/* ENHANCED (--amiga): full-colour pictures and cars in place of the PC ones.
 *
 * The pictures are one folder of ready files, gfx/ next to the executable (or in the working directory), made by
 * tools/amiga_export.py (game data, never committed):
 *   pic_<id>.bmp            picture <id>: 32-bit = a full-colour version (the Amiga's), drawn instead of the game's;
 *                           8-bit = the game's own picture (no full-colour version), drawn as the game draws it
 *   car_<m>.bmp             the body of car model m: alpha 0 = see-through, 254 = paint, 255 = other
 *   car_<m>_<piece>.bmp     roof / rbump / fbump: chopped roof, stripped bumpers (same coding)
 *   cars.txt                where each piece goes on its body
 *
 * How they reach the screen: the game draws as always (its 16-colour planes stay the game's own state); the planes
 * have a full-colour twin (platform/shadow.h) that every write keeps in step. Around each blit, sprite and car strip
 * this module is the drawing source of the twin: a picture with a full-colour version gives its pixels, a car frame
 * its full-colour body, a RAM copy of the screen the colours it was copied with. Cars: car_compose turns the PC body
 * into the full-colour body's shape (in the game's 16 colours, so the game's masks, wheels, driver, hot spots fit);
 * the twin gets the full-colour body, its paint recoloured to the live paint register when shown. */
#include "enh/amiga.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "platform/ega.h"
#include "platform/shadow.h"
#include "platform/video.h"

static bool on;
static char dir[1024];

typedef struct { uint32_t *px; int w, h; bool tried; } Img;   /* 0xAARRGGBB; px NULL: none (or the game's own) */

/* a 32-bit picture file (an 8-bit one is the game's own picture: no full-colour version) */
static bool load_img(Img *im, const char *name)
{
    if (im->tried) return im->px != NULL;
    im->tried = true;
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", dir, name);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) return false;
    if (SDL_BITSPERPIXEL(s->format) <= 8) { SDL_DestroySurface(s); return false; }
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(s);
    if (!c) return false;
    im->w = c->w;
    im->h = c->h;
    im->px = malloc((size_t)c->w * c->h * sizeof(uint32_t));
    if (im->px)
        for (int y = 0; y < c->h; y++) memcpy(im->px + y * c->w, (uint8_t *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
    SDL_DestroySurface(c);
    return im->px != NULL;
}

/* ---------------------------------------------------------------- set-up */

#define PIECES 3                    /* roof, rear bumper, front bumper */
static const char *piece_name[PIECES] = { "roof", "rbump", "fbump" };
static const int piece_flag[PIECES] = { 4, 2, 1 };    /* car_compose flags: 4 chopped roof, 2 rear / 1 front stripped */
static struct { int x, y; bool has; } piece_at[22][PIECES];

bool amiga_init(bool enable)
{
    on = false;
    if (enable) {
        const char *bases[2] = { SDL_GetBasePath(), "." };
        for (int i = 0; i < 2 && !on; i++) {
            if (!bases[i]) continue;
            SDL_snprintf(dir, sizeof dir, "%s%sgfx", bases[i], i == 1 ? "/" : "");
            char path[1200];
            SDL_snprintf(path, sizeof path, "%s/cars.txt", dir);
            FILE *f = fopen(path, "r");
            if (!f) continue;
            char line[128];
            while (fgets(line, sizeof line, f)) {
                int m, x, y;
                char name[16];
                if (line[0] == '#' || sscanf(line, "%d %15s %d %d", &m, name, &x, &y) != 4 || m < 0 || m >= 22) continue;
                for (int k = 0; k < PIECES; k++)
                    if (!strcmp(name, piece_name[k])) { piece_at[m][k].x = x; piece_at[m][k].y = y; piece_at[m][k].has = true; }
            }
            fclose(f);
            on = true;
        }
        if (!on) fprintf(stderr, "amiga: no gfx folder (make it with tools/amiga_export.py); the game's colours stay\n");
    }
    sh_enable(on);
    return on;
}

/* ---------------------------------------------------------------- which bitmap holds which picture */

#define TRACK 64
static struct { u16 seg, off; s16 id; } track[TRACK];
static int track_next;

static void car_forget(FarPtr bmp);

void amiga_note_pic(FarPtr bmp, s16 id)
{
    if (!on) return;
    car_forget(bmp);                                          /* a car frame there is gone */
    for (int i = 0; i < TRACK; i++)
        if (track[i].seg == bmp.seg && track[i].off == bmp.off) { track[i].id = id; return; }
    track[track_next].seg = bmp.seg;
    track[track_next].off = bmp.off;
    track[track_next].id = id;
    track_next = (track_next + 1) % TRACK;
}

#define PIC_IDS 4096
static Img pic_img[PIC_IDS];

/* the full-colour version of the picture bitmap bmp holds (its size), or NULL */
static const Img *pic_of(FarPtr bmp)
{
    for (int i = 0; i < TRACK; i++)
        if (track[i].seg == bmp.seg && track[i].off == bmp.off && track[i].id > 0 && track[i].id < PIC_IDS) {
            Img *im = &pic_img[track[i].id];
            char name[32];
            SDL_snprintf(name, sizeof name, "pic_%d.bmp", track[i].id);
            if (!load_img(im, name) || im->w != desc_w(bmp) || im->h != desc_h(bmp)) return NULL;
            return im;
        }
    return NULL;
}

static bool is_vram(FarPtr d) { return desc_planes(d).seg >= 0xA000; }

static u32 vram_base(FarPtr d)
{
    FarPtr p = desc_planes(d);
    return ((u32)(p.seg - 0xA000) * 16u + p.off) & 0xFFFFu;
}

static u32 stride_of(FarPtr d) { return desc_type(d) == DESC_TYPE_EGA ? EGA_BYTES_PER_LINE : desc_stride(d); }

/* colour index of pixel (x, y) of a RAM bitmap; -1 = clear (mask bit) */
static int bmp_px(FarPtr d, int x, int y)
{
    FarPtr pl = desc_planes(d), mk = desc_mask(d);
    u32 st = desc_stride(d), plane = st * desc_h(d), o = (u32)y * st + (u32)(x >> 3);
    u8 bit = (u8)(0x80 >> (x & 7));
    if ((mk.seg || mk.off) && (mem[lin(mk.seg, mk.off) + o] & bit)) return -1;
    u32 b = lin(pl.seg, pl.off);
    int c = 0;
    for (int p = 0; p < 4; p++) if (mem[b + p * plane + o] & bit) c |= 1 << p;
    return c;
}

static bool bmp_ok(FarPtr d)
{
    FarPtr pl = desc_planes(d), mk = desc_mask(d);
    u32 plane = desc_stride(d) * (u32)desc_h(d);
    return lin(pl.seg, pl.off) + 4 * plane <= MEM_SIZE && (!(mk.seg || mk.off) || lin(mk.seg, mk.off) + plane <= MEM_SIZE);
}

/* ---------------------------------------------------------------- cars */

static Img car_img[22], piece_img[22][PIECES];
static struct { uint32_t *px; int w, h, flags; } car_work[22];          /* body + this flags' pieces */
/* a fitted car frame: per frame pixel its 16-colour index (fit, 0xFF = clear) and twin entry (px) */
static struct { u16 seg, off; int w, h; uint8_t *fit; u32 *px; } car_frames[8];
static int car_next;
static struct { u16 seg, off, bseg, boff; int shift; } car_shifted[8];  /* frames 1..3 of a drive: base + shift */

static void frame_free(int i)
{
    free(car_frames[i].fit);
    free(car_frames[i].px);
    car_frames[i].fit = NULL;
    car_frames[i].px = NULL;
}

/* a picture loaded where a car frame was: the frame is gone (its memory is used again) */
static void car_forget(FarPtr bmp)
{
    for (int i = 0; i < 8; i++) {
        if (car_frames[i].fit && car_frames[i].seg == bmp.seg && car_frames[i].off == bmp.off) frame_free(i);
        if (car_shifted[i].seg == bmp.seg && car_shifted[i].off == bmp.off) car_shifted[i].seg = car_shifted[i].off = 0;
    }
}

static u32 live_rgb(int i)
{
    u8 v = ega_get_palette_reg(i & 15);
    return ega_palette[(v & 7) | (v >> 1 & 8)];
}

static float value_of(u32 c)
{
    int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255, m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    return (float)m / 255.0f;
}

/* the body with the pieces flags asks for (pasted over their whole rectangle) */
static bool car_build(int m, int flags)
{
    char name[64];
    SDL_snprintf(name, sizeof name, "car_%d.bmp", m);
    if (!load_img(&car_img[m], name)) return false;
    flags &= 7;
    if (car_work[m].px && car_work[m].flags == flags) return true;
    int w = car_img[m].w, h = car_img[m].h;
    free(car_work[m].px);
    car_work[m].px = malloc((size_t)w * h * sizeof(uint32_t));
    if (!car_work[m].px) return false;
    memcpy(car_work[m].px, car_img[m].px, (size_t)w * h * sizeof(uint32_t));
    car_work[m].w = w;
    car_work[m].h = h;
    car_work[m].flags = flags;
    for (int k = 0; k < PIECES; k++) {
        if (!(flags & piece_flag[k]) || !piece_at[m][k].has) continue;
        SDL_snprintf(name, sizeof name, "car_%d_%s.bmp", m, piece_name[k]);
        if (!load_img(&piece_img[m][k], name)) continue;
        Img *p = &piece_img[m][k];
        for (int y = 0; y < p->h; y++)
            for (int x = 0; x < p->w; x++) {
                int tx = piece_at[m][k].x + x, ty = piece_at[m][k].y + y;
                if (tx >= 0 && ty >= 0 && tx < w && ty < h) car_work[m].px[ty * w + tx] = p->px[y * p->w + x];
            }
    }
    return true;
}

/* the paint registers the PC body uses: 6 / 7 (the player's car, set_paint_palette) or 12 / 3 (an opponent's,
 * opp_palette_set); light = the brighter one */
static void paint_regs(const int *cnt, int *light, int *dark)
{
    int a = 6, b = 7;
    if (cnt[12] + cnt[3] > cnt[6] + cnt[7]) { a = 12; b = 3; }
    bool al = value_of(live_rgb(a)) >= value_of(live_rgb(b));
    *light = al ? a : b;
    *dark = al ? b : a;
}

bool amiga_car_body(FarPtr frame, s16 model, s16 flags)
{
    if (!on || model < 0 || model >= 22 || is_vram(frame) || !bmp_ok(frame)) return false;
    FarPtr mk = desc_mask(frame);
    if (!(mk.seg || mk.off) || !car_build(model, flags)) return false;
    int w = desc_w(frame), h = desc_h(frame), aw = car_work[model].w, ah = car_work[model].h;
    int cnt[16] = { 0 };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = bmp_px(frame, x, y);
            if (c >= 0) cnt[c]++;
        }
    int light, dark;
    paint_regs(cnt, &light, &dark);
    uint8_t *fit = malloc((size_t)w * h);
    u32 *px = malloc((size_t)w * h * sizeof(u32));
    if (!fit || !px) { free(fit); free(px); return false; }
    u32 live[16];
    for (int i = 0; i < 16; i++) live[i] = live_rgb(i);
    FarPtr pl = desc_planes(frame);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(frame), plane = st * (u32)h;
    /* the body fits the PC frame: scaled down if it is larger, bottom and centre aligned */
    float sc = 1.0f;
    if (aw > w) sc = (float)w / (float)aw;
    if (ah * sc > h) sc = (float)h / (float)ah;
    int sw = (int)(aw * sc + 0.5f), shh = (int)(ah * sc + 0.5f), ox = (w - sw) / 2, oy = h - shh;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int ax = (int)((float)(x - ox) / sc), ay = (int)((float)(y - oy) / sc);
            uint32_t a = (x >= ox && y >= oy && ax < aw && ay < ah) ? car_work[model].px[ay * aw + ax] : 0;
            uint8_t q = 0xFF;
            u32 v = SH_GAME;
            if ((a >> 24) == 254) {                                   /* paint: the light or the dark register */
                q = (uint8_t)(value_of(a) > 0.62f ? light : dark);
                v = SH_PAINT(light) | (a & 0xFFFFFF);
            } else if ((a >> 24) == 255) {                            /* the nearest live colour */
                long best = -1;
                for (int i = 0; i < 16; i++) {
                    long d = labs((long)(a >> 16 & 255) - (long)(live[i] >> 16 & 255)) +
                             labs((long)(a >> 8 & 255) - (long)(live[i] >> 8 & 255)) + labs((long)(a & 255) - (long)(live[i] & 255));
                    if (best < 0 || d < best) { best = d; q = (uint8_t)i; }
                }
                v = SH_RGB | (a & 0xFFFFFF);
            }
            fit[y * w + x] = q;
            px[y * w + x] = v;
            u32 o = (u32)y * st + (u32)(x >> 3);
            u8 bit = (u8)(0x80 >> (x & 7));
            for (int p = 0; p < 4; p++) {
                if (q != 0xFF && (q >> p & 1)) mem[b + p * plane + o] |= bit;
                else mem[b + p * plane + o] &= (u8)~bit;
            }
            if (q == 0xFF) mem[mb + o] |= bit; else mem[mb + o] &= (u8)~bit;
        }
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == frame.seg && car_frames[i].off == frame.off) frame_free(i);
    frame_free(car_next);
    car_frames[car_next].seg = frame.seg;
    car_frames[car_next].off = frame.off;
    car_frames[car_next].w = w;
    car_frames[car_next].h = h;
    car_frames[car_next].fit = fit;
    car_frames[car_next].px = px;
    car_next = (car_next + 1) % 8;
    return true;
}

/* a PC tyre picture: its black (colour 0) reaching the picture's edge goes see-through (the corners around the round
 * tyre, invisible on the PC's black floor, black blocks on a lighter one) */
void amiga_tyre_edge_clear(FarPtr pic)
{
    if (!on || is_vram(pic) || !bmp_ok(pic)) return;
    FarPtr mk = desc_mask(pic);
    if (!(mk.seg || mk.off)) return;
    int w = desc_w(pic), h = desc_h(pic), n = 0;
    int *stack = malloc(sizeof(int) * (size_t)w * h * 4 + sizeof(int));
    uint8_t *seen = calloc((size_t)w * h, 1);
    if (!stack || !seen) { free(stack); free(seen); return; }
    for (int x = 0; x < w; x++) { stack[n++] = x; stack[n++] = (h - 1) * w + x; }
    for (int y = 0; y < h; y++) { stack[n++] = y * w; stack[n++] = y * w + w - 1; }
    u32 mb = lin(mk.seg, mk.off), st = desc_stride(pic);
    while (n) {
        int k = stack[--n], x = k % w, y = k / w;
        if (seen[k] || bmp_px(pic, x, y) != 0) continue;
        seen[k] = 1;
        mem[mb + (u32)y * st + (u32)(x >> 3)] |= (u8)(0x80 >> (x & 7));
        if (x > 0) stack[n++] = k - 1;
        if (x + 1 < w) stack[n++] = k + 1;
        if (y > 0) stack[n++] = k - w;
        if (y + 1 < h) stack[n++] = k + w;
    }
    free(stack);
    free(seen);
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

/* ---------------------------------------------------------------- sources: what a bitmap's pixels look like */

enum { SRC_NONE, SRC_VRAM, SRC_CAR, SRC_BLOCK, SRC_PIC };
typedef struct {
    int kind;
    FarPtr bmp;
    u32 base, stride;            /* SRC_VRAM */
    int f, shift;                /* SRC_CAR */
    const u32 *block;            /* SRC_BLOCK: stride * 8 entries a row */
    const Img *img;              /* SRC_PIC */
    u32 mask;                    /* linear address of the mask, 0 = none or not used */
} Src;

static void src_resolve(Src *s, FarPtr bmp, bool masked)
{
    memset(s, 0, sizeof *s);
    s->bmp = bmp;
    if (far_is_null(bmp)) return;
    if (is_vram(bmp)) {
        s->kind = SRC_VRAM;
        s->base = vram_base(bmp);
        s->stride = stride_of(bmp);
        return;
    }
    if (!bmp_ok(bmp)) return;
    s->stride = desc_stride(bmp);
    FarPtr mk = desc_mask(bmp);
    if (masked && (mk.seg || mk.off)) s->mask = lin(mk.seg, mk.off);
    FarPtr base = bmp;
    for (int i = 0; i < 8; i++)
        if (car_shifted[i].seg == bmp.seg && car_shifted[i].off == bmp.off && (bmp.seg || bmp.off)) {
            base.seg = car_shifted[i].bseg;
            base.off = car_shifted[i].boff;
            s->shift = car_shifted[i].shift;
        }
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == base.seg && car_frames[i].off == base.off &&
            car_frames[i].h == desc_h(bmp) && desc_w(bmp) >= car_frames[i].w && desc_w(bmp) <= car_frames[i].w + 8) {
            s->kind = SRC_CAR;
            s->f = i;
            return;
        }
    s->shift = 0;
    if ((s->img = pic_of(bmp)) != NULL) { s->kind = SRC_PIC; return; }
    FarPtr pl = desc_planes(bmp);
    if ((s->block = sh_block_get(pl.seg, pl.off, s->stride, desc_h(bmp))) != NULL) s->kind = SRC_BLOCK;
}

static bool src_clear(const Src *s, int x, int y)
{
    return s->mask && (mem[s->mask + (u32)y * s->stride + (u32)(x >> 3)] & (0x80 >> (x & 7)));
}

static u32 src_px(const Src *s, int x, int y)
{
    if (x < 0 || y < 0) return SH_GAME;
    switch (s->kind) {
    case SRC_VRAM:
        return sh_at(s->base, s->stride, x, y);
    case SRC_CAR: {
        int w = car_frames[s->f].w, bx = x - s->shift;
        if (y >= car_frames[s->f].h || bx < 0 || bx >= w) return SH_GAME;
        int c = bmp_px(s->bmp, x, y);
        return c >= 0 && c == car_frames[s->f].fit[y * w + bx] ? car_frames[s->f].px[y * w + bx] : SH_GAME;
    }
    case SRC_BLOCK:
        return (u32)x < s->stride * 8 && y < (int)desc_h(s->bmp) ? s->block[(u32)y * s->stride * 8 + (u32)x] : SH_GAME;
    case SRC_PIC:
        return x < s->img->w && y < s->img->h ? SH_RGB | (s->img->px[y * s->img->w + x] & 0xFFFFFF) : SH_GAME;
    default:
        return SH_GAME;
    }
}

/* ---------------------------------------------------------------- blits */

static struct { Src src; Rect c; u32 *prev; } blit;   /* prev: the RAM destination's entries before the blit */

static u32 blit_fn(void *user, int x, int y)
{
    (void)user;
    int j = y - blit.c.dy, i = (x >> 3) - blit.c.dx;
    if (j < 0 || j >= blit.c.h || i < 0 || i >= blit.c.w) return SH_NONE;
    int sx = (blit.c.sx + i) * 8 + (x & 7), sy = blit.c.sy + j;
    if (src_clear(&blit.src, sx, sy)) return SH_KEEP;
    return src_px(&blit.src, sx, sy);
}

void amiga_blit_begin(FarPtr src, FarPtr dst, const Rect *r, bool masked)
{
    free(blit.prev);
    blit.prev = NULL;
    if (!on || !r || !clip_rect(src, dst, r, &blit.c)) { blit.src.kind = SRC_NONE; return; }
    src_resolve(&blit.src, src, masked);
    if (is_vram(dst)) {
        u32 st = stride_of(dst);
        sh_begin(vram_base(dst), st, (int)st * 8, desc_h(dst), blit_fn, NULL);
        return;
    }
    u32 st = desc_stride(dst);                     /* a RAM destination: its entries before, if it has any */
    Src d;                                         /* what it shows now: a picture, an earlier copy */
    src_resolve(&d, dst, false);
    if (d.kind == SRC_PIC || d.kind == SRC_BLOCK) {
        int W = (int)st * 8, h = desc_h(dst);
        if ((blit.prev = malloc((size_t)W * h * sizeof(u32))) != NULL)
            for (int y = 0; y < h; y++)
                for (int x = 0; x < W; x++) blit.prev[y * W + x] = src_px(&d, x, y);
    }
}

void amiga_blit_end(FarPtr src, FarPtr dst, const Rect *r)
{
    (void)src; (void)r;
    sh_end();
    if (!on || is_vram(dst) || (blit.src.kind == SRC_NONE && !blit.prev)) { free(blit.prev); blit.prev = NULL; return; }
    /* a RAM copy of a screen (or of something with full colours): it keeps its entries */
    u32 st = desc_stride(dst);
    int h = desc_h(dst), W = (int)st * 8;
    u32 *px = blit.prev ? blit.prev : calloc((size_t)W * h, sizeof(u32));
    blit.prev = NULL;
    if (!px) return;
    for (int j = 0; j < blit.c.h; j++)
        for (int x = 0; x < blit.c.w * 8; x++) {
            int dx = blit.c.dx * 8 + x, dy = blit.c.dy + j, sx = blit.c.sx * 8 + x, sy = blit.c.sy + j;
            if (dx >= W || dy >= h || src_clear(&blit.src, sx, sy)) continue;
            px[dy * W + dx] = src_px(&blit.src, sx, sy);
        }
    FarPtr pl = desc_planes(dst);
    sh_block_set(pl.seg, pl.off, st, h, px);
    for (int i = 0; i < TRACK; i++)                 /* no longer just the picture it was loaded with */
        if (track[i].seg == dst.seg && track[i].off == dst.off) track[i].id = 0;
}

/* ---------------------------------------------------------------- the driving car (sprite over its background) */

static struct { Src spr, bg; Rect c; u16 bgrel, bgstr; int rows; } sprite;

static u32 sprite_fn(void *user, int x, int y)
{
    (void)user;
    int j = y - sprite.c.dy, i = (x >> 3) - sprite.c.dx;
    if (j < 0 || j >= sprite.rows || i < 0 || i >= sprite.c.w) return SH_NONE;
    int sx = (sprite.c.sx + i) * 8 + (x & 7), sy = sprite.c.sy + j;
    if (!src_clear(&sprite.spr, sx, sy)) return src_px(&sprite.spr, sx, sy);
    if (!sprite.bgstr) return SH_GAME;
    u32 rel = (u32)sprite.bgrel + (u32)j * sprite.bgstr + (u32)i;    /* the background behind it */
    return src_px(&sprite.bg, (int)(rel % sprite.bgstr) * 8 + (x & 7), (int)(rel / sprite.bgstr));
}

void amiga_sprite_begin(FarPtr spr, FarPtr dst, FarPtr bg, const Rect *c, u16 bgrel, u16 bgstr, s16 rows)
{
    if (!on || !is_vram(dst)) return;
    src_resolve(&sprite.spr, spr, true);
    src_resolve(&sprite.bg, bg, false);
    sprite.c = *c;
    sprite.bgrel = bgrel;
    sprite.bgstr = bgstr;
    sprite.rows = rows;
    u32 st = desc_stride(dst);
    sh_begin(vram_base(dst), st, (int)st * 8, desc_h(dst), sprite_fn, NULL);
}

void amiga_sprite_end(void) { sh_end(); }

/* ---------------------------------------------------------------- car strips cached in VRAM */

static struct { Src frame; int frow0; } cache;

static u32 cache_fn(void *user, int x, int y)
{
    (void)user;
    int fy = cache.frow0 + y;
    if (src_clear(&cache.frame, x, fy)) return SH_KEEP;
    return src_px(&cache.frame, x, fy);
}

void amiga_cache_begin(FarPtr frame, FarPtr tp, u16 stride, s16 rows, s16 frow0)
{
    if (!on || tp.seg < 0xA000) return;
    src_resolve(&cache.frame, frame, true);
    cache.frow0 = frow0;
    sh_begin(((u32)(tp.seg - 0xA000) * 16u + tp.off) & 0xFFFFu, stride, stride * 8, rows, cache_fn, NULL);
}

void amiga_cache_end(void)
{
    sh_end();
}
