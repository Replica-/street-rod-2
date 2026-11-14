/* ENHANCED (--amiga): the Amiga release's pictures and cars in place of the PC ones, in full colour.
 *
 * The graphics are ready files in gfx/amiga next to the executable (or in the working directory), made once by
 * tools/amiga_export.py from the user's Amiga disks (game data, never committed):
 *   pic_<id>.bmp            replaces PC picture <id> (same size)
 *   car_<m>.bmp             the body of car model m: alpha 0 = background, 254 = paint, 255 = other
 *   car_<m>_<piece>.bmp     roof / rbump / fbump: chopped roof, stripped bumpers (same coding)
 *   cars.txt                where each piece goes on its body
 * Nothing is converted or guessed at run time.
 *
 * How they reach the screen: the game still draws its 16-colour picture (the game's own canvas); when it blits a
 * picture that has a replacement onto a screen page, the blitted rectangle goes to the frame composer with the
 * replacement (ega_add_backdrop), which shows it in full colour wherever the screen still shows that picture -
 * everything the game draws on top stays. Cars: car_compose turns the PC body into the Amiga body (in the game's 16
 * colours, so layout, wheels, driver, hot spots stay), and wherever the car is drawn (blit, parked copy, driving
 * animation) the full-colour body goes on top, its paint recoloured to the car's paint over the Amiga shading. */
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

static bool on;
static char dir[1024];

typedef struct { uint32_t *px; int w, h; bool tried; } Img;   /* 0xAARRGGBB */

static bool load_img(Img *im, const char *name)
{
    if (im->tried) return im->px != NULL;
    im->tried = true;
    char path[1200];
    SDL_snprintf(path, sizeof path, "%s/%s", dir, name);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) return false;
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
    if (!enable) return false;
    const char *bases[2] = { SDL_GetBasePath(), "." };
    for (int i = 0; i < 2 && !on; i++) {
        if (!bases[i]) continue;
        SDL_snprintf(dir, sizeof dir, "%s%sgfx/amiga", bases[i], i == 1 ? "/" : "");
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
    if (!on) fprintf(stderr, "amiga: no gfx/amiga folder (make it with tools/amiga_export.py); the PC pictures stay\n");
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

static Img pic_img[NMAP];

static int map_of(FarPtr bmp)
{
    for (int i = 0; i < TRACK; i++)
        if (track[i].seg == bmp.seg && track[i].off == bmp.off)
            for (int m = 0; m < NMAP; m++)
                if (amiga_map[m].pc_id == track[i].id) return m;
    return -1;
}

static int page_of(FarPtr page)
{
    FarPtr p = desc_planes(page);
    return (int)(((u32)(p.seg - 0xA000) * 16u + p.off) / 0x2000);
}

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
static struct { u16 seg, off; int model, w, h, light, dark; uint8_t *fit; } car_frames[8];   /* fit: 0xFF = clear */
static int car_next;
static struct { u16 seg, off, bseg, boff; int shift; } car_shifted[8];  /* frames 1..3 of a drive: base + shift */

/* a picture loaded where a car frame was: the frame is gone (its memory is used again) */
static void car_forget(FarPtr bmp)
{
    for (int i = 0; i < 8; i++) {
        if (car_frames[i].fit && car_frames[i].seg == bmp.seg && car_frames[i].off == bmp.off) {
            free(car_frames[i].fit);
            car_frames[i].fit = NULL;
        }
        if (car_shifted[i].seg == bmp.seg && car_shifted[i].off == bmp.off) car_shifted[i].seg = car_shifted[i].off = 0;
    }
}

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
        float f = f6 - (float)i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
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
    float h1, s1, v1, h2, s2, v2;
    rgb_to_hsv(live_rgb(a), &h1, &s1, &v1);
    rgb_to_hsv(live_rgb(b), &h2, &s2, &v2);
    *light = v1 >= v2 ? a : b;
    *dark = v1 >= v2 ? b : a;
}

bool amiga_car_body(FarPtr frame, s16 model, s16 flags)
{
    if (!on || model < 0 || model >= 22 || desc_type(frame) == DESC_TYPE_EGA || !bmp_ok(frame)) return false;
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
    if (!fit) return false;
    u32 live[16];
    for (int i = 0; i < 16; i++) live[i] = live_rgb(i);
    FarPtr pl = desc_planes(frame);
    u32 b = lin(pl.seg, pl.off), mb = lin(mk.seg, mk.off), st = desc_stride(frame), plane = st * (u32)h;
    int ox = (w - aw) / 2, oy = h - ah;                               /* bottom and centre aligned */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int ax = x - ox, ay = y - oy;
            uint32_t a = (ax >= 0 && ay >= 0 && ax < aw && ay < ah) ? car_work[model].px[ay * aw + ax] : 0;
            uint8_t q = 0xFF;
            if ((a >> 24) == 254) {                                   /* paint: the light or the dark register */
                float hh, ss, vv;
                rgb_to_hsv(a, &hh, &ss, &vv);
                q = (uint8_t)(vv > 0.62f ? light : dark);
            } else if ((a >> 24) == 255) {                            /* the nearest live colour */
                long best = -1;
                for (int i = 0; i < 16; i++) {
                    long d = labs((long)(a >> 16 & 255) - (long)(live[i] >> 16 & 255)) +
                             labs((long)(a >> 8 & 255) - (long)(live[i] >> 8 & 255)) + labs((long)(a & 255) - (long)(live[i] & 255));
                    if (best < 0 || d < best) { best = d; q = (uint8_t)i; }
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
    for (int i = 0; i < 8; i++)
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
    car_frames[car_next].light = light;
    car_frames[car_next].dark = dark;
    car_frames[car_next].fit = fit;
    car_next = (car_next + 1) % 8;
    return true;
}

/* a PC tyre picture: its black (colour 0) reaching the picture's edge goes see-through (the corners around the round
 * tyre, invisible on the PC's black floor, black blocks on the Amiga's grey one) */
void amiga_tyre_edge_clear(FarPtr pic)
{
    if (!on || desc_type(pic) == DESC_TYPE_EGA || !bmp_ok(pic)) return;
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

/* a car frame drawn (rect r) on page dpage: the full-colour body, paint recoloured to the live paint (hue and
 * saturation of the light paint register, the Amiga brightness), where the frame shows the body; parts of the body
 * outside the PC frame (taller roofs) are drawn always (0xFE) */
static bool car_blit(FarPtr src, int dpage, const Rect *r)
{
    int f = -1, shift = 0;
    FarPtr base = src;
    for (int i = 0; i < 8; i++)
        if ((src.seg || src.off) && car_shifted[i].seg == src.seg && car_shifted[i].off == src.off) {
            base.seg = car_shifted[i].bseg;
            base.off = car_shifted[i].boff;
            shift = car_shifted[i].shift;
        }
    for (int i = 0; i < 8; i++)
        if (car_frames[i].fit && car_frames[i].seg == base.seg && car_frames[i].off == base.off) f = i;
    if (f < 0) return false;
    int model = car_frames[f].model, w = car_frames[f].w, h = car_frames[f].h, sw = desc_w(src);
    if (desc_h(src) != h || sw < w || sw > w + 8) return false;      /* not the car (its memory used again) */
    if (!car_work[model].px || r->sx < 0 || r->sy < 0 || r->sx + r->w > sw || r->sy + r->h > h || !bmp_ok(src))
        return true;
    int aw = car_work[model].w, ah = car_work[model].h, ox = (w - aw) / 2, oy = h - ah;
    int x0 = r->sx, y0 = r->sy, x1 = r->sx + r->w, y1 = r->sy + r->h;
    if (r->sx == 0 && r->w >= w && r->sy == 0) {
        if (ox + shift < x0) x0 = ox + shift;
        if (ox + shift + aw > x1) x1 = ox + shift + aw;
        if (oy < y0) y0 = oy;
    }
    if (r->dx + (x0 - r->sx) < 0) x0 = r->sx - r->dx;
    if (r->dx + (x1 - r->sx) > 320) x1 = r->sx - r->dx + 320;
    if (r->dy + (y0 - r->sy) < 0) y0 = r->sy - r->dy;
    int RW = x1 - x0, RH = y1 - y0;
    if (RW <= 0 || RH <= 0) return true;
    uint8_t *ref = malloc((size_t)RW * RH);
    uint32_t *rgb = malloc((size_t)RW * RH * sizeof(uint32_t));
    if (!ref || !rgb) { free(ref); free(rgb); return true; }
    float ph, ps, pv;
    rgb_to_hsv(live_rgb(car_frames[f].light), &ph, &ps, &pv);
    for (int y = 0; y < RH; y++)
        for (int x = 0; x < RW; x++) {
            int fx = x0 + x, fy = y0 + y, ax = fx - shift - ox, ay = fy - oy, k = y * RW + x;
            uint32_t a = (ax >= 0 && ay >= 0 && ax < aw && ay < ah) ? car_work[model].px[ay * aw + ax] : 0;
            bool amiga = (a >> 24) >= 254;
            uint32_t col = 0xFF000000u | (a & 0xFFFFFF);
            if ((a >> 24) == 254) {                                   /* paint over the Amiga shading */
                float hh, ss, vv;
                rgb_to_hsv(a, &hh, &ss, &vv);
                col = hsv_to_rgb(ph, ps < 0.08f ? 0.0f : (ss < ps ? ss : ps), vv * (0.55f + 0.45f * pv));
            }
            rgb[k] = col;
            bool in_frame = fx >= r->sx && fy >= r->sy && fx < r->sx + r->w && fy < r->sy + r->h;
            if (!in_frame) { ref[k] = amiga ? 0xFE : 0xFF; continue; }
            int c = bmp_px(src, fx, fy), bx = fx - shift;
            uint8_t q = (bx >= 0 && bx < w) ? car_frames[f].fit[fy * w + bx] : 0xFF;
            ref[k] = (c < 0 || q == 0xFF || c != q || !amiga) ? 0xFF : (uint8_t)c;
        }
    if (getenv("SR_AMIGA_TRACE"))
        fprintf(stderr, "amiga: car %d -> page %d at %d,%d %dx%d, paint %d/%d\n", model, dpage, r->dx + (x0 - r->sx),
                r->dy + (y0 - r->sy), RW, RH, car_frames[f].light, car_frames[f].dark);
    ega_add_backdrop(ref, rgb, dpage, r->dx + (x0 - r->sx), r->dy + (y0 - r->sy), RW, RH, BD_CAR);
    return true;
}

void amiga_car_shown(FarPtr frame, FarPtr page, int x, int y, int frow0, int rows)
{
    if (!on || desc_type(page) != DESC_TYPE_EGA) return;
    Rect r;
    r.w = (s16)desc_w(frame); r.h = (s16)rows; r.sx = 0; r.sy = (s16)frow0; r.dx = (s16)x; r.dy = (s16)y;
    car_blit(frame, page_of(page), &r);
}

void amiga_on_sprite(FarPtr sprite, FarPtr page, const Rect *r)
{
    if (!on || !r || desc_type(page) != DESC_TYPE_EGA) return;
    car_blit(sprite, page_of(page), r);
}

/* ---------------------------------------------------------------- blits onto the screen */

void amiga_on_blit(FarPtr src, FarPtr dst, const Rect *r, bool masked)
{
    (void)masked;
    if (!on || !r || desc_type(dst) != DESC_TYPE_EGA) return;
    int dpage = page_of(dst);
    if (desc_type(src) == DESC_TYPE_EGA) {                            /* page to page: the regions go along */
        ega_copy_backdrops(page_of(src), r->sx, r->sy, r->w, r->h, dpage, r->dx, r->dy);
        return;
    }
    if (car_blit(src, dpage, r)) return;
    if (getenv("SR_AMIGA_TRACE"))
        for (int i = 0; i < TRACK; i++)
            if (track[i].seg == src.seg && track[i].off == src.off)
                fprintf(stderr, "amiga: picture %d %dx%d -> page %d at %d,%d\n", track[i].id, r->w, r->h, dpage, r->dx, r->dy);
    int m = map_of(src);
    if (m < 0) return;
    char name[32];
    SDL_snprintf(name, sizeof name, "pic_%d.bmp", amiga_map[m].pc_id);
    Img *im = &pic_img[m];
    int sw = desc_w(src), sh = desc_h(src);
    if (!load_img(im, name) || im->w != sw || im->h != sh || !bmp_ok(src)) return;
    int w = r->w, h = r->h, sx = r->sx, sy = r->sy;
    if (w <= 0 || h <= 0 || sx < 0 || sy < 0 || sx + w > sw || sy + h > sh) return;
    uint8_t *ref = malloc((size_t)w * h);
    uint32_t *rgb = malloc((size_t)w * h * sizeof(uint32_t));
    if (!ref || !rgb) { free(ref); free(rgb); return; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int c = bmp_px(src, sx + x, sy + y);
            ref[y * w + x] = c < 0 ? 0xFF : (uint8_t)c;               /* 0xFF: not part of the picture */
            rgb[y * w + x] = 0xFF000000u | im->px[(sy + y) * sw + sx + x];
        }
    ega_add_backdrop(ref, rgb, dpage, r->dx, r->dy, w, h, BD_STRICT);
}
