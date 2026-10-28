#pragma once

// Loader OpenGL minimo (Fase 3b), vendorizado a mano en vez de traer GLAD2
// por FetchContent: GLAD2 genera su loader via Python en el configure de
// CMake, una superficie de fallo extra en CI (Windows+Linux) sin verificar
// en este proyecto. Mismo rendimiento (es la misma llamada a
// glfwGetProcAddress por debajo de cualquier loader) y misma forma de API
// que GLAD/GLEW -- variables globales con el nombre exacto de la funcion GL,
// tipadas PFNGLXXXPROC igual que los headers oficiales de Khronos -- para
// que el codigo que las usa (OpenGLRenderer.cpp) se lea como OpenGL normal.
//
// Cubre solo las ~33 funciones que este proyecto necesita (viewport/clear,
// VAO/VBO/EBO, shaders/programa, un uniform de matriz) -- nada de extensiones
// ni de todo el catalogo de GL 3.3 core. Cargar con loadOpenGLFunctions()
// DESPUES de que el contexto este actual (glfwMakeContextCurrent).

#include <cstddef>
#include <cstdint>

namespace iron3d::gl {

// --- Tipos basicos (iguales a los de gl.h / Khronos, definidos aqui para no
// depender de ningun header de sistema) -----------------------------------
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = ptrdiff_t;

// --- Constantes (solo las que hacen falta) --------------------------------
constexpr GLenum GL_FALSE_ = 0;
constexpr GLenum GL_TRUE_ = 1;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum GL_LESS = 0x0201;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_LINES = 0x0001;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;

// --- Punteros a funcion (nombres y firmas identicos a los headers oficiales
// de Khronos) ---------------------------------------------------------------
using PFNGLVIEWPORTPROC = void (*)(GLint, GLint, GLsizei, GLsizei);
using PFNGLCLEARCOLORPROC = void (*)(GLfloat, GLfloat, GLfloat, GLfloat);
using PFNGLCLEARPROC = void (*)(GLbitfield);
using PFNGLENABLEPROC = void (*)(GLenum);
using PFNGLDISABLEPROC = void (*)(GLenum);
using PFNGLDEPTHFUNCPROC = void (*)(GLenum);
using PFNGLDEPTHMASKPROC = void (*)(GLboolean);
using PFNGLDRAWARRAYSPROC = void (*)(GLenum, GLint, GLsizei);
using PFNGLDRAWELEMENTSPROC = void (*)(GLenum, GLsizei, GLenum, const void*);

using PFNGLGENVERTEXARRAYSPROC = void (*)(GLsizei, GLuint*);
using PFNGLBINDVERTEXARRAYPROC = void (*)(GLuint);
using PFNGLDELETEVERTEXARRAYSPROC = void (*)(GLsizei, const GLuint*);

using PFNGLGENBUFFERSPROC = void (*)(GLsizei, GLuint*);
using PFNGLBINDBUFFERPROC = void (*)(GLenum, GLuint);
using PFNGLBUFFERDATAPROC = void (*)(GLenum, GLsizeiptr, const void*, GLenum);
using PFNGLBUFFERSUBDATAPROC = void (*)(GLenum, ptrdiff_t, GLsizeiptr, const void*);
using PFNGLDELETEBUFFERSPROC = void (*)(GLsizei, const GLuint*);

using PFNGLVERTEXATTRIBPOINTERPROC = void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using PFNGLENABLEVERTEXATTRIBARRAYPROC = void (*)(GLuint);

using PFNGLCREATESHADERPROC = GLuint (*)(GLenum);
using PFNGLSHADERSOURCEPROC = void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PFNGLCOMPILESHADERPROC = void (*)(GLuint);
using PFNGLGETSHADERIVPROC = void (*)(GLuint, GLenum, GLint*);
using PFNGLGETSHADERINFOLOGPROC = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNGLDELETESHADERPROC = void (*)(GLuint);

using PFNGLCREATEPROGRAMPROC = GLuint (*)();
using PFNGLATTACHSHADERPROC = void (*)(GLuint, GLuint);
using PFNGLLINKPROGRAMPROC = void (*)(GLuint);
using PFNGLGETPROGRAMIVPROC = void (*)(GLuint, GLenum, GLint*);
using PFNGLGETPROGRAMINFOLOGPROC = void (*)(GLuint, GLsizei, GLsizei*, GLchar*);
using PFNGLDELETEPROGRAMPROC = void (*)(GLuint);
using PFNGLUSEPROGRAMPROC = void (*)(GLuint);

using PFNGLGETUNIFORMLOCATIONPROC = GLint (*)(GLuint, const GLchar*);
using PFNGLUNIFORMMATRIX4FVPROC = void (*)(GLint, GLsizei, GLboolean, const GLfloat*);
using PFNGLUNIFORM3FVPROC = void (*)(GLint, GLsizei, const GLfloat*);
using PFNGLUNIFORM1FPROC = void (*)(GLint, GLfloat);

// --- Variables globales (una por funcion, mismo nombre que en OpenGL de
// verdad -- el codigo que las llama se lee como OpenGL normal) -------------
extern PFNGLVIEWPORTPROC glViewport;
extern PFNGLCLEARCOLORPROC glClearColor;
extern PFNGLCLEARPROC glClear;
extern PFNGLENABLEPROC glEnable;
extern PFNGLDISABLEPROC glDisable;
extern PFNGLDEPTHFUNCPROC glDepthFunc;
extern PFNGLDEPTHMASKPROC glDepthMask;
extern PFNGLDRAWARRAYSPROC glDrawArrays;
extern PFNGLDRAWELEMENTSPROC glDrawElements;

extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;

extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;

extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;

extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC glDeleteShader;

extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLUSEPROGRAMPROC glUseProgram;

extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLUNIFORM3FVPROC glUniform3fv;
extern PFNGLUNIFORM1FPROC glUniform1f;

// Rellena todos los punteros de arriba via glfwGetProcAddress -- llamar
// DESPUES de glfwMakeContextCurrent(window). Devuelve false (con un mensaje
// en stderr diciendo cual) si alguna funcion no se pudo cargar.
bool loadOpenGLFunctions();

} // namespace iron3d::gl
