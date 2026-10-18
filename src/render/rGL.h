#ifndef AT_GL_H
#define AT_GL_H

#ifndef DEDICATED

#ifdef AA_GLES2
// fixed function GL 1.x on GLES2 (the browser client): rGLES2.h declares the
// gl*/glu* entry points rGLES2.cpp implements, on top of GLES2/gl2.h
#include "rGLES2.h"
#else

#ifdef WIN32
#include <windows.h>
#endif


#define NO_SDL_GLEXT
#include <SDL_opengl.h>
#include <SDL_version.h>

#if SDL_VERSION_ATLEAST(2,0,0)
// SDL 1.2's SDL_opengl.h pulled GLU in, SDL2's does not. Emscripten's
// LEGACY_GL_EMULATION implements gluPerspective/gluLookAt, the rest is guarded
// off in the sources under __EMSCRIPTEN__.
#ifdef MACOSX
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif
#endif

#endif // AA_GLES2

/*
// include OpenGL header
#ifdef HAVE_SDL_OPENGL_H
#include <SDL_opengl.h>
#else
#ifdef HAVE_SDL_SDL_OPENGL_H
#include <SDL/SDL_opengl.h>
#else
#ifdef HAVE_OPENGL_GL_H
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#ifdef HAVE_GL_GL_H
#include <GL/gl.h>
#include <GL/glu.h>
#else
#error No suitable OpenGL header found by configure!
#endif
#endif
#endif
#endif
*/

#else

typedef float GLfloat;
typedef unsigned char GLubyte;
typedef unsigned int GLuint;
typedef unsigned int GLenum;
#endif


#ifdef DEBUG
#ifndef DEDICATED
#define AA_GL_ERROR_CHECKING
#endif
#endif

#ifdef AA_GL_ERROR_CHECKING
//! for debugging purposes: checks for OpenGL errors and prints them to the console.
void sr_CheckGLError();
#else
inline void sr_CheckGLError(){}
#endif

#endif
