/* ENHANCED (R2.1): rain in the 3D race view (r3d/rain.c). */
#ifndef R3D_RAIN_H
#define R3D_RAIN_H

#include "r3d/r3d.h"

bool r3d_rain_init(void);
void r3d_rain_shutdown(void);
void r3d_rain_update(const float *eye);              /* once per displayed frame */
void r3d_rain_draw_world(const float *mvp);          /* streaks; depth test on, depth writes off */
void r3d_rain_draw_screen(float aspect);             /* windscreen drops in the viewport */

/* R2.2: puffs (spray, tyre smoke): world position, velocity (units / s), start size, grey level, opacity */
void r3d_puff_emit(const float *pos, const float *vel, float size, float grey, float alpha);
void r3d_puffs_update(void);                          /* once per displayed frame */
void r3d_puffs_draw(const float *mvp, float cam_yaw);
void r3d_fx_set_light(float k);                      /* R2.3: light on rain and smoke */

#endif
