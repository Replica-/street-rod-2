/* ENHANCED (R1): the OpenGL 3.3 entry points r3d uses, loaded by r3d_init through SDL_GL_GetProcAddress. */
#ifndef R3D_GL_H
#define R3D_GL_H

#include <SDL3/SDL_opengl.h>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

/* OpenGL 1.1 entry points have no PFN typedefs in glext.h */
typedef void (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (APIENTRYP PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void (APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef void (APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
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
typedef void (APIENTRYP PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                                             void *pixels);

#define R3D_GL_FUNCS(X) \
    X(VIEWPORT, Viewport) X(SCISSOR, Scissor) X(CLEARCOLOR, ClearColor) X(CLEAR, Clear) X(ENABLE, Enable) \
    X(DISABLE, Disable) X(BLENDFUNC, BlendFunc) X(DEPTHFUNC, DepthFunc) X(GENTEXTURES, GenTextures) \
    X(DELETETEXTURES, DeleteTextures) X(BINDTEXTURE, BindTexture) X(TEXPARAMETERI, TexParameteri) \
    X(TEXIMAGE2D, TexImage2D) X(TEXSUBIMAGE2D, TexSubImage2D) X(PIXELSTOREI, PixelStorei) \
    X(DRAWARRAYS, DrawArrays) X(READPIXELS, ReadPixels) X(ACTIVETEXTURE, ActiveTexture) \
    X(CREATESHADER, CreateShader) X(SHADERSOURCE, ShaderSource) X(COMPILESHADER, CompileShader) \
    X(GETSHADERIV, GetShaderiv) X(GETSHADERINFOLOG, GetShaderInfoLog) X(DELETESHADER, DeleteShader) \
    X(CREATEPROGRAM, CreateProgram) X(ATTACHSHADER, AttachShader) X(LINKPROGRAM, LinkProgram) \
    X(GETPROGRAMIV, GetProgramiv) X(GETPROGRAMINFOLOG, GetProgramInfoLog) X(USEPROGRAM, UseProgram) \
    X(DELETEPROGRAM, DeleteProgram) X(GETUNIFORMLOCATION, GetUniformLocation) X(UNIFORM1I, Uniform1i) \
    X(UNIFORM1F, Uniform1f) X(UNIFORM3F, Uniform3f) X(UNIFORM4F, Uniform4f) X(UNIFORMMATRIX4FV, UniformMatrix4fv) \
    X(GENVERTEXARRAYS, GenVertexArrays) X(DELETEVERTEXARRAYS, DeleteVertexArrays) \
    X(BINDVERTEXARRAY, BindVertexArray) X(GENBUFFERS, GenBuffers) X(DELETEBUFFERS, DeleteBuffers) \
    X(BINDBUFFER, BindBuffer) X(BUFFERDATA, BufferData) X(VERTEXATTRIBPOINTER, VertexAttribPointer) \
    X(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray) X(GENFRAMEBUFFERS, GenFramebuffers) \
    X(DELETEFRAMEBUFFERS, DeleteFramebuffers) X(BINDFRAMEBUFFER, BindFramebuffer) \
    X(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D) X(GENRENDERBUFFERS, GenRenderbuffers) \
    X(DELETERENDERBUFFERS, DeleteRenderbuffers) X(BINDRENDERBUFFER, BindRenderbuffer) \
    X(RENDERBUFFERSTORAGE, RenderbufferStorage) X(FRAMEBUFFERRENDERBUFFER, FramebufferRenderbuffer) \
    X(CHECKFRAMEBUFFERSTATUS, CheckFramebufferStatus) X(BLITFRAMEBUFFER, BlitFramebuffer) \
    X(RENDERBUFFERSTORAGEMULTISAMPLE, RenderbufferStorageMultisample)

#define R3D_GL_MEMBER(UP, name) PFNGL##UP##PROC name;
typedef struct { R3D_GL_FUNCS(R3D_GL_MEMBER) } R3dGl;
#undef R3D_GL_MEMBER

extern R3dGl gl;

/* Compiles and links a vertex + fragment shader pair; 0 on failure (logged). */
GLuint r3d_program(const char *vs, const char *fs);

#endif
