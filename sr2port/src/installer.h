#pragma once
/* PORT (installer.c): downloads and unpacks the game files from Street Rod Online when the game folder lacks them
 * (with want_amiga and no gfx/ folder, the Amiga disks too). Returns false when setup failed or was cancelled. */
#include <stdbool.h>

bool installer_run(const char *game_dir, bool want_amiga);
