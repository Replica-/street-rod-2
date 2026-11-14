/* ENHANCED (R1.1, R1.2): OpenGL 3.3 core presenter, see r3d.h. GL entry points are loaded through
 * SDL_GL_GetProcAddress (no opengl32 import, no loader library). Everything is drawn into an off-screen buffer the
 * size of the letterbox (SR_SNAPSHOT_DIR reads it back), then copied to the window. */
#include "r3d/r3d.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "r3d/gl.h"
#include "r3d/scene.h"

R3dGl gl;

static SDL_Window *win;
static SDL_GLContext ctx;
static GLuint frame_tex, frame_prog, vao;
static GLint frame_loc_tex, frame_loc_keyed;
static GLuint fbo, fbo_tex, fbo_depth;
static int fbo_w, fbo_h;
static int tex_w, tex_h;
static bool classic;

/* views pushed by the race code (r3d_view_push) and their display times */
static R3dView view_prev, view_last;
static Uint64 t_prev, t_last;
static int views;

static bool load(void)
{
#define R3D_GL_LOAD(UP, name) \
    if (!(*(void **)&gl.name = (void *)SDL_GL_GetProcAddress("gl" #name))) { \
        fprintf(stderr, "r3d: missing gl" #name "\n"); return false; }
    R3D_GL_FUNCS(R3D_GL_LOAD)
#undef R3D_GL_LOAD
    return true;
}

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "r3d: shader: %s\n", log);
    }
    return s;
}

GLuint r3d_program(const char *vs, const char *fs)
{
    GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs), p = gl.CreateProgram();
    gl.AttachShader(p, v);
    gl.AttachShader(p, f);
    gl.LinkProgram(p);
    gl.DeleteShader(v);
    gl.DeleteShader(f);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        gl.GetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "r3d: link: %s\n", log);
        return 0;
    }
    return p;
}

/* A full-viewport quad from gl_VertexID; the frame's top row is v = 0. keyed: the frame's alpha byte is used
 * (0 = see-through), else the frame is opaque. */
static const char *frame_vs =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "    vec2 p = vec2((gl_VertexID & 1) != 0 ? 1.0 : -1.0, (gl_VertexID & 2) != 0 ? 1.0 : -1.0);\n"
    "    uv = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";
static const char *frame_fs =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "uniform sampler2D frame;\n"
    "uniform int keyed;\n"
    "out vec4 colour;\n"
    "void main() {\n"
    "    vec4 c = texture(frame, uv);\n"
    "    colour = vec4(c.rgb, keyed != 0 ? c.a : 1.0);\n"
    "}\n";

SDL_WindowFlags r3d_window_flags(void)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    return SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
}

bool r3d_init(SDL_Window *window)
{
    win = window;
    ctx = SDL_GL_CreateContext(window);
    if (!ctx) {
        fprintf(stderr, "r3d: no OpenGL 3.3 context (%s); the picture is not shown\n", SDL_GetError());
        return false;
    }
    if (!load()) {
        SDL_GL_DestroyContext(ctx);
        ctx = NULL;
        return false;
    }
    SDL_GL_SetSwapInterval((SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) ? 0 : 1);
    frame_prog = r3d_program(frame_vs, frame_fs);
    frame_loc_tex = gl.GetUniformLocation(frame_prog, "frame");
    frame_loc_keyed = gl.GetUniformLocation(frame_prog, "keyed");
    gl.GenVertexArrays(1, &vao);
    gl.GenTextures(1, &frame_tex);
    gl.BindTexture(GL_TEXTURE_2D, frame_tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!r3d_scene_init()) fprintf(stderr, "r3d: scene init failed\n");
    return frame_prog != 0;
}

static void fbo_free(void)
{
    if (fbo) gl.DeleteFramebuffers(1, &fbo);
    if (fbo_tex) gl.DeleteTextures(1, &fbo_tex);
    if (fbo_depth) gl.DeleteRenderbuffers(1, &fbo_depth);
    fbo = fbo_tex = fbo_depth = 0;
    fbo_w = fbo_h = 0;
}

static void fbo_size(int w, int h)
{
    if (w == fbo_w && h == fbo_h && fbo) return;
    fbo_free();
    gl.GenTextures(1, &fbo_tex);
    gl.BindTexture(GL_TEXTURE_2D, fbo_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.GenRenderbuffers(1, &fbo_depth);
    gl.BindRenderbuffer(GL_RENDERBUFFER, fbo_depth);
    gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fbo_depth);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf(stderr, "r3d: framebuffer incomplete\n");
    fbo_w = w;
    fbo_h = h;
}

void r3d_shutdown(void)
{
    if (!ctx) return;
    r3d_scene_shutdown();
    fbo_free();
    gl.DeleteTextures(1, &frame_tex);
    gl.DeleteVertexArrays(1, &vao);
    gl.DeleteProgram(frame_prog);
    SDL_GL_DestroyContext(ctx);
    ctx = NULL;
}

bool r3d_active(void) { return ctx != NULL; }
void r3d_set_classic(bool on) { classic = on; }
bool r3d_classic(void) { return classic; }

void r3d_letterbox(float w, float h, float *x, float *y, float *rw, float *rh)
{
    float vw = w, vh = h;
    if (vw * 3 > vh * 4) vw = vh * 4 / 3; else vh = vw * 3 / 4;
    *x = (w - vw) / 2;
    *y = (h - vh) / 2;
    *rw = vw;
    *rh = vh;
}

/* ---------------------------------------------------------------- views (R1.2) */

#define VIEW_LIVE_NS (300 * SDL_NS_PER_MS)

void r3d_view_push(const R3dView *v)
{
    Uint64 now = SDL_GetTicksNS();
    if (views == 0 || now - t_last > VIEW_LIVE_NS) views = 0;      /* a new race view: no blend with an old one */
    view_prev = views ? view_last : *v;
    t_prev = views ? t_last : now;
    view_last = *v;
    t_last = now;
    views++;
}

bool r3d_view_live(void)
{
    return ctx && !classic && views > 0 && SDL_GetTicksNS() - t_last < VIEW_LIVE_NS;
}

static float blend(float a, float b, float t, float snap)
{
    return fabsf(b - a) > snap ? b : a + (b - a) * t;
}

/* The view for the display time: one original frame behind, blended between the last two pushes. */
static R3dView view_now(void)
{
    R3dView v = view_last;
    if (views < 2) return v;
    Uint64 now = SDL_GetTicksNS();
    double span = (double)(t_last - t_prev);
    if (span < 1e6) return v;
    float t = (float)((double)(now - t_last) / span);
    if (t > 1) t = 1;
    const R3dView *a = &view_prev, *b = &view_last;
    v.pos = blend(a->pos, b->pos, t, 8.0f);              /* a jump of more than 8 segments is a reset */
    v.cam_x = blend(a->cam_x, b->cam_x, t, 1500.0f);
    v.cam_y = blend(a->cam_y, b->cam_y, t, 1500.0f);
    v.heading = blend(a->heading, b->heading, t, 400.0f);
    v.height = blend(a->height, b->height, t, 400.0f);
    v.opp_pos = (a->opp_pos < 0 || b->opp_pos < 0) ? b->opp_pos : blend(a->opp_pos, b->opp_pos, t, 8.0f);
    v.opp_lat = blend(a->opp_lat, b->opp_lat, t, 1500.0f);
    if (a->opp.vis && b->opp.vis && a->opp.along == b->opp.along) {
        v.opp.pos = blend(a->opp.pos, b->opp.pos, t, 8.0f);
        v.opp.lat = blend(a->opp.lat, b->opp.lat, t, 1500.0f);
        v.opp.w = blend(a->opp.w, b->opp.w, t, 1e9f);
        v.opp.h = blend(a->opp.h, b->opp.h, t, 1e9f);
    }
    return v;
}

/* ---------------------------------------------------------------- present */

/* The race-view rectangle of the 320x200 frame: (0,18)-(319,101) (race_render.md 1.3). */
#define VIEW_TOP 18
#define VIEW_BOTTOM 102

void r3d_present(const uint32_t *frame, int fw, int fh)
{
    if (!ctx) return;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    float lx, ly, lw, lh;
    r3d_letterbox((float)pw, (float)ph, &lx, &ly, &lw, &lh);
    int w = (int)lw, h = (int)lh;
    if (SDL_GetWindowFlags(win) & SDL_WINDOW_HIDDEN) { w = 960; h = 720; }   /* headless check runs */
    if (w < 4 || h < 3) return;
    fbo_size(w, h);

    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.Viewport(0, 0, w, h);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    bool live = r3d_view_live() && fw == 320 && fh == 200;
    if (live) {
        int y0 = h * VIEW_TOP / 200, y1 = h * VIEW_BOTTOM / 200;
        gl.Viewport(0, h - y1, w, y1 - y0);
        gl.Enable(GL_SCISSOR_TEST);
        gl.Scissor(0, h - y1, w, y1 - y0);
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_LEQUAL);
        R3dView v = view_now();
        if (SDL_getenv("SR_R3D_TRACE"))                         /* developer aid: the blended view per frame */
            fprintf(stderr, "r3d %.4f pos %.4f x %.1f hd %.1f\n", (double)SDL_GetTicksNS() / 1e9, (double)v.pos,
                    (double)v.cam_x, (double)v.heading);
        r3d_scene_draw(&v, w, y1 - y0);
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_SCISSOR_TEST);
        gl.Viewport(0, 0, w, h);
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, frame_tex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (fw != tex_w || fh != tex_h) {
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_BGRA, GL_UNSIGNED_BYTE, frame);
        tex_w = fw;
        tex_h = fh;
    } else {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_BGRA, GL_UNSIGNED_BYTE, frame);
    }
    if (live) {
        gl.Enable(GL_BLEND);
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    gl.UseProgram(frame_prog);
    gl.Uniform1i(frame_loc_tex, 0);
    gl.Uniform1i(frame_loc_keyed, live ? 1 : 0);
    gl.BindVertexArray(vao);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.Disable(GL_BLEND);

    /* to the window, letterboxed */
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, pw, ph);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    int dx = (int)lx, dy = (int)(ph - ly - lh);
    gl.BlitFramebuffer(0, 0, w, h, dx, dy, dx + (int)lw, dy + (int)lh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_GL_SwapWindow(win);
}

bool r3d_save_bmp(const char *path)
{
    if (!ctx || !fbo) return false;
    SDL_Surface *s = SDL_CreateSurface(fbo_w, fbo_h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    uint8_t *tmp = malloc((size_t)fbo_w * fbo_h * 4);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.ReadPixels(0, 0, fbo_w, fbo_h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    for (int y = 0; y < fbo_h; y++)                            /* GL rows are bottom-up */
        SDL_memcpy((uint8_t *)s->pixels + (size_t)y * s->pitch, tmp + (size_t)(fbo_h - 1 - y) * fbo_w * 4,
                   (size_t)fbo_w * 4);
    free(tmp);
    bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}
