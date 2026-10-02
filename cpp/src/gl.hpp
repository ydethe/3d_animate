// Tiny OpenGL 3.3-core function loader. GL 1.1 entry points come from libGL
// directly (declared by <GL/gl.h>); everything newer is loaded at runtime from
// the platform's getProcAddress (EGL or GLFW). This avoids a GLEW/glad apt dep.
#pragma once

#include <GL/gl.h>
#include <GL/glext.h>

namespace animate {
namespace gl {

using GetProcFn = void* (*)(const char*);

// Load all modern entry points. Returns false if any required one is missing.
bool load(GetProcFn getproc);

// Modern entry points we use (declared as function pointers, defined in gl.cpp).
#define ANIMATE_GL_FUNCS(X)                                          \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                            \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                            \
    X(PFNGLBUFFERDATAPROC, glBufferData)                            \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                      \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                  \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                  \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)            \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)  \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)          \
    X(PFNGLCREATESHADERPROC, glCreateShader)                        \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                        \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                      \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                          \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                        \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                      \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                        \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                          \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                        \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)              \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                            \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                      \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)            \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                              \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                              \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                              \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                              \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                    \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                  \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                  \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)        \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)          \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)  \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)    \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)            \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)

#define ANIMATE_GL_DECLARE(type, name) extern type name;
ANIMATE_GL_FUNCS(ANIMATE_GL_DECLARE)
#undef ANIMATE_GL_DECLARE

}  // namespace gl
}  // namespace animate

// Bring the loaded pointers into scope with their canonical gl* spellings so
// call sites read like ordinary GL code. (glActiveTexture / glDraw* / glEnable
// and other <= GL 1.3 entry points come straight from libGL via <GL/gl.h>.)
using animate::gl::glAttachShader;
using animate::gl::glBindBuffer;
using animate::gl::glBindFramebuffer;
using animate::gl::glBindRenderbuffer;
using animate::gl::glBindVertexArray;
using animate::gl::glBufferData;
using animate::gl::glCheckFramebufferStatus;
using animate::gl::glCompileShader;
using animate::gl::glCreateProgram;
using animate::gl::glCreateShader;
using animate::gl::glDeleteBuffers;
using animate::gl::glDeleteFramebuffers;
using animate::gl::glDeleteProgram;
using animate::gl::glDeleteRenderbuffers;
using animate::gl::glDeleteShader;
using animate::gl::glDeleteVertexArrays;
using animate::gl::glEnableVertexAttribArray;
using animate::gl::glFramebufferRenderbuffer;
using animate::gl::glFramebufferTexture2D;
using animate::gl::glGenBuffers;
using animate::gl::glGenerateMipmap;
using animate::gl::glGenFramebuffers;
using animate::gl::glGenRenderbuffers;
using animate::gl::glGenVertexArrays;
using animate::gl::glGetProgramInfoLog;
using animate::gl::glGetProgramiv;
using animate::gl::glGetShaderInfoLog;
using animate::gl::glGetShaderiv;
using animate::gl::glGetUniformLocation;
using animate::gl::glLinkProgram;
using animate::gl::glRenderbufferStorage;
using animate::gl::glShaderSource;
using animate::gl::glUniform1f;
using animate::gl::glUniform1i;
using animate::gl::glUniform3f;
using animate::gl::glUniform4f;
using animate::gl::glUniformMatrix4fv;
using animate::gl::glUseProgram;
using animate::gl::glVertexAttribPointer;
