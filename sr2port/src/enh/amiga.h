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
