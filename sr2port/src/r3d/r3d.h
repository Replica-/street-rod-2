/* ENHANCED (R1): the OpenGL 3.3 presenter. The game's 320x200 (or text-mode) frame is drawn as a texture in a 4:3
 * letterbox; the 3D race view (R1.3 on) is drawn underneath it into the race-view rectangle.
 * Without an OpenGL context (SDL_VIDEO_DRIVER=dummy, headless smoke tests) every call is a no-op. */
#ifndef R3D_H
#define R3D_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Window flags r3d needs; call before creating the window. */
SDL_WindowFlags r3d_window_flags(void);
/* Creates the context on the window; false = no OpenGL (presentation is skipped, the game still runs). */
bool r3d_init(SDL_Window *window);
void r3d_shutdown(void);
bool r3d_active(void);

/* --classic: the faithful 2D race view only (no 3D scene). */
void r3d_set_classic(bool on);
bool r3d_classic(void);

/* The letterboxed picture rectangle inside a w x h area (4:3). */
void r3d_letterbox(float w, float h, float *x, float *y, float *rw, float *rh);

/* Draws the frame (XRGB8888, fw x fh) and presents (vsync). */
void r3d_present(const uint32_t *frame, int fw, int fh);

#endif
