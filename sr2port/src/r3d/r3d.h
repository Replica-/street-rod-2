/* ENHANCED (R1): the OpenGL 3.3 presenter. The game's 320x200 (or text-mode) frame is drawn as a texture in a 4:3
 * letterbox; during races the 3D view is drawn first into the race-view rectangle and the frame goes on top, with
 * the pixels the race code marked see-through (alpha 0) letting it show.
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

/* Draws the frame (XRGB8888, fw x fh; alpha byte = 0 see-through while a race view is live) and presents. */
void r3d_present(const uint32_t *frame, int fw, int fh);
/* Developer aid: the last presented picture (3D + frame) as a BMP. */
bool r3d_save_bmp(const char *path);

/* The race view state at one original frame (road_frame), in game units. r3d keeps the last two and draws the
 * blend for the display time, one original frame behind. */
typedef struct {
    float pos;          /* track position in segments: seg + substep / 64 */
    float cam_x;        /* lateral camera position (0xA00 = start lane) */
    float cam_y;        /* camera height above the road (0x76C at start) */
    float heading;      /* car heading (track YAW units) */
    float height;       /* road height at the car (track HGT units) */
    float opp_pos;      /* opponent position in segments, < 0 = none */
    float opp_lat;      /* opponent lateral position */
    int mode;           /* road mode DS:ADD6 */
} R3dView;

void r3d_view_push(const R3dView *v);
/* True while the race code keeps pushing views (3D race view on screen). */
bool r3d_view_live(void);

#endif
