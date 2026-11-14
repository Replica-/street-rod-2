/* ENHANCED (--amiga): Amiga pictures in place of the PC ones (enh/amiga.c). */
#pragma once
#include <stdbool.h>

#include "mem.h"
#include "platform/video.h"

/* Loads the Amiga pictures from Game/amiga when enable; false (PC pictures stay) if they are missing. */
bool amiga_init(bool enable);
/* pic_get: bitmap bmp now holds picture id. */
void amiga_note_pic(FarPtr bmp, s16 id);
/* A blit (drv_blit / drv_blit_masked) of src onto dst: a screen page gets the Amiga version of a mapped picture. */
void amiga_on_blit(FarPtr src, FarPtr dst, const Rect *r, bool masked);
/* car_compose: the body bitmap of model just made becomes the Amiga body with the customisation pieces of flags
 * (fitted, 16 colours; full colour on screen); true = done, the PC roof / bumper pieces are not wanted. */
bool amiga_car_body(FarPtr frame, s16 model, s16 flags);
/* car_draw: a parked car (rows frow0.. of frame) put on page at x, y by a VRAM copy instead of a blit. */
void amiga_car_shown(FarPtr frame, FarPtr page, int x, int y, int frow0, int rows);
/* car_compose: frame = base moved shift pixels right (the driving animation frames 1..3). */
void amiga_car_shifted(FarPtr frame, FarPtr base, int shift);
/* the animation step drew sprite (rect r) on page. */
void amiga_on_sprite(FarPtr sprite, FarPtr page, const Rect *r);
