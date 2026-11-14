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

/* --rain (R2.1): rain in the 3D race view (falling streaks, drops on the windscreen, grey sky, wet road). */
void r3d_set_rain(bool on);
/* --dusk / --night (R2.3): 0 day, 1 dusk, 2 night with headlights. */
void r3d_set_time_of_day(int t);

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
#define R3D_SHAPES 28        /* 0..22 SR2_SHP sets, 23..27 sign pictures (never mirrored) */
void r3d_shape_upload(int type, const uint32_t *bgra, int w, int h, int ox, int oy, float scale);
typedef struct { int16_t seg, x, lift; uint8_t type; int8_t side; } R3dObject;   /* lift = origin height */
void r3d_objects_set(const R3dObject *objs, int n);

/* The original's sky / horizon panorama (road_draw_sky 3e66, 3cca), BGRA, w = 320 pixels per 320 / FOCAL radians
 * of view, horizon at row horizon_row; drawn as a cylinder around the camera. h = 0: no panorama. */
void r3d_sky_upload(const uint32_t *bgra, int w, int h, int horizon_row);

typedef struct {
    float pos;          /* track position in segments: seg + substep / 64 */
    float cam_x;        /* lateral camera position (0xA00 = start lane) */
    float cam_y;        /* camera height above the road (0x76C at start) */
    float heading;      /* car heading (track YAW units) */
    float height;       /* the car's height above its segment's road height (track HGT units; 0 on the road) */
    float opp_pos;      /* opponent position in segments, < 0 = none */
    float opp_lat;      /* opponent lateral position */
    int mode;           /* road mode DS:ADD6 */
    R3dSprite opp;      /* the opponent's car (R1.4) */
    R3dSprite tree;     /* the drag strip's start lights (R1.6) */
    /* Aqueduct (R1.7, aq_project_front 18ec): floor edges, wall-top edges (lateral), wall-top and floor height
     * above the base road level, and the picture's roll (radians) */
    float aq_fl, aq_fr, aq_tl, aq_tr, aq_th, aq_floor, roll;
    bool squeal;        /* R2.2: the tyres squeal (DS:72E0): tyre smoke */
    uint32_t pal[16];   /* the live palette registers as 0xRRGGBB (the original's colours by index) */
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
    int16_t wall_phase;         /* drag strip (R1.6): barrier walls, colour blocks of 5 from this segment; -1 = none */
    int16_t aqueduct;           /* R1.7: a straight concrete channel; c[] = the Aqueduct flags (3726:4E62) */
} R3dTrack;
void r3d_track_set(const R3dTrack *t);
/* True while the race code keeps pushing views (3D race view on screen). */
bool r3d_view_live(void);
/* ENHANCED: the retrace period (ns) locked to the display: n refreshes (swap interval n) with n refreshes closest to
 * the VGA's 70 Hz (60 Hz display: 1, 120 Hz: 2 = 60 Hz, 144 Hz: 2 = 72 Hz); 0 when the picture is not shown with vsync
 * (hidden window, --classic, unknown rate). r3d_vsync_lock(true) sets that swap interval, false the normal 1. */
Uint64 r3d_vsync_period_ns(void);
void r3d_vsync_lock(bool on);

#endif
