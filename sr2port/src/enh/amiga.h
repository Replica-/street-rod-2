/* ENHANCED (--amiga): full-colour pictures and cars in place of the PC ones (enh/amiga.c). */
#pragma once
#include <stdbool.h>

#include "mem.h"
#include "platform/video.h"

/* Loads the picture set from gfx/ when enable; false (the game's own colours) if it is missing. */
bool amiga_init(bool enable);
/* pic_get: bitmap bmp now holds picture id. */
void amiga_note_pic(FarPtr bmp, s16 id);
/* around a blit (drv_blit / drv_blit_masked) of src onto dst: the full-colour pixels of what it draws */
void amiga_blit_begin(FarPtr src, FarPtr dst, const Rect *r, bool masked);
void amiga_blit_end(FarPtr src, FarPtr dst, const Rect *r);
/* around ega_sprite_over_bg's composite rows: sprite spr over the RAM background bg onto dst (c = the clipped byte
 * rect; the background byte of row j, column i is bgrel + j * bgstr + i from bg's planes) */
void amiga_sprite_begin(FarPtr spr, FarPtr dst, FarPtr bg, const Rect *c, u16 bgrel, u16 bgstr, s16 rows);
void amiga_sprite_end(void);
/* around ega_vram_cache_strips' blit of frame rows frow0.. into the VRAM strip at tp (stride bytes a row) */
void amiga_cache_begin(FarPtr frame, FarPtr tp, u16 stride, s16 rows, s16 frow0);
void amiga_cache_end(void);
/* car_compose: the body bitmap of model just made becomes the full-colour body with the customisation pieces of
 * flags (fitted into the PC frame, its 16-colour shape too); true = done, the PC roof / bumper pieces are not wanted. */
bool amiga_car_body(FarPtr frame, s16 model, s16 flags);
/* car_compose, --amiga: the black around a PC tyre picture goes see-through. */
void amiga_tyre_edge_clear(FarPtr pic);
/* car_compose: frame = base moved shift pixels right (the driving animation frames 1..3). */
void amiga_car_shifted(FarPtr frame, FarPtr base, int shift);
