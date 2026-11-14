#pragma once
/* EGA/VGA 16-colour display memory and scan-out (BIOS mode 0Dh, 320x200, as driver 21a0 programs it
 * with DS:0254 = 1). video.md §1.1, §4.3, §4.10.
 *
 * Video memory is four 64 KB planes (not in mem[], see mem.h). Offset o of segments A000h-AFFFh
 * addresses byte o of every plane; a byte holds 8 pixels, MSB leftmost; plane k supplies bit k of the
 * colour. The screen is 40 bytes per line starting at the CRTC start address; scan lines past the CRTC
 * line compare restart at offset 0 (the dashboard split screen, 21a0:0014). Colours go through the 16
 * attribute-controller palette registers, which the game sets with INT 10h AX=1002h/1000h (pal_set
 * 0f38:1fa4, pal_reg 0f38:1f4b); the DAC is never programmed.
 *
 * This module owns the plane memory, the palette registers and the scan-out. The CPU-side access
 * through the sequencer / graphics controller (map mask, set/reset, write modes, latches ...) is the
 * register model in platform/vga.h; game and video code never write the planes directly. */
#include "types.h"

#define EGA_PLANE_SIZE 0x10000u
#define EGA_BYTES_PER_LINE 40

void ega_init(void);                         /* installs the frame source, clears the planes */
void ega_show(void);                         /* makes the planes the displayed frame source (after text mode) */
u8  *ega_plane(int plane);                   /* plane 0..3, EGA_PLANE_SIZE bytes (vga.c, compose) */

void ega_set_start(u16 offset);              /* CRTC start address (bytes) */
void ega_set_line_compare(int line);         /* first screen row (0..199) shown from offset 0; >= 200 = off */
void ega_touch(void);                        /* marks the screen changed (after writes to the planes) */
void ega_set_page_mask(int page, const u8 *mask);   /* ENHANCED: 320x200 see-through mask of a page, NULL = none */
/* ENHANCED (--amiga, see ega.c): full-colour pictures where the screen still shows the game's picture. Regions at
 * column x, row y of page `page` (plane offset / 2000h); add takes ownership of ref / rgb (malloc'd, ref 0xFF = not
 * part of the picture). */
#define BD_STRICT 1                 /* a pixel counts only if its 4 neighbours still match too */
#define BD_CAR 2                    /* a car: replaces the earlier car region of the page */
#define BD_COPY 4                   /* carried along by a page-to-page copy: replaces nothing */
void ega_add_backdrop(u8 *ref, u32 *rgb, int page, int x, int y, int w, int h, int flags);
void ega_copy_backdrops(int spage, int sx, int sy, int w, int h, int dpage, int dx, int dy);
void ega_drop_backdrops(int page, int x, int y, int w, int h);
/* PC drawing (a fill, a picture without a replacement) over (x, y, w, h): the regions inside go, the others count the
 * pixels drawn over and go when 70 % of them are (a page built from several pieces) */
void ega_cover_backdrops(int page, int x, int y, int w, int h);
/* ENHANCED: where the mouse pointer is drawn (w = 0: none); its pixels do not decide whether a backdrop is on screen */
void ega_pointer_rect(int x, int y, int w, int h);

/* Palette register 0..15 = value as the attribute controller holds it: bits 0-2 = BGR, bit 4 =
 * intensity (the 200-line interpretation of mode 0Dh; the game ORs 10h into values 8..15), register 16 =
 * overscan (stored, not shown: the port window has no border). */
void ega_set_palette_reg(int reg, u8 value);
u8   ega_get_palette_reg(int reg);

/* The 16 IRGB colours as XRGB8888. */
extern const u32 ega_palette[16];
