/* ENHANCED (R2.1): rain in the 3D race view - streaks falling around the camera (world space, slanted by the car's
 * own motion) and drops on the windscreen (screen space, drawn over the 3D view and under the 2D cockpit). */
#include "r3d/rain.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "r3d/gl.h"

typedef struct { float x, y, z, r, g, b, a; } FVert;

static GLuint prog, vao, vbo;
static GLint loc_mvp;

static const char *vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 pos;\n"
    "layout(location = 1) in vec4 col;\n"
    "uniform mat4 mvp;\n"
    "out vec4 c;\n"
    "void main() { c = col; gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char *fs =
    "#version 330 core\n"
    "in vec4 c;\n"
    "out vec4 colour;\n"
    "void main() { colour = c; }\n";

#define DROPS 2500
#define BOX 5000.0f                 /* half size of the rain box around the eye (world units) */
#define TOP 2600.0f
#define FALL 2500.0f                /* units per second (the road is 1800 wide) */
static struct { float x, y, z; } drop[DROPS];
static bool seeded;
static float last_eye[3], vel[3];
static Uint64 last_ns;

#define SPOTS 48
static struct { float x, y, r, life, age, slide; bool on; } spot[SPOTS];
static float spawn_acc;

#define MAX_FV 24000
static FVert fv[MAX_FV];

static float frand(float a, float b) { return a + (b - a) * (float)rand() / (float)RAND_MAX; }

bool r3d_rain_init(void)
{
    prog = r3d_program(vs, fs);
    if (!prog) return false;
    loc_mvp = gl.GetUniformLocation(prog, "mvp");
    gl.GenVertexArrays(1, &vao);
    gl.GenBuffers(1, &vbo);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FVert), (void *)0);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FVert), (void *)offsetof(FVert, r));
    gl.EnableVertexAttribArray(1);
    gl.BindVertexArray(0);
    return true;
}

void r3d_rain_shutdown(void)
{
    if (vbo) gl.DeleteBuffers(1, &vbo);
    if (vao) gl.DeleteVertexArrays(1, &vao);
    if (prog) gl.DeleteProgram(prog);
    vbo = vao = prog = 0;
}

static void draw(const float *mvp, GLenum kind, int n)
{
    gl.UseProgram(prog);
    gl.UniformMatrix4fv(loc_mvp, 1, GL_FALSE, mvp);
    gl.BindVertexArray(vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(FVert)), fv, GL_STREAM_DRAW);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.DrawArrays(kind, 0, n);
    gl.Disable(GL_BLEND);
    gl.BindVertexArray(0);
}

/* advances the drops (call once per displayed frame) */
void r3d_rain_update(const float *eye)
{
    Uint64 now = SDL_GetTicksNS();
    float dt = last_ns ? (float)((double)(now - last_ns) / 1e9) : 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    last_ns = now;
    if (!seeded) {
        for (int i = 0; i < DROPS; i++) {
            drop[i].x = eye[0] + frand(-BOX, BOX);
            drop[i].y = eye[1] + frand(-200.0f, TOP);
            drop[i].z = eye[2] + frand(-BOX, BOX);
        }
        last_eye[0] = eye[0]; last_eye[1] = eye[1]; last_eye[2] = eye[2];
        seeded = true;
    }
    if (dt > 0) {
        for (int k = 0; k < 3; k++) {                      /* the car's velocity, smoothed */
            float v = (eye[k] - last_eye[k]) / dt;
            if (fabsf(v) > 40000.0f) v = 0;                /* a reset, not motion */
            vel[k] += (v - vel[k]) * 0.2f;
            last_eye[k] = eye[k];
        }
    }
    for (int i = 0; i < DROPS; i++) {
        drop[i].y -= FALL * dt;
        if (drop[i].y < eye[1] - 200.0f) drop[i].y += TOP + 200.0f;
        if (drop[i].x < eye[0] - BOX) drop[i].x += 2 * BOX; else if (drop[i].x > eye[0] + BOX) drop[i].x -= 2 * BOX;
        if (drop[i].z < eye[2] - BOX) drop[i].z += 2 * BOX; else if (drop[i].z > eye[2] + BOX) drop[i].z -= 2 * BOX;
    }
    /* windscreen spots: appear, sit, now and then run down, fade */
    spawn_acc += dt * 30.0f;
    for (int i = 0; i < SPOTS && spawn_acc >= 1.0f; i++) {
        if (spot[i].on) continue;
        spot[i].on = true;
        spot[i].x = frand(-1.0f, 1.0f);
        spot[i].y = frand(-1.0f, 1.0f);
        spot[i].r = frand(0.006f, 0.02f);
        spot[i].life = frand(0.8f, 2.6f);
        spot[i].age = 0;
        spot[i].slide = rand() % 4 == 0 ? frand(0.1f, 0.5f) : 0.0f;
        spawn_acc -= 1.0f;
    }
    if (spawn_acc > 3.0f) spawn_acc = 3.0f;
    for (int i = 0; i < SPOTS; i++) {
        if (!spot[i].on) continue;
        spot[i].age += dt;
        spot[i].y -= spot[i].slide * dt;
        if (spot[i].age > spot[i].life || spot[i].y < -1.2f) spot[i].on = false;
    }
}

void r3d_rain_draw_world(const float *mvp)
{
    int n = 0;
    float sx = -vel[0] * 0.025f, sz = -vel[2] * 0.025f;    /* streak: fall + the car's own motion */
    for (int i = 0; i < DROPS && n + 2 <= MAX_FV; i++) {
        FVert a = { drop[i].x, drop[i].y, drop[i].z, 0.78f, 0.82f, 0.90f, 0.10f };
        FVert b = { drop[i].x - sx, drop[i].y + FALL * 0.03f, drop[i].z - sz, 0.88f, 0.90f, 0.96f, 0.65f };
        fv[n++] = a;
        fv[n++] = b;
    }
    draw(mvp, GL_LINES, n);
}

/* aspect = viewport width / height in pixels, so the spots are round */
void r3d_rain_draw_screen(float aspect)
{
    static const float id[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    int n = 0;
    for (int i = 0; i < SPOTS && n + 24 <= MAX_FV; i++) {
        if (!spot[i].on) continue;
        float fade = 1.0f - spot[i].age / spot[i].life;
        float rx = spot[i].r, ry = spot[i].r * aspect;
        for (int k = 0; k < 8; k++) {                      /* an 8-sided drop, a lighter rim at the bottom */
            float a0 = (float)k * 0.785398f, a1 = (float)(k + 1) * 0.785398f;
            float lit = 0.5f - 0.5f * sinf((a0 + a1) * 0.5f);
            FVert c = { spot[i].x, spot[i].y, 0, 0.70f, 0.75f, 0.82f, 0.18f * fade };
            FVert p0 = { spot[i].x + rx * cosf(a0), spot[i].y + ry * sinf(a0), 0, 0.85f + 0.15f * lit, 0.88f + 0.12f * lit,
                         0.95f, (0.25f + 0.35f * lit) * fade };
            FVert p1 = { spot[i].x + rx * cosf(a1), spot[i].y + ry * sinf(a1), 0, 0.85f + 0.15f * lit, 0.88f + 0.12f * lit,
                         0.95f, (0.25f + 0.35f * lit) * fade };
            fv[n++] = c; fv[n++] = p0; fv[n++] = p1;
        }
    }
    if (n) draw(id, GL_TRIANGLES, n);
}
