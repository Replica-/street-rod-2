/* ENHANCED (--amiga): full-colour pictures and cars in place of the PC ones.
 *
 * The pictures are one folder of ready files, gfx/ next to the executable (or in the working directory), made by
 * tools/amiga_export.py (game data, never committed):
 *   pic_<id>.bmp            picture <id>: 32-bit = a full-colour version (the Amiga's), drawn instead of the game's
 *                           (alpha 0 = hidden: what is there stays); 8-bit = the game's own picture (no full-colour
 *                           version), drawn as the game draws it
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

bool amiga_active(void) { return on; }

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

static Img car_img[22], piece_img[22][PIECES], wheel_img[3][4];
static struct { uint32_t *px; int w, h, flags; } car_work[22];          /* body + this flags' pieces */
/* a car frame: per frame pixel its 16-colour index (fit, 0xFF = clear) and twin entry (px) */
static struct { u16 seg, off; int w, h; uint8_t *fit; u32 *px; } car_frames[8];
static int car_next;
static struct { bool on; int model, flags, w, h, light, dark, x0, y0, x1, y1; } car_job;   /* amiga_car_begin ->
                                                       amiga_car_frames; x0..y1: the PC body's box */

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
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == bmp.seg && car_frames[i].off == bmp.off) frame_free(i);
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

bool amiga_car_begin(FarPtr frame, s16 model, s16 flags)
{
    car_job.on = false;
    if (!on || model < 0 || model >= 22 || is_vram(frame) || !bmp_ok(frame)) return false;
    FarPtr mk = desc_mask(frame);
    if (!(mk.seg || mk.off) || !car_build(model, flags)) return false;
    int w = desc_w(frame), h = desc_h(frame), cnt[16] = { 0 };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = bmp_px(frame, x, y);
            if (c >= 0) cnt[c]++;
        }
    paint_regs(cnt, &car_job.light, &car_job.dark);
    car_job.x0 = w; car_job.y0 = h; car_job.x1 = 0; car_job.y1 = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (bmp_px(frame, x, y) >= 0) {
                if (x < car_job.x0) car_job.x0 = x;
                if (y < car_job.y0) car_job.y0 = y;
                if (x + 1 > car_job.x1) car_job.x1 = x + 1;
                if (y + 1 > car_job.y1) car_job.y1 = y + 1;
            }
    if (car_job.x1 <= car_job.x0 || car_job.y1 <= car_job.y0) return false;
    car_job.on = true;
    car_job.model = model;
    car_job.flags = flags;
    car_job.w = w;
    car_job.h = h;
    return true;
}

/* frame f of the car: the whole Amiga car laid over the PC car, as the game composes its own: the Amiga body (the
 * same drawing) scaled onto the PC body's box, the Amiga wheels where the game draws its tyres (pc_x: left edge of the
 * 32-pixel PC tyre, bottom on the frame's bottom), behind the body; 2 * f pixels to the right (the driving frames).
 * The game's 16-colour planes and mask get its shape, the twin its colours. */
#define PC_TYRE_W 32
static void car_frame(FarPtr frame, int f, int tyre, int pc_x0, int pc_x1)
{
    int m = car_job.model, h = car_job.h, fw = desc_w(frame), fh = desc_h(frame);
    int aw = car_work[m].w, ah = car_work[m].h;
    const Img *wh = tyre >= 0 && tyre < 3 ? &wheel_img[tyre][f & 3] : NULL;
    if (wh) {
        char name[32];
        SDL_snprintf(name, sizeof name, "wheel_%d_%d.bmp", tyre, f & 3);
        if (!load_img((Img *)wh, name)) wh = NULL;
    }
    int bx0 = aw, by0 = ah, bx1 = 0, by1 = 0;                     /* the Amiga body's box */
    for (int y = 0; y < ah; y++)
        for (int x = 0; x < aw; x++)
            if (car_work[m].px[y * aw + x] >> 24) {
                if (x < bx0) bx0 = x;
                if (y < by0) by0 = y;
                if (x + 1 > bx1) bx1 = x + 1;
                if (y + 1 > by1) by1 = y + 1;
            }
    if (bx1 <= bx0 || by1 <= by0) return;
    int pw = car_job.x1 - car_job.x0, ph = car_job.y1 - car_job.y0;
    int ww = PC_TYRE_W, whh = wh ? (wh->h * PC_TYRE_W + wh->w / 2) / wh->w : 0;
    int pcx[2] = { pc_x0, pc_x1 };
    /* each wheel centred in its arch of the laid body: around the PC tyre's centre (+-26 columns), the columns whose
     * lowest body row is within the top quarter of the arch (a raised step beside it is not the opening); its middle */
    for (int k = 0; k < 2; k++) {
        int c = pcx[k] + ww / 2, lowv[53], base = -1, dmax = 0, lo = -1, hi = -1;
        for (int i = 0; i < 53; i++) {
            int x = c - 26 + i;
            lowv[i] = -1;
            if (x < car_job.x0 || x >= car_job.x1) continue;
            int ax = bx0 + (x - car_job.x0) * (bx1 - bx0) / pw;
            for (int y = car_job.y1 - 1; y >= car_job.y0; y--) {
                int ay = by0 + (y - car_job.y0) * (by1 - by0) / ph;
                if (car_work[m].px[ay * aw + ax] >> 24) { lowv[i] = y; break; }
            }
            if (lowv[i] > base) base = lowv[i];
        }
        for (int i = 0; i < 53; i++)
            if (lowv[i] >= 0 && base - lowv[i] > dmax) dmax = base - lowv[i];
        if (dmax < 10) continue;                       /* no arch (a fender skirt over the wheel): the PC tyre's place */
        for (int i = 0; i < 53; i++)
            if (lowv[i] >= 0 && 4 * (base - lowv[i]) >= 3 * dmax) { if (lo < 0) lo = i; hi = i; }   /* near its top */
        pcx[k] = c - 26 + lo + (hi - lo + 1 - ww) / 2;
    }
    uint8_t *fit = malloc((size_t)fw * fh);
    u32 *px = malloc((size_t)fw * fh * sizeof(u32));
    if (!fit || !px) { free(fit); free(px); return; }
    u32 live[16];
    for (int i = 0; i < 16; i++) live[i] = live_rgb(i);
    FarPtr pl = desc_planes(frame), mk = desc_mask(frame);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(frame), plane = st * (u32)fh;
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++) {
            int cx = x - 2 * f;
            uint32_t a = 0;
            if (cx >= car_job.x0 && cx < car_job.x1 && y >= car_job.y0 && y < car_job.y1) {   /* the body */
                int ax = bx0 + (cx - car_job.x0) * (bx1 - bx0) / pw, ay = by0 + (y - car_job.y0) * (by1 - by0) / ph;
                a = car_work[m].px[ay * aw + ax];
            }
            for (int k = 0; k < 2 && !(a >> 24) && wh; k++)          /* a wheel behind it */
                if (cx >= pcx[k] && cx < pcx[k] + ww && y >= h - whh && y < h) {
                    int wx = (cx - pcx[k]) * wh->w / ww, wy = (y - (h - whh)) * wh->h / whh;
                    a = wh->px[wy * wh->w + wx];
                }
            uint8_t q = 0xFF;
            u32 v = SH_GAME;
            if ((a >> 24) == 254) {                                   /* paint: the light or the dark register */
                q = (uint8_t)(value_of(a) > 0.62f ? car_job.light : car_job.dark);
                v = SH_PAINT(car_job.light) | (a & 0xFFFFFF);
            } else if ((a >> 24) == 255) {                            /* the nearest live colour */
                long best = -1;
                for (int i = 0; i < 16; i++) {
                    long d = labs((long)(a >> 16 & 255) - (long)(live[i] >> 16 & 255)) +
                             labs((long)(a >> 8 & 255) - (long)(live[i] >> 8 & 255)) + labs((long)(a & 255) - (long)(live[i] & 255));
                    if (best < 0 || d < best) { best = d; q = (uint8_t)i; }
                }
                v = SH_RGB | (a & 0xFFFFFF);
            }
            fit[y * fw + x] = q;
            px[y * fw + x] = v;
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
    car_frames[car_next].w = fw;
    car_frames[car_next].h = fh;
    car_frames[car_next].fit = fit;
    car_frames[car_next].px = px;
    car_next = (car_next + 1) % 8;
}

void amiga_car_frames(FarPtr *frames, s16 n, s16 tyre, s16 pc_x0, s16 pc_x1)
{
    if (!car_job.on) return;
    for (s16 i = 0; i < n && i < 4; i++)
        if (!far_is_null(frames[i]) && !is_vram(frames[i]) && bmp_ok(frames[i]) &&
            (desc_mask(frames[i]).seg || desc_mask(frames[i]).off))
            car_frame(frames[i], i, tyre, pc_x0, pc_x1);
    car_job.on = false;
}

/* ---------------------------------------------------------------- sources: what a bitmap's pixels look like */

enum { SRC_NONE, SRC_VRAM, SRC_CAR, SRC_BLOCK, SRC_PIC };
typedef struct {
    int kind;
    FarPtr bmp;
    u32 base, stride;            /* SRC_VRAM */
    int f;                       /* SRC_CAR */
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
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == bmp.seg && car_frames[i].off == bmp.off &&
            car_frames[i].h == desc_h(bmp) && car_frames[i].w == desc_w(bmp)) {
            s->kind = SRC_CAR;
            s->f = i;
            return;
        }
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
        int w = car_frames[s->f].w;
        if (y >= car_frames[s->f].h || x >= w) return SH_GAME;
        int c = bmp_px(s->bmp, x, y);
        return c >= 0 && c == car_frames[s->f].fit[y * w + x] ? car_frames[s->f].px[y * w + x] : SH_GAME;
    }
    case SRC_BLOCK:
        return (u32)x < s->stride * 8 && y < (int)desc_h(s->bmp) ? s->block[(u32)y * s->stride * 8 + (u32)x] : SH_GAME;
    case SRC_PIC:
        if (x >= s->img->w || y >= s->img->h) return SH_GAME;
        if (!(s->img->px[y * s->img->w + x] >> 24)) return SH_KEEP;   /* see-through: the picture is hidden there */
        if ((s->img->px[y * s->img->w + x] >> 24) == 254)              /* the car's paint (the dashboards' top band) */
            return SH_PAINT(7) | (s->img->px[y * s->img->w + x] & 0xFFFFFF);
        return SH_RGB | (s->img->px[y * s->img->w + x] & 0xFFFFFF);
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
            u32 v = src_px(&blit.src, sx, sy);
            if (v != SH_KEEP) px[dy * W + dx] = v;
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

void amiga_sprite_tails(FarPtr dst, s16 rows)
{
    if (!on || !is_vram(dst) || !sprite.bgstr) return;
    u32 st = desc_stride(dst), base = vram_base(dst);
    u32 *px = sh_pixels();
    for (int j = sprite.rows; j < sprite.rows + rows; j++)
        for (int i = 0; i < sprite.c.w; i++)
            for (int k = 0; k < 8; k++) {
                u32 o = (base + (u32)(sprite.c.dy + j) * st + (u32)(sprite.c.dx + i)) & 0xFFFFu;
                if (px[o * 8 + (u32)k] != SH_THROUGH) continue;
                u32 rel = (u32)sprite.bgrel + (u32)j * sprite.bgstr + (u32)i;
                px[o * 8 + (u32)k] = src_px(&sprite.bg, (int)(rel % sprite.bgstr) * 8 + k, (int)(rel / sprite.bgstr));
            }
}

/* ---------------------------------------------------------------- car strips cached in VRAM */

static struct { Src frame; int frow0; bool plain; } cache;   /* plain: the background a fill */

static u32 cache_fn(void *user, int x, int y)
{
    (void)user;
    int fy = cache.frow0 + y;
    if (src_clear(&cache.frame, x, fy)) return cache.plain ? SH_THROUGH : SH_KEEP;
    return src_px(&cache.frame, x, fy);
}

void amiga_cache_begin(FarPtr frame, FarPtr tp, u16 stride, s16 rows, s16 frow0, bool plain)
{
    if (!on || tp.seg < 0xA000) return;
    src_resolve(&cache.frame, frame, true);
    cache.frow0 = frow0;
    cache.plain = plain;
    sh_begin(((u32)(tp.seg - 0xA000) * 16u + tp.off) & 0xFFFFu, stride, stride * 8, rows, cache_fn, NULL);
}

void amiga_cache_end(void)
{
    sh_end();
}

/* ---------------------------------------------------------------- the paint swatch (paint job) */

static const u32 swatch[AMIGA_SWATCH_N] = {
    0xC81E1E, 0xFF5A28, 0xFF9614, 0xFFD228, 0xF0F0A0, 0x9BDC28, 0x28B43C, 0x146E3C, 0x28C8B4, 0x50B4FF,
    0x1E64DC, 0x14288C, 0x7846C8, 0xC850C8, 0xFF8CB4, 0x8C1E3C, 0x8C5A32, 0xE6E6E6, 0x8C8C96, 0x282830,
};
#define SW_CW 16                                  /* cell width, height */
#define SW_CH 10
static struct { bool on; int x, y, pages[2]; u8 *planes[2]; u32 *px[2]; } sw;

static void swatch_draw(int sel)
{
    u32 *sp = sh_pixels();
    for (int pg = 0; pg < 2; pg++)
        for (int j = 0; j < 2 * SW_CH + 2; j++)
            for (int i = 0; i < 10 * SW_CW + 2; i++) {
                int x = sw.x + i, y = sw.y + j;
                if (x < 0 || x >= 320 || y < 0 || y >= 200) continue;
                u32 o = ((u32)sw.pages[pg] * 0x2000u + (u32)y * EGA_BYTES_PER_LINE + (u32)(x >> 3)) & 0xFFFFu;
                u8 bit = (u8)(0x80 >> (x & 7));
                for (int p = 0; p < 4; p++) ega_plane(p)[o] &= (u8)~bit;    /* the game sees black */
                int cx = (i - 1) / SW_CW, cy = (j - 1) / SW_CH, k = cy * 10 + cx;
                bool edge = i == 0 || j == 0 || i == 10 * SW_CW + 1 || j == 2 * SW_CH + 1 ||
                            (i - 1) % SW_CW == SW_CW - 1 || (j - 1) % SW_CH == SW_CH - 1;
                bool frame = k == sel && ((i - 1) % SW_CW == 0 || (i - 1) % SW_CW >= SW_CW - 2 ||
                                          (j - 1) % SW_CH == 0 || (j - 1) % SW_CH >= SW_CH - 2);
                u32 v = edge ? 0x202020 : frame ? 0xFFFFFF : swatch[k];
                sp[o * 8 + (u32)(x & 7)] = SH_RGB | v;
            }
    ega_touch();
}

void amiga_swatch_show(int x, int y, int sel)
{
    if (!on || sw.on) return;
    sw.x = x;
    sw.y = y;
    sw.pages[0] = (int)(vram_base(g_front()) / 0x2000);
    sw.pages[1] = (int)(vram_base(g_back()) / 0x2000);
    for (int pg = 0; pg < 2; pg++) {                      /* what is there, to put back */
        sw.planes[pg] = malloc(4 * 0x2000);
        sw.px[pg] = malloc(0x2000 * 8 * sizeof(u32));
        if (!sw.planes[pg] || !sw.px[pg]) return;
        for (int p = 0; p < 4; p++) memcpy(sw.planes[pg] + p * 0x2000, ega_plane(p) + sw.pages[pg] * 0x2000, 0x2000);
        memcpy(sw.px[pg], sh_pixels() + (u32)sw.pages[pg] * 0x2000u * 8, 0x2000 * 8 * sizeof(u32));
    }
    sw.on = true;
    swatch_draw(sel);
}

void amiga_swatch_select(int sel)
{
    if (sw.on) swatch_draw(sel);
}

void amiga_swatch_hide(void)
{
    if (!sw.on) return;
    for (int pg = 0; pg < 2; pg++) {                      /* only the swatch's rows go back */
        for (int y = sw.y; y < sw.y + 2 * SW_CH + 2 && y < 200; y++) {
            u32 o = (u32)y * EGA_BYTES_PER_LINE;
            for (int p = 0; p < 4; p++)
                memcpy(ega_plane(p) + sw.pages[pg] * 0x2000 + o, sw.planes[pg] + p * 0x2000 + o, EGA_BYTES_PER_LINE);
            memcpy(sh_pixels() + ((u32)sw.pages[pg] * 0x2000u + o) * 8, sw.px[pg] + o * 8, EGA_BYTES_PER_LINE * 8 * sizeof(u32));
        }
        free(sw.planes[pg]);
        free(sw.px[pg]);
        sw.planes[pg] = NULL;
        sw.px[pg] = NULL;
    }
    sw.on = false;
    ega_touch();
}

int amiga_swatch_hit(int x, int y)
{
    if (!sw.on || x < sw.x + 1 || y < sw.y + 1) return -1;
    int cx = (x - sw.x - 1) / SW_CW, cy = (y - sw.y - 1) / SW_CH;
    return cx < 10 && cy < 2 ? cy * 10 + cx : -1;
}

u32 amiga_swatch_rgb(int i) { return i >= 0 && i < AMIGA_SWATCH_N ? swatch[i] : 0; }

/* the swatch colours chosen, per car record (DGROUP offset), with the game's own colour index then: a record that
 * has a different colour now (another save loaded) is not used. Kept in paint.cfg beside the gfx folder. */
#define PAINT_MAX 32
static struct { u16 car; int index; u32 rgb; } paint[PAINT_MAX];
static int npaint;
static bool paint_loaded;

static void paint_path(char *out, size_t n) { SDL_snprintf(out, n, "%s/../paint.cfg", dir); }

static void paint_load(void)
{
    if (paint_loaded) return;
    paint_loaded = true;
    char path[1200];
    paint_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    unsigned car, rgb;
    int index;
    while (npaint < PAINT_MAX && fscanf(f, "%x %d %x", &car, &index, &rgb) == 3) {
        paint[npaint].car = (u16)car;
        paint[npaint].index = index;
        paint[npaint].rgb = rgb;
        npaint++;
    }
    fclose(f);
}

void amiga_paint_set(u16 car, int index, u32 rgb)
{
    if (!on) return;
    paint_load();
    int k = 0;
    for (int i = 0; i < npaint; i++)
        if (paint[i].car != car) paint[k++] = paint[i];
    npaint = k;
    if (rgb && npaint < PAINT_MAX) { paint[npaint].car = car; paint[npaint].index = index; paint[npaint].rgb = rgb; npaint++; }
    char path[1200];
    paint_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < npaint; i++) fprintf(f, "%04x %d %06x\n", paint[i].car, paint[i].index, paint[i].rgb);
    fclose(f);
}

void amiga_paint_apply(u16 car, int index)
{
    u32 rgb = 0;
    if (on) {
        paint_load();
        for (int i = 0; i < npaint; i++)
            if (paint[i].car == car && paint[i].index == index) rgb = paint[i].rgb;
    }
    amiga_paint_preview(rgb, index);
}

/* the paint pair (registers 6 dark, 7 light) of game colour `index` shown as rgb: 7 = rgb, 6 = a darker shade */
void amiga_paint_preview(u32 rgb, int index)
{
    int e6 = DSB((u16)(DS_PAINT + 3 * index)), e7 = DSB((u16)(DS_PAINT + 3 * index + 1));
    u32 dark = rgb ? ((rgb >> 16 & 255) * 7 / 10) << 16 | ((rgb >> 8 & 255) * 7 / 10) << 8 | (rgb & 255) * 7 / 10 : 0;
    if (rgb && !dark) dark = 0x010101;
    sh_set_paint(6, dark, e6);
    sh_set_paint(7, rgb, e7);
    ega_touch();
}
