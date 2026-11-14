#include "platform/ega.h"

#include <stdio.h>
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

/* ENHANCED (--amiga, enh/amiga.c): a backdrop in full colour. ref = the colour indices of the PC backdrop as the game
 * put it on screen, rgb = the replacement; where the screen still shows the backdrop pixel, the replacement is shown.
 * Only while most of the area matches (the backdrop is on screen), so it turns itself off on other screens. */
static const u8 *bd_ref;
static const u32 *bd_rgb;
static int bd_x, bd_y, bd_w, bd_h;

void ega_set_backdrop(const u8 *ref, const u32 *rgb, int x, int y, int w, int h)
{
    bd_ref = ref; bd_rgb = rgb; bd_x = x; bd_y = y; bd_w = w; bd_h = h;
    dirty = true;
}

static void apply_backdrop(u32 *xrgb)
{
    static u8 idx[320 * 200];
    if (!bd_ref || bd_x < 0 || bd_y < 0 || bd_x + bd_w > 320 || bd_y + bd_h > 200) return;
    int same = 0;
    for (int y = 0; y < bd_h; y++) {
        int sy = bd_y + y;
        u16 row = sy < line_compare ? (u16)(start_addr + sy * EGA_BYTES_PER_LINE)
                                    : (u16)((sy - line_compare) * EGA_BYTES_PER_LINE);
        for (int x = 0; x < bd_w; x++) {
            int sx = bd_x + x;
            u16 o = (u16)(row + (sx >> 3));
            int bit = 7 - (sx & 7);
            int c = (planes[0][o] >> bit & 1) | (planes[1][o] >> bit & 1) << 1 | (planes[2][o] >> bit & 1) << 2 |
                    (planes[3][o] >> bit & 1) << 3;
            idx[y * bd_w + x] = (u8)c;
            same += c == bd_ref[y * bd_w + x];
        }
    }
    if (same * 10 < bd_w * bd_h * 6) return;                    /* under 60 %: not the backdrop's screen */
    /* a pixel is backdrop when it and its 4 neighbours still match: something drawn on top in the backdrop's own
     * colour (the garage name in colour 0 on its white box, where the composed backdrop is 0) stays the game's */
#define BD_SAME(xx, yy) ((xx) < 0 || (yy) < 0 || (xx) >= bd_w || (yy) >= bd_h || \
                         idx[(yy) * bd_w + (xx)] == bd_ref[(yy) * bd_w + (xx)])
    for (int y = 0; y < bd_h; y++)
        for (int x = 0; x < bd_w; x++)
            if (BD_SAME(x, y) && BD_SAME(x - 1, y) && BD_SAME(x + 1, y) && BD_SAME(x, y - 1) && BD_SAME(x, y + 1))
                xrgb[(bd_y + y) * 320 + bd_x + x] = bd_rgb[y * bd_w + x];
#undef BD_SAME
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
    apply_backdrop(xrgb);
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
