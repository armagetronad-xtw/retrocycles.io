/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2026  Armagetron Advanced Team (http://sourceforge.net/projects/armagetronad/)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

***************************************************************************

*/

// rGLES2.h: the fixed function OpenGL 1.x subset the game calls, declared on
// top of OpenGL ES 2 for builds with AA_GLES2 (the browser client). rGL.h
// includes this instead of the desktop GL/GLU headers; rGLES2.cpp implements
// the immediate mode, the matrix stacks, the fixed function state and the
// GLU calls in C++ and issues batched GLES2 draws. Entry points GLES2 has
// (glTexParameteri, glGenTextures, ...) keep their gl2.h declarations; the
// ones the layer must see (glEnable, glBindTexture, glClear, ...) are
// redefined with the same signatures in rGLES2.cpp.

#ifndef ArmageTron_rGLES2_H
#define ArmageTron_rGLES2_H

#include <GLES2/gl2.h>

typedef double GLdouble;
typedef double GLclampd;

// --- GL 1.x enums missing from gl2.h --------------------------------------

// primitives
#define GL_QUADS                     0x0007
#define GL_QUAD_STRIP                0x0008
#define GL_POLYGON                   0x0009

// matrix modes and queries
#define GL_MODELVIEW                 0x1700
#define GL_PROJECTION                0x1701
#define GL_TEXTURE                   0x1702
#define GL_MODELVIEW_MATRIX          0x0BA6
#define GL_PROJECTION_MATRIX         0x0BA7
#define GL_TEXTURE_MATRIX            0x0BA8
#define GL_CURRENT_COLOR             0x0B00

// enables
#define GL_FOG                       0x0B60
#define GL_ALPHA_TEST                0x0BC0
#define GL_LIGHTING                  0x0B50
#define GL_COLOR_MATERIAL            0x0B57
#define GL_NORMALIZE                 0x0BA1
#define GL_LIGHT0                    0x4000
#define GL_LIGHT1                    0x4001
#define GL_LIGHT2                    0x4002
#define GL_LIGHT3                    0x4003
#define GL_TEXTURE_GEN_S             0x0C60
#define GL_TEXTURE_GEN_T             0x0C61
#define GL_TEXTURE_GEN_R             0x0C62
#define GL_TEXTURE_GEN_Q             0x0C63
#define GL_POLYGON_OFFSET_POINT      0x2A01
#define GL_POLYGON_OFFSET_LINE       0x2A02
#define GL_FRAGMENT_PROGRAM_ARB      0x8804

// fog
#define GL_FOG_MODE                  0x0B65
#define GL_FOG_DENSITY               0x0B62
#define GL_FOG_START                 0x0B63
#define GL_FOG_END                   0x0B64
#define GL_FOG_COLOR                 0x0B66
#define GL_EXP                       0x0800
#define GL_EXP2                      0x0801

// lighting
#define GL_AMBIENT                   0x1200
#define GL_DIFFUSE                   0x1201
#define GL_SPECULAR                  0x1202
#define GL_POSITION                  0x1203
#define GL_EMISSION                  0x1600
#define GL_SHININESS                 0x1601
#define GL_AMBIENT_AND_DIFFUSE       0x1602
#define GL_LIGHT_MODEL_AMBIENT       0x0B53
#define GL_FLAT                      0x1D00
#define GL_SMOOTH                    0x1D01

// texture environment, generation, formats
#define GL_TEXTURE_ENV               0x2300
#define GL_TEXTURE_ENV_MODE          0x2200
#define GL_MODULATE                  0x2100
#define GL_DECAL                     0x2101
#define GL_S                         0x2000
#define GL_T                         0x2001
#define GL_R                         0x2002
#define GL_Q                         0x2003
#define GL_TEXTURE_GEN_MODE          0x2500
#define GL_OBJECT_PLANE              0x2501
#define GL_OBJECT_LINEAR             0x2401
#define GL_CLAMP                     0x2900
#define GL_RGB5                      0x8050
#define GL_RGB8                      0x8051
#define GL_RGBA4                     0x8056
#define GL_RGBA8                     0x8058
#define GL_LUMINANCE8_ALPHA8         0x8045
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50

// client arrays
#define GL_VERTEX_ARRAY              0x8074
#define GL_NORMAL_ARRAY              0x8075
#define GL_COLOR_ARRAY               0x8076
#define GL_TEXTURE_COORD_ARRAY       0x8078
#define GL_T2F_V3F                   0x2A27

// attribute stacks and display lists
#define GL_ALL_ATTRIB_BITS           0x000FFFFF
#define GL_CLIENT_ALL_ATTRIB_BITS    0xFFFFFFFF
#define GL_COMPILE                   0x1300
#define GL_COMPILE_AND_EXECUTE       0x1301

#ifdef __cplusplus
extern "C" {
#endif

// --- immediate mode --------------------------------------------------------
void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex3fv(GLfloat const* v);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glColor3fv(GLfloat const* v);
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor4fv(GLfloat const* v);
void glTexCoord2f(GLfloat s, GLfloat t);
void glTexCoord2fv(GLfloat const* v);
void glTexCoord2d(GLdouble s, GLdouble t);
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r);
void glTexCoord3fv(GLfloat const* v);
void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void glNormal3fv(GLfloat const* v);
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);

// --- matrices ---------------------------------------------------------------
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glLoadMatrixf(GLfloat const* m);
void glMultMatrixf(GLfloat const* m);
void glMultMatrixd(GLdouble const* m);
void glPushMatrix(void);
void glPopMatrix(void);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);

// --- fixed function state -------------------------------------------------
void glShadeModel(GLenum mode);
void glAlphaFunc(GLenum func, GLclampf ref);
void glFogi(GLenum pname, GLint param);
void glFogf(GLenum pname, GLfloat param);
void glFogfv(GLenum pname, GLfloat const* params);
void glLightfv(GLenum light, GLenum pname, GLfloat const* params);
void glLightModelfv(GLenum pname, GLfloat const* params);
void glMaterialfv(GLenum face, GLenum pname, GLfloat const* params);
void glTexEnvi(GLenum target, GLenum pname, GLint param);
void glTexEnvf(GLenum target, GLenum pname, GLfloat param);
void glTexGeni(GLenum coord, GLenum pname, GLint param);
void glTexGenfv(GLenum coord, GLenum pname, GLfloat const* params);
void glPushAttrib(GLbitfield mask);
void glPopAttrib(void);
void glPushClientAttrib(GLbitfield mask);
void glPopClientAttrib(void);

// --- client side arrays ---------------------------------------------------
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, void const* pointer);
void glNormalPointer(GLenum type, GLsizei stride, void const* pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, void const* pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, void const* pointer);
void glInterleavedArrays(GLenum format, GLsizei stride, void const* pointer);

// --- display lists (glGenLists returns 0: only sr_GLES2GenLists ids work) ----
GLuint glGenLists(GLsizei range);
void glNewList(GLuint list, GLenum mode);
void glEndList(void);
void glCallList(GLuint list);
void glDeleteLists(GLuint list, GLsizei range);
//! display list ids that glNewList/glCallList honour (static buffers); glGenLists
//! keeps returning 0 so that rDisplayList stays on immediate mode
GLuint sr_GLES2GenLists(GLsizei range);

// --- GLU --------------------------------------------------------------------
typedef struct GLUquadric GLUquadric;
typedef GLUquadric GLUquadricObj;
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ, GLdouble centerX, GLdouble centerY,
               GLdouble centerZ, GLdouble upX, GLdouble upY, GLdouble upZ);
GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, void const* data);
GLubyte const* gluErrorString(GLenum error);
GLUquadric* gluNewQuadric(void);
void gluDeleteQuadric(GLUquadric* quad);
void gluSphere(GLUquadric* quad, GLdouble radius, GLint slices, GLint stacks);

#ifdef __cplusplus
}
#endif

// --- statistics and control (C++ only) --------------------------------------
#ifdef __cplusplus
struct rGLES2Stats
{
    unsigned draws;        // GLES2 draw calls issued
    unsigned vertices;     // vertices uploaded
    unsigned flushes;      // batch flushes (draws + empty flushes)
    unsigned stateBreaks;  // flushes forced by a state change while a batch was open
    unsigned programSwitches;
    unsigned textureBinds; // real glBindTexture calls
    unsigned immediateCalls; // glVertex* + glColor* + glTexCoord* + glNormal* calls
    unsigned begins;       // glBegin blocks (each was one draw under the old emulation)
};

//! statistics of the frame that ended with the last glFinish/glFlush
rGLES2Stats const& sr_GLES2LastFrameStats();

//! flushes pending geometry (called by the layer itself on glFlush/glFinish/glClear)
void sr_GLES2Flush();

//! 1 or 2: the WebGL major version of the context the layer runs on (0 before init)
int sr_GLES2ContextVersion();

//! per pixel lighting for the draws that follow: with it, lit geometry is shaded
//! in the fragment shader (normal and eye position interpolated, the same two
//! light model) and GL_EMISSION is applied there as a self lit fraction of the
//! texel instead of being folded into the ambient term. Off: per vertex, as GL.
void sr_GLES2PerPixelLighting(bool on);

//! GLSL programs compiled so far (one per state combination that was drawn,
//! plus the three post processing programs once bloom runs)
int sr_GLES2ProgramCount();

//! puts the *unlit* draws that follow into the bloom mask, contributing their
//! own colour times this factor (0 turns it off again, the default). The second
//! way into the mask beside a lit material's GL_EMISSION, for surfaces the game
//! draws as flat vertex colour - the trail walls (BROWSER_WALL_GLOW).
void sr_GLES2SetGlow(float intensity);

//! selective bloom (BROWSER_BLOOM): while it is on, every draw whose material
//! declares GL_EMISSION under per pixel lighting, or that sr_GLES2SetGlow
//! marked, is remembered and replayed into an emission buffer at the end of the
//! frame; nothing else glows.
//! intensity is the fraction of the blurred emission added to the frame
//! (0..1; a negative value leaves the one in use alone).
void sr_GLES2SetBloom(bool on, float intensity);

//! depth of field with bokeh (the browser's attract scene), applied by the shim
//! after the bloom: focus distance (grid units from the eye), the in-focus band
//! (as 1 - focus/distance), the largest blur radius in pixels at 1536 wide, the
//! brightness above which a pixel turns into a bokeh disc and how much it is
//! weighted up, the disc's corners (0 round, 5..8 a polygon) and its tap rings
//! (1..4: 6, 18, 36, 60 taps). Runs where the bloom runs, on WebGL 2 only.
void sr_GLES2SetDepthOfField(bool on, float focus, float band, float radius, float threshold, float gain, int sides,
                             int rings, float nearScale = .4f);
bool sr_GLES2DepthOfFieldPending();
void sr_GLES2DepthOfField(GLuint target, int width, int height);

//! the shape of the bloom (BROWSER_BLOOM_WIDE / _SPILL): wide is the weight of
//! a second, four times wider blur level added with the first (0: not drawn),
//! spill how much of the composite's strongest channel above a quarter of full
//! scale is added to all three, so strong glow burns towards white instead of
//! clipping per channel (0: plain additive, as before)
void sr_GLES2SetBloomShape(float wide, float spill);

//! true when bloom is on and this frame drew something emissive: the owner of
//! the frame (web/wasm/sdl_shim.cpp) then has to hand a resolved depth buffer
//! to sr_GLES2Bloom below
bool sr_GLES2BloomPending();

//! draws the emission buffer (the remembered emissive geometry, depth tested
//! against depth, which must hold the finished frame's depth at its size),
//! blurs it at quarter resolution and adds it to target. Called once per frame,
//! after any multisample resolve and before the frame is presented; restores
//! the layer's GL state. Forgets the remembered geometry either way.
void sr_GLES2Bloom(GLuint target, GLuint depth, int width, int height);
#endif

#endif
