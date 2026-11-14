#include "platform/shadow.h"

#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "platform/ega.h"

static bool on;
static u32 *px;                              /* EGA_PLANE_SIZE * 8 */
static u32 latch[8];

static struct {
    bool on;
    u32 base, stride;
    int w, h;
    ShSource fn;
    void *user;
} ctx;

void sh_enable(bool enable)
{
    on = enable;
    if (on && !px) px = calloc((size_t)EGA_PLANE_SIZE * 8, sizeof(u32));
    if (!px) on = false;
}

bool sh_enabled(void) { return on; }
u32 *sh_pixels(void) { return px; }

void sh_clear(void)
{
    if (px) memset(px, 0, (size_t)EGA_PLANE_SIZE * 8 * sizeof(u32));
}

void sh_latch(u16 off)
{
    if (on) memcpy(latch, px + (u32)off * 8, sizeof latch);
}

static u32 ctx_value(u16 off, int k)
{
    if (!ctx.on || off < ctx.base) return SH_NONE;
    u32 rel = off - ctx.base, row = rel / ctx.stride;
    int x = (int)(rel % ctx.stride) * 8 + k;
    if (row >= (u32)ctx.h || x >= ctx.w) return SH_NONE;
    return ctx.fn(ctx.user, x, (int)row);
}

void sh_write(u16 off, u8 before, u8 after, u8 mask, int kind)
{
    if (!on) return;
    u32 *p = px + (u32)off * 8;
    for (int k = 0; k < 8; k++) {
        u8 bit = (u8)(0x80 >> k);
        if (!(mask & bit)) continue;
        if (kind == SH_W_LATCH) { p[k] = latch[k]; continue; }
        u32 v = ctx_value(off, k);
        if (v == SH_KEEP) continue;
        if (v != SH_NONE) { p[k] = v; continue; }
        if (kind == SH_W_FILL || ((before ^ after) & bit)) p[k] = SH_GAME;
    }
}

void sh_copy(u16 soff, u16 doff)
{
    if (on && soff != doff) memmove(px + (u32)doff * 8, px + (u32)soff * 8, 8 * sizeof(u32));
}

void sh_begin(u32 base, u32 stride, int w, int h, ShSource fn, void *user)
{
    if (!on || !fn || stride == 0 || w <= 0 || h <= 0) { ctx.on = false; return; }
    ctx.on = true;
    ctx.base = base;
    ctx.stride = stride;
    ctx.w = w;
    ctx.h = h;
    ctx.fn = fn;
    ctx.user = user;
}

void sh_end(void) { ctx.on = false; }

u32 sh_at(u32 base, u32 stride, int x, int y)
{
    if (!on || x < 0 || y < 0) return SH_GAME;
    u32 off = (base + (u32)y * stride + (u32)(x >> 3)) & 0xFFFFu;
    return px[off * 8 + (u32)(x & 7)];
}

/* ---------------------------------------------------------------- RAM blocks */

#define BLOCKS 32
static struct { u16 seg, off; u32 stride; int h; u32 sum; u32 *px; } blocks[BLOCKS];
static int block_next;

static u32 ram_sum(u16 seg, u16 off, u32 stride, int h)
{
    u32 a = lin(seg, off), n = 4 * stride * (u32)h, s = 2166136261u;
    if (a + n > MEM_SIZE) return 0;
    for (u32 i = 0; i < n; i++) s = (s ^ mem[a + i]) * 16777619u;
    return s;
}

void sh_block_set(u16 seg, u16 off, u32 stride, int h, u32 *bpx)
{
    if (!on || !bpx) { free(bpx); return; }
    int slot = -1;
    for (int i = 0; i < BLOCKS; i++)
        if (blocks[i].px && blocks[i].seg == seg && blocks[i].off == off) slot = i;
    if (slot < 0) { slot = block_next; block_next = (block_next + 1) % BLOCKS; }
    free(blocks[slot].px);
    blocks[slot].seg = seg;
    blocks[slot].off = off;
    blocks[slot].stride = stride;
    blocks[slot].h = h;
    blocks[slot].px = bpx;
    blocks[slot].sum = ram_sum(seg, off, stride, h);
}

const u32 *sh_block_get(u16 seg, u16 off, u32 stride, int h)
{
    if (!on) return NULL;
    for (int i = 0; i < BLOCKS; i++)
        if (blocks[i].px && blocks[i].seg == seg && blocks[i].off == off && blocks[i].stride == stride &&
            blocks[i].h == h)
            return ram_sum(seg, off, stride, h) == blocks[i].sum ? blocks[i].px : NULL;
    return NULL;
}

/* ---------------------------------------------------------------- colours */

static void rgb_to_hsv(u32 c, float *h, float *s, float *v)
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

static u32 hsv_to_rgb(float h, float s, float v)
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
    return 0xFF000000u | (u32)(r * 255.0f + 0.5f) << 16 | (u32)(g * 255.0f + 0.5f) << 8 | (u32)(b * 255.0f + 0.5f);
}

u32 sh_colour(u32 v, const u32 *pal)
{
    if (!SH_IS_PAINT(v)) return v | 0xFF000000u;
    /* the paint over the Amiga shading: hue and saturation of the live paint register, the Amiga brightness */
    float ph, ps, pv, hh, ss, vv;
    rgb_to_hsv(pal[v >> 24 & 15], &ph, &ps, &pv);
    rgb_to_hsv(v, &hh, &ss, &vv);
    return hsv_to_rgb(ph, ps < 0.08f ? 0.0f : (ss < ps ? ss : ps), vv * (0.55f + 0.45f * pv));
}
