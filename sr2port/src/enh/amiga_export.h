#pragma once
/* ENHANCED (amiga_export.c): makes the Amiga picture set (gfx/) from the Amiga disks and the PC libraries in game_dir;
 * needs SR.EXE loaded (the PC pictures' palette). false + err when it could not. */
#include <stdbool.h>
#include <stddef.h>

bool amiga_export_run(const char *game_dir, const char *out_dir, char *err, size_t errn);
