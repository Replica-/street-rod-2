#pragma once
/* ENHANCED (--amiga): a full-colour twin of the EGA planes, kept in step with every write to them.
 *
 * One entry per pixel of the 64 KB plane address space (byte offset * 8 + pixel, 0 = leftmost). An entry says what the
 * pixel looks like: SH_GAME = the game's own colour (the palette register of its plane index), or a full colour. The
 * game's drawing goes through vga.c, which reports each write here:
 *   - latch copies (write mode 1) and CPU copies (vmovedata) carry the entries of the source pixels along (page flips,
 *     mirror copies, saved backgrounds, the pointer's save-under);
 *   - fills, lines and text (set/reset, write mode 2) make the pixels SH_GAME;
 *   - other data writes make the pixels they change SH_GAME (a sprite's see-through pixels stay as they are);
 *   - while a drawing source is set (sh_begin, around a blit of a picture with a full-colour version, a car ...), the
 *     source decides for the pixels of its area.
 * RAM copies of the screen (a blit from a page into a RAM bitmap) keep their entries as blocks (sh_block_*), checked
 * against the RAM contents when used, so a background saved and drawn back keeps its colours. The scan-out (ega.c)
 * shows the entries. */
#include "types.h"

#define SH_GAME 0u                   /* the game's colour */
#define SH_KEEP 1u                   /* (a source's answer) the pixel stays as it is */
#define SH_NONE 2u                   /* (a source's answer) not the source's pixel: the usual rule */
/* see-through: the car strip cache's plain background (the game assumes a plain road); shown as the game's colour,
 * replaced by the real background where the strip lands (amiga_sprite_tails) */
#define SH_THROUGH 3u
#define SH_RGB 0xFF000000u           /* | 0xRRGGBB */
/* | the Amiga shading 0xRRGGBB: a car's paint, recoloured to palette register reg (hue, saturation) when shown */
#define SH_PAINT(reg) (0x80000000u | (u32)((reg) & 15) << 24)
#define SH_IS_PAINT(v) (((v) >> 28) == 8u)

enum { SH_W_LATCH, SH_W_FILL, SH_W_DATA };

typedef u32 (*ShSource)(void *user, int x, int y);    /* x, y: pixel of the source's area */

void sh_enable(bool on);
bool sh_enabled(void);
u32 *sh_pixels(void);                                  /* EGA_PLANE_SIZE * 8 entries */
void sh_clear(void);

void sh_latch(u16 off);                                /* vga_rd: the latches were loaded from off */
void sh_write(u16 off, u8 before, u8 after, u8 mask, int kind);   /* vga_wr, per plane */
void sh_copy(u16 soff, u16 doff);                      /* vmovedata: a CPU copy from one plane offset to another */

/* a drawing source for the area of `h` rows of `stride` bytes from plane offset `base` (w pixels wide) */
void sh_begin(u32 base, u32 stride, int w, int h, ShSource fn, void *user);
void sh_end(void);

/* the entry of pixel (x, y) of an area of rows of `stride` bytes from plane offset base */
u32 sh_at(u32 base, u32 stride, int x, int y);

/* RAM blocks: the entries of a RAM bitmap (planes at seg:off, stride, h rows) that holds a copy of the screen */
void sh_block_set(u16 seg, u16 off, u32 stride, int h, u32 *px);   /* takes px (stride * 8 * h entries) */
const u32 *sh_block_get(u16 seg, u16 off, u32 stride, int h);      /* NULL: none, or the RAM changed since */

/* the car paint of palette register reg shown in rgb (0xRRGGBB) instead of the register's colour; 0 = the register's */
void sh_set_paint(int reg, u32 rgb);

/* the colour of an entry (pal: the 16 live palette colours 0xAARRGGBB) */
u32 sh_colour(u32 v, const u32 *pal);
