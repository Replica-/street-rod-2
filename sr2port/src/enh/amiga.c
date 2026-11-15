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
 *   hand_l/r, arm_l/r.bmp   the driver's fists and arms, placed over the dashboard here (amiga_hands)
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

#include "game/records.h"
#include "host.h"
#include "platform/ega.h"
#include "platform/shadow.h"
#include "platform/video.h"
#include "r3d/r3d.h"

static bool on;
static char dir[1024];

typedef struct { uint32_t *px; int w, h; bool tried; } Img;   /* 0xAARRGGBB; px NULL: none (or the game's own) */

/* ENHANCED: hand-made patches for a picture, sr2port/assets/gfx/<stem>@<x>,<y>.bmp (only the pixels that were made,
 * alpha 0 = keep the picture's): put over the picture at (x, y) */
static void apply_patches(Img *im, const char *name)
{
    char dirp[1100], stem[64];
    SDL_snprintf(dirp, sizeof dirp, "%sassets/gfx", SDL_GetBasePath() ? SDL_GetBasePath() : "");
    const char *slash = strrchr(name, '/');
    SDL_strlcpy(stem, slash ? slash + 1 : name, sizeof stem);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    size_t sl = strlen(stem);
    int count = 0;
    char **files = SDL_GlobDirectory(dirp, NULL, 0, &count);
    for (int i = 0; files && i < count; i++) {
        int x0, y0;
        if (strncmp(files[i], stem, sl) || files[i][sl] != '@' || SDL_sscanf(files[i] + sl + 1, "%d,%d", &x0, &y0) != 2) continue;
        char path[1300];
        SDL_snprintf(path, sizeof path, "%s/%s", dirp, files[i]);
        SDL_Surface *ps = SDL_LoadBMP(path);
        SDL_Surface *pc = ps ? SDL_ConvertSurface(ps, SDL_PIXELFORMAT_ARGB8888) : NULL;
        if (ps) SDL_DestroySurface(ps);
        if (!pc) continue;
        for (int y = 0; y < pc->h; y++)
            for (int x = 0; x < pc->w; x++) {
                uint32_t v = ((uint32_t *)((uint8_t *)pc->pixels + (size_t)y * pc->pitch))[x];
                int tx = x0 + x, ty = y0 + y;
                if ((v >> 24) && tx >= 0 && ty >= 0 && tx < im->w && ty < im->h) im->px[ty * im->w + tx] = v;
            }
        SDL_DestroySurface(pc);
    }
    SDL_free(files);
}

/* a 32-bit picture file (an 8-bit one is the game's own picture: no full-colour version) */
static bool load_img(Img *im, const char *name)
{
    if (im->tried) return im->px != NULL;
    im->tried = true;
    char path[1200];
    /* ENHANCED: a hand-edited replacement shipped with the game (sr2port/assets/gfx, copied beside the executable)
     * before the generated picture set */
    SDL_snprintf(path, sizeof path, "%sassets/gfx/%s", SDL_GetBasePath() ? SDL_GetBasePath() : "", name);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) {
        SDL_snprintf(path, sizeof path, "%s/%s", dir, name);
        s = SDL_LoadBMP(path);
    }
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
    if (im->px) apply_patches(im, name);
    return im->px != NULL;
}

/* ---------------------------------------------------------------- set-up */

#define PIECES 3                    /* roof, rear bumper, front bumper */
static const char *piece_name[PIECES] = { "roof", "rbump", "fbump" };
static const int piece_flag[PIECES] = { 4, 2, 1 };    /* car_compose flags: 4 chopped roof, 2 rear / 1 front stripped */
static struct { int x, y; bool has; } piece_at[22][PIECES];

bool amiga_active(void) { return on; }

bool amiga_gfx_present(void)
{
    const char *bases[2] = { SDL_GetBasePath(), "." };
    for (int i = 0; i < 2; i++) {
        if (!bases[i]) continue;
        char path[1200];
        SDL_snprintf(path, sizeof path, "%s%sgfx/cars.txt", bases[i], i == 1 ? "/" : "");
        if (SDL_GetPathInfo(path, NULL)) return true;
    }
    return false;
}

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
static bool pc_pics;

void amiga_pc_pictures(bool pc) { pc_pics = pc; }

void amiga_note_pic(FarPtr bmp, s16 id)
{
    if (!on) return;
    car_forget(bmp);                                          /* a car frame there is gone */
    if (pc_pics) id = -1;                                     /* no full-colour version: the game's own */
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
        if ((s->img->px[y * s->img->w + x] >> 24) == 254)      /* the car's paint (the dashboards' top band, the paint can):
                                                                 * the brighter of the player's pair 6 / 7, as the car */
            return SH_PAINT(value_of(live_rgb(6)) > value_of(live_rgb(7)) ? 6 : 7) | (s->img->px[y * s->img->w + x] & 0xFFFFFF);
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

/* ---------------------------------------------------------------- widescreen strips */

/* ENHANCED (widescreen): painted strips (outpainted from the picture) for the window's side bars while a full-screen
 * picture is shown; whether it is shown is checked on the frame itself (its ceiling rows and edge columns match the
 * picture), so page copies, wipes and dialogs over the middle need no tracking */
static struct { s16 pic; const char *name; Img l, r; } wide[] = { { 1018, "garage", { 0 }, { 0 } } };

/* the part of the driving car left of the screen (game pixels), in full colour */
#define OFFCAR_W 240
static struct { u32 px[OFFCAR_W * 120]; int w, h, x, y; Uint64 ns; } offcars[4];   /* per page: the one shown is used, so
                                                                                     * the strip's part and the picture's part are the same step */

void amiga_sprite_unclipped(FarPtr spr, FarPtr dst, const Rect *r)
{
    if (!on || !r || !is_vram(dst)) return;
    Src s;
    src_resolve(&s, spr, true);
    if (s.kind != SRC_CAR) return;
    int page = (int)(vram_base(dst) / 0x2000) & 3;
    #define offcar offcars[page]
    if (r->dx >= 0) { offcar.w = 0; offcar.ns = SDL_GetTicksNS(); return; }   /* all on screen */
    int left = r->dx - r->sx, top = r->dy - r->sy;       /* the frame's (0, 0) on the screen */
    int w = -left, h = (int)desc_h(spr);
    if (w > OFFCAR_W) w = OFFCAR_W;
    if (h > 120) h = 120;
    u32 rgb[16];
    ega_rgb_now(rgb);
    /* the sprite's mask as ega_sprite_over_bg reads it: in the planes' segment at the mask's offset, one plane, a set bit
     * = the background shows (bg & mask) */
    FarPtr spl = desc_planes(spr);
    u32 mk = lin(spl.seg, desc_mask(spr).off), st = desc_stride(spr);
    bool has_mask = desc_mask(spr).off != 0 || desc_mask(spr).seg != 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 *o = &offcar.px[y * w + x];
            if (has_mask && (mem[mk + (u32)y * st + (u32)(x >> 3)] & (0x80 >> (x & 7)))) { *o = 0; continue; }
            if (src_clear(&s, x, y)) { *o = 0; continue; }
            u32 v = src_px(&s, x, y);
            if (v == SH_GAME || v == SH_KEEP || v == SH_NONE) {
                int c = bmp_px(spr, x, y);
                *o = c >= 0 ? rgb[c & 15] : 0;
            } else *o = sh_colour(v, rgb) | 0xFF000000u;
        }
    for (int x = 0; x < w; x++) {                         /* padding columns of the frame (all black, no mask): never on
                                                           * screen in the original, so see-through here */
        bool any = false;
        for (int y = 0; y < h && !any; y++) any = (offcar.px[y * w + x] & 0xFFFFFF) != 0;
        if (!any) for (int y = 0; y < h; y++) offcar.px[y * w + x] = 0;
    }
    offcar.w = w;
    offcar.h = h;
    offcar.x = left;
    offcar.y = top;
    offcar.ns = SDL_GetTicksNS();
    #undef offcar
}

void amiga_vram_copied(u32 src_off, u32 dst_off)
{
    int s = (int)(src_off / 0x2000) & 3, d = (int)(dst_off / 0x2000) & 3;
    if (s == d || offcars[s].ns <= offcars[d].ns) return;    /* (a copy is many calls: once per newer source) */
    offcars[d] = offcars[s];
}

const R3dStrips *amiga_wide_strips(const u32 *frame, int fw, int fh)
{
    static R3dStrips st;
    if (!on || !frame || fw != 320 || fh != 200) return NULL;
    for (size_t k = 0; k < SDL_arraysize(wide); k++) {
        Img *pic = &pic_img[wide[k].pic];
        char name[64];
        SDL_snprintf(name, sizeof name, "pic_%d.bmp", wide[k].pic);
        if (!load_img(pic, name) || pic->w != 320) continue;
        int n = 0, hit = 0;
        for (int y = 2; y < 44 && y < pic->h; y += 6)                     /* the ceiling */
            for (int x = 8; x < 320; x += 24, n++) hit += (frame[y * 320 + x] & 0xFFFFFF) == (pic->px[y * 320 + x] & 0xFFFFFF);
        for (int y = 50; y < pic->h; y += 10)                              /* the edge columns */
            for (int x = 2; x < 320; x += 312, n++) hit += (frame[y * 320 + x] & 0xFFFFFF) == (pic->px[y * 320 + x] & 0xFFFFFF);
        if (hit * 100 < n * 85) continue;
        SDL_snprintf(name, sizeof name, "wide/%s_l.bmp", wide[k].name);
        load_img(&wide[k].l, name);
        SDL_snprintf(name, sizeof name, "wide/%s_r.bmp", wide[k].name);
        load_img(&wide[k].r, name);
        if (!wide[k].l.px && !wide[k].r.px) return NULL;
        st.l = wide[k].l.px; st.lw = wide[k].l.w; st.lh = wide[k].l.h;
        st.r = wide[k].r.px; st.rw = wide[k].r.w; st.rh = wide[k].r.h;
        st.game_w = (wide[k].l.px ? wide[k].l.w : wide[k].r.w) * 320 / 1280;   /* made at 4 x the picture's width */
        st.rows = pic->h;
        st.below = frame[196 * 320] & 0xFFFFFF;           /* the status line's colour at its left end */
        int sp_ = ega_shown_page() & 3;
        bool car = offcars[sp_].w > 0 && SDL_GetTicksNS() - offcars[sp_].ns < 400 * SDL_NS_PER_MS;
        st.car = car ? offcars[sp_].px : NULL;
        st.car_w = offcars[sp_].w; st.car_h = offcars[sp_].h; st.car_x = offcars[sp_].x; st.car_y = offcars[sp_].y;
        return &st;
    }
    return NULL;
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

/* the swatch colours chosen, per car record (DGROUP offset), with its model and the game's own colour index then: a
 * record that holds another car now or has another colour is not used. Kept with the saved game: beside HOTROD<n>.SAV,
 * HOTROD<n>.SAV.paint, a line per car "<place> <model> <index> <rrggbb>" (place: 0 the current car, 1.. the car list,
 * in the save's order); the save itself stays the game's own. */
#define PAINT_MAX 32
#define PAINT_EXT ".paint"
static struct { u16 car; int model, index; u32 rgb; } paint[PAINT_MAX];
static int npaint;

void amiga_paint_set(u16 car, int index, u32 rgb)
{
    if (!on) return;
    int k = 0;
    for (int i = 0; i < npaint; i++)
        if (paint[i].car != car) paint[k++] = paint[i];
    npaint = k;
    if (rgb && npaint < PAINT_MAX) {
        paint[npaint].car = car;
        paint[npaint].model = (s16)DSW((u16)(car + CR_MODEL));
        paint[npaint].index = index;
        paint[npaint].rgb = rgb;
        npaint++;
    }
}

void amiga_paint_reset(void) { npaint = 0; }

/* the car record at place n of the save's order (0 the current car, then the car list), or 0 */
static u16 car_at(int n)
{
    if (n == 0) return DSW(DS_cur_car);
    u16 c = DSW(DS_car_list);
    for (int k = 1; c && k < n; k++) c = DSW((u16)(c + CR_NEXT));
    return c;
}

/* the host path of the supplement of saved game save_name (a DOS name: drive and path parts ignored, as dos.c) */
static char *paint_file(const char *save_name, bool create)
{
    const char *b = save_name;
    for (const char *q = save_name; *q; q++)
        if (*q == ':' || *q == '\\' || *q == '/') b = q + 1;
    char name[64];
    SDL_snprintf(name, sizeof name, "%s" PAINT_EXT, b);
    return host_game_path(name, create);
}

void amiga_paint_save(const char *save_name)
{
    if (!on) return;
    char *path = paint_file(save_name, true);
    if (!path) return;
    FILE *f = NULL;
    for (int n = 0; n < 64; n++) {
        u16 c = car_at(n);
        if (!c) { if (n) break; continue; }
        for (int i = 0; i < npaint; i++)
            if (paint[i].car == c && paint[i].model == (s16)DSW((u16)(c + CR_MODEL))) {
                if (!f && !(f = fopen(path, "w"))) { host_free(path); return; }
                fprintf(f, "%d %d %d %06x\n", n, paint[i].model, paint[i].index, (unsigned)paint[i].rgb);
            }
    }
    if (f) fclose(f);
    else remove(path);                                        /* no swatch colours: none left from an older save */
    host_free(path);
}

void amiga_paint_load(const char *save_name)
{
    npaint = 0;
    if (!on) return;
    char *path = paint_file(save_name, false);
    if (!path) return;
    FILE *f = fopen(path, "r");
    host_free(path);
    if (!f) return;
    char line[128];
    while (npaint < PAINT_MAX && fgets(line, sizeof line, f)) {
        int n, model, index;
        unsigned rgb;
        if (sscanf(line, "%d %d %d %x", &n, &model, &index, &rgb) != 4) continue;
        u16 c = car_at(n);
        if (!c || (s16)DSW((u16)(c + CR_MODEL)) != model) continue;
        paint[npaint].car = c;
        paint[npaint].model = model;
        paint[npaint].index = index;
        paint[npaint].rgb = rgb;
        npaint++;
    }
    fclose(f);
}

void amiga_paint_apply(u16 car, int index)
{
    u32 rgb = 0;
    if (on) {
        for (int i = 0; i < npaint; i++)
            if (paint[i].car == car && paint[i].model == (s16)DSW((u16)(car + CR_MODEL)) && paint[i].index == index)
                rgb = paint[i].rgb;
    }
    amiga_paint_preview(rgb, index);
}

/* palette registers rd (dark) / rl (light) holding the paint pair of game colour `index` shown as rgb: rl = rgb,
 * rd = a darker shade (rgb 0: their own colours) */
static void pair_paint(int rd, int rl, u32 rgb, int index)
{
    int ed = DSB((u16)(DS_PAINT + 3 * index)), el = DSB((u16)(DS_PAINT + 3 * index + 1));
    u32 dark = rgb ? ((rgb >> 16 & 255) * 7 / 10) << 16 | ((rgb >> 8 & 255) * 7 / 10) << 8 | (rgb & 255) * 7 / 10 : 0;
    if (rgb && !dark) dark = 0x010101;
    sh_set_paint(rd, dark, ed);
    sh_set_paint(rl, rgb, el);
    ega_touch();
}

/* the paint pair (registers 6 dark, 7 light) of game colour `index` shown as rgb: 7 = rgb, 6 = a darker shade */
void amiga_paint_preview(u32 rgb, int index) { pair_paint(6, 7, rgb, index); }

/* An opponent's own colour: one of the swatch colours (not the near-black), by the opponent's number (consecutive
 * opponents get different ones), instead of the game's six; the King (21) black. */
#define OPP_KING 0x15
#define KING_RGB 0x282830           /* the swatch's black: its shading still shows */
static u32 opp_rgb(int opp)
{
    if (opp == OPP_KING) return KING_RGB;
    u32 c[AMIGA_SWATCH_N];
    int n = 0;
    for (int i = 0; i < AMIGA_SWATCH_N; i++)
        if (value_of(swatch[i]) > 0.25f) c[n++] = swatch[i];
    if (opp < 0) opp = -opp;
    return c[(opp * 7 + 3) % n];
}

void amiga_opp_paint(int opp, int index)
{
    if (on) pair_paint(6, 7, opp_rgb(opp), index);
}

void amiga_opp_race_paint(int opp, int index)
{
    if (on) pair_paint(12, 3, opp < 0 ? 0 : opp_rgb(opp), index);
}

/* ---------------------------------------------------------------- the driver's hands */

/* The steering wheel's knob: the game draws a 5 x 5 dot at its knob table's place for the wheel angle (-24..24), moved
 * a step when the angle changes; here a round knob that glides along the same path to the angle, behind the hands. */
#define DS_knob_xy 0x1C80           /* {u8 x, u8 y} for wheel angle w = -24..24 (race_dash.c) */
#define KNOB_EASE_S 0.06f           /* seconds: how fast the knob catches up with the wheel */
#define KNOB_MAX_SPEED 150.0f       /* wheel angle units a second at most (lock to lock in a third of a second) */

/* The hands (hand_l / hand_r: fists, arm_l / arm_r: an arm reaching up): straight, both fists on the rim where it meets
 * the bottom of the screen; turning left the right arm pushes the rim up on the right, turning right the left arm on
 * the left. The fists ride the rim (a circle about (RIM_X, RIM_Y)) with the knob's angle, HAND_FOLLOW of it: turning
 * left the left fist goes down, turning right the right one. */
#define RIM_X 170.0f
#define RIM_Y 92.2f
#define RIM_R 87.6f                 /* the fists' centres */
#define FIST_DEG 76.0f              /* from the top, either side */
#define HAND_FOLLOW 0.35f
enum { HAND_L, HAND_R, ARM_L, ARM_R, HANDS };
static const char *hand_file[HANDS] = { "hand_l.bmp", "hand_r.bmp", "arm_l.bmp", "arm_r.bmp" };
static const float arm_at[2][2] = { { 74, 59 }, { 269, 59 } };   /* the arms' centres */
static Img hand_img[HANDS];
static int hands_w = -1, knob_to;
static float knob_at;
static Uint64 knob_t;
static u32 layer[320 * 90];

void amiga_hands(int w, int angle)
{
    if (!on) return;
    if (angle < -24) angle = -24;
    if (angle > 24) angle = 24;
    if (hands_w < 0) knob_at = (float)angle;     /* a new race: the knob starts where the wheel is */
    if (w == hands_w && angle == knob_to) return;
    hands_w = w;
    if (angle != knob_to && !amiga_hands_moving()) knob_t = SDL_GetTicksNS();   /* the glide starts now */
    knob_to = angle;
    ega_touch();
}

bool amiga_hands_moving(void)
{
    return on && hands_w >= 0 && SDL_fabsf((float)knob_to - knob_at) > 0.01f;
}

static void knob_xy(float w, float *x, float *y)
{
    int i = (int)SDL_floorf(w);
    if (i < -24) i = -24;
    if (i > 23) i = 23;
    float f = w - (float)i;
    u16 a = (u16)(DS_knob_xy + 2 * i), b = (u16)(a + 2);
    *x = (float)DSB(a) + ((float)DSB(b) - (float)DSB(a)) * f;
    *y = (float)DSB((u16)(a + 1)) + ((float)DSB((u16)(b + 1)) - (float)DSB((u16)(a + 1))) * f;
}

/* a red ball of radius 2.6 centred at (cx, cy) (pixel centres at + 0.5), 4 x 4 samples a pixel, lit from the top left */
static void knob_draw(float cx, float cy)
{
    const float r = 2.6f;
    for (int y = (int)(cy - r) - 1; y <= (int)(cy + r) + 1; y++)
        for (int x = (int)(cx - r) - 1; x <= (int)(cx + r) + 1; x++) {
            if (x < 0 || x >= 320 || y < 0 || y >= 90) continue;
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float dx = (float)x + (sx + 0.5f) / 4 - cx, dy = (float)y + (sy + 0.5f) / 4 - cy;
                    in += dx * dx + dy * dy <= r * r;
                }
            if (!in) continue;
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float lit = 1.0f - SDL_sqrtf((dx + 1.0f) * (dx + 1.0f) + (dy + 1.0f) * (dy + 1.0f)) / (2.0f * r);
            if (lit < 0) lit = 0;
            int R = (int)(150 + 105 * lit), G = (int)(20 + 110 * lit * lit), B = (int)(20 + 110 * lit * lit);
            u32 bg = layer[y * 320 + x];
            int a = in * 255 / 16;
            if (bg >> 24) {                                   /* over what is there (the layer is still empty) */
                R = (R * a + (int)(bg >> 16 & 255) * (255 - a)) / 255;
                G = (G * a + (int)(bg >> 8 & 255) * (255 - a)) / 255;
                B = (B * a + (int)(bg & 255) * (255 - a)) / 255;
                a = 255;
            }
            layer[y * 320 + x] = (u32)a << 24 | (u32)R << 16 | (u32)G << 8 | (u32)B;
        }
}

/* hand picture i centred at (cx, cy) of the layer, in front */
static void hand_put(int i, float cx, float cy)
{
    const Img *im = &hand_img[i];
    int x0 = (int)SDL_floorf(cx - (float)im->w / 2 + 0.5f), y0 = (int)SDL_floorf(cy - (float)im->h / 2 + 0.5f);
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            u32 c = im->px[y * im->w + x];
            int tx = x0 + x, ty = y0 + y;
            if (c >> 24 && tx >= 0 && tx < 320 && ty >= 0 && ty < 90) layer[ty * 320 + tx] = c;
        }
}

const u32 *amiga_hands_layer(void)
{
    if (!on || hands_w < 0 || hands_w > 2) return NULL;
    for (int i = 0; i < HANDS; i++)
        if (!load_img(&hand_img[i], hand_file[i])) return NULL;
    Uint64 now = SDL_GetTicksNS();
    float dt = knob_t ? (float)(now - knob_t) / 1e9f : 0.0f;
    knob_t = now;
    if (dt > 0.1f) dt = 0.1f;
    float step = ((float)knob_to - knob_at) * (1.0f - SDL_expf(-dt / KNOB_EASE_S));
    if (SDL_fabsf(step) > KNOB_MAX_SPEED * dt) step = step > 0 ? KNOB_MAX_SPEED * dt : -KNOB_MAX_SPEED * dt;
    knob_at += step;
    if (SDL_fabsf((float)knob_to - knob_at) <= 0.01f) knob_at = (float)knob_to;
    memset(layer, 0, sizeof layer);
    float kx, ky;
    knob_xy(knob_at, &kx, &ky);
    knob_draw(kx + 0.5f, ky + 0.5f);
    float turn = SDL_atan2f(kx - RIM_X, RIM_Y - ky) * HAND_FOLLOW;   /* the wheel's turn (radians, + = right) */
    const float fist = FIST_DEG * 3.14159265f / 180.0f;
    if (hands_w == 2) hand_put(ARM_L, arm_at[0][0], arm_at[0][1]);
    else hand_put(HAND_L, RIM_X + RIM_R * SDL_sinf(turn - fist), RIM_Y - RIM_R * SDL_cosf(turn - fist));
    if (hands_w == 1) hand_put(ARM_R, arm_at[1][0], arm_at[1][1]);
    else hand_put(HAND_R, RIM_X + RIM_R * SDL_sinf(turn + fist), RIM_Y - RIM_R * SDL_cosf(turn + fist));
    return layer;
}
