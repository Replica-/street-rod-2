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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "r3d/gl.h"
#include "r3d/rain.h"

#define DEPTH_UNIT 64.0f
#define FOCAL (4.0f * DEPTH_UNIT)               /* frame pixels */
#define SEG_LEN (8.0f * DEPTH_UNIT)
#define YAW_RAD (1.0f / FOCAL)
#define HGT_UNIT 30.0f
#define X_MID 2400.0f                           /* lateral reference: the road centre 0x960 */
#define HOUSE_DEPTH 500.0f                      /* ENHANCED: how far the town houses reach back (the 2D view shows fronts only) */
#define DRAW_SEGS 160
#define FAR_X 16000.0f

typedef struct { float x, y, z, r, g, b; } Vert;

static GLuint prog, vao, vbo;
static GLint loc_mvp, loc_eye, loc_fog, loc_fogr, loc_lite, loc_fwd;

/* R2.3 time of day: 0 day, 1 dusk, 2 night (headlights); light = ambient, headlight strength; fwd = view dir */
static int tod;
static float lite[4] = { 1, 0, 0, 0 }, fwd[3] = { 0, 1, 0 };

void r3d_set_time_of_day(int t) { tod = t; }

/* R2.1 weather: fog start, fog span, colour factor (wet / dim) */
static bool rain;
static float fog_range[3] = { 20000.0f, 50000.0f, 1.0f };

void r3d_set_rain(bool on) { rain = on; }

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
    "uniform vec3 fogr;\n"
    "uniform vec4 lite;\n"
    "uniform vec3 fwd;\n"
    "float light(vec3 p) {\n"
    "    vec2 r = p.xz - eye.xz;\n"
    "    float a = dot(r, fwd.xy), s = abs(dot(r, vec2(fwd.y, -fwd.x)));\n"
    "    float cone = clamp(1.0 - s / (0.42 * a + 500.0), 0.0, 1.0) * step(0.0, a);\n"
    "    float fall = clamp(1.0 - a / 14000.0, 0.0, 1.0);\n"
    "    return lite.x + lite.y * cone * fall * fall;\n"
    "}\n"
    "out vec4 colour;\n"
    "void main() {\n"
    "    float d = length(w.xz - eye.xz);\n"
    "    float f = clamp((d - fogr.x) / fogr.y, 0.0, 1.0);\n"
    "    colour = vec4(mix(c * fogr.z * light(w), fog, f), 1.0);\n"
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
        float a = -trk.yaw[i] * YAW_RAD;               /* a larger YAW bends the road left */
        cx[i + 1] = cx[i] + SEG_LEN * sinf(a);
        cz[i + 1] = cz[i] + SEG_LEN * cosf(a);
    }
    /* cross-section direction at point i: between the segments before and after it */
    for (int i = 0; i <= R3D_TRACK_N; i++) {
        int a = i > 0 ? i - 1 : 0, b = i < R3D_TRACK_N ? i : R3D_TRACK_N - 1;
        th[i] = -0.5f * (trk.yaw[a] + trk.yaw[b]) * YAW_RAD;
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

/* ---------------------------------------------------------------- sprites (R1.4) */

typedef struct { float x, y, z, u, v; } SVert;

static GLuint sprog, svao, svbo, stex[R3D_SPRITE_SLOTS];
static GLint sloc_mvp, sloc_eye, sloc_fog, sloc_fogr, sloc_tex, sloc_lite, sloc_fwd;
static uint32_t shash[R3D_SPRITE_SLOTS];

static const char *svs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "layout(location = 1) in vec2 uv;\n"
    "uniform mat4 mvp;\n"
    "out vec2 t;\n"
    "out vec3 w;\n"
    "void main() { t = uv; w = pos; gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char *sfs =
    "#version 330 core\n"
    "in vec2 t;\n"
    "in vec3 w;\n"
    "uniform sampler2D tex;\n"
    "uniform vec3 eye;\n"
    "uniform vec3 fog;\n"
    "uniform vec3 fogr;\n"
    "uniform vec4 lite;\n"
    "uniform vec3 fwd;\n"
    "float light(vec3 p) {\n"
    "    vec2 r = p.xz - eye.xz;\n"
    "    float a = dot(r, fwd.xy), s = abs(dot(r, vec2(fwd.y, -fwd.x)));\n"
    "    float cone = clamp(1.0 - s / (0.42 * a + 500.0), 0.0, 1.0) * step(0.0, a);\n"
    "    float fall = clamp(1.0 - a / 14000.0, 0.0, 1.0);\n"
    "    return lite.x + lite.y * cone * fall * fall;\n"
    "}\n"
    "out vec4 colour;\n"
    "void main() {\n"
    "    vec4 c = texture(tex, t);\n"
    "    if (c.a < 0.5) discard;\n"
    "    float d = length(w.xz - eye.xz);\n"
    "    float f = clamp((d - fogr.x) / fogr.y, 0.0, 1.0);\n"
    "    colour = vec4(mix(c.rgb * fogr.z * light(w), fog, f), 1.0);\n"
    "}\n";

void r3d_sprite_upload(int slot, const uint32_t *bgra, int w, int h)
{
    if (slot < 0 || slot >= R3D_SPRITE_SLOTS || !stex[slot] || w <= 0 || h <= 0) return;
    uint32_t hsh = 2166136261u ^ (uint32_t)(w * 7919 + h);
    for (int i = 0; i < w * h; i++) hsh = (hsh ^ bgra[i]) * 16777619u;
    if (hsh == shash[slot]) return;
    shash[slot] = hsh;
    gl.BindTexture(GL_TEXTURE_2D, stex[slot]);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
}

static bool sprites_init(void)
{
    sprog = r3d_program(svs, sfs);
    if (!sprog) return false;
    sloc_mvp = gl.GetUniformLocation(sprog, "mvp");
    sloc_eye = gl.GetUniformLocation(sprog, "eye");
    sloc_fog = gl.GetUniformLocation(sprog, "fog");
    sloc_tex = gl.GetUniformLocation(sprog, "tex");
    sloc_fogr = gl.GetUniformLocation(sprog, "fogr");
    sloc_lite = gl.GetUniformLocation(sprog, "lite");
    sloc_fwd = gl.GetUniformLocation(sprog, "fwd");
    gl.GenTextures(R3D_SPRITE_SLOTS, stex);
    for (int i = 0; i < R3D_SPRITE_SLOTS; i++) {
        gl.BindTexture(GL_TEXTURE_2D, stex[i]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    gl.GenVertexArrays(1, &svao);
    gl.GenBuffers(1, &svbo);
    gl.BindVertexArray(svao);
    gl.BindBuffer(GL_ARRAY_BUFFER, svbo);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SVert), (void *)0);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(SVert), (void *)offsetof(SVert, u));
    gl.EnableVertexAttribArray(1);
    gl.BindVertexArray(0);
    return true;
}

static void sprites_shutdown(void)
{
    if (stex[0]) gl.DeleteTextures(R3D_SPRITE_SLOTS, stex);
    if (svbo) gl.DeleteBuffers(1, &svbo);
    if (svao) gl.DeleteVertexArrays(1, &svao);
    if (sprog) gl.DeleteProgram(sprog);
    memset(stex, 0, sizeof stex);
    memset(shash, 0, sizeof shash);
    svbo = svao = sprog = 0;
}

/* one sprite: base centre on the track at (pos, lat), facing the camera (yaw) or lying along the road */
static void sprite_draw(const R3dSprite *s, float cam_yaw, const float *mvp, const float *eye, const float *fog)
{
    if (!s->vis || s->slot < 0 || s->slot >= R3D_SPRITE_SLOTS) return;
    float bx, by, bz;
    track_point(s->pos, s->lat, s->lift, &bx, &by, &bz);
    float rx, rz;
    if (s->along) {                                      /* the track's forward direction at pos */
        float ax, ay, az;
        track_point(s->pos + 0.1f, s->lat, s->lift, &ax, &ay, &az);
        float l = sqrtf((ax - bx) * (ax - bx) + (az - bz) * (az - bz));
        rx = l > 0 ? (ax - bx) / l : 0;
        rz = l > 0 ? (az - bz) / l : 1;
    } else { rx = cosf(cam_yaw); rz = -sinf(cam_yaw); }  /* the camera's right */
    float hw = s->w * 0.5f;
    SVert q[6] = {
        { bx - rx * hw, by,        bz - rz * hw, 0, 1 }, { bx + rx * hw, by,        bz + rz * hw, 1, 1 },
        { bx + rx * hw, by + s->h, bz + rz * hw, 1, 0 }, { bx - rx * hw, by,        bz - rz * hw, 0, 1 },
        { bx + rx * hw, by + s->h, bz + rz * hw, 1, 0 }, { bx - rx * hw, by + s->h, bz - rz * hw, 0, 0 },
    };
    gl.UseProgram(sprog);
    gl.UniformMatrix4fv(sloc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(sloc_eye, eye[0], eye[1], eye[2]);
    gl.Uniform3f(sloc_fog, fog[0], fog[1], fog[2]);
    gl.Uniform3f(sloc_fogr, fog_range[0], fog_range[1], fog_range[2]);
    gl.Uniform4f(sloc_lite, lite[0], lite[1], lite[2], lite[3]);
    gl.Uniform3f(sloc_fwd, fwd[0], fwd[1], fwd[2]);
    gl.Uniform1i(sloc_tex, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, stex[s->slot]);
    gl.BindVertexArray(svao);
    gl.BindBuffer(GL_ARRAY_BUFFER, svbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
    gl.BindVertexArray(0);
}

/* ---------------------------------------------------------------- scenery */

static struct { GLuint tex; int w, h, ox, oy; float sc; uint32_t hash; } shp[R3D_SHAPES];
#define MAX_OBJECTS 12000
static R3dObject objects[MAX_OBJECTS];
static int nobjects;
#define MAX_SVERTS 36000
static SVert sverts[MAX_SVERTS];

void r3d_shape_upload(int type, const uint32_t *bgra, int w, int h, int ox, int oy, float scale)
{
    if (type < 0 || type >= R3D_SHAPES || w <= 0 || h <= 0 || !sprog) return;
    uint32_t hsh = 2166136261u ^ (uint32_t)(w * 7919 + h * 31 + ox * 131 + oy);
    for (int i = 0; i < w * h; i++) hsh = (hsh ^ bgra[i]) * 16777619u;
    shp[type].w = w; shp[type].h = h; shp[type].ox = ox; shp[type].oy = oy; shp[type].sc = scale;
    if (shp[type].tex && hsh == shp[type].hash) return;
    shp[type].hash = hsh;
    if (!shp[type].tex) {
        gl.GenTextures(1, &shp[type].tex);
        gl.BindTexture(GL_TEXTURE_2D, shp[type].tex);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    gl.BindTexture(GL_TEXTURE_2D, shp[type].tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
}

void r3d_objects_set(const R3dObject *objs, int n)
{
    if (n > MAX_OBJECTS) n = MAX_OBJECTS;
    if (n < 0) n = 0;
    memcpy(objects, objs, (size_t)n * sizeof *objs);
    nobjects = n;
}

/* the objects of segments first..last-1 as camera-facing pictures, one draw per shape type */
/* gantry signs (lift 400) follow their gantry, which is drawn as far as the road: up to far_last */
static void scenery_draw(int first, int last, int far_last, float cam_yaw, const float *mvp, const float *eye,
                         const float *fog)
{
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);
    gl.UseProgram(sprog);
    gl.UniformMatrix4fv(sloc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(sloc_eye, eye[0], eye[1], eye[2]);
    gl.Uniform3f(sloc_fog, fog[0], fog[1], fog[2]);
    gl.Uniform3f(sloc_fogr, fog_range[0], fog_range[1], fog_range[2]);
    gl.Uniform4f(sloc_lite, lite[0], lite[1], lite[2], lite[3]);
    gl.Uniform3f(sloc_fwd, fwd[0], fwd[1], fwd[2]);
    gl.Uniform1i(sloc_tex, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindVertexArray(svao);
    gl.BindBuffer(GL_ARRAY_BUFFER, svbo);
    for (int t = 0; t < R3D_SHAPES; t++) {
        if (!shp[t].tex) continue;
        int n = 0;
        float sc = shp[t].sc, w = (float)shp[t].w, h = (float)shp[t].h, ox = (float)shp[t].ox, oy = (float)shp[t].oy;
        for (int i = 0; i < nobjects && n + 6 <= MAX_SVERTS; i++) {
            const R3dObject *o = &objects[i];
            if (o->type != t || o->seg < first || o->seg >= (o->lift >= 400 ? far_last : last)) continue;
            float bx, by, bz;
            track_point((float)o->seg, (float)o->x, (float)o->lift, &bx, &by, &bz);
            bool mir = o->side <= 0 && t < 23;
            float l = (mir ? -(w - ox) : -ox) * sc, r = (mir ? ox : w - ox) * sc;
            float u0 = mir ? 1.0f : 0.0f, u1 = 1.0f - u0;
            float top = by + oy * sc, bot = by + (oy - h) * sc;
            SVert q[6] = {
                { bx + rx * l, bot, bz + rz * l, u0, 1 }, { bx + rx * r, bot, bz + rz * r, u1, 1 },
                { bx + rx * r, top, bz + rz * r, u1, 0 }, { bx + rx * l, bot, bz + rz * l, u0, 1 },
                { bx + rx * r, top, bz + rz * r, u1, 0 }, { bx + rx * l, top, bz + rz * l, u0, 0 },
            };
            memcpy(sverts + n, q, sizeof q);
            n += 6;
        }
        if (!n) continue;
        gl.BindTexture(GL_TEXTURE_2D, shp[t].tex);
        gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(SVert)), sverts, GL_STREAM_DRAW);
        gl.DrawArrays(GL_TRIANGLES, 0, n);
    }
    gl.BindVertexArray(0);
}

/* ---------------------------------------------------------------- GL */

bool r3d_scene_init(void)
{
    prog = r3d_program(vs, fs);
    if (!prog || !sprites_init() || !r3d_rain_init()) return false;
    loc_mvp = gl.GetUniformLocation(prog, "mvp");
    loc_eye = gl.GetUniformLocation(prog, "eye");
    loc_fog = gl.GetUniformLocation(prog, "fog");
    loc_fogr = gl.GetUniformLocation(prog, "fogr");
    loc_lite = gl.GetUniformLocation(prog, "lite");
    loc_fwd = gl.GetUniformLocation(prog, "fwd");
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
    for (int t = 0; t < R3D_SHAPES; t++)
        if (shp[t].tex) gl.DeleteTextures(1, &shp[t].tex);
    memset(shp, 0, sizeof shp);
    r3d_rain_shutdown();
    sprites_shutdown();
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
 * h10 h11 at p1; k[4] scales the colour at its corners (p0 X0, p0 X1, p1 X1, p1 X0; NULL = 1) */
static void band_k(float p0, float p1, float X0, float X1, float h00, float h01, float h10, float h11,
                   float r, float g, float b, const float *k)
{
    if (nv + 6 > MAX_VERTS) return;
    Vert v[4];
    track_point(p0, X0, h00, &v[0].x, &v[0].y, &v[0].z);
    track_point(p0, X1, h01, &v[1].x, &v[1].y, &v[1].z);
    track_point(p1, X1, h11, &v[2].x, &v[2].y, &v[2].z);
    track_point(p1, X0, h10, &v[3].x, &v[3].y, &v[3].z);
    for (int j = 0; j < 4; j++) {
        float m = k ? k[j] : 1.0f;
        v[j].r = fminf(r * m, 1.0f); v[j].g = fminf(g * m, 1.0f); v[j].b = fminf(b * m, 1.0f);
    }
    verts[nv++] = v[0]; verts[nv++] = v[1]; verts[nv++] = v[2];
    verts[nv++] = v[0]; verts[nv++] = v[2]; verts[nv++] = v[3];
}

static void band(float p0, float p1, float X0, float X1, float h00, float h01, float h10, float h11,
                 float r, float g, float b)
{
    band_k(p0, p1, X0, X1, h00, h01, h10, h11, r, g, b, NULL);
}

/* a small steady number 0..1 for (a, b) */
static float hash01(int a, int b)
{
    unsigned h = (unsigned)a * 2654435761u ^ (unsigned)b * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xFFFF) / 65535.0f;
}

/* ENHANCED: the hillsides and drop-offs lit: by their slope against a sun up on the left, lighter towards the top,
 * with steady rock streaks along the slope (segment i) */
static void hill(int i, float p0, float p1, float X0, float X1, float h00, float h01, float h10, float h11,
                 float r, float g, float b)
{
    float a[3], bb[3], c[3];
    track_point(p0, X0, h00, &a[0], &a[1], &a[2]);
    track_point(p0, X1, h01, &bb[0], &bb[1], &bb[2]);
    track_point(p1, X0, h10, &c[0], &c[1], &c[2]);
    float u[3] = { bb[0] - a[0], bb[1] - a[1], bb[2] - a[2] }, w[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    float n[3] = { u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0] };
    float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    float lit = 1.0f;
    if (len > 0) {
        if (n[1] < 0) len = -len;                              /* facing up */
        const float L[3] = { -0.48f, 0.80f, 0.36f };
        float d = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / len;
        lit = 0.55f + 0.6f * (d > 0 ? d : 0);
    }
    float hs[4] = { h00, h01, h11, h10 }, k[4];
    for (int j = 0; j < 4; j++) {
        float t = fabsf(hs[j]) / 600.0f;
        if (t > 1) t = 1;
        float streak = 0.9f + 0.2f * hash01(i + (j == 2 || j == 3), j == 1 || j == 2);
        k[j] = lit * (0.82f + 0.3f * t) * streak;
    }
    band_k(p0, p1, X0, X1, h00, h01, h10, h11, r, g, b, k);
}

/* the 16 colours of the live palette registers (R3dView.pal), and a colour by palette index */
static float pal_rgb[16][3];
#define PC(k) pal_rgb[k][0], pal_rgb[k][1], pal_rgb[k][2]

/* the original's colours (road_draw_band_front 4322, road_draw_front 6a32): road 1 (finish band 15), grass 9 right
 * beside the road, centre dash 4, Mulholland kerbs / rails 2 between the outer edge and the road, hillsides and
 * drop-offs 14, intersection pavements 4, drag-strip walls 11 / 4 */
static void build(int first, int last, int mode)
{
    nv = 0;
    bool mul = mode == 0x10;
    float w = mul ? 200.0f : 400.0f;                           /* outer edge distance (07e4) */
    for (int i = first; i < last; i++) {
        float p0 = (float)i, p1 = (float)(i + 1);
        unsigned c = trk.c[i];
        float xl0, xr0, xl1, xr1;
        edges(i, &xl0, &xr0);
        edges(i + 1 < R3D_TRACK_N ? i + 1 : i, &xl1, &xr1);
        float xl = fminf(xl0, xl1), xr = fmaxf(xr0, xr1);
        int road = ((c & 0x10) && (!mul || i >= 0x3AD)) ? 15 : 1;
        if (xr > xl) {
            if (road == 1) {                                   /* ENHANCED: asphalt, darker at its edges */
                float r = pal_rgb[1][0], g = pal_rgb[1][1], b = pal_rgb[1][2];
                if (r + g + b < 0.45f) { r = 0.20f; g = 0.20f; b = 0.22f; }   /* the game's black road: dark grey */
                float tone = (i / 3) & 1 ? 1.0f : 0.94f, mid = (xl + xr) * 0.5f;
                const float ke[4] = { 0.72f * tone, 1.0f * tone, 1.0f * tone, 0.72f * tone };
                const float kc[4] = { 1.0f * tone, 0.72f * tone, 0.72f * tone, 1.0f * tone };
                band_k(p0, p1, xl, mid, 0, 0, 0, 0, r, g, b, ke);
                band_k(p0, p1, mid, xr, 0, 0, 0, 0, r, g, b, kc);
            } else band(p0, p1, xl, xr, 0, 0, 0, 0, PC(road));
        }
        float sl = 0x5DC - w, sr = 0xCE4 + w;
        bool cross = c & 1;
        if (cross) {                                           /* intersection pavements (4322 cross path) */
            if (trk.l[i] & 0x80) {
                float m = sl + (xl - sl) * 0.5f;
                band(p0, p1, sl, m, 0.5f, 0.5f, 0.5f, 0.5f, PC(4));
                band(p0, p1, m, xl, 0.5f, 0.5f, 0.5f, 0.5f, PC(9));
            } else band(p0, p1, sl, xl, 0.5f, 0.5f, 0.5f, 0.5f, PC(4));
            if (trk.r[i] & 0x80) {
                float m = sr - (sr - xr) * 0.5f;
                band(p0, p1, m, sr, 0.5f, 0.5f, 0.5f, 0.5f, PC(4));
                band(p0, p1, xr, m, 0.5f, 0.5f, 0.5f, 0.5f, PC(9));
            } else band(p0, p1, xr, sr, 0.5f, 0.5f, 0.5f, 0.5f, PC(4));
            band(p0, p0 + 0.08f, sl, xl, 1, 1, 1, 1, PC(2));    /* kerb lines */
            band(p0, p0 + 0.08f, xr, sr, 1, 1, 1, 1, PC(2));
        }
        if ((c & 0xF000) && !(mode & 0x16)) {                 /* gantry (banner 55ce): posts and bar in 10, 400 up */
            float pw = 60, bt = 400, bb = 340;                  /* camera-facing posts and bar */
            band(p0, p0, xl - pw, xl, 0, 0, bt, bt, PC(10));
            band(p0, p0, xr, xr + pw, 0, 0, bt, bt, PC(10));
            band(p0, p0, xl - pw, xr + pw, bb, bb, bt, bt, PC(10));
        } else if (mul && !(c & 0x80)) {                       /* kerbs / rails */
            band(p0, p1, sl, xl, 0, 0, 0, 0, PC(2));
            band(p0, p1, xr, sr, 0, 0, 0, 0, PC(2));
        }
        if (mode == 0 && !(c & 4)) {                           /* town houses (town_walls_front 4c1c) */
            for (int side = 0; side < 2; side++) {
                unsigned F = side ? trk.r[i] : trk.l[i], Fm1 = i > 0 ? (side ? trk.r[i - 1] : trk.l[i - 1]) : 0;
                unsigned Fp1 = i + 1 < R3D_TRACK_N ? (side ? trk.r[i + 1] : trk.l[i + 1]) : 0;
                float face = side ? sr : sl, back = side ? sr + HOUSE_DEPTH : sl - HOUSE_DEPTH;
                float X0 = side ? face : back, X1 = side ? back : face;   /* X0 < X1 */
                float wall = 100, top = (F & 4) ? wall + ((F & 8) ? 100 : 50) : wall;
                int wc = (F & 0x10) ? 2 : 3;
                band(p0, p1, face, face, 0, wall, 0, wall, PC(wc));          /* the front, facing the road */
                if (F & 4) band(p0, p1, face, face, wall, top, wall, top, PC(4));   /* the storey */
                band(p0, p1, X0, X1, top, top, top, top, PC(F & 4 ? 4 : wc));      /* the roof */
                if (!(Fm1 & 4) || (F & 4) != (Fm1 & 4))                     /* the near end of a row */
                    band(p0, p0, X0, X1, 0, 0, top, top, PC(2));
                if (i + 1 < R3D_TRACK_N && (Fp1 & 4) != (F & 4))           /* the far end */
                    band(p1, p1, X0, X1, 0, 0, top, top, PC(2));
            }
        }
        if (c & 8) band(p0, p1 - 0.5f, X_MID - 20, X_MID + 20, 3, 3, 3, 3, PC(4));     /* centre dash */
        if (trk.wall_phase >= 0) {                             /* drag strip barrier walls (597e), 50 high */
            int ph = i - trk.wall_phase;
            int wc = (ph < 0 ? -ph : ph) % 10 < 5 ? 11 : 4;
            band(p0, p1, xl, xl, 0, 50, 0, 50, PC(wc));
            band(p0, p1, xr, xr, 0, 50, 0, 50, PC(wc));
        }
        /* grass beside the road (or beyond the outer edge), Mulholland banks: hillside > 0, drop-off < 0 */
        float gl = (cross || mul) ? sl : xl, gr_ = (cross || mul) ? sr : xr;
        int i1 = i + 1 < R3D_TRACK_N ? i + 1 : i;
        float bl0 = trk.bl[i], bl1 = trk.bl[i1], br0 = trk.br[i], br1 = trk.br[i1];
        float fl = -(w - 0x177) * 4, frr = (w + 0x339) * 4;     /* far bank x (07e4 XFL / XFR) */
        if (bl0 != 0 || bl1 != 0) {
            hill(i, p0, p1, fl, gl, bl0, 0, bl1, 0, PC(14));
            hill(i, p0, p1, X_MID - FAR_X, fl, bl0, bl0, bl1, bl1, PC(14));
        } else band(p0, p1, X_MID - FAR_X, gl, 0, 0, 0, 0, PC(9));
        if (br0 != 0 || br1 != 0) {
            hill(i, p0, p1, gr_, frr, 0, br0, 0, br1, PC(14));
            hill(i, p0, p1, frr, X_MID + FAR_X, br0, br0, br1, br1, PC(14));
        } else band(p0, p1, gr_, X_MID + FAR_X, 0, 0, 0, 0, PC(9));
    }
}

/* R1.7: the Aqueduct (mode 8) as aq_project_front 18ec projects it: a straight concrete channel, floor between
 * aq_fl..aq_fr at aq_floor, walls up to the top edges aq_tl / aq_tr at aq_th, flat ground beyond. Flags per segment
 * (race_render.md 1.6): 1 joint line, 2 no centre trough, 4 / 0x100 left wall opening, 8 / 0x200 right, 0x400 white
 * floor band. */
static void build_channel(int first, int last, const R3dView *v)
{
    nv = 0;
    float fl = v->aq_fl, fr = v->aq_fr, tl = v->aq_tl, tr = v->aq_tr, th = v->aq_th, fh = v->aq_floor;
    float mid = (fl + fr) * 0.5f;
    for (int i = first; i < last; i++) {
        float p0 = (float)i, p1 = (float)(i + 1);
        unsigned f = trk.c[i];
        band(p0, p1, fl, fr, fh, fh, fh, fh, PC((f & 0x400) ? 15 : 5));
        if (!(f & 2)) band(p0, p1, mid - 0x28, mid + 0x28, fh + 2, fh + 2, fh + 2, fh + 2, PC(2));
        if (f & 1) band(p0, p0 + 0.06f, fl, fr, fh + 3, fh + 3, fh + 3, fh + 3, PC(2));
        band(p0, p1, tl, fl, th, fh, th, fh, PC((f & (4 | 0x100)) ? 0 : 5));
        band(p0, p1, fr, tr, fh, th, fh, th, PC((f & (8 | 0x200)) ? 0 : 5));
        band(p0, p1, X_MID - FAR_X, tl, th, th, th, th, PC(0));
        band(p0, p1, tr, X_MID + FAR_X, th, th, th, th, PC(0));
    }
}

/* R2.2: spray behind a moving opponent in the rain, smoke from our rear wheels while the tyres squeal; the emission
 * rate follows time, not the frame rate */
static void emit_puffs(const R3dView *v)
{
    static Uint64 last_ns;
    static float last_opp = -1, acc_spray, acc_smoke;
    Uint64 now = SDL_GetTicksNS();
    float dt = last_ns ? (float)((double)(now - last_ns) / 1e9) : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    last_ns = now;
    r3d_puffs_update();
    float p[3], vel[3] = { 0, 60.0f, 0 };
    if (rain && v->opp.vis) {
        bool moving = last_opp >= 0 && v->opp.pos - last_opp > 0.0005f;
        last_opp = v->opp.pos;
        if (moving) {
            for (acc_spray += dt * 60.0f; acc_spray >= 1.0f; acc_spray -= 1.0f) {
                float side = (rand() & 1) ? 0.32f : -0.32f;
                track_point(v->opp.pos - 0.04f, v->opp.lat + side * v->opp.w, 18.0f, &p[0], &p[1], &p[2]);
                r3d_puff_emit(p, vel, 45.0f, 0.82f, 0.50f);
            }
        }
    } else last_opp = -1;
    if (v->squeal) {
        for (acc_smoke += dt * 30.0f; acc_smoke >= 1.0f; acc_smoke -= 1.0f) {
            float side = (rand() & 1) ? 260.0f : -160.0f;
            track_point(v->pos - 0.12f, v->cam_x + side, 12.0f, &p[0], &p[1], &p[2]);
            r3d_puff_emit(p, vel, 45.0f, 0.86f, 0.55f);
        }
    } else acc_smoke = 0;
}

/* ---------------------------------------------------------------- sky */

#define SKY_R 100000.0f
static GLuint sky_tex;
static int sky_w, sky_h, sky_hz;
static uint32_t sky_hash;

void r3d_sky_upload(const uint32_t *bgra, int w, int h, int horizon_row)
{
    if (!bgra || w <= 0 || h <= 0) { sky_h = 0; return; }
    uint32_t hsh = 2166136261u ^ (uint32_t)(w * 31 + h * 7 + horizon_row);
    for (int i = 0; i < w * h; i++) hsh = (hsh ^ bgra[i]) * 16777619u;
    sky_w = w; sky_h = h; sky_hz = horizon_row;
    if (sky_tex && hsh == sky_hash) return;
    sky_hash = hsh;
    if (!sky_tex) {
        gl.GenTextures(1, &sky_tex);
        gl.BindTexture(GL_TEXTURE_2D, sky_tex);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    gl.BindTexture(GL_TEXTURE_2D, sky_tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
}

/* The panorama on a cylinder around the eye: one pixel = 1 / FOCAL radian, as the road (the original scrolls it
 * twice as fast; in 3D it has to stay with the world), the horizon row on the eye's horizon; below it the
 * original's ground fill (colour 9) down to 45 degrees, so the far end of the ground meets it. */
static void sky_draw(const float *mvp, const float *eye, const float *fog)
{
    if (sky_h <= 0 || !sky_tex) return;
    gl.Disable(GL_DEPTH_TEST);
    const int N = 96;
    float upp = SKY_R / FOCAL;                                 /* world height of one panorama row */
    float top = eye[1] + (float)sky_hz * upp, bot = eye[1] + (float)(sky_hz - sky_h) * upp;
    /* ground fill (its own vertices: verts holds the road mesh) */
    static Vert fill[96 * 6];
    int nf = 0;
    for (int k = 0; k < N; k++) {
        float a0 = 6.2831853f * k / N, a1 = 6.2831853f * (k + 1) / N;
        float x0 = eye[0] + SKY_R * sinf(a0), z0 = eye[2] + SKY_R * cosf(a0);
        float x1 = eye[0] + SKY_R * sinf(a1), z1 = eye[2] + SKY_R * cosf(a1);
        Vert q0 = { x0, bot, z0, PC(9) }, q1 = { x1, bot, z1, PC(9) };
        Vert q2 = { x1, eye[1] - SKY_R, z1, PC(9) }, q3 = { x0, eye[1] - SKY_R, z0, PC(9) };
        fill[nf++] = q0; fill[nf++] = q1; fill[nf++] = q2;
        fill[nf++] = q0; fill[nf++] = q2; fill[nf++] = q3;
    }
    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(loc_eye, eye[0], eye[1], eye[2]);
    gl.Uniform3f(loc_fog, fog[0], fog[1], fog[2]);
    gl.Uniform3f(loc_fogr, fog_range[0], fog_range[1], fog_range[2]);
    gl.Uniform4f(loc_lite, lite[0], lite[1], lite[2], lite[3]);
    gl.Uniform3f(loc_fwd, fwd[0], fwd[1], fwd[2]);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nf * sizeof(Vert)), fill, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, nf);
    /* the panorama */
    int n = 0;
    float urad = FOCAL / (float)sky_w;                         /* texture repeats per radian */
    for (int k = 0; k < N && n + 6 <= MAX_SVERTS; k++) {
        float a0 = 6.2831853f * k / N, a1 = 6.2831853f * (k + 1) / N;
        float x0 = eye[0] + SKY_R * sinf(a0), z0 = eye[2] + SKY_R * cosf(a0);
        float x1 = eye[0] + SKY_R * sinf(a1), z1 = eye[2] + SKY_R * cosf(a1);
        float u0 = a0 * urad, u1 = a1 * urad;
        SVert q[6] = { { x0, bot, z0, u0, 1 }, { x1, bot, z1, u1, 1 }, { x1, top, z1, u1, 0 },
                       { x0, bot, z0, u0, 1 }, { x1, top, z1, u1, 0 }, { x0, top, z0, u0, 0 } };
        memcpy(sverts + n, q, sizeof q);
        n += 6;
    }
    gl.UseProgram(sprog);
    gl.UniformMatrix4fv(sloc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(sloc_eye, eye[0], eye[1], eye[2]);
    gl.Uniform3f(sloc_fog, fog[0], fog[1], fog[2]);
    gl.Uniform3f(sloc_fogr, fog_range[0], fog_range[1], fog_range[2]);
    gl.Uniform4f(sloc_lite, lite[0], lite[1], lite[2], lite[3]);
    gl.Uniform3f(sloc_fwd, fwd[0], fwd[1], fwd[2]);
    gl.Uniform1i(sloc_tex, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, sky_tex);
    gl.BindVertexArray(svao);
    gl.BindBuffer(GL_ARRAY_BUFFER, svbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(SVert)), sverts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, n);
    gl.BindVertexArray(0);
    gl.Enable(GL_DEPTH_TEST);
}

void r3d_scene_draw(const R3dView *v, int vw, int vh, bool mirror)
{
    for (int k = 0; k < 16; k++) {
        pal_rgb[k][0] = (float)(v->pal[k] >> 16 & 255) / 255.0f;
        pal_rgb[k][1] = (float)(v->pal[k] >> 8 & 255) / 255.0f;
        pal_rgb[k][2] = (float)(v->pal[k] & 255) / 255.0f;
    }
    float sky[3] = { pal_rgb[0][0], pal_rgb[0][1], pal_rgb[0][2] }, haze[3] = { 0.70f, 0.80f, 0.92f };
    if (rain) {                                            /* R2.1: overcast, short visibility, wet and dim */
        sky[0] = 0.42f; sky[1] = 0.46f; sky[2] = 0.52f;
        haze[0] = 0.46f; haze[1] = 0.50f; haze[2] = 0.55f;
        fog_range[0] = 2500.0f; fog_range[1] = 30000.0f; fog_range[2] = 0.72f;
    } else { fog_range[0] = 1e9f; fog_range[1] = 1.0f; fog_range[2] = 1.0f; }    /* the original has no haze */
    lite[0] = 1; lite[1] = 0;
    if (tod == 1) {                                        /* R2.3 dusk: warm low light */
        sky[0] = 0.78f; sky[1] = 0.45f; sky[2] = 0.32f; haze[0] = 0.62f; haze[1] = 0.42f; haze[2] = 0.38f;
        lite[0] = 0.62f;
    } else if (tod == 2) {                                 /* R2.3 night: headlights */
        sky[0] = 0.02f; sky[1] = 0.03f; sky[2] = 0.08f; haze[0] = 0.03f; haze[1] = 0.04f; haze[2] = 0.08f;
        lite[0] = 0.12f; lite[1] = 1.15f;
        fog_range[0] = 6000.0f; fog_range[1] = 20000.0f;
    }
    gl.ClearColor(sky[0], sky[1], sky[2], 1);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!have_trk) return;

    int seg = (int)floorf(v->pos);
    int first, last;
    if (mirror) { first = seg - DRAW_SEGS / 2; last = seg + 3; }      /* R1.5: what lies behind */
    else { first = seg - 3; last = seg + DRAW_SEGS; }
    if (first < 0) first = 0;
    if (last > R3D_TRACK_N) last = R3D_TRACK_N;
    if (trk.aqueduct) build_channel(first, last, v);
    else build(first, last, v->mode);

    /* eye: the original's camera is 0x7D0 - cam_y above the road, offset by the car's own height deviation (taken
     * against the car's segment when pushed: a blended absolute height against the blended position's segment
     * stepped at every segment boundary on a hill) */
    float ex, ey, ez;
    float above = (0x7D0 - v->cam_y) + v->height * HGT_UNIT;
    track_point(v->pos, v->cam_x, above, &ex, &ey, &ez);
    float yaw = -v->heading * YAW_RAD;                    /* a larger heading looks left */
    fwd[0] = sinf(yaw); fwd[1] = cosf(yaw);                /* R2.3: the headlights point where the car points */
    r3d_fx_set_light(tod == 2 ? 0.3f : tod == 1 ? 0.75f : 1.0f);
    float proj[16], view[16], mvp[16];
    if (mirror) {
        /* R1.5: the original's mirror projects with a quarter of the front focal length (road_proj_x_mirror: no x4),
         * centred on x 280, horizon at row 36 of the glass rows 22..42; looking back, flipped left-right */
        mat_projection(proj, 50.0f, 200000.0f);
        proj[0] = -(FOCAL / 4.0f) / 37.5f;
        proj[5] = (FOCAL / 4.0f) / 10.5f;
        proj[9] = 0.38f;
        yaw += 3.14159265f;
    } else mat_projection(proj, 50.0f, 200000.0f);
    mat_view(view, ex, ey, ez, yaw);
    if (v->roll != 0) {                                    /* R1.7: the Aqueduct's roll (0379 rot) */
        float c = cosf(v->roll), s = sinf(v->roll);
        float rz[16] = { c, s, 0, 0,   -s, c, 0, 0,   0, 0, 1, 0,   0, 0, 0, 1 };
        mat_mul(view, rz, view);
    }
    mat_mul(mvp, proj, view);

    {
        const float e3[3] = { ex, ey, ez };
        sky_draw(mvp, e3, haze);
    }
    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(loc_eye, ex, ey, ez);
    gl.Uniform3f(loc_fog, haze[0], haze[1], haze[2]);
    gl.Uniform3f(loc_fogr, fog_range[0], fog_range[1], fog_range[2]);
    gl.Uniform4f(loc_lite, lite[0], lite[1], lite[2], lite[3]);
    gl.Uniform3f(loc_fwd, fwd[0], fwd[1], fwd[2]);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof(Vert)), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, nv);
    gl.BindVertexArray(0);

    const float eye[3] = { ex, ey, ez };
    if (mirror) scenery_draw(seg - 8 < 0 ? 0 : seg - 8, seg + 1, last, yaw, mvp, eye, haze);
    else scenery_draw(first, seg + 12 < last ? seg + 12 : last, last, yaw, mvp, eye, haze);
    sprite_draw(&v->opp, yaw, mvp, eye, haze);
    sprite_draw(&v->tree, yaw, mvp, eye, haze);
    if (!mirror) emit_puffs(v);
    r3d_puffs_draw(mvp, yaw);
    if (rain) {                                            /* R2.1 */
        if (!mirror) r3d_rain_update(eye);
        r3d_rain_draw_world(mvp);
        if (!mirror) {
            gl.Disable(GL_DEPTH_TEST);
            r3d_rain_draw_screen((float)vw / (float)(vh > 0 ? vh : 1));
            gl.Enable(GL_DEPTH_TEST);
        }
    }
}
