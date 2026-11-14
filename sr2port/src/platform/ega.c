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
 * colour indices, 0xFF = not part of it; rgb = the replacement) at page rows y.., columns x.. of any page (the game
 * draws on the back page, copies and flips). Where the screen still shows the reference pixel (and its 4 neighbours
 * do too, so things drawn on top in the picture's own colour stay the game's), the replacement is shown; only while
 * most of the visible region matches, so regions of earlier screens switch themselves off. Newest last (on top). */
#define BD_MAX 32
static struct { u8 *ref; u32 *rgb; int x, y, w, h; } bd[BD_MAX];
static int bd_n;

void ega_add_backdrop(u8 *ref, u32 *rgb, int x, int y, int w, int h)
{
    if (!ref || !rgb || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > 320 || y + h > 204) { free(ref); free(rgb); return; }
    int k = 0;
    for (int i = 0; i < bd_n; i++) {                            /* drop the regions the new one covers */
        bool inside = bd[i].x >= x && bd[i].y >= y && bd[i].x + bd[i].w <= x + w && bd[i].y + bd[i].h <= y + h;
        if (inside) { free(bd[i].ref); free(bd[i].rgb); }
        else bd[k++] = bd[i];
    }
    bd_n = k;
    if (bd_n == BD_MAX) {                                       /* the oldest goes */
        free(bd[0].ref); free(bd[0].rgb);
        memmove(bd, bd + 1, (BD_MAX - 1) * sizeof bd[0]);
        bd_n--;
    }
    bd[bd_n].ref = ref; bd[bd_n].rgb = rgb; bd[bd_n].x = x; bd[bd_n].y = y; bd[bd_n].w = w; bd[bd_n].h = h;
    bd_n++;
    dirty = true;
}

static void apply_backdrops(u32 *xrgb)
{
    if (!bd_n) return;
    static u8 idx[320 * 200];
    int prow[200];                                              /* page row shown on each screen row */
    for (int y = 0; y < 200; y++) {
        u16 row = y < line_compare ? (u16)(start_addr + y * EGA_BYTES_PER_LINE)
                                   : (u16)((y - line_compare) * EGA_BYTES_PER_LINE);
        prow[y] = (row % 0x2000) / EGA_BYTES_PER_LINE;
        for (int x = 0; x < 320; x++) {
            u16 o = (u16)(row + (x >> 3));
            int bit = 7 - (x & 7);
            idx[y * 320 + x] = (u8)((planes[0][o] >> bit & 1) | (planes[1][o] >> bit & 1) << 1 |
                                    (planes[2][o] >> bit & 1) << 2 | (planes[3][o] >> bit & 1) << 3);
        }
    }
    for (int r = 0; r < bd_n; r++) {
        int same = 0, seen = 0;
        for (int y = 0; y < 200; y++) {
            int ry = prow[y] - bd[r].y;
            if (ry < 0 || ry >= bd[r].h) continue;
            for (int x = 0; x < bd[r].w; x++) {
                u8 q = bd[r].ref[ry * bd[r].w + x];
                if (q == 0xFF) continue;
                seen++;
                same += idx[y * 320 + bd[r].x + x] == q;
            }
        }
        if (!seen || same * 10 < seen * 6) continue;           /* under 60 %: not on screen */
        for (int y = 0; y < 200; y++) {
            int ry = prow[y] - bd[r].y;
            if (ry < 0 || ry >= bd[r].h) continue;
            for (int x = 0; x < bd[r].w; x++) {
                int sx = bd[r].x + x;
                u8 q = bd[r].ref[ry * bd[r].w + x];
                if (q == 0xFF || idx[y * 320 + sx] != q) continue;
                bool ok = true;                                 /* the 4 neighbours (screen) still the picture */
                if (x > 0 && bd[r].ref[ry * bd[r].w + x - 1] != 0xFF && idx[y * 320 + sx - 1] != bd[r].ref[ry * bd[r].w + x - 1]) ok = false;
                if (x + 1 < bd[r].w && bd[r].ref[ry * bd[r].w + x + 1] != 0xFF && idx[y * 320 + sx + 1] != bd[r].ref[ry * bd[r].w + x + 1]) ok = false;
                if (ry > 0 && y > 0 && prow[y - 1] == prow[y] - 1 && bd[r].ref[(ry - 1) * bd[r].w + x] != 0xFF &&
                    idx[(y - 1) * 320 + sx] != bd[r].ref[(ry - 1) * bd[r].w + x]) ok = false;
                if (ry + 1 < bd[r].h && y + 1 < 200 && prow[y + 1] == prow[y] + 1 && bd[r].ref[(ry + 1) * bd[r].w + x] != 0xFF &&
                    idx[(y + 1) * 320 + sx] != bd[r].ref[(ry + 1) * bd[r].w + x]) ok = false;
                if (ok) xrgb[y * 320 + sx] = bd[r].rgb[ry * bd[r].w + x];
            }
        }
    }
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
