/* ENHANCED (R1): the 3D race scene, drawn by r3d_present into the race-view viewport (depth test on). */
#ifndef R3D_SCENE_H
#define R3D_SCENE_H

#include "r3d/r3d.h"

bool r3d_scene_init(void);
void r3d_scene_shutdown(void);
/* v = the interpolated view; vw, vh = viewport size in pixels. */
void r3d_scene_draw(const R3dView *v, int vw, int vh, bool mirror);

#endif
