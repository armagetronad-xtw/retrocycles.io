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

// rGLES2.cpp: fixed function OpenGL 1.x on OpenGL ES 2 / WebGL for the
// browser client (AA_GLES2). See rGLES2.h for what is declared.
//
// Everything the game draws arrives through glBegin/glVertex*/glEnd (or the
// client array calls, which are expanded into the same path). Vertices are
// appended to one CPU side batch with the current color, texture coordinate
// and normal; strips, fans, quads and quad strips are turned into indexed
// triangles (lines into indexed lines) as they come in. The batch is drawn
// with one glDrawElements when the fixed function state it was started with
// changes (enables, blend/depth/cull/polygon offset state, bound texture,
// any matrix, fog/light/material/alpha test parameters), when the primitive
// class changes (triangles/lines/points), when the index space is full, and
// on glClear/glViewport/glReadPixels/glFlush/glFinish. A change in the middle
// of a glBegin block splits the block: the vertices the open primitive still
// needs are carried over into the new batch.
//
// The real GLES2 entry points are reached through Emscripten's
// emscripten_gl* aliases (webgl/webgl1.h); the gl* names of the calls this
// layer intercepts are defined here with their gl2.h signatures, so the game
// sources need no renaming.

#ifdef AA_GLES2

#include "rGLES2.h"

#include <webgl/webgl1.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace
{

// --- small helpers ----------------------------------------------------------

void LogOnce(char const* what)
{
    static std::vector<char const*> seen;
    for (size_t i = 0; i < seen.size(); ++i)
        if (!strcmp(seen[i], what))
            return;
    seen.push_back(what);
    printf("rGLES2: %s\n", what);
}

// --- matrices, column major like GL ---------------------------------------

struct Mat4
{
    float m[16];
};

Mat4 Identity()
{
    Mat4 r;
    memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

// a * b
Mat4 Mul(Mat4 const& a, Mat4 const& b)
{
    Mat4 r;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
        {
            float s = 0;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * b.m[col * 4 + k];
            r.m[col * 4 + row] = s;
        }
    return r;
}

void Transform(Mat4 const& a, float const* v, float* out)
{
    for (int row = 0; row < 4; ++row)
        out[row] = a.m[row] * v[0] + a.m[4 + row] * v[1] + a.m[8 + row] * v[2] + a.m[12 + row] * v[3];
}

struct MatrixStack
{
    Mat4 top;
    std::vector<Mat4> saved;
};

enum
{
    MAT_MODELVIEW = 0,
    MAT_PROJECTION = 1,
    MAT_TEXTURE = 2
};

// --- the fixed function state a draw depends on ---------------------------

enum Cap
{
    CAP_TEXTURE_2D = 1 << 0,
    CAP_FOG = 1 << 1,
    CAP_ALPHA_TEST = 1 << 2,
    CAP_LIGHTING = 1 << 3,
    CAP_LIGHT0 = 1 << 4,
    CAP_LIGHT1 = 1 << 5,
    CAP_BLEND = 1 << 6,
    CAP_DEPTH_TEST = 1 << 7,
    CAP_CULL_FACE = 1 << 8,
    CAP_POLYGON_OFFSET_FILL = 1 << 9,
    CAP_DITHER = 1 << 10
};

// the caps that are real GLES2 enables
unsigned const CAPS_REAL = CAP_BLEND | CAP_DEPTH_TEST | CAP_CULL_FACE | CAP_POLYGON_OFFSET_FILL | CAP_DITHER;

struct Light
{
    float ambient[4], diffuse[4], specular[4], position[4]; // position in eye space
};

// plain data only: compared with memcmp
struct FixedState
{
    unsigned caps;
    GLenum blendSrc, blendDst;
    GLenum depthFunc;
    int depthMask;
    GLenum cullFace, frontFace;
    float polyFactor, polyUnits;
    float lineWidth;
    GLenum alphaFunc;
    float alphaRef;
    GLenum fogMode;
    float fogDensity, fogStart, fogEnd;
    float fogColor[4];
    Light lights[2];
    float matAmbient[4], matDiffuse[4], matSpecular[4], matEmission[4];
    float matShininess;
    float globalAmbient[4];
    int perPixel; // sr_GLES2PerPixelLighting: lit draws use the PIXEL program variant
    float glow;   // sr_GLES2SetGlow: unlit draws that belong in the bloom mask, and how brightly
    GLuint texture;
    Mat4 matrix[3];
};

void SetVec4(float* v, float a, float b, float c, float d)
{
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
}

void InitState(FixedState& s)
{
    memset(&s, 0, sizeof(s));
    s.caps = CAP_DITHER;
    s.blendSrc = GL_ONE;
    s.blendDst = GL_ZERO;
    s.depthFunc = GL_LESS;
    s.depthMask = 1;
    s.cullFace = GL_BACK;
    s.frontFace = GL_CCW;
    s.lineWidth = 1;
    s.alphaFunc = GL_ALWAYS;
    s.fogMode = GL_EXP;
    s.fogDensity = 1;
    s.fogEnd = 1;
    for (int i = 0; i < 2; ++i)
    {
        SetVec4(s.lights[i].ambient, 0, 0, 0, 1);
        float d = i == 0 ? 1.f : 0.f;
        SetVec4(s.lights[i].diffuse, d, d, d, 1);
        SetVec4(s.lights[i].specular, d, d, d, 1);
        SetVec4(s.lights[i].position, 0, 0, 1, 0);
    }
    SetVec4(s.matAmbient, .2f, .2f, .2f, 1);
    SetVec4(s.matDiffuse, .8f, .8f, .8f, 1);
    SetVec4(s.matSpecular, 0, 0, 0, 1);
    SetVec4(s.matEmission, 0, 0, 0, 1);
    SetVec4(s.globalAmbient, .2f, .2f, .2f, 1);
    for (int i = 0; i < 3; ++i)
        s.matrix[i] = Identity();
}

// --- vertex format ---------------------------------------------------------

struct Vertex
{
    float pos[4];
    float tex[4];
    float nrm[3];
    unsigned char col[4];
};

enum
{
    ATTR_POS = 0,
    ATTR_COL = 1,
    ATTR_TEX = 2,
    ATTR_NRM = 3
};

// --- shader programs ---------------------------------------------------------

enum
{
    PROG_TEX = 1,
    PROG_LIGHT = 2,
    PROG_FOG = 4,
    PROG_ALPHA = 8,
    PROG_PIXEL = 16, // only ever combined with PROG_LIGHT
    PROG_EMIT = 32,  // the bloom pass: the emissive term of a PIXEL draw, or a glowing unlit one
    PROG_COUNT = 64
};

struct Program
{
    GLuint id;
    GLint uMvp, uTexMat, uMv, uLightPos, uLightDiff, uLightSpec, uAmbient, uShininess, uEmission, uGlow, uFog,
        uFogColor, uAlphaRef, uPointSize;
    Mat4 lastMvp, lastTexMat;
    bool haveLastMvp, haveLastTexMat;
    // the values last sent to the other uniforms UseState sets (UNI_*), so an unchanged one is
    // not sent again: a WebGL call costs a few microseconds of JavaScript and validation
    std::vector<float> lastUniform[7];
};

// the slots of Program::lastUniform
enum { UNI_MV, UNI_GLOW, UNI_EMISSION, UNI_LIGHTS, UNI_AMBIENT_SHININESS, UNI_FOG, UNI_ALPHA_REF };

// the two light model of GL_LIGHT0/1: ambient (with the emission folded in by
// Draw() on the per vertex path), diffuse and specular of a directional or
// positional light on an eye space normal and position; shininess 0 gives the
// full specular color wherever the light hits, as GL does. Evaluated in the
// vertex shader (LIGHT) or in the fragment shader (LIGHT + PIXEL).
#define AA_GLES2_LIGHT_GLSL                                                                                            \
    "uniform vec4 u_lightPos[2];\n"                                                                                   \
    "uniform vec4 u_lightDiff[2];\n"                                                                                  \
    "uniform vec4 u_lightSpec[2];\n"                                                                                  \
    "uniform vec4 u_ambient;\n"                                                                                       \
    "uniform float u_shininess;\n"                                                                                    \
    "vec3 Light(vec3 n, vec3 eye, vec4 pos, vec4 diff, vec4 spec)\n"                                                 \
    "{\n"                                                                                                             \
    "    vec3 l = pos.w == 0.0 ? normalize(pos.xyz) : normalize(pos.xyz - eye);\n"                                  \
    "    float ndl = max(dot(n, l), 0.0);\n"                                                                          \
    "    vec3 c = ndl * diff.rgb;\n"                                                                                  \
    "    if (ndl > 0.0)\n"                                                                                            \
    "    {\n"                                                                                                         \
    "        vec3 h = normalize(l + vec3(0.0, 0.0, 1.0));\n"                                                          \
    "        float ndh = max(dot(n, h), 0.0);\n"                                                                      \
    "        c += (u_shininess > 0.0 ? pow(ndh, u_shininess) : 1.0) * spec.rgb;\n"                                   \
    "    }\n"                                                                                                         \
    "    return c;\n"                                                                                                 \
    "}\n"                                                                                                             \
    "vec3 Lit(vec3 n, vec3 eye)\n"                                                                                    \
    "{\n"                                                                                                             \
    "    vec3 c = u_ambient.rgb;\n"                                                                                   \
    "    c += Light(n, eye, u_lightPos[0], u_lightDiff[0], u_lightSpec[0]);\n"                                        \
    "    c += Light(n, eye, u_lightPos[1], u_lightDiff[1], u_lightSpec[1]);\n"                                        \
    "    return clamp(c, 0.0, 1.0);\n"                                                                                \
    "}\n"

char const* const s_vertexShader =
    "attribute vec4 a_pos;\n"
    "attribute vec4 a_col;\n"
    "attribute vec4 a_tex;\n"
    "attribute vec3 a_nrm;\n"
    "uniform mat4 u_mvp;\n"
    "uniform float u_pointSize;\n"
    "varying vec4 v_col;\n"
    "#ifdef TEX\n"
    "uniform mat4 u_texmat;\n"
    "varying vec4 v_tex;\n"
    "#endif\n"
    "#ifdef LIGHT\n"
    "uniform mat4 u_mv;\n"
    "#endif\n"
    "#ifdef LIGHT\n"
    "#ifdef PIXEL\n"
    "varying vec3 v_nrm;\n" // eye space, not normalized
    "varying vec3 v_eye;\n"
    "#else\n" AA_GLES2_LIGHT_GLSL
    "#endif\n"
    "#endif\n"
    "#ifdef FOG\n"
    "uniform vec4 u_fog;\n" // mode, density, start, end
    "varying float v_fog;\n"
    "#endif\n"
    "void main()\n"
    "{\n"
    "    gl_Position = u_mvp * a_pos;\n"
    "    gl_PointSize = u_pointSize;\n"
    "#ifdef TEX\n"
    "    v_tex = u_texmat * a_tex;\n"
    "#endif\n"
    "#ifdef LIGHT\n"
    "    vec4 eye = u_mv * a_pos;\n"
    "#endif\n"
    "#ifdef LIGHT\n"
    "    vec3 n = mat3(u_mv[0].xyz, u_mv[1].xyz, u_mv[2].xyz) * a_nrm;\n"
    "#ifdef PIXEL\n"
    "    v_nrm = n;\n"
    "    v_eye = eye.xyz;\n"
    "    v_col = a_col;\n" // unused: the fragment shader shades
    "#else\n"
    "    v_col = vec4(Lit(normalize(n), eye.xyz), u_ambient.a);\n"
    "#endif\n"
    "#else\n"
    "    v_col = a_col;\n"
    "#endif\n"
    "#ifdef FOG\n"
    // The fog distance comes from the clip w, not from the modelview: eCamera
    // puts the whole view transform (gluLookAt + the translation) into the
    // PROJECTION matrix and leaves the modelview identity, so u_mv * a_pos is
    // the world position, whose z is the height above the floor. |clip.w| is
    // the distance along the view axis whichever matrix the view sits in.
    // Dividing by the vertex' own w keeps it right for the infinite floor and
    // sky planes (eDisplay.cpp), which pass vertices at infinity, w == 0: they
    // land beyond the fog's end and fade into it the way a horizon should.
    "    float z = abs(gl_Position.w) / max(abs(a_pos.w), 1e-6);\n"
    "    float f;\n"
    "    if (u_fog.x < 0.5) f = (u_fog.w - z) / (u_fog.w - u_fog.z);\n"
    "    else if (u_fog.x < 1.5) f = exp(-u_fog.y * z);\n"
    "    else f = exp(-u_fog.y * u_fog.y * z * z);\n"
    "    v_fog = clamp(f, 0.0, 1.0);\n"
    "#endif\n"
    "}\n";

char const* const s_fragmentShader =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying vec4 v_col;\n"
    "#ifdef TEX\n"
    "uniform sampler2D u_tex;\n"
    "varying vec4 v_tex;\n"
    "#endif\n"
    "#ifdef FOG\n"
    "uniform vec4 u_fogColor;\n"
    "varying float v_fog;\n"
    "#endif\n"
    "#ifdef ALPHA\n"
    "uniform float u_alphaRef;\n"
    "#endif\n"
    "#ifdef PIXEL\n"
    "varying vec3 v_nrm;\n"
    "varying vec3 v_eye;\n"
    "uniform vec4 u_emission;\n" AA_GLES2_LIGHT_GLSL
    "#endif\n"
    "#if defined(EMIT) && !defined(PIXEL)\n"
    "uniform float u_glow;\n" // the unlit mask: the surface as drawn, scaled
    "#endif\n"
    "void main()\n"
    "{\n"
    "#ifdef PIXEL\n"
    // the interpolated normal shrinks across a crease and can vanish; a
    // vanished one faces the viewer rather than turning into NaN
    "    float len = length(v_nrm);\n"
    "    vec3 n = len > 0.0 ? v_nrm / len : vec3(0.0, 0.0, 1.0);\n"
    "    vec3 lit = Lit(n, v_eye);\n"
    "#ifdef EMIT\n"
    // the bloom pass: only what the emission added above, so the mask is the
    // glow itself and fades where the light already carries the surface
    "    vec4 c = vec4(u_emission.rgb * (1.0 - lit), u_ambient.a);\n"
    "#else\n"
    // emission as the self lit fraction of the texel: screened over the light
    // so the result never exceeds the texel itself (full player color at most)
    "    lit += u_emission.rgb * (1.0 - lit);\n"
    "    vec4 c = vec4(lit, u_ambient.a);\n"
    "#endif\n"
    "#else\n"
    "    vec4 c = v_col;\n"
    "#endif\n"
    "#ifdef TEX\n"
    "    c *= texture2DProj(u_tex, v_tex);\n"
    "#endif\n"
    "#ifdef ALPHA\n"
    "    if (c.a <= u_alphaRef) discard;\n"
    "#endif\n"
    "#if defined(EMIT) && !defined(PIXEL)\n"
    // an unlit glowing surface (the trail walls) contributes its own colour
    "    c.rgb *= u_glow;\n"
    "#endif\n"
    "#ifdef FOG\n"
    "#ifdef EMIT\n"
    // the glow fades with the distance instead of picking up the fog color
    "    c.rgb *= v_fog;\n"
    "#else\n"
    "    c.rgb = mix(u_fogColor.rgb, c.rgb, v_fog);\n"
    "#endif\n"
    "#endif\n"
    "    gl_FragColor = c;\n"
    "}\n";

// --- the layer's state --------------------------------------------------------

FixedState s_state;   // what the game set
FixedState s_real;    // what the context has (only the real GLES2 parts are kept current)
bool s_realDirty = false; // s_real is stale (the bloom pass touched the context): re-apply everything
int s_viewport[4] = {0, 0, 0, 0}; // the last glViewport, restored after the bloom pass
bool s_stateInit = false;
unsigned s_stateVersion = 1; // bumped on every change of s_state

MatrixStack s_stacks[3];
int s_matrixMode = MAT_MODELVIEW;

float s_color[4] = {1, 1, 1, 1};
float s_texCoord[4] = {0, 0, 0, 1};
float s_normal[3] = {0, 0, 1};

// the batch
std::vector<Vertex> s_verts;
std::vector<uint16_t> s_idx;
GLenum s_batchMode = GL_TRIANGLES; // GL_TRIANGLES, GL_LINES or GL_POINTS
FixedState s_batchState;
unsigned s_batchVersion = 0;
unsigned const MAX_BATCH_VERTICES = 65000;

// the open glBegin block
bool s_inBegin = false;
GLenum s_beginMode = 0;
unsigned s_primFirst = 0;  // index of the block's first vertex (fan center, loop start)
unsigned s_primCount = 0;  // vertices of the block in the batch
unsigned s_stripTris = 0;  // triangles emitted from a strip, for the winding parity

// client arrays
struct ClientArray
{
    bool enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    unsigned char const* pointer;
};
ClientArray s_vertexArray, s_normalArray, s_texCoordArray, s_colorArray;

// GLES2 side
bool s_init = false;
int s_contextVersion = 0;
GLuint s_vbo = 0, s_ibo = 0;
Program s_programs[PROG_COUNT];
Program* s_currentProgram = 0;
int s_programCount = 0;

rGLES2Stats s_stats, s_lastStats;

void MarkStateChange()
{
    ++s_stateVersion;
}

void EnsureState()
{
    if (!s_stateInit)
    {
        s_stateInit = true;
        InitState(s_state);
        InitState(s_real);
        for (int i = 0; i < 3; ++i)
            s_stacks[i].top = Identity();
    }
}

// --- program creation ---------------------------------------------------------

GLuint CompileShader(GLenum type, char const* defines, char const* body)
{
    GLuint shader = emscripten_glCreateShader(type);
    char const* sources[2] = {defines, body};
    emscripten_glShaderSource(shader, 2, sources, 0);
    emscripten_glCompileShader(shader);
    GLint ok = 0;
    emscripten_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        GLsizei len = 0;
        emscripten_glGetShaderInfoLog(shader, sizeof(log), &len, log);
        printf("rGLES2: shader compile error (%s): %.*s\n", defines, len, log);
    }
    return shader;
}

Program* GetProgram(unsigned key)
{
    Program& p = s_programs[key];
    if (p.id)
        return &p;

    char defines[256];
    snprintf(defines, sizeof(defines), "%s%s%s%s%s%s", (key & PROG_TEX) ? "#define TEX\n" : "",
             (key & PROG_LIGHT) ? "#define LIGHT\n" : "", (key & PROG_FOG) ? "#define FOG\n" : "",
             (key & PROG_ALPHA) ? "#define ALPHA\n" : "", (key & PROG_PIXEL) ? "#define PIXEL\n" : "",
             (key & PROG_EMIT) ? "#define EMIT\n" : "");

    GLuint vs = CompileShader(GL_VERTEX_SHADER, defines, s_vertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, defines, s_fragmentShader);
    p.id = emscripten_glCreateProgram();
    emscripten_glAttachShader(p.id, vs);
    emscripten_glAttachShader(p.id, fs);
    emscripten_glBindAttribLocation(p.id, ATTR_POS, "a_pos");
    emscripten_glBindAttribLocation(p.id, ATTR_COL, "a_col");
    emscripten_glBindAttribLocation(p.id, ATTR_TEX, "a_tex");
    emscripten_glBindAttribLocation(p.id, ATTR_NRM, "a_nrm");
    emscripten_glLinkProgram(p.id);
    GLint ok = 0;
    emscripten_glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        GLsizei len = 0;
        emscripten_glGetProgramInfoLog(p.id, sizeof(log), &len, log);
        printf("rGLES2: program link error (%s): %.*s\n", defines, len, log);
    }
    emscripten_glDeleteShader(vs);
    emscripten_glDeleteShader(fs);

    p.uMvp = emscripten_glGetUniformLocation(p.id, "u_mvp");
    p.uTexMat = emscripten_glGetUniformLocation(p.id, "u_texmat");
    p.uMv = emscripten_glGetUniformLocation(p.id, "u_mv");
    p.uEmission = emscripten_glGetUniformLocation(p.id, "u_emission");
    p.uGlow = emscripten_glGetUniformLocation(p.id, "u_glow");
    p.uLightPos = emscripten_glGetUniformLocation(p.id, "u_lightPos");
    p.uLightDiff = emscripten_glGetUniformLocation(p.id, "u_lightDiff");
    p.uLightSpec = emscripten_glGetUniformLocation(p.id, "u_lightSpec");
    p.uAmbient = emscripten_glGetUniformLocation(p.id, "u_ambient");
    p.uShininess = emscripten_glGetUniformLocation(p.id, "u_shininess");
    p.uFog = emscripten_glGetUniformLocation(p.id, "u_fog");
    p.uFogColor = emscripten_glGetUniformLocation(p.id, "u_fogColor");
    p.uAlphaRef = emscripten_glGetUniformLocation(p.id, "u_alphaRef");
    p.uPointSize = emscripten_glGetUniformLocation(p.id, "u_pointSize");
    p.haveLastMvp = p.haveLastTexMat = false;
    for (std::vector<float>& last : p.lastUniform)
        last.clear();

    emscripten_glUseProgram(p.id);
    GLint sampler = emscripten_glGetUniformLocation(p.id, "u_tex");
    if (sampler >= 0)
        emscripten_glUniform1i(sampler, 0);
    if (p.uPointSize >= 0)
        emscripten_glUniform1f(p.uPointSize, 1.0f);
    s_currentProgram = &p;
    ++s_stats.programSwitches;
    ++s_programCount;
    printf("rGLES2: program %d compiled (%s%s%s%s%s%s)\n", s_programCount,
           (key & PROG_TEX) ? "TEX " : "", (key & PROG_LIGHT) ? "LIGHT " : "", (key & PROG_PIXEL) ? "PIXEL " : "",
           (key & PROG_FOG) ? "FOG " : "", (key & PROG_ALPHA) ? "ALPHA " : "", (key & PROG_EMIT) ? "EMIT" : "");
    return &p;
}

void EnsureInit()
{
    if (s_init)
        return;
    s_init = true;
    EnsureState();

    char const* version = reinterpret_cast<char const*>(emscripten_glGetString(GL_VERSION));
    s_contextVersion = (version && strstr(version, "WebGL 2")) ? 2 : 1;
    printf("rGLES2: context %s\n", version ? version : "?");

    emscripten_glGenBuffers(1, &s_vbo);
    emscripten_glGenBuffers(1, &s_ibo);
    emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    GLsizei const stride = sizeof(Vertex);
    emscripten_glVertexAttribPointer(ATTR_POS, 4, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, pos));
    emscripten_glVertexAttribPointer(ATTR_TEX, 4, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, tex));
    emscripten_glVertexAttribPointer(ATTR_NRM, 3, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, nrm));
    emscripten_glVertexAttribPointer(ATTR_COL, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void const*)offsetof(Vertex, col));
    emscripten_glEnableVertexAttribArray(ATTR_POS);
    emscripten_glEnableVertexAttribArray(ATTR_TEX);
    emscripten_glEnableVertexAttribArray(ATTR_NRM);
    emscripten_glEnableVertexAttribArray(ATTR_COL);
    emscripten_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    s_verts.reserve(4096);
    s_idx.reserve(8192);
}

// --- applying state to the context -------------------------------------------

void ApplyRealCap(unsigned cap, GLenum glcap, unsigned wanted)
{
    if ((wanted & cap) != (s_real.caps & cap))
    {
        if (wanted & cap)
            emscripten_glEnable(glcap);
        else
            emscripten_glDisable(glcap);
        s_real.caps = (s_real.caps & ~cap) | (wanted & cap);
    }
}

void ApplyState(FixedState const& s)
{
    if (s_realDirty)
    {
        // the post processing passes set their own state: make every
        // comparison below mismatch so the whole state is written again
        s_realDirty = false;
        s_real.caps = ~s.caps;
        s_real.blendSrc = s_real.blendDst = 0;
        s_real.depthFunc = 0;
        s_real.depthMask = -1;
        s_real.cullFace = s_real.frontFace = 0;
        s_real.polyFactor = s_real.polyUnits = -1e30f;
        s_real.lineWidth = -1;
        s_real.texture = ~0u;
    }
    ApplyRealCap(CAP_BLEND, GL_BLEND, s.caps);
    ApplyRealCap(CAP_DEPTH_TEST, GL_DEPTH_TEST, s.caps);
    ApplyRealCap(CAP_CULL_FACE, GL_CULL_FACE, s.caps);
    ApplyRealCap(CAP_POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_FILL, s.caps);
    ApplyRealCap(CAP_DITHER, GL_DITHER, s.caps);
    if (s.blendSrc != s_real.blendSrc || s.blendDst != s_real.blendDst)
    {
        emscripten_glBlendFunc(s.blendSrc, s.blendDst);
        s_real.blendSrc = s.blendSrc;
        s_real.blendDst = s.blendDst;
    }
    if (s.depthFunc != s_real.depthFunc)
    {
        emscripten_glDepthFunc(s.depthFunc);
        s_real.depthFunc = s.depthFunc;
    }
    if (s.depthMask != s_real.depthMask)
    {
        emscripten_glDepthMask(s.depthMask ? GL_TRUE : GL_FALSE);
        s_real.depthMask = s.depthMask;
    }
    if (s.cullFace != s_real.cullFace)
    {
        emscripten_glCullFace(s.cullFace);
        s_real.cullFace = s.cullFace;
    }
    if (s.frontFace != s_real.frontFace)
    {
        emscripten_glFrontFace(s.frontFace);
        s_real.frontFace = s.frontFace;
    }
    if (s.polyFactor != s_real.polyFactor || s.polyUnits != s_real.polyUnits)
    {
        emscripten_glPolygonOffset(s.polyFactor, s.polyUnits);
        s_real.polyFactor = s.polyFactor;
        s_real.polyUnits = s.polyUnits;
    }
    if (s.lineWidth != s_real.lineWidth)
    {
        emscripten_glLineWidth(s.lineWidth);
        s_real.lineWidth = s.lineWidth;
    }
    if ((s.caps & CAP_TEXTURE_2D) && s.texture != s_real.texture)
    {
        emscripten_glBindTexture(GL_TEXTURE_2D, s.texture);
        s_real.texture = s.texture;
        ++s_stats.textureBinds;
    }
}

void MulVec4(float const* a, float const* b, float* out)
{
    for (int i = 0; i < 4; ++i)
        out[i] = a[i] * b[i];
}

// display lists, compiled into static buffers (see glNewList below)
struct ListBatch
{
    FixedState state;
    Mat4 modelview; // relative to the modelview the list was compiled under
    GLenum mode;
    unsigned firstVertex, vertexCount, firstIndex, indexCount;
};
struct DisplayList
{
    std::vector<ListBatch> batches;
    std::vector<Vertex> verts;
    std::vector<uint16_t> idx;
    GLuint vbo, ibo;
    FixedState endState; // what executing the list leaves behind
    bool emissive;       // a batch feeds the bloom: keep the vertices on the CPU too
};
std::map<GLuint, DisplayList> s_lists;
GLuint s_nextList = 1;
DisplayList* s_recording = 0;
Mat4 s_recordBaseInverse;
FixedState s_recordSaved;

void RecordBatch();

unsigned ProgramKeyFor(FixedState const& s)
{
    unsigned key = 0;
    if ((s.caps & CAP_TEXTURE_2D) && s.texture != 0)
        key |= PROG_TEX;
    if (s.caps & CAP_LIGHTING)
        key |= s.perPixel ? PROG_LIGHT | PROG_PIXEL : PROG_LIGHT;
    if (s.caps & CAP_FOG)
        key |= PROG_FOG;
    if ((s.caps & CAP_ALPHA_TEST) && s.alphaFunc != GL_ALWAYS)
        key |= PROG_ALPHA;
    return key;
}

// the last perspective projection a draw used (the 3D view; the depth of field
// turns depth into distance with it)
Mat4 s_lastPerspective;
bool s_haveLastPerspective = false;

// the program and the context state (uniforms included) for a draw in state s
// has this uniform slot of the program changed since it was last sent? (remembers the new values)
bool UniformChanged(Program* p, int slot, float const* v, size_t n)
{
    std::vector<float>& last = p->lastUniform[slot];
    if (last.size() == n && memcmp(&last[0], v, n * sizeof(float)) == 0)
        return false;
    last.assign(v, v + n);
    return true;
}

// glUniform*fv (width 1, 2, 4 floats, or 16: a matrix) unless the program already holds these values
void CachedUniform(Program* p, int slot, GLint location, int width, int count, float const* v)
{
    if (location < 0 || !UniformChanged(p, slot, v, size_t(width) * count))
        return;
    switch (width)
    {
    case 1:
        emscripten_glUniform1fv(location, count, v);
        break;
    case 2:
        emscripten_glUniform2fv(location, count, v);
        break;
    case 4:
        emscripten_glUniform4fv(location, count, v);
        break;
    default:
        emscripten_glUniformMatrix4fv(location, count, GL_FALSE, v);
        break;
    }
}

void UseState(FixedState const& s, unsigned key)
{
    EnsureInit();
    float const* pm = s.matrix[MAT_PROJECTION].m;
    if (fabsf(pm[3]) + fabsf(pm[7]) + fabsf(pm[11]) > 1e-6f)
    {
        s_lastPerspective = s.matrix[MAT_PROJECTION];
        s_haveLastPerspective = true;
    }

    Program* p = GetProgram(key);
    if (p != s_currentProgram)
    {
        emscripten_glUseProgram(p->id);
        s_currentProgram = p;
        ++s_stats.programSwitches;
    }

    ApplyState(s);

    Mat4 mvp = Mul(s.matrix[MAT_PROJECTION], s.matrix[MAT_MODELVIEW]);
    if (!p->haveLastMvp || memcmp(&mvp, &p->lastMvp, sizeof(Mat4)))
    {
        emscripten_glUniformMatrix4fv(p->uMvp, 1, GL_FALSE, mvp.m);
        p->lastMvp = mvp;
        p->haveLastMvp = true;
    }
    if (key & PROG_TEX)
    {
        if (!p->haveLastTexMat || memcmp(&s.matrix[MAT_TEXTURE], &p->lastTexMat, sizeof(Mat4)))
        {
            emscripten_glUniformMatrix4fv(p->uTexMat, 1, GL_FALSE, s.matrix[MAT_TEXTURE].m);
            p->lastTexMat = s.matrix[MAT_TEXTURE];
            p->haveLastTexMat = true;
        }
    }
    if (key & PROG_LIGHT)
        CachedUniform(p, UNI_MV, p->uMv, 16, 1, s.matrix[MAT_MODELVIEW].m);
    if ((key & PROG_EMIT) && !(key & PROG_PIXEL))
        CachedUniform(p, UNI_GLOW, p->uGlow, 1, 1, &s.glow);
    if (key & PROG_LIGHT)
    {
        float pos[8], diff[8], spec[8], ambient[4];
        // per vertex: the emission is part of the ambient term as in GL;
        // per pixel: the fragment shader applies it after the lighting
        for (int i = 0; i < 4; ++i)
            ambient[i] = (s.perPixel ? 0 : s.matEmission[i]) + s.globalAmbient[i] * s.matAmbient[i];
        if (key & PROG_PIXEL)
            CachedUniform(p, UNI_EMISSION, p->uEmission, 4, 1, s.matEmission);
        for (int l = 0; l < 2; ++l)
        {
            bool on = (s.caps & (l == 0 ? CAP_LIGHT0 : CAP_LIGHT1)) != 0;
            memcpy(pos + 4 * l, s.lights[l].position, sizeof(float) * 4);
            if (on)
            {
                MulVec4(s.lights[l].diffuse, s.matDiffuse, diff + 4 * l);
                MulVec4(s.lights[l].specular, s.matSpecular, spec + 4 * l);
                for (int i = 0; i < 3; ++i)
                    ambient[i] += s.lights[l].ambient[i] * s.matAmbient[i];
            }
            else
            {
                SetVec4(diff + 4 * l, 0, 0, 0, 0);
                SetVec4(spec + 4 * l, 0, 0, 0, 0);
            }
        }
        ambient[3] = s.matDiffuse[3];
        // the three light arrays change together (a light or a material): one comparison for all
        float lights[24];
        memcpy(lights, pos, sizeof(pos));
        memcpy(lights + 8, diff, sizeof(diff));
        memcpy(lights + 16, spec, sizeof(spec));
        if (UniformChanged(p, UNI_LIGHTS, lights, 24))
        {
            emscripten_glUniform4fv(p->uLightPos, 2, pos);
            emscripten_glUniform4fv(p->uLightDiff, 2, diff);
            emscripten_glUniform4fv(p->uLightSpec, 2, spec);
        }
        float const ambientShininess[5] = {ambient[0], ambient[1], ambient[2], ambient[3], s.matShininess};
        if (UniformChanged(p, UNI_AMBIENT_SHININESS, ambientShininess, 5))
        {
            emscripten_glUniform4fv(p->uAmbient, 1, ambient);
            emscripten_glUniform1f(p->uShininess, s.matShininess);
        }
    }
    if (key & PROG_FOG)
    {
        float fog[4] = {s.fogMode == GL_LINEAR ? 0.f : (s.fogMode == GL_EXP ? 1.f : 2.f), s.fogDensity, s.fogStart,
                        s.fogEnd};
        float fogBoth[8];
        memcpy(fogBoth, fog, sizeof(fog));
        memcpy(fogBoth + 4, s.fogColor, sizeof(float) * 4);
        if (UniformChanged(p, UNI_FOG, fogBoth, 8))
        {
            emscripten_glUniform4fv(p->uFog, 1, fog);
            emscripten_glUniform4fv(p->uFogColor, 1, s.fogColor);
        }
    }
    if (key & PROG_ALPHA)
    {
        if (s.alphaFunc != GL_GREATER)
            LogOnce("alpha test function other than GL_GREATER treated as GL_GREATER");
        CachedUniform(p, UNI_ALPHA_REF, p->uAlphaRef, 1, 1, &s.alphaRef);
    }
}

// one GLES2 draw of the given geometry in the given state; the batch path and
// the bloom pass (which replays a remembered batch with PROG_EMIT added) share it
void DrawGeometry(FixedState const& s, unsigned key, GLenum mode, Vertex const* verts, size_t vertexCount,
                  uint16_t const* idx, size_t idxCount)
{
    UseState(s, key);
    emscripten_glBufferData(GL_ARRAY_BUFFER, vertexCount * sizeof(Vertex), verts, GL_STREAM_DRAW);
    emscripten_glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxCount * sizeof(uint16_t), idx, GL_STREAM_DRAW);
    emscripten_glDrawElements(mode, idxCount, GL_UNSIGNED_SHORT, 0);

    ++s_stats.draws;
    s_stats.vertices += vertexCount;
}

void RecordEmissive(FixedState const& s, unsigned key, GLenum mode, Vertex const* verts, size_t vertexCount,
                    uint16_t const* idx, size_t idxCount);

// the inverse of an affine or projective 4x4 matrix (identity if singular)
Mat4 Inverse(Mat4 const& a)
{
    float const* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0)
        return Identity();
    Mat4 r;
    for (int i = 0; i < 16; ++i)
        r.m[i] = inv[i] / det;
    return r;
}

// a batch drawn while a list is being compiled goes into the list instead
void RecordBatch()
{
    DisplayList& l = *s_recording;
    ListBatch b;
    b.state = s_batchState;
    b.modelview = Mul(s_recordBaseInverse, s_batchState.matrix[MAT_MODELVIEW]);
    b.mode = s_batchMode;
    b.firstVertex = l.verts.size();
    b.vertexCount = s_verts.size();
    b.firstIndex = l.idx.size();
    b.indexCount = s_idx.size();
    l.verts.insert(l.verts.end(), s_verts.begin(), s_verts.end());
    l.idx.insert(l.idx.end(), s_idx.begin(), s_idx.end());
    l.batches.push_back(b);
}

void SetVertexPointers(unsigned firstVertex)
{
    GLsizei const stride = sizeof(Vertex);
    size_t const base = size_t(firstVertex) * stride;
    emscripten_glVertexAttribPointer(ATTR_POS, 4, GL_FLOAT, GL_FALSE, stride, (void const*)(base + offsetof(Vertex, pos)));
    emscripten_glVertexAttribPointer(ATTR_TEX, 4, GL_FLOAT, GL_FALSE, stride, (void const*)(base + offsetof(Vertex, tex)));
    emscripten_glVertexAttribPointer(ATTR_NRM, 3, GL_FLOAT, GL_FALSE, stride, (void const*)(base + offsetof(Vertex, nrm)));
    emscripten_glVertexAttribPointer(ATTR_COL, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void const*)(base + offsetof(Vertex, col)));
}

void Draw()
{
    if (s_recording)
    {
        RecordBatch();
        s_verts.clear();
        s_idx.clear();
        return;
    }
    unsigned const key = ProgramKeyFor(s_batchState);
    RecordEmissive(s_batchState, key, s_batchMode, &s_verts[0], s_verts.size(), &s_idx[0], s_idx.size());
    DrawGeometry(s_batchState, key, s_batchMode, &s_verts[0], s_verts.size(), &s_idx[0], s_idx.size());
    s_verts.clear();
    s_idx.clear();
}

// draws the batch; the vertices the open primitive still needs stay
void Flush()
{
    ++s_stats.flushes;
    if (s_idx.empty())
        return; // nothing referenced yet; pending vertices stay for the next batch

    unsigned keepTail = 0, keepFirst = 0;
    if (s_inBegin && s_primCount > 0)
    {
        switch (s_beginMode)
        {
        case GL_TRIANGLES:
            keepTail = s_primCount % 3;
            break;
        case GL_QUADS:
            keepTail = s_primCount % 4;
            break;
        case GL_LINES:
            keepTail = s_primCount % 2;
            break;
        case GL_POINTS:
            keepTail = 0;
            break;
        case GL_LINE_STRIP:
            keepTail = 1;
            break;
        case GL_TRIANGLE_STRIP:
        case GL_QUAD_STRIP:
            keepTail = s_primCount < 2 ? s_primCount : 2;
            break;
        case GL_TRIANGLE_FAN:
        case GL_POLYGON:
        case GL_LINE_LOOP:
            keepFirst = 1;
            keepTail = s_primCount < 2 ? 0 : 1;
            break;
        }
    }

    std::vector<Vertex> carry;
    if (keepFirst)
        carry.push_back(s_verts[s_primFirst]);
    for (unsigned i = keepTail; i > 0; --i)
        carry.push_back(s_verts[s_verts.size() - i]);

    Draw();

    s_verts.assign(carry.begin(), carry.end());
    s_primFirst = 0;
    s_primCount = carry.size();
}

// --- selective bloom ----------------------------------------------------------
//
// The mask is not a threshold over the finished frame: only draws that declare
// GL_EMISSION under per pixel lighting (gCycle's CYCLE_LIGHTING trim, nothing
// else in the game) are remembered here, geometry and state, while the frame is
// drawn. At the end of the frame the owner of the framebuffer calls
// sr_GLES2Bloom, and that geometry is drawn a second time into an emission
// buffer with the EMIT program variant (the emissive term alone, textured and
// alpha tested exactly as in the frame) against the finished depth buffer with
// GL_LEQUAL and no depth writes: what a wall hid stays hidden. The buffer is
// then reduced to a quarter, blurred separably and added to the frame.
// Everything unlit - walls, floor, trails, HUD, console text - is never
// recorded and cannot glow.

// generous since BROWSER_WALL_GLOW put the trail walls in the mask: a busy
// six cycle frame submits ~40000 vertices in all, most of them wall, and a
// mask that silently stops recording mid frame would make part of a trail stop
// glowing. The recorded vectors are reused frame to frame, so this is a
// ceiling on steady memory (~6 MB), not an allocation per frame.
unsigned const MAX_BLOOM_BATCHES = 256;
unsigned const MAX_BLOOM_VERTICES = 120000;

struct BloomBatch
{
    FixedState state;
    unsigned key;
    GLenum mode;
    std::vector<Vertex> verts;
    std::vector<uint16_t> idx;
    // a display list's batch is drawn from the list's static buffers instead (listVbo != 0):
    // its vertices are already on the GPU, so nothing is copied or uploaded per frame
    GLuint listVbo, listIbo;
    unsigned firstVertex, vertexCount, firstIndex, indexCount;
};

struct BloomState
{
    bool on;
    float intensity;
    bool failed; // a buffer or program did not come up: stay out of the way

    std::vector<BloomBatch> batches;
    unsigned count;    // used entries of batches
    unsigned vertices; // vertices recorded this frame

    GLuint emitFbo, emitTex;
    GLuint blurFbo[2], blurTex[2];
    GLuint wideFbo[2], wideTex[2]; // the second, wider level at a sixteenth (BROWSER_BLOOM_WIDE)
    GLuint depth; // the depth renderbuffer the emission pass tests against
    int width, height, qWidth, qHeight, sWidth, sHeight;
    GLuint quad;
    float wide;  // weight of the wide level, 0 = not drawn
    float spill; // strong glow spills into all three channels (BROWSER_BLOOM_SPILL)

    GLuint progDown, progBlur, progAdd;
    GLint uDownTexel, uBlurStep, uAddIntensity, uAddWide, uAddSpill, uAddTex2;

    BloomState()
      : on(false), intensity(.35f), failed(false), count(0), vertices(0), emitFbo(0), emitTex(0), depth(0), width(0),
        height(0), qWidth(0), qHeight(0), sWidth(0), sHeight(0), quad(0), wide(0), spill(0), progDown(0), progBlur(0),
        progAdd(0), uDownTexel(-1), uBlurStep(-1), uAddIntensity(-1), uAddWide(-1), uAddSpill(-1), uAddTex2(-1)
    {
        blurFbo[0] = blurFbo[1] = blurTex[0] = blurTex[1] = 0;
        wideFbo[0] = wideFbo[1] = wideTex[0] = wideTex[1] = 0;
    }
};

BloomState s_bloom;

// does this draw feed the bloom (its emissive part)?
bool BloomWants(FixedState const& s, unsigned key)
{
    if (!s_bloom.on || s_bloom.failed)
        return false;
    if (key & PROG_PIXEL)
        // the lit path: the material's own GL_EMISSION is the mask
        return s.matEmission[0] > 0 || s.matEmission[1] > 0 || s.matEmission[2] > 0;
    // the unlit path: only what sr_GLES2SetGlow declared
    return s.glow > 0;
}

// a display list's emissive batch (glCallList): remembered by where it lies in the list's buffers
void RecordEmissiveList(FixedState const& s, unsigned key, GLenum mode, GLuint vbo, GLuint ibo, unsigned firstVertex,
                        unsigned vertexCount, unsigned firstIndex, unsigned indexCount)
{
    if (!BloomWants(s, key) || s_bloom.count >= MAX_BLOOM_BATCHES)
        return;
    if (s_bloom.batches.size() <= s_bloom.count)
        s_bloom.batches.resize(s_bloom.count + 1);
    BloomBatch& b = s_bloom.batches[s_bloom.count++];
    b.state = s;
    b.key = key | PROG_EMIT;
    b.mode = mode;
    b.verts.clear();
    b.idx.clear();
    b.listVbo = vbo;
    b.listIbo = ibo;
    b.firstVertex = firstVertex;
    b.vertexCount = vertexCount;
    b.firstIndex = firstIndex;
    b.indexCount = indexCount;
}

void RecordEmissive(FixedState const& s, unsigned key, GLenum mode, Vertex const* verts, size_t vertexCount,
                    uint16_t const* idx, size_t idxCount)
{
    if (!s_bloom.on || s_bloom.failed)
        return;
    if (key & PROG_PIXEL)
    {
        // the lit path: the material's own GL_EMISSION is the mask
        if (s.matEmission[0] <= 0 && s.matEmission[1] <= 0 && s.matEmission[2] <= 0)
            return;
    }
    else if (s.glow <= 0)
    {
        // the unlit path: only what sr_GLES2SetGlow declared
        return;
    }
    if (s_bloom.count >= MAX_BLOOM_BATCHES || s_bloom.vertices + vertexCount > MAX_BLOOM_VERTICES)
        return;

    if (s_bloom.batches.size() <= s_bloom.count)
        s_bloom.batches.resize(s_bloom.count + 1);
    BloomBatch& b = s_bloom.batches[s_bloom.count++];
    b.state = s;
    b.key = key | PROG_EMIT;
    b.mode = mode;
    b.listVbo = b.listIbo = 0;
    b.verts.assign(verts, verts + vertexCount);
    b.idx.assign(idx, idx + idxCount);
    s_bloom.vertices += vertexCount;
}

char const* const s_postVertexShader =
    "attribute vec2 a_pos;\n"
    "varying vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    v_uv = a_pos * .5 + .5;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

// full resolution emission to a quarter of it: four bilinear taps are a 4x4 box
char const* const s_downShader =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "varying vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    vec3 c = texture2D(u_tex, v_uv + u_texel * vec2(-1.0, -1.0)).rgb;\n"
    "    c += texture2D(u_tex, v_uv + u_texel * vec2(1.0, -1.0)).rgb;\n"
    "    c += texture2D(u_tex, v_uv + u_texel * vec2(-1.0, 1.0)).rgb;\n"
    "    c += texture2D(u_tex, v_uv + u_texel * vec2(1.0, 1.0)).rgb;\n"
    "    gl_FragColor = vec4(c * .25, 1.0);\n"
    "}\n";

// a nine tap gaussian (sigma 2 quarter resolution pixels, so about eight full
// resolution ones) in five bilinear taps along u_step
char const* const s_blurShader =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_step;\n"
    "varying vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    vec3 c = texture2D(u_tex, v_uv).rgb * .2270270;\n"
    "    vec2 o1 = u_step * 1.3846154;\n"
    "    vec2 o2 = u_step * 3.2307692;\n"
    "    c += (texture2D(u_tex, v_uv + o1).rgb + texture2D(u_tex, v_uv - o1).rgb) * .3162162;\n"
    "    c += (texture2D(u_tex, v_uv + o2).rgb + texture2D(u_tex, v_uv - o2).rgb) * .0702703;\n"
    "    gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

// added to the frame (blend ONE, ONE); alpha stays the frame's. u_tex2 is
// the wide level (weight u_wide, 0 when it was not drawn). Additive blending
// clips per channel, so a strong amber glow over an amber trail tops out at
// yellow and never reaches white; u_spill hands what exceeds a quarter of
// full scale in the brightest channel to all three, the way an overexposed
// light burns out on film.
char const* const s_addShader =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform sampler2D u_tex2;\n"
    "uniform float u_intensity;\n"
    "uniform float u_wide;\n"
    "uniform float u_spill;\n"
    "varying vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    vec3 c = texture2D(u_tex, v_uv).rgb;\n"
    "    if (u_wide > 0.0)\n"
    "        c += texture2D(u_tex2, v_uv).rgb * u_wide;\n"
    "    c *= u_intensity;\n"
    "    float peak = max(c.r, max(c.g, c.b));\n"
    "    c += vec3(u_spill * max(peak - 0.25, 0.0));\n"
    "    gl_FragColor = vec4(c, 0.0);\n"
    "}\n";

GLuint BuildPostProgram(char const* name, char const* fragment)
{
    GLuint vs = CompileShader(GL_VERTEX_SHADER, "", s_postVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, "", fragment);
    GLuint program = emscripten_glCreateProgram();
    emscripten_glAttachShader(program, vs);
    emscripten_glAttachShader(program, fs);
    emscripten_glBindAttribLocation(program, ATTR_POS, "a_pos");
    emscripten_glLinkProgram(program);
    GLint ok = 0;
    emscripten_glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        GLsizei len = 0;
        emscripten_glGetProgramInfoLog(program, sizeof(log), &len, log);
        printf("rGLES2: program link error (bloom %s): %.*s\n", name, len, log);
        emscripten_glDeleteProgram(program);
        program = 0;
    }
    emscripten_glDeleteShader(vs);
    emscripten_glDeleteShader(fs);
    if (program)
    {
        emscripten_glUseProgram(program);
        GLint sampler = emscripten_glGetUniformLocation(program, "u_tex");
        if (sampler >= 0)
            emscripten_glUniform1i(sampler, 0);
        s_currentProgram = 0;
        ++s_programCount;
        printf("rGLES2: program %d compiled (BLOOM %s)\n", s_programCount, name);
    }
    return program;
}

GLuint MakeBloomTexture(int width, int height)
{
    GLuint texture = 0;
    emscripten_glGenTextures(1, &texture);
    emscripten_glBindTexture(GL_TEXTURE_2D, texture);
    emscripten_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

void DeleteBloomTargets()
{
    if (s_bloom.emitFbo)
    {
        emscripten_glDeleteFramebuffers(1, &s_bloom.emitFbo);
        emscripten_glDeleteTextures(1, &s_bloom.emitTex);
        emscripten_glDeleteFramebuffers(2, s_bloom.blurFbo);
        emscripten_glDeleteTextures(2, s_bloom.blurTex);
        emscripten_glDeleteFramebuffers(2, s_bloom.wideFbo);
        emscripten_glDeleteTextures(2, s_bloom.wideTex);
    }
    s_bloom.emitFbo = s_bloom.emitTex = 0;
    s_bloom.blurFbo[0] = s_bloom.blurFbo[1] = s_bloom.blurTex[0] = s_bloom.blurTex[1] = 0;
    s_bloom.wideFbo[0] = s_bloom.wideFbo[1] = s_bloom.wideTex[0] = s_bloom.wideTex[1] = 0;
    s_bloom.width = s_bloom.height = 0;
    s_bloom.depth = 0;
}

bool EnsureBloomTargets(GLuint depth, int width, int height)
{
    if (s_bloom.emitFbo && s_bloom.width == width && s_bloom.height == height && s_bloom.depth == depth)
        return true;
    DeleteBloomTargets();

    s_bloom.width = width;
    s_bloom.height = height;
    s_bloom.qWidth = width / 4 > 0 ? width / 4 : 1;
    s_bloom.qHeight = height / 4 > 0 ? height / 4 : 1;
    s_bloom.depth = depth;

    s_bloom.emitTex = MakeBloomTexture(width, height);
    emscripten_glGenFramebuffers(1, &s_bloom.emitFbo);
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_bloom.emitFbo);
    emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloom.emitTex, 0);
    emscripten_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    bool ok = emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    for (int i = 0; ok && i < 2; ++i)
    {
        s_bloom.blurTex[i] = MakeBloomTexture(s_bloom.qWidth, s_bloom.qHeight);
        emscripten_glGenFramebuffers(1, &s_bloom.blurFbo[i]);
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_bloom.blurFbo[i]);
        emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloom.blurTex[i], 0);
        ok = emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    // the wide level: a sixteenth of the frame, blurred at that scale (sigma ~32 full pixels)
    s_bloom.sWidth = s_bloom.qWidth / 4 > 0 ? s_bloom.qWidth / 4 : 1;
    s_bloom.sHeight = s_bloom.qHeight / 4 > 0 ? s_bloom.qHeight / 4 : 1;
    for (int i = 0; ok && i < 2; ++i)
    {
        s_bloom.wideTex[i] = MakeBloomTexture(s_bloom.sWidth, s_bloom.sHeight);
        emscripten_glGenFramebuffers(1, &s_bloom.wideFbo[i]);
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_bloom.wideFbo[i]);
        emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_bloom.wideTex[i], 0);
        ok = emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    if (!ok)
    {
        printf("rGLES2: bloom buffers incomplete (%dx%d), bloom off.\n", width, height);
        DeleteBloomTargets();
        return false;
    }

    if (!s_bloom.quad)
    {
        float const quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        emscripten_glGenBuffers(1, &s_bloom.quad);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_bloom.quad);
        emscripten_glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    }
    if (!s_bloom.progDown)
    {
        s_bloom.progDown = BuildPostProgram("down", s_downShader);
        s_bloom.progBlur = BuildPostProgram("blur", s_blurShader);
        s_bloom.progAdd = BuildPostProgram("add", s_addShader);
        s_bloom.uDownTexel = emscripten_glGetUniformLocation(s_bloom.progDown, "u_texel");
        s_bloom.uBlurStep = emscripten_glGetUniformLocation(s_bloom.progBlur, "u_step");
        s_bloom.uAddIntensity = emscripten_glGetUniformLocation(s_bloom.progAdd, "u_intensity");
        s_bloom.uAddWide = emscripten_glGetUniformLocation(s_bloom.progAdd, "u_wide");
        s_bloom.uAddSpill = emscripten_glGetUniformLocation(s_bloom.progAdd, "u_spill");
        s_bloom.uAddTex2 = emscripten_glGetUniformLocation(s_bloom.progAdd, "u_tex2");
        if (s_bloom.progAdd)
        {
            emscripten_glUseProgram(s_bloom.progAdd);
            emscripten_glUniform1i(s_bloom.uAddTex2, 1);
        }
    }
    if (!s_bloom.progDown || !s_bloom.progBlur || !s_bloom.progAdd)
    {
        DeleteBloomTargets();
        return false;
    }
    s_realDirty = true; // the texture and program bindings above are not the layer's
    return true;
}

// the post processing passes draw one quad of their own: the layer's vertex
// setup and fixed state are restored by EndPost
void BeginPost()
{
    emscripten_glDisable(GL_DEPTH_TEST);
    emscripten_glDisable(GL_BLEND);
    emscripten_glDisable(GL_CULL_FACE);
    emscripten_glDisable(GL_POLYGON_OFFSET_FILL);
    emscripten_glDisable(GL_DITHER);
    emscripten_glDisableVertexAttribArray(ATTR_COL);
    emscripten_glDisableVertexAttribArray(ATTR_TEX);
    emscripten_glDisableVertexAttribArray(ATTR_NRM);
    emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_bloom.quad);
    emscripten_glVertexAttribPointer(ATTR_POS, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

void EndPost()
{
    emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    GLsizei const stride = sizeof(Vertex);
    emscripten_glVertexAttribPointer(ATTR_POS, 4, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, pos));
    emscripten_glVertexAttribPointer(ATTR_TEX, 4, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, tex));
    emscripten_glVertexAttribPointer(ATTR_NRM, 3, GL_FLOAT, GL_FALSE, stride, (void const*)offsetof(Vertex, nrm));
    emscripten_glVertexAttribPointer(ATTR_COL, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void const*)offsetof(Vertex, col));
    emscripten_glEnableVertexAttribArray(ATTR_COL);
    emscripten_glEnableVertexAttribArray(ATTR_TEX);
    emscripten_glEnableVertexAttribArray(ATTR_NRM);
    emscripten_glViewport(s_viewport[0], s_viewport[1], s_viewport[2], s_viewport[3]);
    s_currentProgram = 0;
    s_realDirty = true;
}

void PostPass(GLuint program, GLuint fbo, GLuint texture, int width, int height)
{
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    emscripten_glViewport(0, 0, width, height);
    emscripten_glUseProgram(program);
    emscripten_glBindTexture(GL_TEXTURE_2D, texture);
    emscripten_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++s_stats.draws;
}

GLenum DrawModeFor(GLenum beginMode)
{
    switch (beginMode)
    {
    case GL_POINTS:
        return GL_POINTS;
    case GL_LINES:
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        return GL_LINES;
    default:
        return GL_TRIANGLES;
    }
}

inline unsigned char ToByte(float f)
{
    if (f <= 0)
        return 0;
    if (f >= 1)
        return 255;
    return static_cast<unsigned char>(f * 255.f + .5f);
}

void EmitIndex(unsigned i)
{
    s_idx.push_back(static_cast<uint16_t>(i));
}

void EmitVertex(float x, float y, float z, float w)
{
    if (!s_inBegin)
        return;
    EnsureState();

    GLenum mode = DrawModeFor(s_beginMode);
    if (s_batchVersion != s_stateVersion)
    {
        if (memcmp(&s_state, &s_batchState, sizeof(FixedState)))
        {
            if (!s_idx.empty())
            {
                ++s_stats.stateBreaks;
                Flush();
            }
            s_batchState = s_state;
        }
        s_batchVersion = s_stateVersion;
    }
    if (mode != s_batchMode)
    {
        Flush();
        s_batchMode = mode;
    }
    if (s_verts.size() >= MAX_BATCH_VERTICES)
        Flush();

    Vertex v;
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    v.pos[3] = w;
    memcpy(v.tex, s_texCoord, sizeof(v.tex));
    memcpy(v.nrm, s_normal, sizeof(v.nrm));
    for (int i = 0; i < 4; ++i)
        v.col[i] = ToByte(s_color[i]);
    s_verts.push_back(v);
    ++s_primCount;
    unsigned n = s_verts.size();
    unsigned c = s_primCount;

    switch (s_beginMode)
    {
    case GL_POINTS:
        EmitIndex(n - 1);
        break;
    case GL_LINES:
        if (c % 2 == 0)
        {
            EmitIndex(n - 2);
            EmitIndex(n - 1);
        }
        break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
        if (c >= 2)
        {
            EmitIndex(n - 2);
            EmitIndex(n - 1);
        }
        break;
    case GL_TRIANGLES:
        if (c % 3 == 0)
        {
            EmitIndex(n - 3);
            EmitIndex(n - 2);
            EmitIndex(n - 1);
        }
        break;
    case GL_QUADS:
        if (c % 4 == 0)
        {
            EmitIndex(n - 4);
            EmitIndex(n - 3);
            EmitIndex(n - 2);
            EmitIndex(n - 4);
            EmitIndex(n - 2);
            EmitIndex(n - 1);
        }
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
        if (c >= 3)
        {
            if (s_stripTris % 2 == 0)
            {
                EmitIndex(n - 3);
                EmitIndex(n - 2);
            }
            else
            {
                EmitIndex(n - 2);
                EmitIndex(n - 3);
            }
            EmitIndex(n - 1);
            ++s_stripTris;
        }
        break;
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        if (c >= 3)
        {
            EmitIndex(s_primFirst);
            EmitIndex(n - 2);
            EmitIndex(n - 1);
        }
        break;
    }
}

// --- client arrays --------------------------------------------------------------

float ReadArray(ClientArray const& a, unsigned index, int component)
{
    GLsizei stride = a.stride ? a.stride : a.size * static_cast<GLsizei>(sizeof(float));
    unsigned char const* p = a.pointer + index * stride;
    if (a.type == GL_FLOAT)
        return reinterpret_cast<float const*>(p)[component];
    if (a.type == GL_UNSIGNED_BYTE)
        return p[component] / 255.f;
    LogOnce("client array type other than GL_FLOAT/GL_UNSIGNED_BYTE ignored");
    return 0;
}

void EmitArrayVertex(unsigned index)
{
    if (s_texCoordArray.enabled)
    {
        s_texCoord[0] = ReadArray(s_texCoordArray, index, 0);
        s_texCoord[1] = s_texCoordArray.size > 1 ? ReadArray(s_texCoordArray, index, 1) : 0;
        s_texCoord[2] = s_texCoordArray.size > 2 ? ReadArray(s_texCoordArray, index, 2) : 0;
        s_texCoord[3] = s_texCoordArray.size > 3 ? ReadArray(s_texCoordArray, index, 3) : 1;
    }
    if (s_normalArray.enabled)
        for (int i = 0; i < 3; ++i)
            s_normal[i] = ReadArray(s_normalArray, index, i);
    if (s_colorArray.enabled)
    {
        for (int i = 0; i < 3; ++i)
            s_color[i] = ReadArray(s_colorArray, index, i);
        s_color[3] = s_colorArray.size > 3 ? ReadArray(s_colorArray, index, 3) : 1;
    }
    if (s_vertexArray.enabled)
    {
        float x = ReadArray(s_vertexArray, index, 0);
        float y = s_vertexArray.size > 1 ? ReadArray(s_vertexArray, index, 1) : 0;
        float z = s_vertexArray.size > 2 ? ReadArray(s_vertexArray, index, 2) : 0;
        float w = s_vertexArray.size > 3 ? ReadArray(s_vertexArray, index, 3) : 1;
        EmitVertex(x, y, z, w);
    }
}

// --- textures ---------------------------------------------------------------------

// flushes if the batch samples the texture that is about to change
void FlushIfTextureUsed(GLuint texture)
{
    if (!s_verts.empty() && (s_batchState.caps & CAP_TEXTURE_2D) && s_batchState.texture == texture)
        Flush();
}

GLenum UnsizedFormat(GLint internalFormat)
{
    switch (internalFormat)
    {
    case GL_RGB5:
    case GL_RGB8:
    case GL_RGB:
        return GL_RGB;
    case GL_RGBA4:
    case GL_RGBA8:
    case GL_RGBA:
        return GL_RGBA;
    case GL_LUMINANCE:
        return GL_LUMINANCE;
    case GL_LUMINANCE_ALPHA:
    case GL_LUMINANCE8_ALPHA8:
        return GL_LUMINANCE_ALPHA;
    case GL_ALPHA:
        return GL_ALPHA;
    default:
        return static_cast<GLenum>(internalFormat);
    }
}

// uploads with the luminance formats expanded to RGB/RGBA, which every WebGL
// version can mipmap
void TexImage(GLenum target, GLint level, GLenum internalFormat, GLsizei width, GLsizei height, GLenum format,
              GLenum type, void const* pixels)
{
    GLenum storage = UnsizedFormat(internalFormat);
    format = UnsizedFormat(format);
    if (pixels && type == GL_UNSIGNED_BYTE && (format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA))
    {
        int in = format == GL_LUMINANCE ? 1 : 2;
        int out = in + 2;
        std::vector<unsigned char> expanded(static_cast<size_t>(width) * height * out);
        unsigned char const* src = static_cast<unsigned char const*>(pixels);
        for (size_t i = 0, n = static_cast<size_t>(width) * height; i < n; ++i)
        {
            unsigned char l = src[i * in];
            expanded[i * out] = expanded[i * out + 1] = expanded[i * out + 2] = l;
            if (in == 2)
                expanded[i * out + 3] = src[i * in + 1];
        }
        format = in == 1 ? GL_RGB : GL_RGBA;
        emscripten_glTexImage2D(target, level, format, width, height, 0, format, type, &expanded[0]);
        return;
    }
    if (storage != format)
        storage = format; // GLES2: internal format must match the format
    emscripten_glTexImage2D(target, level, storage, width, height, 0, format, type, pixels);
}

// --- GLU sphere -----------------------------------------------------------------

void Sphere(double radius, int slices, int stacks)
{
    if (slices < 3)
        slices = 3;
    if (stacks < 2)
        stacks = 2;
    double const pi = 3.14159265358979323846;
    for (int i = 0; i < stacks; ++i)
    {
        double t0 = pi * i / stacks, t1 = pi * (i + 1) / stacks;
        double z0 = cos(t0), r0 = sin(t0), z1 = cos(t1), r1 = sin(t1);
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j)
        {
            double a = 2 * pi * j / slices;
            double x = cos(a), y = sin(a);
            glNormal3f(x * r1, y * r1, z1);
            glVertex3f(x * r1 * radius, y * r1 * radius, z1 * radius);
            glNormal3f(x * r0, y * r0, z0);
            glVertex3f(x * r0 * radius, y * r0 * radius, z0 * radius);
        }
        glEnd();
    }
}

} // namespace

// --- C++ side API ------------------------------------------------------------------

rGLES2Stats const& sr_GLES2LastFrameStats()
{
    return s_lastStats;
}

void sr_GLES2Flush()
{
    Flush();
}

int sr_GLES2ContextVersion()
{
    return s_contextVersion;
}

void sr_GLES2PerPixelLighting(bool on)
{
    EnsureState();
    if (s_state.perPixel == (on ? 1 : 0))
        return;
    s_state.perPixel = on ? 1 : 0;
    MarkStateChange();
}

void sr_GLES2SetGlow(float intensity)
{
    EnsureState();
    if (s_state.glow == intensity)
        return;
    s_state.glow = intensity;
    MarkStateChange();
}

// WebGL 2 names the layer's GLES2 headers do not have
extern "C" void emscripten_glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield,
                                             GLenum);
static GLenum const AA_GL_READ_FRAMEBUFFER = 0x8CA8, AA_GL_DRAW_FRAMEBUFFER = 0x8CA9;

int sr_GLES2ProgramCount()
{
    return s_programCount;
}

void sr_GLES2SetBloom(bool on, float intensity)
{
    if (intensity >= 0)
        s_bloom.intensity = intensity > 1 ? 1 : intensity;
    if (on == s_bloom.on)
        return;
    s_bloom.on = on;
    s_bloom.count = 0;
    s_bloom.vertices = 0;
    if (!on && s_init)
        DeleteBloomTargets(); // the buffers are the frame's size; the programs stay
}

void sr_GLES2SetBloomShape(float wide, float spill)
{
    s_bloom.wide = wide > 0 ? wide : 0;
    s_bloom.spill = spill > 0 ? spill : 0;
}

bool sr_GLES2BloomPending()
{
    return s_bloom.on && !s_bloom.failed && s_bloom.count > 0;
}

void sr_GLES2Bloom(GLuint target, GLuint depth, int width, int height)
{
    Flush(); // nothing of the frame may still be pending in the batch
    unsigned const count = s_bloom.count;
    s_bloom.count = 0;
    s_bloom.vertices = 0;
    if (!s_bloom.on || s_bloom.failed || count == 0 || width < 4 || height < 4)
        return;

    if (!EnsureBloomTargets(depth, width, height))
    {
        s_bloom.failed = true;
        return;
    }

    // the emission buffer: the emissive geometry again, this time as the
    // emissive term alone and tested against the finished frame's depth
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_bloom.emitFbo);
    emscripten_glViewport(0, 0, width, height);
    emscripten_glClearColor(0, 0, 0, 0);
    emscripten_glClear(GL_COLOR_BUFFER_BIT);
    emscripten_glClearColor(0, 0, 0, 1); // back to the frame's (rSysDep::ClearGL)
    for (unsigned i = 0; i < count; ++i)
    {
        BloomBatch const& b = s_bloom.batches[i];
        FixedState state = b.state;
        state.caps |= CAP_DEPTH_TEST;
        state.depthFunc = GL_LEQUAL; // the fragments that survived in the frame
        state.depthMask = 0;
        if ((b.key & PROG_PIXEL) == 0)
        {
            // an unlit glowing surface keeps its alpha fade but accumulates:
            // the frame's GL_ONE_MINUS_SRC_ALPHA would darken the empty buffer
            state.caps |= CAP_BLEND;
            state.blendSrc = GL_SRC_ALPHA;
            state.blendDst = GL_ONE;
        }
        if (b.listVbo)
        {
            if (b.indexCount == 0)
                continue; // its list was deleted after it was drawn
            UseState(state, b.key);
            emscripten_glBindBuffer(GL_ARRAY_BUFFER, b.listVbo);
            emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.listIbo);
            SetVertexPointers(b.firstVertex);
            emscripten_glDrawElements(b.mode, b.indexCount, GL_UNSIGNED_SHORT,
                                      (void const*)(size_t(b.firstIndex) * sizeof(uint16_t)));
            emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
            emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
            SetVertexPointers(0);
            ++s_stats.draws;
            s_stats.vertices += b.vertexCount;
        }
        else
            DrawGeometry(state, b.key, b.mode, &b.verts[0], b.verts.size(), &b.idx[0], b.idx.size());
    }

    // a quarter of it, blurred once in each direction, added to the frame
    BeginPost();
    emscripten_glUseProgram(s_bloom.progDown);
    emscripten_glUniform2f(s_bloom.uDownTexel, 1.f / width, 1.f / height);
    PostPass(s_bloom.progDown, s_bloom.blurFbo[0], s_bloom.emitTex, s_bloom.qWidth, s_bloom.qHeight);

    emscripten_glUseProgram(s_bloom.progBlur);
    emscripten_glUniform2f(s_bloom.uBlurStep, 1.f / s_bloom.qWidth, 0.f);
    PostPass(s_bloom.progBlur, s_bloom.blurFbo[1], s_bloom.blurTex[0], s_bloom.qWidth, s_bloom.qHeight);
    emscripten_glUniform2f(s_bloom.uBlurStep, 0.f, 1.f / s_bloom.qHeight);
    PostPass(s_bloom.progBlur, s_bloom.blurFbo[0], s_bloom.blurTex[1], s_bloom.qWidth, s_bloom.qHeight);

    bool const wide = s_bloom.wide > 0;
    if (wide)
    {
        // the long soft halo: the blurred quarter reduced once more and blurred
        // at a sixteenth, so the same nine taps reach four times as far
        emscripten_glUseProgram(s_bloom.progDown);
        emscripten_glUniform2f(s_bloom.uDownTexel, 1.f / s_bloom.qWidth, 1.f / s_bloom.qHeight);
        PostPass(s_bloom.progDown, s_bloom.wideFbo[0], s_bloom.blurTex[0], s_bloom.sWidth, s_bloom.sHeight);
        emscripten_glUseProgram(s_bloom.progBlur);
        emscripten_glUniform2f(s_bloom.uBlurStep, 1.f / s_bloom.sWidth, 0.f);
        PostPass(s_bloom.progBlur, s_bloom.wideFbo[1], s_bloom.wideTex[0], s_bloom.sWidth, s_bloom.sHeight);
        emscripten_glUniform2f(s_bloom.uBlurStep, 0.f, 1.f / s_bloom.sHeight);
        PostPass(s_bloom.progBlur, s_bloom.wideFbo[0], s_bloom.wideTex[1], s_bloom.sWidth, s_bloom.sHeight);
    }

    emscripten_glEnable(GL_BLEND);
    emscripten_glBlendFunc(GL_ONE, GL_ONE);
    emscripten_glUseProgram(s_bloom.progAdd);
    emscripten_glUniform1f(s_bloom.uAddIntensity, s_bloom.intensity);
    emscripten_glUniform1f(s_bloom.uAddWide, wide ? s_bloom.wide : 0.f);
    emscripten_glUniform1f(s_bloom.uAddSpill, s_bloom.spill);
    emscripten_glActiveTexture(GL_TEXTURE1);
    emscripten_glBindTexture(GL_TEXTURE_2D, wide ? s_bloom.wideTex[0] : s_bloom.blurTex[0]);
    emscripten_glActiveTexture(GL_TEXTURE0);
    PostPass(s_bloom.progAdd, target, s_bloom.blurTex[0], width, height);
    emscripten_glActiveTexture(GL_TEXTURE1);
    emscripten_glBindTexture(GL_TEXTURE_2D, 0);
    emscripten_glActiveTexture(GL_TEXTURE0);
    EndPost();
}

// --- depth of field with bokeh (the browser's attract scene) -------------------------
//
// After the bloom, before the frame goes to the canvas (sr_GLES2DepthOfField, called
// by the shim). The frame's colour and depth are copied into textures; a half
// resolution pass turns the depth into a circle of confusion (CoC, in pixels: the
// distance from the focus as 1 - focus/distance, minus an in-focus band) next to the
// colour; a gather pass sums, for every pixel, the taps of a bokeh shape (rings of
// points inside a hexagon or a circle) whose own CoC reaches it (scatter as gather),
// weighting bright taps up, so a light behind the focus spreads into a disc of the
// shape; a full resolution pass blends the sharp frame into the blurred one by the
// pixel's own CoC. The distance comes from the depth through the last perspective
// projection a draw used (the scene's camera). WebGL 2 only (depth textures, blits).

struct DofState
{
    bool on = false;
    bool failed = false;
    bool logged = false;
    float focus = 50, band = .15f, radius = 12, threshold = .6f, gain = 4, nearScale = .4f;
    int sides = 6, rings = 4;
    int width = 0, height = 0, hWidth = 0, hHeight = 0;
    GLuint copyFbo = 0, colorTex = 0, depthTex = 0;
    GLuint halfFbo[2] = {0, 0}, halfTex[2] = {0, 0};
    GLuint quad = 0;
    GLuint progPrep = 0, progGather = 0, progComposite = 0;
    GLint uPrepTexel, uPrepDepth, uPrepLin, uPrepFocus, uPrepRadius;
    GLint uGatherTexel, uGatherTaps, uGatherCount, uGatherRadius, uGatherThreshold, uGatherGain, uGatherSpacing;
    GLint uCompDepth, uCompBlur, uCompLin, uCompFocus, uCompRadius, uCompHalfTexel;
    float taps[128];
    int tapCount = 0, tapSides = -1, tapRings = -1;
};
DofState s_dof;

// the CoC in full resolution pixels from a depth sample: lin = (P[10], P[14]) of the
// projection (distance = P14 / (ndc + P10)), focus = (distance, band, radius)
char const* const s_dofCommon =
    "uniform vec2 u_lin;\n"
    "uniform vec3 u_focus;\n"
    "uniform float u_near;\n"
    "float Coc(float depth)\n"
    "{\n"
    "    float dist = u_lin.y / (depth * 2.0 - 1.0 + u_lin.x);\n"
    "    if (dist <= 0.0) dist = 1e6;\n"
    // behind the focus 1 - focus/distance (1 at infinity); in front a gentler
    // 1 - distance/focus scaled by u_near, so the play area nearer the eye stays readable
    "    float c = dist > u_focus.x ? 1.0 - u_focus.x / dist : (1.0 - dist / u_focus.x) * u_near;\n"
    "    return clamp((c - u_focus.y) / (1.0 - u_focus.y), 0.0, 1.0) * u_focus.z;\n"
    "}\n";

// full -> half: the colour of the four pixels, the largest CoC among them (in alpha,
// as a fraction of the largest radius)
char const* const s_dofPrepShader =
    "precision highp float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform sampler2D u_depth;\n"
    "uniform vec2 u_texel;\n"
    "varying vec2 v_uv;\n"
    "%COMMON%"
    "void main()\n"
    "{\n"
    "    vec2 o = u_texel * 0.5;\n"
    "    vec3 c = texture2D(u_tex, v_uv + vec2(-o.x, -o.y)).rgb + texture2D(u_tex, v_uv + vec2(o.x, -o.y)).rgb\n"
    "           + texture2D(u_tex, v_uv + vec2(-o.x, o.y)).rgb + texture2D(u_tex, v_uv + vec2(o.x, o.y)).rgb;\n"
    "    float k = max(max(Coc(texture2D(u_depth, v_uv + vec2(-o.x, -o.y)).r), Coc(texture2D(u_depth, v_uv + vec2(o.x, -o.y)).r)),\n"
    "                  max(Coc(texture2D(u_depth, v_uv + vec2(-o.x, o.y)).r), Coc(texture2D(u_depth, v_uv + vec2(o.x, o.y)).r)));\n"
    "    gl_FragColor = vec4(c * 0.25, k / max(u_focus.z, 1.0));\n"
    "}\n";

// the bokeh: every tap of the shape whose own CoC reaches this pixel adds to it,
// bright taps weighted up so they read as discs
char const* const s_dofGatherShader =
    "precision highp float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec2 u_texel;\n"
    "uniform vec2 u_taps[64];\n"
    "uniform int u_count;\n"
    "uniform float u_radius;\n"   // the largest CoC in half resolution pixels
    "uniform float u_threshold;\n"
    "uniform float u_gain;\n"
    "uniform float u_spacing;\n"  // the rings' spacing in the unit kernel
    "varying vec2 v_uv;\n"
    "void main()\n"
    "{\n"
    "    vec4 center = texture2D(u_tex, v_uv);\n"
    "    vec3 sum = center.rgb;\n"
    "    float weight = 1.0;\n"
    // the whole kernel shifted by up to one ring spacing per pixel: the taps of
    // neighbouring pixels fill each other's gaps (no grid pattern in large
    // discs) and the shape keeps its corners
    "    float h = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);\n"
    "    vec2 jitter = (vec2(h, fract(h * 7.31 + 0.13)) - 0.5) * u_spacing;\n"
    "    for (int i = 0; i < 64; ++i)\n"
    "    {\n"
    "        if (i >= u_count) break;\n"
    "        vec2 o = u_taps[i] + jitter;\n"
    "        float reach = length(o) * u_radius;\n"
    "        vec4 t = texture2D(u_tex, v_uv + o * u_radius * u_texel);\n"
    "        float coc = t.a * u_radius;\n"
    "        float w = clamp(coc - reach + 1.0, 0.0, 1.0);\n"
    "        float lum = dot(t.rgb, vec3(0.299, 0.587, 0.114));\n"
    "        w *= 1.0 + u_gain * smoothstep(u_threshold, 1.0, lum);\n"
    "        sum += t.rgb * w;\n"
    "        weight += w;\n"
    "    }\n"
    "    gl_FragColor = vec4(sum / weight, center.a);\n"
    "}\n";

// half -> full: the sharp frame where the pixel is in focus, the bokeh where not
char const* const s_dofCompositeShader =
    "precision highp float;\n"
    "uniform sampler2D u_tex;\n"
    "uniform sampler2D u_depth;\n"
    "uniform sampler2D u_blur;\n"
    "uniform vec2 u_halfTexel;\n"
    "varying vec2 v_uv;\n"
    "%COMMON%"
    "void main()\n"
    "{\n"
    "    vec3 sharp = texture2D(u_tex, v_uv).rgb;\n"
    // a small tent over the half resolution bokeh: the jitter's grain goes
    "    vec2 t = u_halfTexel * 1.25;\n"
    "    vec4 blur = 0.125 * (texture2D(u_blur, v_uv + vec2(-t.x, -t.y)) + texture2D(u_blur, v_uv + vec2(t.x, -t.y))\n"
    "                       + texture2D(u_blur, v_uv + vec2(-t.x, t.y)) + texture2D(u_blur, v_uv + vec2(t.x, t.y))\n"
    "                       + texture2D(u_blur, v_uv + vec2(-1.4 * t.x, 0.0)) + texture2D(u_blur, v_uv + vec2(1.4 * t.x, 0.0))\n"
    "                       + texture2D(u_blur, v_uv + vec2(0.0, -1.4 * t.y)) + texture2D(u_blur, v_uv + vec2(0.0, 1.4 * t.y)));\n"
    // by the pixel's own CoC only: the half resolution's largest-of-four would
    // lay a blurred near object's dark edge over sharp pixels around it
    "    float k = Coc(texture2D(u_depth, v_uv).r);\n"
    "    float m = smoothstep(0.6, 2.5, k);\n"
    "    gl_FragColor = vec4(mix(sharp, blur.rgb, m), 1.0);\n"
    "}\n";

std::string DofSource(char const* body)
{
    std::string s(body);
    size_t at = s.find("%COMMON%");
    if (at != std::string::npos)
        s.replace(at, 8, s_dofCommon);
    return s;
}

void DeleteDofTargets()
{
    if (s_dof.copyFbo)
    {
        emscripten_glDeleteFramebuffers(1, &s_dof.copyFbo);
        emscripten_glDeleteTextures(1, &s_dof.colorTex);
        emscripten_glDeleteTextures(1, &s_dof.depthTex);
        emscripten_glDeleteFramebuffers(2, s_dof.halfFbo);
        emscripten_glDeleteTextures(2, s_dof.halfTex);
    }
    s_dof.copyFbo = s_dof.colorTex = s_dof.depthTex = 0;
    s_dof.halfFbo[0] = s_dof.halfFbo[1] = s_dof.halfTex[0] = s_dof.halfTex[1] = 0;
    s_dof.width = s_dof.height = 0;
}

bool EnsureDofTargets(int width, int height)
{
    if (s_dof.copyFbo && s_dof.width == width && s_dof.height == height)
        return true;
    DeleteDofTargets();
    s_dof.width = width;
    s_dof.height = height;
    s_dof.hWidth = width / 2 > 0 ? width / 2 : 1;
    s_dof.hHeight = height / 2 > 0 ? height / 2 : 1;

    s_dof.colorTex = MakeBloomTexture(width, height);
    emscripten_glGenTextures(1, &s_dof.depthTex);
    emscripten_glBindTexture(GL_TEXTURE_2D, s_dof.depthTex);
    emscripten_glTexImage2D(GL_TEXTURE_2D, 0, 0x81A6 /* GL_DEPTH_COMPONENT24 */, width, height, 0, GL_DEPTH_COMPONENT,
                            GL_UNSIGNED_INT, 0);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    emscripten_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    emscripten_glGenFramebuffers(1, &s_dof.copyFbo);
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_dof.copyFbo);
    emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_dof.colorTex, 0);
    emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_dof.depthTex, 0);
    bool ok = emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    emscripten_glGenFramebuffers(2, s_dof.halfFbo);
    for (int i = 0; i < 2; ++i)
    {
        s_dof.halfTex[i] = MakeBloomTexture(s_dof.hWidth, s_dof.hHeight);
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_dof.halfFbo[i]);
        emscripten_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_dof.halfTex[i], 0);
        ok = ok && emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    if (!s_dof.quad)
    {
        float const quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
        emscripten_glGenBuffers(1, &s_dof.quad);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_dof.quad);
        emscripten_glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    }
    if (!s_dof.progPrep)
    {
        std::string prep = DofSource(s_dofPrepShader), comp = DofSource(s_dofCompositeShader);
        s_dof.progPrep = BuildPostProgram("dof prep", prep.c_str());
        s_dof.progGather = BuildPostProgram("dof gather", s_dofGatherShader);
        s_dof.progComposite = BuildPostProgram("dof composite", comp.c_str());
        if (s_dof.progPrep)
        {
            s_dof.uPrepTexel = emscripten_glGetUniformLocation(s_dof.progPrep, "u_texel");
            s_dof.uPrepDepth = emscripten_glGetUniformLocation(s_dof.progPrep, "u_depth");
            s_dof.uPrepLin = emscripten_glGetUniformLocation(s_dof.progPrep, "u_lin");
            s_dof.uPrepFocus = emscripten_glGetUniformLocation(s_dof.progPrep, "u_focus");
            s_dof.uPrepRadius = emscripten_glGetUniformLocation(s_dof.progPrep, "u_near");
        }
        if (s_dof.progGather)
        {
            s_dof.uGatherTexel = emscripten_glGetUniformLocation(s_dof.progGather, "u_texel");
            s_dof.uGatherTaps = emscripten_glGetUniformLocation(s_dof.progGather, "u_taps");
            s_dof.uGatherCount = emscripten_glGetUniformLocation(s_dof.progGather, "u_count");
            s_dof.uGatherRadius = emscripten_glGetUniformLocation(s_dof.progGather, "u_radius");
            s_dof.uGatherThreshold = emscripten_glGetUniformLocation(s_dof.progGather, "u_threshold");
            s_dof.uGatherGain = emscripten_glGetUniformLocation(s_dof.progGather, "u_gain");
            s_dof.uGatherSpacing = emscripten_glGetUniformLocation(s_dof.progGather, "u_spacing");
        }
        if (s_dof.progComposite)
        {
            s_dof.uCompDepth = emscripten_glGetUniformLocation(s_dof.progComposite, "u_depth");
            s_dof.uCompBlur = emscripten_glGetUniformLocation(s_dof.progComposite, "u_blur");
            s_dof.uCompLin = emscripten_glGetUniformLocation(s_dof.progComposite, "u_lin");
            s_dof.uCompFocus = emscripten_glGetUniformLocation(s_dof.progComposite, "u_focus");
            s_dof.uCompHalfTexel = emscripten_glGetUniformLocation(s_dof.progComposite, "u_halfTexel");
            s_dof.uCompRadius = emscripten_glGetUniformLocation(s_dof.progComposite, "u_near");
        }
    }
    ok = ok && s_dof.progPrep && s_dof.progGather && s_dof.progComposite;
    return ok;
}

// the bokeh's taps: rings of points spread evenly inside a regular polygon of
// `sides` corners (0: a circle), at most 64
void BuildDofTaps(int sides, int rings)
{
    if (s_dof.tapSides == sides && s_dof.tapRings == rings)
        return;
    s_dof.tapSides = sides;
    s_dof.tapRings = rings;
    int n = 0;
    float const rotation = sides > 0 ? (float)M_PI / sides * .5f : 0;
    for (int r = 1; r <= rings && n < 64; ++r)
    {
        float const radius = float(r) / rings;
        int const count = r * 6;
        for (int k = 0; k < count && n < 64; ++k)
        {
            float const a = 2 * (float)M_PI * k / count + rotation;
            float scale = 1;
            if (sides >= 3)
            {
                // the polygon's boundary at this angle: its inradius over the
                // projection onto the nearest edge's normal
                float const sector = 2 * (float)M_PI / sides;
                float const local = fmodf(a - rotation + 100 * sector, sector) - sector * .5f;
                scale = cosf(sector * .5f) / cosf(local);
            }
            s_dof.taps[2 * n] = cosf(a) * radius * scale;
            s_dof.taps[2 * n + 1] = sinf(a) * radius * scale;
            ++n;
        }
    }
    s_dof.tapCount = n;
}

void sr_GLES2SetDepthOfField(bool on, float focus, float band, float radius, float threshold, float gain, int sides,
                             int rings, float nearScale)
{
    s_dof.on = on && radius > 0;
    s_dof.focus = focus > .1f ? focus : .1f;
    s_dof.band = band < 0 ? 0 : (band > .95f ? .95f : band);
    s_dof.radius = radius > 64 ? 64 : radius;
    s_dof.threshold = threshold < 0 ? 0 : (threshold > .99f ? .99f : threshold);
    s_dof.gain = gain < 0 ? 0 : gain;
    s_dof.sides = sides < 0 ? 0 : (sides > 12 ? 12 : sides);
    s_dof.rings = rings < 1 ? 1 : (rings > 4 ? 4 : rings);
    s_dof.nearScale = nearScale < 0 ? 0 : (nearScale > 1 ? 1 : nearScale);
}

bool sr_GLES2DepthOfFieldPending()
{
    // where the bloom runs (the quality tiers above Low), on WebGL 2
    return s_dof.on && !s_dof.failed && s_haveLastPerspective && s_bloom.on && s_contextVersion == 2;
}

void sr_GLES2DepthOfField(GLuint target, int width, int height)
{
    Flush();
    if (!sr_GLES2DepthOfFieldPending() || width < 8 || height < 8)
        return;
    if (!EnsureDofTargets(width, height))
    {
        printf("rGLES2: depth of field unavailable, off.\n");
        s_dof.failed = true;
        DeleteDofTargets();
        return;
    }
    BuildDofTaps(s_dof.sides, s_dof.rings);

    // the frame's colour and depth into textures
    emscripten_glBindFramebuffer(AA_GL_READ_FRAMEBUFFER, target);
    emscripten_glBindFramebuffer(AA_GL_DRAW_FRAMEBUFFER, s_dof.copyFbo);
    emscripten_glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                                 GL_NEAREST);

    // the frustum's depth terms F10/F14 out of a projection that may carry the
    // view (eCamera loads the look-at into GL_PROJECTION): its row 3 is minus the
    // view's z axis v (unit), its row 2 is F10 * v + F14 * (0, 0, 0, 1)
    float const* m = s_lastPerspective.m;
    float const r2[4] = {m[2], m[6], m[10], m[14]}, r3[4] = {m[3], m[7], m[11], m[15]};
    float const vv = r3[0] * r3[0] + r3[1] * r3[1] + r3[2] * r3[2];
    float const f10 = vv > 0 ? -(r2[0] * r3[0] + r2[1] * r3[1] + r2[2] * r3[2]) / vv : -1;
    float const f14 = r2[3] + f10 * r3[3];
    float const lin[2] = {f10, f14};
    float const focus[3] = {s_dof.focus, s_dof.band, s_dof.radius * width / 1536.f};

    emscripten_glDisable(GL_DEPTH_TEST);
    emscripten_glDisable(GL_BLEND);
    emscripten_glDisable(GL_CULL_FACE);
    emscripten_glDisable(GL_POLYGON_OFFSET_FILL);
    emscripten_glDisable(GL_DITHER);
    emscripten_glDisableVertexAttribArray(ATTR_COL);
    emscripten_glDisableVertexAttribArray(ATTR_TEX);
    emscripten_glDisableVertexAttribArray(ATTR_NRM);
    emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_dof.quad);
    emscripten_glVertexAttribPointer(ATTR_POS, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // depth on unit 1 for the passes that read it
    emscripten_glActiveTexture(GL_TEXTURE1);
    emscripten_glBindTexture(GL_TEXTURE_2D, s_dof.depthTex);
    emscripten_glActiveTexture(GL_TEXTURE0);

    emscripten_glUseProgram(s_dof.progPrep);
    emscripten_glUniform1i(s_dof.uPrepDepth, 1);
    emscripten_glUniform2f(s_dof.uPrepTexel, 1.f / width, 1.f / height);
    emscripten_glUniform2fv(s_dof.uPrepLin, 1, lin);
    emscripten_glUniform3fv(s_dof.uPrepFocus, 1, focus);
    emscripten_glUniform1f(s_dof.uPrepRadius, s_dof.nearScale);
    PostPass(s_dof.progPrep, s_dof.halfFbo[0], s_dof.colorTex, s_dof.hWidth, s_dof.hHeight);

    emscripten_glUseProgram(s_dof.progGather);
    emscripten_glUniform2f(s_dof.uGatherTexel, 1.f / s_dof.hWidth, 1.f / s_dof.hHeight);
    emscripten_glUniform2fv(s_dof.uGatherTaps, s_dof.tapCount, s_dof.taps);
    emscripten_glUniform1i(s_dof.uGatherCount, s_dof.tapCount);
    emscripten_glUniform1f(s_dof.uGatherRadius, focus[2] * .5f);
    emscripten_glUniform1f(s_dof.uGatherThreshold, s_dof.threshold);
    emscripten_glUniform1f(s_dof.uGatherGain, s_dof.gain);
    emscripten_glUniform1f(s_dof.uGatherSpacing, 1.f / s_dof.rings);
    PostPass(s_dof.progGather, s_dof.halfFbo[1], s_dof.halfTex[0], s_dof.hWidth, s_dof.hHeight);

    emscripten_glUseProgram(s_dof.progComposite);
    emscripten_glUniform1i(s_dof.uCompDepth, 1);
    emscripten_glUniform1i(s_dof.uCompBlur, 2);
    emscripten_glUniform2fv(s_dof.uCompLin, 1, lin);
    emscripten_glUniform3fv(s_dof.uCompFocus, 1, focus);
    emscripten_glUniform1f(s_dof.uCompRadius, s_dof.nearScale);
    emscripten_glUniform2f(s_dof.uCompHalfTexel, 1.f / s_dof.hWidth, 1.f / s_dof.hHeight);
    emscripten_glActiveTexture(GL_TEXTURE2);
    emscripten_glBindTexture(GL_TEXTURE_2D, s_dof.halfTex[1]);
    emscripten_glActiveTexture(GL_TEXTURE0);
    PostPass(s_dof.progComposite, target, s_dof.colorTex, width, height);

    emscripten_glActiveTexture(GL_TEXTURE2);
    emscripten_glBindTexture(GL_TEXTURE_2D, 0);
    emscripten_glActiveTexture(GL_TEXTURE1);
    emscripten_glBindTexture(GL_TEXTURE_2D, 0);
    emscripten_glActiveTexture(GL_TEXTURE0);
    EndPost();
    if (!s_dof.logged)
    {
        s_dof.logged = true;
        printf("rGLES2: depth of field on (%dx%d, half %dx%d, %d taps)\n", width, height, s_dof.hWidth, s_dof.hHeight,
               s_dof.tapCount);
    }
}

// --- the GL entry points ------------------------------------------------------------

extern "C" {

// immediate mode

void glBegin(GLenum mode)
{
    if (s_inBegin)
        glEnd();
    ++s_stats.begins;
    s_inBegin = true;
    s_beginMode = mode;
    s_primFirst = s_verts.size();
    s_primCount = 0;
    s_stripTris = 0;
}

void glEnd(void)
{
    if (!s_inBegin)
        return;
    unsigned c = s_primCount, unused = 0;
    switch (s_beginMode)
    {
    case GL_TRIANGLES:
        unused = c % 3;
        break;
    case GL_QUADS:
        unused = c % 4;
        break;
    case GL_LINES:
        unused = c % 2;
        break;
    case GL_LINE_STRIP:
        unused = c < 2 ? c : 0;
        break;
    case GL_LINE_LOOP:
        if (c >= 2)
        {
            EmitIndex(s_verts.size() - 1);
            EmitIndex(s_primFirst);
        }
        else
            unused = c;
        break;
    case GL_TRIANGLE_STRIP:
    case GL_QUAD_STRIP:
    case GL_TRIANGLE_FAN:
    case GL_POLYGON:
        unused = c < 3 ? c : 0;
        break;
    default:
        break;
    }
    // drop the vertices of an incomplete primitive
    s_verts.resize(s_verts.size() - unused);
    s_inBegin = false;
    s_primCount = 0;
}

void glVertex2f(GLfloat x, GLfloat y)
{
    ++s_stats.immediateCalls;
    EmitVertex(x, y, 0, 1);
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    ++s_stats.immediateCalls;
    EmitVertex(x, y, z, 1);
}

void glVertex3fv(GLfloat const* v)
{
    ++s_stats.immediateCalls;
    EmitVertex(v[0], v[1], v[2], 1);
}

void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    ++s_stats.immediateCalls;
    EmitVertex(x, y, z, w);
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    ++s_stats.immediateCalls;
    SetVec4(s_color, r, g, b, 1);
}

void glColor3fv(GLfloat const* v)
{
    glColor3f(v[0], v[1], v[2]);
}

void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    ++s_stats.immediateCalls;
    SetVec4(s_color, r, g, b, a);
}

void glColor4fv(GLfloat const* v)
{
    glColor4f(v[0], v[1], v[2], v[3]);
}

void glTexCoord2f(GLfloat s, GLfloat t)
{
    ++s_stats.immediateCalls;
    SetVec4(s_texCoord, s, t, 0, 1);
}

void glTexCoord2fv(GLfloat const* v)
{
    glTexCoord2f(v[0], v[1]);
}

void glTexCoord2d(GLdouble s, GLdouble t)
{
    glTexCoord2f(static_cast<GLfloat>(s), static_cast<GLfloat>(t));
}

void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    ++s_stats.immediateCalls;
    SetVec4(s_texCoord, s, t, r, 1);
}

void glTexCoord3fv(GLfloat const* v)
{
    glTexCoord3f(v[0], v[1], v[2]);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    ++s_stats.immediateCalls;
    SetVec4(s_texCoord, s, t, r, q);
}

void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    ++s_stats.immediateCalls;
    s_normal[0] = x;
    s_normal[1] = y;
    s_normal[2] = z;
}

void glNormal3fv(GLfloat const* v)
{
    glNormal3f(v[0], v[1], v[2]);
}

void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    bool wasInBegin = s_inBegin;
    GLenum mode = s_beginMode;
    glBegin(GL_QUADS);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
    if (wasInBegin)
        glBegin(mode);
}

// matrices

void glMatrixMode(GLenum mode)
{
    switch (mode)
    {
    case GL_MODELVIEW:
        s_matrixMode = MAT_MODELVIEW;
        break;
    case GL_PROJECTION:
        s_matrixMode = MAT_PROJECTION;
        break;
    case GL_TEXTURE:
        s_matrixMode = MAT_TEXTURE;
        break;
    default:
        LogOnce("unsupported matrix mode");
        break;
    }
}

static void SetCurrentMatrix(Mat4 const& m)
{
    EnsureState();
    s_stacks[s_matrixMode].top = m;
    s_state.matrix[s_matrixMode] = m;
    MarkStateChange();
}

void glLoadIdentity(void)
{
    SetCurrentMatrix(Identity());
}

void glLoadMatrixf(GLfloat const* m)
{
    Mat4 r;
    memcpy(r.m, m, sizeof(r.m));
    SetCurrentMatrix(r);
}

void glMultMatrixf(GLfloat const* m)
{
    EnsureState();
    Mat4 r;
    memcpy(r.m, m, sizeof(r.m));
    SetCurrentMatrix(Mul(s_stacks[s_matrixMode].top, r));
}

void glMultMatrixd(GLdouble const* m)
{
    float f[16];
    for (int i = 0; i < 16; ++i)
        f[i] = static_cast<float>(m[i]);
    glMultMatrixf(f);
}

void glPushMatrix(void)
{
    EnsureState();
    MatrixStack& s = s_stacks[s_matrixMode];
    s.saved.push_back(s.top);
}

void glPopMatrix(void)
{
    EnsureState();
    MatrixStack& s = s_stacks[s_matrixMode];
    if (s.saved.empty())
    {
        LogOnce("glPopMatrix on an empty stack");
        return;
    }
    Mat4 m = s.saved.back();
    s.saved.pop_back();
    SetCurrentMatrix(m);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 t = Identity();
    t.m[12] = x;
    t.m[13] = y;
    t.m[14] = z;
    glMultMatrixf(t.m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 s = Identity();
    s.m[0] = x;
    s.m[5] = y;
    s.m[10] = z;
    glMultMatrixf(s.m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float len = sqrtf(x * x + y * y + z * z);
    if (len <= 0)
        return;
    x /= len;
    y /= len;
    z /= len;
    float rad = angle * 3.14159265358979323846f / 180.f;
    float c = cosf(rad), s = sinf(rad), t = 1 - c;
    Mat4 r = Identity();
    r.m[0] = t * x * x + c;
    r.m[1] = t * x * y + s * z;
    r.m[2] = t * x * z - s * y;
    r.m[4] = t * x * y - s * z;
    r.m[5] = t * y * y + c;
    r.m[6] = t * y * z + s * x;
    r.m[8] = t * x * z + s * y;
    r.m[9] = t * y * z - s * x;
    r.m[10] = t * z * z + c;
    glMultMatrixf(r.m);
}

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    Mat4 m = Identity();
    m.m[0] = static_cast<float>(2 / (r - l));
    m.m[5] = static_cast<float>(2 / (t - b));
    m.m[10] = static_cast<float>(-2 / (f - n));
    m.m[12] = static_cast<float>(-(r + l) / (r - l));
    m.m[13] = static_cast<float>(-(t + b) / (t - b));
    m.m[14] = static_cast<float>(-(f + n) / (f - n));
    glMultMatrixf(m.m);
}

void glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    Mat4 m;
    memset(m.m, 0, sizeof(m.m));
    m.m[0] = static_cast<float>(2 * n / (r - l));
    m.m[5] = static_cast<float>(2 * n / (t - b));
    m.m[8] = static_cast<float>((r + l) / (r - l));
    m.m[9] = static_cast<float>((t + b) / (t - b));
    m.m[10] = static_cast<float>(-(f + n) / (f - n));
    m.m[11] = -1;
    m.m[14] = static_cast<float>(-2 * f * n / (f - n));
    glMultMatrixf(m.m);
}

// enables

void glEnable(GLenum cap)
{
    EnsureState();
    unsigned bit = 0;
    switch (cap)
    {
    case GL_TEXTURE_2D:
        bit = CAP_TEXTURE_2D;
        break;
    case GL_FOG:
        bit = CAP_FOG;
        break;
    case GL_ALPHA_TEST:
        bit = CAP_ALPHA_TEST;
        break;
    case GL_LIGHTING:
        bit = CAP_LIGHTING;
        break;
    case GL_LIGHT0:
        bit = CAP_LIGHT0;
        break;
    case GL_LIGHT1:
        bit = CAP_LIGHT1;
        break;
    case GL_BLEND:
        bit = CAP_BLEND;
        break;
    case GL_DEPTH_TEST:
        bit = CAP_DEPTH_TEST;
        break;
    case GL_CULL_FACE:
        bit = CAP_CULL_FACE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        bit = CAP_POLYGON_OFFSET_FILL;
        break;
    case GL_DITHER:
        bit = CAP_DITHER;
        break;
    case GL_TEXTURE:            // 1D textures are not a thing here
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_Q:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
    case GL_COLOR_MATERIAL:
    case GL_NORMALIZE:
        return;
    default:
        LogOnce("glEnable of an unsupported capability ignored");
        return;
    }
    if (!(s_state.caps & bit))
    {
        s_state.caps |= bit;
        MarkStateChange();
    }
}

void glDisable(GLenum cap)
{
    EnsureState();
    unsigned bit = 0;
    switch (cap)
    {
    case GL_TEXTURE_2D:
        bit = CAP_TEXTURE_2D;
        break;
    case GL_FOG:
        bit = CAP_FOG;
        break;
    case GL_ALPHA_TEST:
        bit = CAP_ALPHA_TEST;
        break;
    case GL_LIGHTING:
        bit = CAP_LIGHTING;
        break;
    case GL_LIGHT0:
        bit = CAP_LIGHT0;
        break;
    case GL_LIGHT1:
        bit = CAP_LIGHT1;
        break;
    case GL_BLEND:
        bit = CAP_BLEND;
        break;
    case GL_DEPTH_TEST:
        bit = CAP_DEPTH_TEST;
        break;
    case GL_CULL_FACE:
        bit = CAP_CULL_FACE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        bit = CAP_POLYGON_OFFSET_FILL;
        break;
    case GL_DITHER:
        bit = CAP_DITHER;
        break;
    default:
        return;
    }
    if (s_state.caps & bit)
    {
        s_state.caps &= ~bit;
        MarkStateChange();
    }
}

GLboolean glIsEnabled(GLenum cap)
{
    EnsureState();
    switch (cap)
    {
    case GL_TEXTURE_2D:
        return (s_state.caps & CAP_TEXTURE_2D) != 0;
    case GL_BLEND:
        return (s_state.caps & CAP_BLEND) != 0;
    case GL_DEPTH_TEST:
        return (s_state.caps & CAP_DEPTH_TEST) != 0;
    case GL_CULL_FACE:
        return (s_state.caps & CAP_CULL_FACE) != 0;
    case GL_LIGHTING:
        return (s_state.caps & CAP_LIGHTING) != 0;
    case GL_ALPHA_TEST:
        return (s_state.caps & CAP_ALPHA_TEST) != 0;
    case GL_FOG:
        return (s_state.caps & CAP_FOG) != 0;
    default:
        return GL_FALSE;
    }
}

// state

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    EnsureState();
    if (s_state.blendSrc != sfactor || s_state.blendDst != dfactor)
    {
        s_state.blendSrc = sfactor;
        s_state.blendDst = dfactor;
        MarkStateChange();
    }
}

void glDepthFunc(GLenum func)
{
    EnsureState();
    if (s_state.depthFunc != func)
    {
        s_state.depthFunc = func;
        MarkStateChange();
    }
}

void glDepthMask(GLboolean flag)
{
    EnsureState();
    int f = flag ? 1 : 0;
    if (s_state.depthMask != f)
    {
        s_state.depthMask = f;
        MarkStateChange();
    }
}

void glCullFace(GLenum mode)
{
    EnsureState();
    if (s_state.cullFace != mode)
    {
        s_state.cullFace = mode;
        MarkStateChange();
    }
}

void glFrontFace(GLenum mode)
{
    EnsureState();
    if (s_state.frontFace != mode)
    {
        s_state.frontFace = mode;
        MarkStateChange();
    }
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    EnsureState();
    if (s_state.polyFactor != factor || s_state.polyUnits != units)
    {
        s_state.polyFactor = factor;
        s_state.polyUnits = units;
        MarkStateChange();
    }
}

void glLineWidth(GLfloat width)
{
    EnsureState();
    if (s_state.lineWidth != width)
    {
        s_state.lineWidth = width;
        MarkStateChange();
    }
}

void glShadeModel(GLenum)
{
    // always smooth
}

void glHint(GLenum, GLenum)
{
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    EnsureState();
    if (s_state.alphaFunc != func || s_state.alphaRef != ref)
    {
        s_state.alphaFunc = func;
        s_state.alphaRef = ref;
        MarkStateChange();
    }
}

void glFogf(GLenum pname, GLfloat param)
{
    EnsureState();
    switch (pname)
    {
    case GL_FOG_MODE:
        s_state.fogMode = static_cast<GLenum>(param);
        break;
    case GL_FOG_DENSITY:
        s_state.fogDensity = param;
        break;
    case GL_FOG_START:
        s_state.fogStart = param;
        break;
    case GL_FOG_END:
        s_state.fogEnd = param;
        break;
    default:
        return;
    }
    MarkStateChange();
}

void glFogi(GLenum pname, GLint param)
{
    glFogf(pname, static_cast<GLfloat>(param));
}

void glFogfv(GLenum pname, GLfloat const* params)
{
    EnsureState();
    if (pname == GL_FOG_COLOR)
    {
        memcpy(s_state.fogColor, params, sizeof(s_state.fogColor));
        MarkStateChange();
    }
    else
        glFogf(pname, params[0]);
}

void glLightfv(GLenum light, GLenum pname, GLfloat const* params)
{
    EnsureState();
    int index = light - GL_LIGHT0;
    if (index < 0 || index >= 2)
    {
        LogOnce("only GL_LIGHT0 and GL_LIGHT1 are supported");
        return;
    }
    Light& l = s_state.lights[index];
    switch (pname)
    {
    case GL_AMBIENT:
        memcpy(l.ambient, params, sizeof(l.ambient));
        break;
    case GL_DIFFUSE:
        memcpy(l.diffuse, params, sizeof(l.diffuse));
        break;
    case GL_SPECULAR:
        memcpy(l.specular, params, sizeof(l.specular));
        break;
    case GL_POSITION:
        Transform(s_stacks[MAT_MODELVIEW].top, params, l.position);
        break;
    default:
        return;
    }
    MarkStateChange();
}

void glLightModelfv(GLenum pname, GLfloat const* params)
{
    EnsureState();
    if (pname == GL_LIGHT_MODEL_AMBIENT)
    {
        memcpy(s_state.globalAmbient, params, sizeof(s_state.globalAmbient));
        MarkStateChange();
    }
}

void glMaterialfv(GLenum, GLenum pname, GLfloat const* params)
{
    EnsureState();
    switch (pname)
    {
    case GL_AMBIENT:
        memcpy(s_state.matAmbient, params, sizeof(s_state.matAmbient));
        break;
    case GL_DIFFUSE:
        memcpy(s_state.matDiffuse, params, sizeof(s_state.matDiffuse));
        break;
    case GL_AMBIENT_AND_DIFFUSE:
        memcpy(s_state.matAmbient, params, sizeof(s_state.matAmbient));
        memcpy(s_state.matDiffuse, params, sizeof(s_state.matDiffuse));
        break;
    case GL_SPECULAR:
        memcpy(s_state.matSpecular, params, sizeof(s_state.matSpecular));
        break;
    case GL_EMISSION:
        memcpy(s_state.matEmission, params, sizeof(s_state.matEmission));
        break;
    case GL_SHININESS:
        s_state.matShininess = params[0];
        break;
    default:
        return;
    }
    MarkStateChange();
}

void glTexEnvi(GLenum, GLenum, GLint param)
{
    if (param != GL_MODULATE)
        LogOnce("texture environment other than GL_MODULATE ignored");
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    glTexEnvi(target, pname, static_cast<GLint>(param));
}

void glTexGeni(GLenum, GLenum, GLint)
{
    LogOnce("texture coordinate generation is not supported");
}

void glTexGenfv(GLenum, GLenum, GLfloat const*)
{
    LogOnce("texture coordinate generation is not supported");
}

void glPushAttrib(GLbitfield)
{
    LogOnce("glPushAttrib/glPopAttrib are not supported");
}

void glPopAttrib(void)
{
}

void glPushClientAttrib(GLbitfield)
{
}

void glPopClientAttrib(void)
{
}

// client arrays

static ClientArray* ArrayFor(GLenum array)
{
    switch (array)
    {
    case GL_VERTEX_ARRAY:
        return &s_vertexArray;
    case GL_NORMAL_ARRAY:
        return &s_normalArray;
    case GL_TEXTURE_COORD_ARRAY:
        return &s_texCoordArray;
    case GL_COLOR_ARRAY:
        return &s_colorArray;
    default:
        return 0;
    }
}

void glEnableClientState(GLenum array)
{
    if (ClientArray* a = ArrayFor(array))
        a->enabled = true;
}

void glDisableClientState(GLenum array)
{
    if (ClientArray* a = ArrayFor(array))
        a->enabled = false;
}

static void SetPointer(ClientArray& a, GLint size, GLenum type, GLsizei stride, void const* pointer)
{
    a.size = size;
    a.type = type;
    a.stride = stride;
    a.pointer = static_cast<unsigned char const*>(pointer);
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, void const* pointer)
{
    SetPointer(s_vertexArray, size, type, stride, pointer);
}

void glNormalPointer(GLenum type, GLsizei stride, void const* pointer)
{
    SetPointer(s_normalArray, 3, type, stride, pointer);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, void const* pointer)
{
    SetPointer(s_texCoordArray, size, type, stride, pointer);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, void const* pointer)
{
    SetPointer(s_colorArray, size, type, stride, pointer);
}

void glInterleavedArrays(GLenum format, GLsizei stride, void const* pointer)
{
    if (format != GL_T2F_V3F)
    {
        LogOnce("glInterleavedArrays: only GL_T2F_V3F is supported");
        return;
    }
    if (!stride)
        stride = 5 * sizeof(float);
    unsigned char const* p = static_cast<unsigned char const*>(pointer);
    SetPointer(s_texCoordArray, 2, GL_FLOAT, stride, p);
    SetPointer(s_vertexArray, 3, GL_FLOAT, stride, p + 2 * sizeof(float));
    s_texCoordArray.enabled = true;
    s_vertexArray.enabled = true;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    float savedTex[4], savedNrm[3], savedCol[4];
    memcpy(savedTex, s_texCoord, sizeof(savedTex));
    memcpy(savedNrm, s_normal, sizeof(savedNrm));
    memcpy(savedCol, s_color, sizeof(savedCol));
    glBegin(mode);
    for (GLsizei i = 0; i < count; ++i)
        EmitArrayVertex(first + i);
    glEnd();
    memcpy(s_texCoord, savedTex, sizeof(savedTex));
    memcpy(s_normal, savedNrm, sizeof(savedNrm));
    memcpy(s_color, savedCol, sizeof(savedCol));
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, void const* indices)
{
    float savedTex[4], savedNrm[3], savedCol[4];
    memcpy(savedTex, s_texCoord, sizeof(savedTex));
    memcpy(savedNrm, s_normal, sizeof(savedNrm));
    memcpy(savedCol, s_color, sizeof(savedCol));
    glBegin(mode);
    for (GLsizei i = 0; i < count; ++i)
    {
        unsigned index;
        switch (type)
        {
        case GL_UNSIGNED_BYTE:
            index = static_cast<unsigned char const*>(indices)[i];
            break;
        case GL_UNSIGNED_SHORT:
            index = static_cast<unsigned short const*>(indices)[i];
            break;
        default:
            index = static_cast<unsigned int const*>(indices)[i];
            break;
        }
        EmitArrayVertex(index);
    }
    glEnd();
    memcpy(s_texCoord, savedTex, sizeof(savedTex));
    memcpy(s_normal, savedNrm, sizeof(savedNrm));
    memcpy(s_color, savedCol, sizeof(savedCol));
}

// display lists. glGenLists still returns 0, so rDisplayList (rim and cycle
// walls) keeps drawing immediately; sr_GLES2GenLists hands out real ones.
// A list keeps the batches compiled between glNewList and glEndList in a
// static buffer, each with its state; glCallList draws them with the
// current projection and the current modelview times the batch's modelview
// relative to the one at glNewList, then leaves the state as the list's
// commands would. Compiling is GL_COMPILE whatever the mode: nothing is
// drawn and the state is put back at glEndList. Textures must be uploaded
// before (a texture upload is not compiled).

GLuint glGenLists(GLsizei)
{
    return 0;
}

GLuint sr_GLES2GenLists(GLsizei range)
{
    if (range <= 0)
        return 0;
    GLuint first = s_nextList;
    s_nextList += range;
    return first;
}

void glNewList(GLuint list, GLenum)
{
    if (!list || s_recording)
        return;
    EnsureInit();
    Flush();
    DisplayList& l = s_lists[list];
    if (l.vbo)
        emscripten_glDeleteBuffers(1, &l.vbo);
    if (l.ibo)
        emscripten_glDeleteBuffers(1, &l.ibo);
    l = DisplayList();
    s_recording = &l;
    s_recordSaved = s_state;
    s_recordBaseInverse = Inverse(s_state.matrix[MAT_MODELVIEW]);
}

void glEndList(void)
{
    if (!s_recording)
        return;
    if (s_inBegin)
        glEnd();
    Flush();
    DisplayList& l = *s_recording;
    s_recording = 0;
    l.endState = s_state;

    // GL_COMPILE: the commands did not happen
    s_state = s_recordSaved;
    for (int i = 0; i < 3; ++i)
        s_stacks[i].top = s_state.matrix[i];
    MarkStateChange();

    if (!l.verts.empty())
    {
        emscripten_glGenBuffers(1, &l.vbo);
        emscripten_glGenBuffers(1, &l.ibo);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, l.vbo);
        emscripten_glBufferData(GL_ARRAY_BUFFER, l.verts.size() * sizeof(Vertex), &l.verts[0], GL_STATIC_DRAW);
        emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, l.ibo);
        emscripten_glBufferData(GL_ELEMENT_ARRAY_BUFFER, l.idx.size() * sizeof(uint16_t), &l.idx[0], GL_STATIC_DRAW);
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
        SetVertexPointers(0);
    }
    // batches that glow are replayed into the bloom from the list's own buffers
    // (RecordEmissiveList), so the vertices need not stay on the CPU
    l.emissive = false;
    for (size_t i = 0; i < l.batches.size(); ++i)
    {
        FixedState const& s = l.batches[i].state;
        if (s.glow > 0 || s.matEmission[0] > 0 || s.matEmission[1] > 0 || s.matEmission[2] > 0)
            l.emissive = true;
    }
    printf("rGLES2: list compiled: %u batches, %u vertices, %u indices\n", unsigned(l.batches.size()),
           unsigned(l.verts.size()), unsigned(l.idx.size()));
    l.verts.clear();
    l.verts.shrink_to_fit();
    l.idx.clear();
    l.idx.shrink_to_fit();
}

void glCallList(GLuint list)
{
    std::map<GLuint, DisplayList>::iterator it = s_lists.find(list);
    if (it == s_lists.end() || &it->second == s_recording)
        return;
    EnsureInit();
    if (s_inBegin)
        LogOnce("glCallList inside glBegin ignored");
    if (s_inBegin)
        return;
    Flush();

    DisplayList& l = it->second;
    if (l.vbo && !s_recording)
    {
        Mat4 const modelview = s_state.matrix[MAT_MODELVIEW];
        Mat4 const projection = s_state.matrix[MAT_PROJECTION];
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, l.vbo);
        emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, l.ibo);
        for (size_t i = 0; i < l.batches.size(); ++i)
        {
            ListBatch const& b = l.batches[i];
            FixedState s = b.state;
            s.matrix[MAT_MODELVIEW] = Mul(modelview, b.modelview);
            s.matrix[MAT_PROJECTION] = projection;
            unsigned const key = ProgramKeyFor(s);
            if (l.emissive)
                RecordEmissiveList(s, key, b.mode, l.vbo, l.ibo, b.firstVertex, b.vertexCount, b.firstIndex,
                                   b.indexCount);
            UseState(s, key);
            SetVertexPointers(b.firstVertex);
            emscripten_glDrawElements(b.mode, b.indexCount, GL_UNSIGNED_SHORT,
                                      (void const*)(size_t(b.firstIndex) * sizeof(uint16_t)));
            ++s_stats.draws;
        }
        emscripten_glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        emscripten_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
        SetVertexPointers(0);
    }
    else if (s_recording)
        LogOnce("glCallList while compiling a list ignored");

    // what the list's commands leave behind, minus the matrices
    for (int i = 0; i < 3; ++i)
        l.endState.matrix[i] = s_state.matrix[i];
    s_state = l.endState;
    MarkStateChange();
}

void glDeleteLists(GLuint list, GLsizei range)
{
    for (GLsizei i = 0; i < range; ++i)
    {
        std::map<GLuint, DisplayList>::iterator it = s_lists.find(list + i);
        if (it == s_lists.end() || &it->second == s_recording)
            continue;
        // this frame's bloom must not draw from the buffers about to go (RecordEmissiveList)
        for (unsigned j = 0; j < s_bloom.count; ++j)
            if (it->second.vbo && s_bloom.batches[j].listVbo == it->second.vbo)
                s_bloom.batches[j].indexCount = 0;
        if (it->second.vbo)
            emscripten_glDeleteBuffers(1, &it->second.vbo);
        if (it->second.ibo)
            emscripten_glDeleteBuffers(1, &it->second.ibo);
        s_lists.erase(it);
    }
}

// textures

void glBindTexture(GLenum target, GLuint texture)
{
    EnsureState();
    EnsureInit();
    if (target != GL_TEXTURE_2D)
        return;
    if (s_state.texture != texture)
    {
        s_state.texture = texture;
        MarkStateChange();
    }
    // bind right away: the upload and parameter calls that follow go to the
    // context directly; a pending batch rebinds its own texture when drawn
    if (s_real.texture != texture)
    {
        emscripten_glBindTexture(GL_TEXTURE_2D, texture);
        s_real.texture = texture;
        ++s_stats.textureBinds;
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint,
                  GLenum format, GLenum type, void const* pixels)
{
    EnsureInit();
    FlushIfTextureUsed(s_real.texture);
    TexImage(target, level, internalformat, width, height, format, type, pixels);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    EnsureInit();
    FlushIfTextureUsed(s_real.texture);
    if (param == GL_CLAMP)
        param = GL_CLAMP_TO_EDGE;
    emscripten_glTexParameteri(target, pname, param);
}

void glDeleteTextures(GLsizei n, GLuint const* textures)
{
    EnsureInit();
    for (GLsizei i = 0; i < n; ++i)
    {
        FlushIfTextureUsed(textures[i]);
        if (s_real.texture == textures[i])
            s_real.texture = 0;
        if (s_state.texture == textures[i])
        {
            s_state.texture = 0;
            MarkStateChange();
        }
    }
    emscripten_glDeleteTextures(n, textures);
}

GLint gluBuild2DMipmaps(GLenum target, GLint internalFormat, GLsizei width, GLsizei height, GLenum format,
                        GLenum type, void const* data)
{
    EnsureInit();
    FlushIfTextureUsed(s_real.texture);
    TexImage(target, 0, internalFormat, width, height, format, type, data);
    emscripten_glGenerateMipmap(target);
    return 0;
}

// frame level calls

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    EnsureInit();
    Flush();
    s_viewport[0] = x;
    s_viewport[1] = y;
    s_viewport[2] = width;
    s_viewport[3] = height;
    emscripten_glViewport(x, y, width, height);
}

void glClear(GLbitfield mask)
{
    EnsureInit();
    Flush();
    if ((mask & GL_DEPTH_BUFFER_BIT) && !s_real.depthMask)
    {
        // the depth clear needs the mask; the next draw restores it
        emscripten_glDepthMask(GL_TRUE);
        s_real.depthMask = 1;
    }
    emscripten_glClear(mask);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void* pixels)
{
    EnsureInit();
    Flush();
    emscripten_glReadPixels(x, y, width, height, format, type, pixels);
}

static void EndFrame()
{
    Flush();
    s_lastStats = s_stats;
    memset(&s_stats, 0, sizeof(s_stats));
}

void glFlush(void)
{
    EnsureInit();
    EndFrame();
    emscripten_glFlush();
}

void glFinish(void)
{
    // no glFinish: WebGL presents when the frame callback returns and a
    // finish would stall on a busy GPU process for nothing
    EnsureInit();
    EndFrame();
    emscripten_glFlush();
}

void glGetFloatv(GLenum pname, GLfloat* params)
{
    EnsureState();
    switch (pname)
    {
    case GL_MODELVIEW_MATRIX:
        memcpy(params, s_stacks[MAT_MODELVIEW].top.m, sizeof(Mat4));
        return;
    case GL_PROJECTION_MATRIX:
        memcpy(params, s_stacks[MAT_PROJECTION].top.m, sizeof(Mat4));
        return;
    case GL_TEXTURE_MATRIX:
        memcpy(params, s_stacks[MAT_TEXTURE].top.m, sizeof(Mat4));
        return;
    case GL_CURRENT_COLOR:
        memcpy(params, s_color, sizeof(s_color));
        return;
    default:
        emscripten_glGetFloatv(pname, params);
        return;
    }
}

// GLU

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
    double top = zNear * tan(fovy * 3.14159265358979323846 / 360.0);
    double right = top * aspect;
    glFrustum(-right, right, -top, top, zNear, zFar);
}

void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ, GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ)
{
    double f[3] = {centerX - eyeX, centerY - eyeY, centerZ - eyeZ};
    double fl = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl <= 0)
        return;
    for (int i = 0; i < 3; ++i)
        f[i] /= fl;
    double up[3] = {upX, upY, upZ};
    double s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
    double sl = sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    if (sl <= 0)
        return;
    for (int i = 0; i < 3; ++i)
        s[i] /= sl;
    double u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};

    Mat4 m = Identity();
    m.m[0] = static_cast<float>(s[0]);
    m.m[4] = static_cast<float>(s[1]);
    m.m[8] = static_cast<float>(s[2]);
    m.m[1] = static_cast<float>(u[0]);
    m.m[5] = static_cast<float>(u[1]);
    m.m[9] = static_cast<float>(u[2]);
    m.m[2] = static_cast<float>(-f[0]);
    m.m[6] = static_cast<float>(-f[1]);
    m.m[10] = static_cast<float>(-f[2]);
    glMultMatrixf(m.m);
    glTranslatef(static_cast<float>(-eyeX), static_cast<float>(-eyeY), static_cast<float>(-eyeZ));
}

GLubyte const* gluErrorString(GLenum error)
{
    switch (error)
    {
    case GL_NO_ERROR:
        return reinterpret_cast<GLubyte const*>("no error");
    case GL_INVALID_ENUM:
        return reinterpret_cast<GLubyte const*>("invalid enumerant");
    case GL_INVALID_VALUE:
        return reinterpret_cast<GLubyte const*>("invalid value");
    case GL_INVALID_OPERATION:
        return reinterpret_cast<GLubyte const*>("invalid operation");
    case GL_OUT_OF_MEMORY:
        return reinterpret_cast<GLubyte const*>("out of memory");
    default:
        return reinterpret_cast<GLubyte const*>("unknown GL error");
    }
}

GLUquadric* gluNewQuadric(void)
{
    // no state is needed; any non-null pointer identifies the quadric
    static int quadric;
    return reinterpret_cast<GLUquadric*>(&quadric);
}

void gluDeleteQuadric(GLUquadric*)
{
}

void gluSphere(GLUquadric*, GLdouble radius, GLint slices, GLint stacks)
{
    bool wasInBegin = s_inBegin;
    GLenum mode = s_beginMode;
    if (wasInBegin)
        glEnd();
    Sphere(radius, slices, stacks);
    if (wasInBegin)
        glBegin(mode);
}

} // extern "C"

#endif // AA_GLES2
