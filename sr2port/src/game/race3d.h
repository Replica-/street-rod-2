/* ENHANCED (R1.2): road_frame's hooks for the 3D race view (game/race3d.c). */
#pragma once
#include <stdbool.h>

/* 3D race view for the current road mode (OpenGL up, no --classic, mode done). */
bool race3d_on(void);
/* Replaces road_frame's overlay step: draws the overlays twice to find the see-through pixels, publishes the page
 * mask and the view state. */
void race3d_overlays(void (*draw_overlays)(void));
