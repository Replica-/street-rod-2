/* ENHANCED (--amiga): full-colour pictures and cars in place of the PC ones (enh/amiga.c). */
#pragma once
#include <stdbool.h>

#include "mem.h"
#include "platform/video.h"

/* Loads the picture set from gfx/ when enable; false (the game's own colours) if it is missing. */
bool amiga_init(bool enable);
/* true while the full-colour picture set is in use */
bool amiga_active(void);
/* pic_get: bitmap bmp now holds picture id. */
void amiga_note_pic(FarPtr bmp, s16 id);
/* while pc: the pictures loaded are shown as the game's own (the PC title between the Amiga ones) */
void amiga_pc_pictures(bool pc);
/* around a blit (drv_blit / drv_blit_masked) of src onto dst: the full-colour pixels of what it draws */
void amiga_blit_begin(FarPtr src, FarPtr dst, const Rect *r, bool masked);
void amiga_blit_end(FarPtr src, FarPtr dst, const Rect *r);
/* around ega_sprite_over_bg's composite rows: sprite spr over the RAM background bg onto dst (c = the clipped byte
 * rect; the background byte of row j, column i is bgrel + j * bgstr + i from bg's planes) */
void amiga_sprite_begin(FarPtr spr, FarPtr dst, FarPtr bg, const Rect *c, u16 bgrel, u16 bgstr, s16 rows);
void amiga_sprite_end(void);
/* after the strip rows (rows below the composite rows, copied from the car strip cache): its see-through pixels get
 * the background behind the car */
void amiga_sprite_tails(FarPtr dst, s16 rows);
/* around ega_vram_cache_strips' blit of frame rows frow0.. into the VRAM strip at tp (stride bytes a row) */
void amiga_cache_begin(FarPtr frame, FarPtr tp, u16 stride, s16 rows, s16 frow0, bool plain);
void amiga_cache_end(void);
/* car_compose: the body bitmap of model just made (frame 0) is to become the whole Amiga car; true = it will, the PC
 * pieces, driver and tyres are not wanted. Then amiga_car_frames fills the n frames (frame f = the car 2 * f pixels
 * right, wheels of tyre type tyre 0..2 in turning frame f, -1 = none; pc_x0 / pc_x1 = the PC tyres' x, for bodies
 * whose arches are not known) in the game's 16 colours (shape, mask) and in full colour. */
bool amiga_car_begin(FarPtr frame, s16 model, s16 flags);
void amiga_car_frames(FarPtr *frames, s16 n, s16 tyre, s16 pc_x0, s16 pc_x1);
/* paint job, --amiga: a swatch of AMIGA_SWATCH_N colours at (x, y) of the screen (both pages; sel framed), hit test
 * (index or -1), its colours; the colour chosen for a car record (with the game's colour index then), applied to the
 * player's car paint (registers 6 / 7) when that car is set up, or a preview */
#define AMIGA_SWATCH_N 20
void amiga_swatch_show(int x, int y, int sel);
void amiga_swatch_select(int sel);
void amiga_swatch_hide(void);
int  amiga_swatch_hit(int x, int y);
u32  amiga_swatch_rgb(int i);
void amiga_paint_set(u16 car, int index, u32 rgb);
void amiga_paint_apply(u16 car, int index);
void amiga_paint_preview(u32 rgb, int index);
/* the race's steering wheel picture w (0 straight, 1 turned left, 2 turned right; -1 = the race is over) and wheel
 * angle (-24..24): the driver's hands and the wheel's knob (gliding to the angle), a layer in front of the dashboard
 * (the split screen's rows below the race view); the layer (320 x 90, 0xAARRGGBB, alpha 0 = none) or NULL, made for
 * now; true while the knob is still moving (the screen is to be shown again) */
void amiga_hands(int w, int angle);
const u32 *amiga_hands_layer(void);
bool amiga_hands_moving(void);
