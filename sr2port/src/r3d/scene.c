/* ENHANCED (R1.2): the 3D race scene. R1.2 draws a test scene (sky, ground with one stripe per track segment) that
 * follows the interpolated view; R1.3 replaces it with the road built from the track arrays. */
#include "r3d/scene.h"

#include <math.h>
#include <stddef.h>

#include "r3d/gl.h"

/* World units: 1 track segment = SEG_LEN along z; lateral game units x LAT_SCALE along x. */
#define SEG_LEN 4.0f
#define LAT_SCALE (1.0f / 400.0f)

typedef struct { float x, y, z, r, g, b; } Vert;

static GLuint prog, vao, vbo;
static GLint loc_mvp;

static const char *vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "layout(location = 1) in vec3 col;\n"
    "uniform mat4 mvp;\n"
    "out vec3 c;\n"
    "void main() { c = col; gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char *fs =
    "#version 330 core\n"
    "in vec3 c;\n"
    "out vec4 colour;\n"
    "void main() { colour = vec4(c, 1.0); }\n";

bool r3d_scene_init(void)
{
    prog = r3d_program(vs, fs);
    if (!prog) return false;
    loc_mvp = gl.GetUniformLocation(prog, "mvp");
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

static void mat_perspective(float *m, float fovy_deg, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fovy_deg * 3.14159265f / 360.0f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1;
    m[14] = 2 * zf * zn / (zn - zf);
}

/* camera at (x, y, z) looking down +z turned by yaw (radians, positive = right) */
static void mat_view(float *m, float x, float y, float z, float yaw)
{
    float c = cosf(yaw), s = sinf(yaw);
    /* rows: right = (c, 0, -s), up = (0, 1, 0), -forward = -(s, 0, c) (the view looks down -z) */
    float r[16] = { c, 0, -s, 0,   0, 1, 0, 0,   -s, 0, -c, 0,   0, 0, 0, 1 };
    float t[16] = { 1, 0, 0, 0,   0, 1, 0, 0,   0, 0, 1, 0,   -x, -y, -z, 1 };
    mat_mul(m, r, t);
}

/* ---------------------------------------------------------------- test scene */

#define MAX_VERTS 6000
static Vert verts[MAX_VERTS];
static int nv;

static void quad(float x0, float x1, float z0, float z1, float y, float r, float g, float b)
{
    if (nv + 6 > MAX_VERTS) return;
    Vert a = { x0, y, z0, r, g, b }, bb = { x1, y, z0, r, g, b }, c = { x1, y, z1, r, g, b }, d = { x0, y, z1, r, g, b };
    verts[nv++] = a; verts[nv++] = bb; verts[nv++] = c;
    verts[nv++] = a; verts[nv++] = c; verts[nv++] = d;
}

void r3d_scene_draw(const R3dView *v, int vw, int vh)
{
    gl.ClearColor(0.35f, 0.55f, 0.85f, 1);                 /* sky */
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float cz = v->pos * SEG_LEN;
    float cx = (v->cam_x - 0xA00) * LAT_SCALE;
    nv = 0;
    int first = (int)floorf(v->pos) - 2;
    for (int s = first; s < first + 120; s++) {
        float z0 = s * SEG_LEN, z1 = z0 + SEG_LEN;
        bool odd = s & 1;
        quad(-60, -2.6f, z0, z1, 0, 0.20f, odd ? 0.45f : 0.40f, 0.15f);      /* grass */
        quad(2.6f, 60, z0, z1, 0, 0.20f, odd ? 0.45f : 0.40f, 0.15f);
        quad(-2.6f, 2.6f, z0, z1, 0, odd ? 0.42f : 0.36f, odd ? 0.42f : 0.36f, odd ? 0.44f : 0.38f);   /* road */
        if (odd) quad(-0.08f, 0.08f, z0, z1, 0.001f, 0.95f, 0.95f, 0.95f);   /* centre line */
    }

    float proj[16], view[16], mvp[16];
    mat_perspective(proj, 40.0f, (float)vw / (float)(vh > 0 ? vh : 1), 0.3f, 600.0f);
    mat_view(view, cx, 1.2f, cz, 0.0f);
    mat_mul(mvp, proj, view);

    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nv * sizeof(Vert)), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, nv);
    gl.BindVertexArray(0);
}
