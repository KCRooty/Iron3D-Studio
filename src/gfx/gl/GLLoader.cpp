#include "GLLoader.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>

namespace iron3d::gl {

PFNGLVIEWPORTPROC glViewport = nullptr;
PFNGLCLEARCOLORPROC glClearColor = nullptr;
PFNGLCLEARPROC glClear = nullptr;
PFNGLENABLEPROC glEnable = nullptr;
PFNGLDISABLEPROC glDisable = nullptr;
PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
PFNGLDEPTHMASKPROC glDepthMask = nullptr;
PFNGLDRAWARRAYSPROC glDrawArrays = nullptr;
PFNGLDRAWELEMENTSPROC glDrawElements = nullptr;

PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;

PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;

PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;

PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;

PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;

PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
PFNGLUNIFORM3FVPROC glUniform3fv = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;

namespace {
// Carga un puntero y avisa por stderr si falta -- no aborta aqui mismo para
// poder listar TODOS los que falten en una sola pasada antes de que
// loadOpenGLFunctions() decida si continuar.
template <typename Fn>
bool load(const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(glfwGetProcAddress(name));
    if (!out) {
        std::fprintf(stderr, "[Iron3D][opengl] No se pudo cargar %s\n", name);
        return false;
    }
    return true;
}
} // namespace

bool loadOpenGLFunctions() {
    bool ok = true;
#define IRON3D_GL_LOAD(name) ok = load(#name, name) && ok
    IRON3D_GL_LOAD(glViewport);
    IRON3D_GL_LOAD(glClearColor);
    IRON3D_GL_LOAD(glClear);
    IRON3D_GL_LOAD(glEnable);
    IRON3D_GL_LOAD(glDisable);
    IRON3D_GL_LOAD(glDepthFunc);
    IRON3D_GL_LOAD(glDepthMask);
    IRON3D_GL_LOAD(glDrawArrays);
    IRON3D_GL_LOAD(glDrawElements);

    IRON3D_GL_LOAD(glGenVertexArrays);
    IRON3D_GL_LOAD(glBindVertexArray);
    IRON3D_GL_LOAD(glDeleteVertexArrays);

    IRON3D_GL_LOAD(glGenBuffers);
    IRON3D_GL_LOAD(glBindBuffer);
    IRON3D_GL_LOAD(glBufferData);
    IRON3D_GL_LOAD(glBufferSubData);
    IRON3D_GL_LOAD(glDeleteBuffers);

    IRON3D_GL_LOAD(glVertexAttribPointer);
    IRON3D_GL_LOAD(glEnableVertexAttribArray);

    IRON3D_GL_LOAD(glCreateShader);
    IRON3D_GL_LOAD(glShaderSource);
    IRON3D_GL_LOAD(glCompileShader);
    IRON3D_GL_LOAD(glGetShaderiv);
    IRON3D_GL_LOAD(glGetShaderInfoLog);
    IRON3D_GL_LOAD(glDeleteShader);

    IRON3D_GL_LOAD(glCreateProgram);
    IRON3D_GL_LOAD(glAttachShader);
    IRON3D_GL_LOAD(glLinkProgram);
    IRON3D_GL_LOAD(glGetProgramiv);
    IRON3D_GL_LOAD(glGetProgramInfoLog);
    IRON3D_GL_LOAD(glDeleteProgram);
    IRON3D_GL_LOAD(glUseProgram);

    IRON3D_GL_LOAD(glGetUniformLocation);
    IRON3D_GL_LOAD(glUniformMatrix4fv);
    IRON3D_GL_LOAD(glUniform3fv);
    IRON3D_GL_LOAD(glUniform1f);
#undef IRON3D_GL_LOAD
    return ok;
}

} // namespace iron3d::gl
