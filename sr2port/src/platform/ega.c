#include "platform/ega.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "host.h"
#include "r3d/r3d.h"

static u8 planes[4][EGA_PLANE_SIZE];
static u16 start_addr;
static int line_compare = 200;
static bool dirty = true;
/* Mode 0Dh defaults after INT 10h AH=0 (200-line values), overscan 0. */
static u8 palette_reg[17] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0 };

const u32 ega_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/* ENHANCED (R1.2): see-through masks of the race pages (index = page number = start address / 2000h), 320 x 200,
 * 1 = the 3D race view shows through. Applied only while a 3D race view is live and the page still holds what it
 * held when the mask was made (R1.8: a crash picture or the next screen drawn on that page is shown whole). */
static const u8 *page_mask[4];
static u32 page_sum[4];

static u32 page_checksum(int page)
{
    u32 h = 2166136261u, base = (u32)page * 0x2000u;
    for (int p = 0; p < 4; p++)
        for (u32 o = 18 * EGA_BYTES_PER_LINE; o < 102 * EGA_BYTES_PER_LINE; o++)
            h = (h ^ planes[p][(base + o) & 0xFFFF]) * 16777619u;
    return h;
}

void ega_set_page_mask(int page, const u8 *mask)
{
    if (page >= 0 && page < 4) {
        page_mask[page] = mask;
        if (mask) page_sum[page] = page_checksum(page);
        dirty = true;
    }
}

/* ENHANCED (--amiga, enh/amiga.c): pictures in full colour. Each region = a picture as the game blitted it (ref = its
 * colour indices, 0xFF = not part of it; rgb = the replacement) at row y, column x of a page (page = plane offset /
 * 2000h; rows past 200 are the page's storage). Page-to-page blits copy the regions inside (the race sky from page 1's
 * storage into the view, back page -> front page), any blit drops the regions it covers. The composer shows the
 * replacement where the screen still shows the reference pixel (and its 4 neighbours do too, so things drawn on top in
 * the picture's own colour stay the game's), only while most of the visible region matches. Newest last (on top). */
#define BD_MAX 128
static struct { u8 *ref; u32 *rgb; int page, x, y, w, h; bool strict, car; } bd[BD_MAX];   /* strict: 4-neighbour rule */
static int bd_n;

static void bd_free(int i) { free(bd[i].ref); free(bd[i].rgb); }

void ega_drop_backdrops(int page, int x, int y, int w, int h)
{
    int k = 0;
    for (int i = 0; i < bd_n; i++) {
        bool inside = bd[i].page == page && bd[i].x >= x && bd[i].y >= y && bd[i].x + bd[i].w <= x + w &&
                      bd[i].y + bd[i].h <= y + h;
        if (inside) bd_free(i);
        else bd[k++] = bd[i];
    }
    if (k != bd_n) dirty = true;
    bd_n = k;
}

void ega_add_backdrop(u8 *ref, u32 *rgb, int page, int x, int y, int w, int h, int flags)
{
    bool strict = flags & BD_STRICT;
    if (!ref || !rgb || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > 320 || page < 0 || page > 7) {
        free(ref); free(rgb); return;
    }
    ega_drop_backdrops(page, x, y, w, h);
    if (flags & BD_CAR) {                                       /* one car per page: its earlier positions go */
        int k = 0;
        for (int i = 0; i < bd_n; i++) {
            if (bd[i].page == page && bd[i].car) bd_free(i);
            else bd[k++] = bd[i];
        }
        bd_n = k;
    }
    if (bd_n == BD_MAX) {                                       /* the oldest goes */
        bd_free(0);
        memmove(bd, bd + 1, (BD_MAX - 1) * sizeof bd[0]);
        bd_n--;
    }
    bd[bd_n].ref = ref; bd[bd_n].rgb = rgb; bd[bd_n].page = page;
    bd[bd_n].x = x; bd[bd_n].y = y; bd[bd_n].w = w; bd[bd_n].h = h; bd[bd_n].strict = strict;
    bd[bd_n].car = (flags & BD_CAR) != 0;
    bd_n++;
    dirty = true;
}

void ega_copy_backdrops(int spage, int sx, int sy, int w, int h, int dpage, int dx, int dy)
{
    struct { u8 *ref; u32 *rgb; int x, y, w, h, flags; } add[BD_MAX];
    int na = 0;
    for (int i = 0; i < bd_n && na < BD_MAX; i++) {             /* the parts of src regions inside the copied rect */
        if (bd[i].page != spage) continue;
        int x0 = bd[i].x > sx ? bd[i].x : sx, y0 = bd[i].y > sy ? bd[i].y : sy;
        int x1 = bd[i].x + bd[i].w < sx + w ? bd[i].x + bd[i].w : sx + w;
        int y1 = bd[i].y + bd[i].h < sy + h ? bd[i].y + bd[i].h : sy + h;
        if (x1 <= x0 || y1 <= y0) continue;
        int cw = x1 - x0, ch = y1 - y0;
        u8 *r = malloc((size_t)cw * ch);
        u32 *c = malloc((size_t)cw * ch * sizeof(u32));
        if (!r || !c) { free(r); free(c); continue; }
        for (int yy = 0; yy < ch; yy++) {
            int o = (y0 - bd[i].y + yy) * bd[i].w + (x0 - bd[i].x);
            memcpy(r + yy * cw, bd[i].ref + o, (size_t)cw);
            memcpy(c + yy * cw, bd[i].rgb + o, (size_t)cw * sizeof(u32));
        }
        add[na].ref = r; add[na].rgb = c; add[na].x = dx + (x0 - sx); add[na].y = dy + (y0 - sy);
        add[na].w = cw; add[na].h = ch; add[na].flags = (bd[i].strict ? BD_STRICT : 0); na++;
    }
    ega_drop_backdrops(dpage, dx, dy, w, h);                    /* overwritten */
    for (int a = 0; a < na; a++)
        ega_add_backdrop(add[a].ref, add[a].rgb, dpage, add[a].x, add[a].y, add[a].w, add[a].h, add[a].flags);
}

static void apply_backdrops(u32 *xrgb)
{
    if (!bd_n) return;
    static u8 idx[320 * 200];
    int prow[200], ppage[200];                                  /* page and page row shown on each screen row */
    for (int y = 0; y < 200; y++) {
        u16 row = y < line_compare ? (u16)(start_addr + y * EGA_BYTES_PER_LINE)
                                   : (u16)((y - line_compare) * EGA_BYTES_PER_LINE);
        ppage[y] = row / 0x2000;
        prow[y] = (row % 0x2000) / EGA_BYTES_PER_LINE;
        for (int x = 0; x < 320; x++) {
            u16 o = (u16)(row + (x >> 3));
            int bit = 7 - (x & 7);
            idx[y * 320 + x] = (u8)((planes[0][o] >> bit & 1) | (planes[1][o] >> bit & 1) << 1 |
                                    (planes[2][o] >> bit & 1) << 2 | (planes[3][o] >> bit & 1) << 3);
        }
    }
#define RY(yy) ((ppage[yy] == bd[r].page) ? prow[yy] - bd[r].y : -1)
    for (int r = 0; r < bd_n; r++) {
        int same = 0, seen = 0, W = bd[r].w;
        for (int y = 0; y < 200; y++) {
            int ry = RY(y);
            if (ry < 0 || ry >= bd[r].h) continue;
            for (int x = 0; x < W; x++) {
                u8 q = bd[r].ref[ry * W + x];
                if (q >= 0xFE) continue;
                seen++;
                same += idx[y * 320 + bd[r].x + x] == q;
            }
        }
        if (!seen || same * 10 < seen * 6) continue;           /* under 60 %: not on screen */
        for (int y = 0; y < 200; y++) {
            int ry = RY(y);
            if (ry < 0 || ry >= bd[r].h) continue;
            int ryu = y > 0 ? RY(y - 1) : -1, ryd = y + 1 < 200 ? RY(y + 1) : -1;
            for (int x = 0; x < W; x++) {
                int sx = bd[r].x + x;
                u8 q = bd[r].ref[ry * W + x];
                if (q == 0xFE) { xrgb[y * 320 + sx] = bd[r].rgb[ry * W + x]; continue; }    /* always (overhang) */
                if (q == 0xFF || idx[y * 320 + sx] != q) continue;
                bool ok = true;                                 /* the 4 neighbours (screen) still the picture */
                if (bd[r].strict) {
                if (x > 0 && bd[r].ref[ry * W + x - 1] < 0xFE && idx[y * 320 + sx - 1] != bd[r].ref[ry * W + x - 1]) ok = false;
                if (x + 1 < W && bd[r].ref[ry * W + x + 1] < 0xFE && idx[y * 320 + sx + 1] != bd[r].ref[ry * W + x + 1]) ok = false;
                if (ryu == ry - 1 && ryu >= 0 && bd[r].ref[ryu * W + x] < 0xFE && idx[(y - 1) * 320 + sx] != bd[r].ref[ryu * W + x]) ok = false;
                if (ryd == ry + 1 && ryd < bd[r].h && bd[r].ref[ryd * W + x] < 0xFE && idx[(y + 1) * 320 + sx] != bd[r].ref[ryd * W + x]) ok = false;
                }
                if (ok) xrgb[y * 320 + sx] = bd[r].rgb[ry * W + x];
            }
        }
    }
#undef RY
}

static bool compose(u32 *xrgb)
{
    if (!dirty) return false;
    dirty = false;
    u32 rgb[16];
    for (int i = 0; i < 16; i++) {
        u8 v = palette_reg[i];              /* 200-line decoding: bits 0-2 BGR, bit 4 intensity */
        rgb[i] = ega_palette[(v & 7) | (v >> 1 & 8)] | 0xFF000000u;   /* ENHANCED: alpha = opaque */
    }
    int page = start_addr / 0x2000;
    const u8 *mask = (page < 4 && r3d_view_live()) ? page_mask[page] : NULL;
    if (mask && page_checksum(page) != page_sum[page]) mask = NULL;
    for (int y = 0; y < 200; y++) {
        u16 row = y < line_compare ? (u16)(start_addr + y * EGA_BYTES_PER_LINE)
                                   : (u16)((y - line_compare) * EGA_BYTES_PER_LINE);
        u32 *out = xrgb + y * 320;
        const u8 *m = (mask && y < line_compare) ? mask + y * 320 : NULL;
        for (int b = 0; b < EGA_BYTES_PER_LINE; b++) {
            u16 o = (u16)(row + b);
            u8 p0 = planes[0][o], p1 = planes[1][o], p2 = planes[2][o], p3 = planes[3][o];
            for (int bit = 7; bit >= 0; bit--) {
                int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
                u32 px = rgb[c];
                if (m && m[b * 8 + 7 - bit]) px &= 0x00FFFFFFu;
                *out++ = px;
            }
        }
    }
    apply_backdrops(xrgb);
    return true;
}

void ega_show(void)
{
    dirty = true;
    host_set_frame_source(compose, 320, 200);
}

void ega_init(void)
{
    memset(planes, 0, sizeof planes);
    start_addr = 0;
    line_compare = 200;
    ega_show();
}

u8 *ega_plane(int plane) { return planes[plane & 3]; }

void ega_set_start(u16 offset)
{
    if (offset != start_addr) { start_addr = offset; dirty = true; }
}

void ega_set_line_compare(int line)
{
    if (line < 0) line = 0;
    if (line > 200) line = 200;
    if (line != line_compare) { line_compare = line; dirty = true; }
}

void ega_touch(void) { dirty = true; }

void ega_set_palette_reg(int reg, u8 value)
{
    if (reg < 0 || reg > 16) return;
    if (palette_reg[reg] != value) { palette_reg[reg] = value; dirty = true; }
}

u8 ega_get_palette_reg(int reg)
{
    return (reg >= 0 && reg <= 16) ? palette_reg[reg] : 0;
}
