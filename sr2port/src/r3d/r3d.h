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
/* A sprite standing in the 3D world (R1.4): a picture uploaded with r3d_sprite_upload. */
typedef struct {
    bool vis;
    int slot;           /* texture slot (r3d_sprite_upload) */
    float pos;          /* track position in segments */
    float lat;          /* lateral position (game units) */
    float w, h;         /* world size (game units) */
    float lift;         /* base height above the road */
    bool along;         /* true: lies along the road (a side view), false: faces the camera */
} R3dSprite;

#define R3D_SPRITE_SLOTS 8
/* BGRA pixels (alpha 0 = transparent); re-uploaded only when they change. */
void r3d_sprite_upload(int slot, const uint32_t *bgra, int w, int h);

/* Scenery (R1.4): SR2_SHP shape types 0..22, each uploaded as a picture of its largest LOD with the pixel position
 * of the shape origin (ox, oy) and the world units per pixel; objects stand on the track (origin at the road
 * surface height, lateral x), mirrored when side <= 0. */
#define R3D_SHAPES 23
void r3d_shape_upload(int type, const uint32_t *bgra, int w, int h, int ox, int oy, float scale);
typedef struct { int16_t seg, x; uint8_t type; int8_t side; } R3dObject;
void r3d_objects_set(const R3dObject *objs, int n);

typedef struct {
    float pos;          /* track position in segments: seg + substep / 64 */
    float cam_x;        /* lateral camera position (0xA00 = start lane) */
    float cam_y;        /* camera height above the road (0x76C at start) */
    float heading;      /* car heading (track YAW units) */
    float height;       /* road height at the car (track HGT units) */
    float opp_pos;      /* opponent position in segments, < 0 = none */
    float opp_lat;      /* opponent lateral position */
    int mode;           /* road mode DS:ADD6 */
    R3dSprite opp;      /* the opponent's car (R1.4) */
} R3dView;

void r3d_view_push(const R3dView *v);

/* The track (race_render.md 1.4: word arrays of segment 3726), copied by r3d when it changes. */
#define R3D_TRACK_N 0x3DE
typedef struct {
    int16_t yaw[R3D_TRACK_N];   /* road heading */
    int16_t hgt[R3D_TRACK_N];   /* road height */
    uint16_t l[R3D_TRACK_N];    /* left side flags */
    uint16_t r[R3D_TRACK_N];    /* right side flags */
    uint16_t c[R3D_TRACK_N];    /* centre flags: low byte & 0xE0 = road width, & 8 = dash, & 0x10 = white */
    int16_t bl[R3D_TRACK_N];    /* left bank: > 0 hillside, < 0 drop-off (Mulholland) */
    int16_t br[R3D_TRACK_N];    /* right bank */
} R3dTrack;
void r3d_track_set(const R3dTrack *t);
/* True while the race code keeps pushing views (3D race view on screen). */
bool r3d_view_live(void);

#endif
