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

/* ---------------------------------------------------------------- sprites (R1.4) */

typedef struct { float x, y, z, u, v; } SVert;

static GLuint sprog, svao, svbo, stex[R3D_SPRITE_SLOTS];
static GLint sloc_mvp, sloc_eye, sloc_fog, sloc_tex;
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
    "out vec4 colour;\n"
    "void main() {\n"
    "    vec4 c = texture(tex, t);\n"
    "    if (c.a < 0.5) discard;\n"
    "    float d = length(w.xz - eye.xz);\n"
    "    float f = clamp((d - 20000.0) / 50000.0, 0.0, 1.0);\n"
    "    colour = vec4(mix(c.rgb, fog, f), 1.0);\n"
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
static void scenery_draw(int first, int last, float cam_yaw, const float *mvp, const float *eye, const float *fog)
{
    float rx = cosf(cam_yaw), rz = -sinf(cam_yaw);
    gl.UseProgram(sprog);
    gl.UniformMatrix4fv(sloc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(sloc_eye, eye[0], eye[1], eye[2]);
    gl.Uniform3f(sloc_fog, fog[0], fog[1], fog[2]);
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
            if (o->type != t || o->seg < first || o->seg >= last) continue;
            float bx, by, bz;
            track_point((float)o->seg, (float)o->x, 0, &bx, &by, &bz);
            bool mir = o->side <= 0;
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
    if (!prog || !sprites_init()) return false;
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
    for (int t = 0; t < R3D_SHAPES; t++)
        if (shp[t].tex) gl.DeleteTextures(1, &shp[t].tex);
    memset(shp, 0, sizeof shp);
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
        /* drag strip barrier walls (597e): road edges up to world height 0x79E (50 above the road), blocks of five
         * segments in colour 0x0B / 4 */
        if (trk.wall_phase >= 0) {
            int ph = i - trk.wall_phase;
            bool cyan = (ph < 0 ? -ph : ph) % 10 < 5;
            float r = cyan ? 0.33f : 0.67f, g = cyan ? 1.0f : 0.0f, b = cyan ? 1.0f : 0.0f;
            band(p0, p1, xl, xl, 0, 50, 0, 50, r, g, b);
            band(p0, p1, xr, xr, 0, 50, 0, 50, r, g, b);
        }
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
        bool alt = (i / 2) & 1;
        float k = alt ? 0.70f : 0.66f;
        if (f & 0x400) band(p0, p1, fl, fr, fh, fh, fh, fh, 0.93f, 0.93f, 0.90f);
        else band(p0, p1, fl, fr, fh, fh, fh, fh, k, k * 0.97f, k * 0.90f);
        if (!(f & 2)) band(p0, p1, mid - 0x28, mid + 0x28, fh + 2, fh + 2, fh + 2, fh + 2, 0.42f, 0.42f, 0.40f);
        if (f & 1) band(p0, p0 + 0.06f, fl, fr, fh + 3, fh + 3, fh + 3, fh + 3, 0.30f, 0.30f, 0.28f);
        float wk = alt ? 0.62f : 0.58f;
        if (f & (4 | 0x100)) band(p0, p1, tl, fl, th, fh, th, fh, 0.16f, 0.15f, 0.14f);        /* opening */
        else band(p0, p1, tl, fl, th, fh, th, fh, wk, wk * 0.96f, wk * 0.88f);
        if (f & (8 | 0x200)) band(p0, p1, fr, tr, fh, th, fh, th, 0.16f, 0.15f, 0.14f);
        else band(p0, p1, fr, tr, fh, th, fh, th, wk, wk * 0.96f, wk * 0.88f);
        float gr = alt ? 0.50f : 0.46f;                    /* dry ground beyond the walls */
        band(p0, p1, X_MID - FAR_X, tl, th, th, th, th, gr, gr * 0.92f, gr * 0.70f);
        band(p0, p1, tr, X_MID + FAR_X, th, th, th, th, gr, gr * 0.92f, gr * 0.70f);
    }
}

void r3d_scene_draw(const R3dView *v, int vw, int vh, bool mirror)
{
    (void)vw; (void)vh;
    const float sky[3] = { 0.36f, 0.56f, 0.86f }, haze[3] = { 0.70f, 0.80f, 0.92f };
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

    /* eye: the original's camera is 0x7D0 - cam_y above the road, offset by the car's own height deviation */
    float ex, ey, ez;
    int si = seg < 0 ? 0 : (seg >= R3D_TRACK_N ? R3D_TRACK_N - 1 : seg);
    float above = (0x7D0 - v->cam_y) + (v->height - trk.hgt[si]) * HGT_UNIT;
    track_point(v->pos, v->cam_x, above, &ex, &ey, &ez);
    float yaw = v->heading * YAW_RAD;
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

    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.Uniform3f(loc_eye, ex, ey, ez);
    gl.Uniform3f(loc_fog, haze[0], haze[1], haze[2]);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof(Vert)), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, nv);
    gl.BindVertexArray(0);

    const float eye[3] = { ex, ey, ez };
    scenery_draw(first, last, yaw, mvp, eye, haze);
    sprite_draw(&v->opp, yaw, mvp, eye, haze);
    sprite_draw(&v->tree, yaw, mvp, eye, haze);
}
