/*
 * The few OpenGL ES 2.0 types and constants the GPU path (gpu_video.c) uses, written out here: SDL's
 * SDL_opengles2.h needs the Khronos headers (GLES2/gl2platform.h), which a Mac with the SDL2 framework or
 * Homebrew does not have. Values are those of the OpenGL ES 2.0 specification (they are part of its ABI);
 * the entry points are loaded at run time through SDL_GL_GetProcAddress, so no GL library is linked.
 */
#ifndef WWF_GL_MIN_H
#define WWF_GL_MIN_H

#include <stddef.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef unsigned int GLuint;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#ifndef GL_APIENTRY
#if defined(_WIN32) && !defined(__SCITECH_SNAP__)
#define GL_APIENTRY __stdcall
#else
#define GL_APIENTRY
#endif
#endif

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VIEWPORT 0x0BA2
#define GL_SCISSOR_BOX 0x0C10
#define GL_SCISSOR_TEST 0x0C11
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_BLEND 0x0BE2
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_DITHER 0x0BD0
#define GL_STENCIL_TEST 0x0B90
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622

#endif
