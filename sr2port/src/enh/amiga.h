/* ENHANCED (--amiga): Amiga pictures in place of the PC ones (enh/amiga.c). */
#pragma once
#include <stdbool.h>

#include "mem.h"

/* Loads the Amiga pictures from Game/amiga when enable; false (PC pictures stay) if they are missing. */
bool amiga_init(bool enable);
/* The garage hands over the backdrop bitmap it composed (LIB2 #18 + overlays), before putting it on screen. */
void amiga_garage_backdrop(FarPtr bmp);
