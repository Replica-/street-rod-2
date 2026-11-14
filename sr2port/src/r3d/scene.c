/* ENHANCED (R1.3): the 3D race scene, built from the track arrays (race_render.md 1.4) each frame.
 *
 * Scale, matched to the original's projection (road_proj_x/y, 07e4): screen x = 160 + 4 (x - cam_x) / z with z in
 * depth units of 1/8 segment. World units are the game's lateral units; one depth unit = DEPTH_UNIT of them, so the
 * focal length is F = 4 * DEPTH_UNIT frame pixels and a segment is 8 * DEPTH_UNIT long. A YAW unit is 1/F radian
 * (the original shifts a row by heading - YAW pixels, i.e. F tan(angle)); an HGT unit is HGT_UNIT world units (the
 * original's 2 * dHGT pixel shift at mid distance). Height y grows upwards here (the original's y grows down:
 * road 0x7D0, eye 0x76C). */
#include "r3d/scene.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "r3d/gl.h"

#define DEPTH_UNIT 64.0f
#define FOCAL (4.0f * DEPTH_UNIT)               /* frame pixels */
#define SEG_LEN (8.0f * DEPTH_UNIT)
#define YAW_RAD (1.0f / FOCAL)
#define HGT_UNIT 30.0f
#define X_MID 2400.0f                           /* lateral reference: the road centre 0x960 */
#define DRAW_SEGS 160
#define FAR_X 16000.0f

typedef struct { float x, y, z, r, g, b; } Vert;

static GLuint prog, vao, vbo;
static GLint loc_mvp, loc_eye, loc_fog;

static const char *vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "layout(location = 1) in vec3 col;\n"
    "uniform mat4 mvp;\n"
    "out vec3 c;\n"
    "out vec3 w;\n"
    "void main() { c = col; w = pos; gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char *fs =
    "#version 330 core\n"
    "in vec3 c;\n"
    "in vec3 w;\n"
    "uniform vec3 eye;\n"
    "uniform vec3 fog;\n"
    "out vec4 colour;\n"
    "void main() {\n"
    "    float d = length(w.xz - eye.xz);\n"
    "    float f = clamp((d - 20000.0) / 50000.0, 0.0, 1.0);\n"
    "    colour = vec4(mix(c, fog, f), 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------- track */

static R3dTrack trk;
static bool have_trk;
static float cx[R3D_TRACK_N + 1], cz[R3D_TRACK_N + 1], cy[R3D_TRACK_N + 1], th[R3D_TRACK_N + 1];

void r3d_track_set(const R3dTrack *t)
{
    if (have_trk && memcmp(t, &trk, sizeof trk) == 0) return;
    trk = *t;
    have_trk = true;
    /* centre line: point i starts segment i, which runs in direction YAW[i] */
    cx[0] = cz[0] = 0;
    for (int i = 0; i < R3D_TRACK_N; i++) {
        float a = trk.yaw[i] * YAW_RAD;
        cx[i + 1] = cx[i] + SEG_LEN * sinf(a);
        cz[i + 1] = cz[i] + SEG_LEN * cosf(a);
    }
    /* cross-section direction at point i: between the segments before and after it */
    for (int i = 0; i <= R3D_TRACK_N; i++) {
        int a = i > 0 ? i - 1 : 0, b = i < R3D_TRACK_N ? i : R3D_TRACK_N - 1;
        th[i] = 0.5f * (trk.yaw[a] + trk.yaw[b]) * YAW_RAD;
        cy[i] = trk.hgt[b] * HGT_UNIT;
    }
    if (SDL_getenv("SR_R3D_TRACE")) {
        int ylo = 0, yhi = 0, hlo = 0, hhi = 0, blo = 0, bhi = 0;
        for (int i = 0; i < R3D_TRACK_N; i++) {
            if (trk.yaw[i] < ylo) ylo = trk.yaw[i];
            if (trk.yaw[i] > yhi) yhi = trk.yaw[i];
            if (trk.hgt[i] < hlo) hlo = trk.hgt[i];
            if (trk.hgt[i] > hhi) hhi = trk.hgt[i];
            int b = trk.bl[i] < trk.br[i] ? trk.bl[i] : trk.br[i], c = trk.bl[i] > trk.br[i] ? trk.bl[i] : trk.br[i];
            if (b < blo) blo = b;
            if (c > bhi) bhi = c;
        }
        fprintf(stderr, "r3d track: yaw %d..%d hgt %d..%d bank %d..%d end (%.0f, %.0f)\n", ylo, yhi, hlo, hhi, blo, bhi,
                (double)cx[R3D_TRACK_N], (double)cz[R3D_TRACK_N]);
    }
}

/* world point of lateral position X at fractional track position p (segments), at height dy above the road */
static void track_point(float p, float X, float dy, float *x, float *y, float *z)
{
    if (p < 0) p = 0;
    if (p > R3D_TRACK_N - 0.001f) p = R3D_TRACK_N - 0.001f;
    int i = (int)p;
    float f = p - (float)i;
    float a = th[i] + (th[i + 1] - th[i]) * f;
    float ox = cx[i] + (cx[i + 1] - cx[i]) * f, oz = cz[i] + (cz[i + 1] - cz[i]) * f;
    float o = X - X_MID;
    *x = ox + o * cosf(a);
    *z = oz - o * sinf(a);
    *y = cy[i] + (cy[i + 1] - cy[i]) * f + dy;
}

/* road edges of segment i (07e4: centre flags low byte & 0xE0) */
static void edges(int i, float *xl, float *xr)
{
    unsigned cm = trk.c[i] & 0xE0;
    if (cm == 0) { *xl = 0x5DC; *xr = 0xCE4; }
    else if (cm & 0x80) { *xl = 0x7D0; *xr = 0xAF0; }
    else if (cm & 0x20) { *xl = *xr = 0xCE4; }
    else { *xl = *xr = 0x5DC; }
}

/* ---------------------------------------------------------------- GL */

bool r3d_scene_init(void)
{
    prog = r3d_program(vs, fs);
    if (!prog) return false;
    loc_mvp = gl.GetUniformLocation(prog, "mvp");
    loc_eye = gl.GetUniformLocation(prog, "eye");
    loc_fog = gl.GetUniformLocation(prog, "fog");
    gl.GenVertexArrays(1, &vao);
    gl.GenBuffers(1, &vbo);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)0);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vert), (void *)offsetof(Vert, r));
    gl.EnableVertexAttribArray(1);
    gl.BindVertexArray(0);
    return true;
}

void r3d_scene_shutdown(void)
{
    if (vbo) gl.DeleteBuffers(1, &vbo);
    if (vao) gl.DeleteVertexArrays(1, &vao);
    if (prog) gl.DeleteProgram(prog);
    vbo = vao = prog = 0;
}

/* ---------------------------------------------------------------- matrices (column-major) */

static void mat_mul(float *o, const float *a, const float *b)
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
                           a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
    for (int i = 0; i < 16; i++) o[i] = t[i];
}

/* the original's projection: frame pixels = centre + FOCAL * x / z, the view strip is 320 x 84 frame pixels */
static void mat_projection(float *m, float zn, float zf)
{
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = FOCAL / 160.0f;
    m[5] = FOCAL / 42.0f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
}

/* camera at (x, y, z) looking along yaw (radians, 0 = +z, positive = towards +x) */
static void mat_view(float *m, float x, float y, float z, float yaw)
{
    float c = cosf(yaw), s = sinf(yaw);
    /* rows: right = (c, 0, -s), up = (0, 1, 0), -forward = -(s, 0, c) (the view looks down -z) */
    float r[16] = { c, 0, -s, 0,   0, 1, 0, 0,   -s, 0, -c, 0,   0, 0, 0, 1 };
    float t[16] = { 1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   -x, -y, -z, 1 };
    mat_mul(m, r, t);
}

/* ---------------------------------------------------------------- mesh */

#define MAX_VERTS 60000
static Vert verts[MAX_VERTS];
static int nv;

/* a strip piece between track positions p0 and p1, lateral X0..X1, heights (above the road) h00 h01 at p0 and
 * h10 h11 at p1 */
static void band(float p0, float p1, float X0, float X1, float h00, float h01, float h10, float h11,
                 float r, float g, float b)
{
    if (nv + 6 > MAX_VERTS) return;
    Vert v[4];
    track_point(p0, X0, h00, &v[0].x, &v[0].y, &v[0].z);
    track_point(p0, X1, h01, &v[1].x, &v[1].y, &v[1].z);
    track_point(p1, X1, h11, &v[2].x, &v[2].y, &v[2].z);
    track_point(p1, X0, h10, &v[3].x, &v[3].y, &v[3].z);
    for (int k = 0; k < 4; k++) { v[k].r = r; v[k].g = g; v[k].b = b; }
    verts[nv++] = v[0]; verts[nv++] = v[1]; verts[nv++] = v[2];
    verts[nv++] = v[0]; verts[nv++] = v[2]; verts[nv++] = v[3];
}

static void build(int first, int last, int mode)
{
    nv = 0;
    float w = mode == 0x10 ? 200.0f : 400.0f;                 /* shoulder width (07e4) */
    for (int i = first; i < last; i++) {
        float p0 = (float)i, p1 = (float)(i + 1);
        bool alt = (i / 2) & 1;                                 /* light / dark bands, two segments each */
        float xl0, xr0, xl1, xr1;
        edges(i, &xl0, &xr0);
        edges(i + 1 < R3D_TRACK_N ? i + 1 : i, &xl1, &xr1);
        float xl = fminf(xl0, xl1), xr = fmaxf(xr0, xr1);
        /* road, shoulders */
        if (xr > xl) band(p0, p1, xl, xr, 0, 0, 0, 0, alt ? 0.34f : 0.31f, alt ? 0.34f : 0.31f, alt ? 0.36f : 0.33f);
        float sl = 0x5DC - w, sr = 0xCE4 + w;
        if (xl > sl) band(p0, p1, sl, xl, 0, 0, 0, 0, alt ? 0.62f : 0.57f, alt ? 0.52f : 0.48f, 0.34f);
        if (sr > xr) band(p0, p1, xr, sr, 0, 0, 0, 0, alt ? 0.62f : 0.57f, alt ? 0.52f : 0.48f, 0.34f);
        /* centre dash (C & 8) */
        if (trk.c[i] & 8) band(p0, p1 - 0.5f, X_MID - 20, X_MID + 20, 3, 3, 3, 3, 0.95f, 0.95f, 0.92f);
        /* banks (Mulholland): hillside (> 0) or drop-off (< 0) beyond the shoulder, then flat ground */
        int i1 = i + 1 < R3D_TRACK_N ? i + 1 : i;
        float bl0 = trk.bl[i], bl1 = trk.bl[i1], br0 = trk.br[i], br1 = trk.br[i1];
        float gr = alt ? 0.24f : 0.21f, gg = alt ? 0.47f : 0.43f, gb = alt ? 0.19f : 0.17f;
        float fl = -(w - 0x177) * 4, frr = (w + 0x339) * 4;     /* far bank x (07e4 XFL / XFR) */
        if (bl0 != 0 || bl1 != 0) {
            bool hill = bl0 > 0 || bl1 > 0;
            band(p0, p1, fl, sl, bl0, 0, bl1, 0, hill ? 0.72f : 0.45f, hill ? 0.62f : 0.38f, hill ? 0.30f : 0.28f);
            band(p0, p1, X_MID - FAR_X, fl, bl0, bl0, bl1, bl1, gr, gg, gb);
        } else band(p0, p1, X_MID - FAR_X, sl, 0, 0, 0, 0, gr, gg, gb);
        if (br0 != 0 || br1 != 0) {
            bool hill = br0 > 0 || br1 > 0;
            band(p0, p1, sr, frr, 0, br0, 0, br1, hill ? 0.72f : 0.45f, hill ? 0.62f : 0.38f, hill ? 0.30f : 0.28f);
            band(p0, p1, frr, X_MID + FAR_X, br0, br0, br1, br1, gr, gg, gb);
        } else band(p0, p1, sr, X_MID + FAR_X, 0, 0, 0, 0, gr, gg, gb);
    }
}

void r3d_scene_draw(const R3dView *v, int vw, int vh)
{
    (void)vw; (void)vh;
    const float sky[3] = { 0.36f, 0.56f, 0.86f }, haze[3] = { 0.70f, 0.80f, 0.92f };
    gl.ClearColor(sky[0], sky[1], sky[2], 1);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!have_trk) return;

    int seg = (int)floorf(v->pos);
    int first = seg - 3 < 0 ? 0 : seg - 3, last = seg + DRAW_SEGS > R3D_TRACK_N ? R3D_TRACK_N : seg + DRAW_SEGS;
    build(first, last, v->mode);

    /* eye: the original's camera is 0x7D0 - cam_y above the road, offset by the car's own height deviation */
    float ex, ey, ez;
    int si = seg < 0 ? 0 : (seg >= R3D_TRACK_N ? R3D_TRACK_N - 1 : seg);
    float above = (0x7D0 - v->cam_y) + (v->height - trk.hgt[si]) * HGT_UNIT;
    track_point(v->pos, v->cam_x, above, &ex, &ey, &ez);
    float proj[16], view[16], mvp[16];
    mat_projection(proj, 50.0f, 200000.0f);
    mat_view(view, ex, ey, ez, v->heading * YAW_RAD);
    mat_mul(mvp, proj, view);

    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(loc_eye, ex, ey, ez);
    gl.Uniform3f(loc_fog, haze[0], haze[1], haze[2]);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof(Vert)), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, nv);
    gl.BindVertexArray(0);
}
