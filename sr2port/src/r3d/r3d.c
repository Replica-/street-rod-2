/* ENHANCED (R1.1): OpenGL 3.3 core presenter, see r3d.h. GL entry points are loaded through SDL_GL_GetProcAddress
 * (no opengl32 import, no loader library). */
#include "r3d/r3d.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

/* OpenGL 1.1 entry points have no PFN typedefs in glext.h */
typedef void (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void (APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);
typedef void (APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h,
                                             GLint border, GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                                GLenum format, GLenum type, const void *pixels);
typedef void (APIENTRYP PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);
typedef void (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);

static struct {
    PFNGLVIEWPORTPROC Viewport;
    PFNGLCLEARCOLORPROC ClearColor;
    PFNGLCLEARPROC Clear;
    PFNGLGENTEXTURESPROC GenTextures;
    PFNGLDELETETEXTURESPROC DeleteTextures;
    PFNGLBINDTEXTUREPROC BindTexture;
    PFNGLTEXPARAMETERIPROC TexParameteri;
    PFNGLTEXIMAGE2DPROC TexImage2D;
    PFNGLTEXSUBIMAGE2DPROC TexSubImage2D;
    PFNGLPIXELSTOREIPROC PixelStorei;
    PFNGLDRAWARRAYSPROC DrawArrays;
    PFNGLACTIVETEXTUREPROC ActiveTexture;
    PFNGLCREATESHADERPROC CreateShader;
    PFNGLSHADERSOURCEPROC ShaderSource;
    PFNGLCOMPILESHADERPROC CompileShader;
    PFNGLGETSHADERIVPROC GetShaderiv;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
    PFNGLDELETESHADERPROC DeleteShader;
    PFNGLCREATEPROGRAMPROC CreateProgram;
    PFNGLATTACHSHADERPROC AttachShader;
    PFNGLLINKPROGRAMPROC LinkProgram;
    PFNGLGETPROGRAMIVPROC GetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
    PFNGLUSEPROGRAMPROC UseProgram;
    PFNGLDELETEPROGRAMPROC DeleteProgram;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
    PFNGLUNIFORM1IPROC Uniform1i;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray;
} gl;

static SDL_Window *win;
static SDL_GLContext ctx;
static GLuint frame_tex, frame_prog, vao;
static int tex_w, tex_h;
static bool classic;

static bool load(void)
{
#define L(name) if (!(*(void **)&gl.name = (void *)SDL_GL_GetProcAddress("gl" #name))) { \
        fprintf(stderr, "r3d: missing gl" #name "\n"); return false; }
    L(Viewport) L(ClearColor) L(Clear) L(GenTextures) L(DeleteTextures) L(BindTexture) L(TexParameteri)
    L(TexImage2D) L(TexSubImage2D) L(PixelStorei) L(DrawArrays) L(ActiveTexture) L(CreateShader) L(ShaderSource)
    L(CompileShader) L(GetShaderiv) L(GetShaderInfoLog) L(DeleteShader) L(CreateProgram) L(AttachShader)
    L(LinkProgram) L(GetProgramiv) L(GetProgramInfoLog) L(UseProgram) L(DeleteProgram) L(GetUniformLocation)
    L(Uniform1i) L(GenVertexArrays) L(DeleteVertexArrays) L(BindVertexArray)
#undef L
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
        char log[512];
        gl.GetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "r3d: shader: %s\n", log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs)
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
        char log[512];
        gl.GetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "r3d: link: %s\n", log);
        return 0;
    }
    return p;
}

/* A full-viewport quad from gl_VertexID; the frame's top row is v = 0. */
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
    "out vec4 colour;\n"
    "void main() { colour = vec4(texture(frame, uv).rgb, 1.0); }\n";

SDL_WindowFlags r3d_window_flags(void)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
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
    SDL_GL_SetSwapInterval(1);
    frame_prog = program(frame_vs, frame_fs);
    gl.GenVertexArrays(1, &vao);
    gl.GenTextures(1, &frame_tex);
    gl.BindTexture(GL_TEXTURE_2D, frame_tex);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return frame_prog != 0;
}

void r3d_shutdown(void)
{
    if (!ctx) return;
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

void r3d_present(const uint32_t *frame, int fw, int fh)
{
    if (!ctx) return;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    gl.Viewport(0, 0, pw, ph);
    gl.ClearColor(0, 0, 0, 1);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float x, y, w, h;
    r3d_letterbox((float)pw, (float)ph, &x, &y, &w, &h);
    gl.Viewport((GLint)x, (GLint)(ph - y - h), (GLsizei)w, (GLsizei)h);   /* GL's origin is bottom left */

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
    gl.UseProgram(frame_prog);
    gl.Uniform1i(gl.GetUniformLocation(frame_prog, "frame"), 0);
    gl.BindVertexArray(vao);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    SDL_GL_SwapWindow(win);
}
