// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// GLES2 resolved at runtime instead of linked from libGLESv2: on SDL builds
// from the context SDL creates, on DRM builds from the dlopen()ed library
// (gpu_libs::gles2_proc). A system without GLES then still starts the app;
// the 3D G-code renderer and the GPU blur fail init and fall back to the CPU.
//
// Include this INSTEAD of <GLES2/gl2.h>: it asks for the header's types only,
// so a GL call missing from the list below fails to compile rather than
// linking against a library that may not exist.

#define GL_GLES_PROTOTYPES 0
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Every GLES2 function the renderer, the backdrop blur and the EGL probe call.
#define HELIX_GLES2_ENTRY_POINTS(X)                                                                \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                                     \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                       \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                           \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                 \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                               \
    X(PFNGLBINDTEXTUREPROC, glBindTexture)                                                         \
    X(PFNGLBLENDFUNCPROC, glBlendFunc)                                                             \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                           \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                   \
    X(PFNGLCLEARPROC, glClear)                                                                     \
    X(PFNGLCLEARCOLORPROC, glClearColor)                                                           \
    X(PFNGLCOLORMASKPROC, glColorMask)                                                             \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                     \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                       \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                     \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                           \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                                     \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                                         \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                                                       \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures)                                                   \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc)                                                             \
    X(PFNGLDEPTHMASKPROC, glDepthMask)                                                             \
    X(PFNGLDISABLEPROC, glDisable)                                                                 \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)                               \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays)                                                           \
    X(PFNGLENABLEPROC, glEnable)                                                                   \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                 \
    X(PFNGLFINISHPROC, glFinish)                                                                   \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                 \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                       \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                           \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                 \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                               \
    X(PFNGLGENTEXTURESPROC, glGenTextures)                                                         \
    X(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation)                                             \
    X(PFNGLGETBOOLEANVPROC, glGetBooleanv)                                                         \
    X(PFNGLGETERRORPROC, glGetError)                                                               \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv)                                                         \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                             \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                       \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                               \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                         \
    X(PFNGLGETSTRINGPROC, glGetString)                                                             \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                           \
    X(PFNGLISENABLEDPROC, glIsEnabled)                                                             \
    X(PFNGLLINEWIDTHPROC, glLineWidth)                                                             \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                         \
    X(PFNGLREADPIXELSPROC, glReadPixels)                                                           \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                         \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                       \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D)                                                           \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri)                                                     \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                                             \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                             \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                             \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                                           \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                                             \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                                           \
    X(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv)                                               \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                               \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                           \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                         \
    X(PFNGLVIEWPORTPROC, glViewport)

namespace helix::gles2 {

#define HELIX_GLES2_DECLARE(type, name) extern type name;
HELIX_GLES2_ENTRY_POINTS(HELIX_GLES2_DECLARE)
#undef HELIX_GLES2_DECLARE

/// Resolve every entry point through @p get_proc (SDL_GL_GetProcAddress with a
/// GL context current, or gpu_libs::gles2_proc). All or nothing; logs the
/// missing names on failure. Runs once per process: later calls return the
/// first call's answer and ignore their @p get_proc.
bool load(void* (*get_proc)(const char*));

} // namespace helix::gles2
