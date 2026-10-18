/* SDL 1.2 shaped shim for the browser client.

   The wasm build links no SDL at all. The client sources keep including
   <SDL.h> and calling the SDL 1.2 API they always used (rSDL.h keeps the 1.2
   shape for the SDL2 native build too); this header declares exactly the
   subset they reach, and sdl_shim.cpp implements it on Emscripten's html5.h
   (WebGL context, canvas size, keyboard/mouse/focus callbacks) and libpng
   (IMG_Load). Everything else (audio, threads, window manager) is a stub.

   Key events carry the SDL 1.2 keysym numbers, so keymap[], the config files
   (KEYBOARD 273 ...) and recordings keep their values; the unicode field
   carries the character of the browser's own layout. */

#ifndef AA_SDLSHIM_H
#define AA_SDLSHIM_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

#define SDLCALL

/* --- version: report 1.2.15, the code takes its SDL 1.2 branches --------- */

typedef struct SDL_version
{
    Uint8 major, minor, patch;
} SDL_version;

#define SDL_MAJOR_VERSION 1
#define SDL_MINOR_VERSION 2
#define SDL_PATCHLEVEL 15
#define SDL_VERSION(x) do { (x)->major = 1; (x)->minor = 2; (x)->patch = 15; } while (0)
#define SDL_VERSION_ATLEAST(X, Y, Z) ((X) < 2)
SDL_version const* SDL_Linked_Version(void);

/* --- init, errors, time ------------------------------------------------- */

#define SDL_INIT_TIMER 0x00000001
#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_NOPARACHUTE 0x00100000
#define SDL_INIT_EVERYTHING 0x0000FFFF

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);
char const* SDL_GetError(void);
void SDL_ClearError(void);
void SDL_Delay(Uint32 ms);
Uint32 SDL_GetTicks(void);

/* --- surfaces ----------------------------------------------------------- */

typedef struct SDL_Rect
{
    Sint16 x, y;
    Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Color
{
    Uint8 r, g, b, unused;
} SDL_Color;

typedef struct SDL_Palette
{
    int ncolors;
    SDL_Color* colors;
} SDL_Palette;

typedef struct SDL_PixelFormat
{
    SDL_Palette* palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint32 colorkey;
    Uint8 alpha;
} SDL_PixelFormat;

/* tightly packed rows (pitch == w * BytesPerPixel), the layout the texture
   uploads and the screenshot code assume */
typedef struct SDL_Surface
{
    Uint32 flags;
    SDL_PixelFormat* format;
    int w, h;
    Uint16 pitch;
    void* pixels;
    SDL_Rect clip_rect;
    int refcount;
    SDL_PixelFormat format_;    /* storage for format */
} SDL_Surface;

#define SDL_SWSURFACE 0x00000000
#define SDL_HWSURFACE 0x00000001
#define SDL_DOUBLEBUF 0x40000000
#define SDL_FULLSCREEN 0x80000000
#define SDL_OPENGL 0x00000002

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface* SDL_ConvertSurface(SDL_Surface* src, SDL_PixelFormat* fmt, Uint32 flags);
void SDL_FreeSurface(SDL_Surface* surface);
int SDL_SaveBMP(SDL_Surface* surface, char const* file);

/* --- video -------------------------------------------------------------- */

typedef struct SDL_VideoInfo
{
    Uint32 hw_available : 1;
    Uint32 wm_available : 1;
    Uint32 blit_hw : 1;
    Uint32 video_mem;
    SDL_PixelFormat* vfmt;
    int current_w;
    int current_h;
} SDL_VideoInfo;

typedef enum
{
    SDL_GL_RED_SIZE,
    SDL_GL_GREEN_SIZE,
    SDL_GL_BLUE_SIZE,
    SDL_GL_ALPHA_SIZE,
    SDL_GL_BUFFER_SIZE,
    SDL_GL_DOUBLEBUFFER,
    SDL_GL_DEPTH_SIZE,
    SDL_GL_STENCIL_SIZE,
    SDL_GL_SWAP_CONTROL = 16
} SDL_GLattr;

SDL_VideoInfo const* SDL_GetVideoInfo(void);
SDL_Rect** SDL_ListModes(SDL_PixelFormat* format, Uint32 flags);
int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
SDL_Surface* SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
int SDL_GL_SetAttribute(SDL_GLattr attr, int value);
int SDL_GL_GetAttribute(SDL_GLattr attr, int* value);
void SDL_GL_SwapBuffers(void);
void* SDL_GL_GetProcAddress(char const* proc);
void SDL_WM_SetCaption(char const* title, char const* icon);
void SDL_WM_SetIcon(SDL_Surface* icon, Uint8* mask);
int SDL_WM_IconifyWindow(void);
int SDL_ShowCursor(int toggle);
void SDL_WarpMouse(Uint16 x, Uint16 y);

#define SDL_APPMOUSEFOCUS 0x01
#define SDL_APPINPUTFOCUS 0x02
#define SDL_APPACTIVE 0x04
Uint8 SDL_GetAppState(void);

/* --- keys: the SDL 1.2 numbering ---------------------------------------- */

typedef enum
{
    SDLK_UNKNOWN = 0,
    SDLK_FIRST = 0,
    SDLK_BACKSPACE = 8,
    SDLK_TAB = 9,
    SDLK_CLEAR = 12,
    SDLK_RETURN = 13,
    SDLK_PAUSE = 19,
    SDLK_ESCAPE = 27,
    SDLK_SPACE = 32,
    SDLK_EXCLAIM = 33,
    SDLK_QUOTEDBL = 34,
    SDLK_HASH = 35,
    SDLK_DOLLAR = 36,
    SDLK_AMPERSAND = 38,
    SDLK_QUOTE = 39,
    SDLK_LEFTPAREN = 40,
    SDLK_RIGHTPAREN = 41,
    SDLK_ASTERISK = 42,
    SDLK_PLUS = 43,
    SDLK_COMMA = 44,
    SDLK_MINUS = 45,
    SDLK_PERIOD = 46,
    SDLK_SLASH = 47,
    SDLK_0 = 48,
    SDLK_1 = 49,
    SDLK_2 = 50,
    SDLK_3 = 51,
    SDLK_4 = 52,
    SDLK_5 = 53,
    SDLK_6 = 54,
    SDLK_7 = 55,
    SDLK_8 = 56,
    SDLK_9 = 57,
    SDLK_COLON = 58,
    SDLK_SEMICOLON = 59,
    SDLK_LESS = 60,
    SDLK_EQUALS = 61,
    SDLK_GREATER = 62,
    SDLK_QUESTION = 63,
    SDLK_AT = 64,
    SDLK_LEFTBRACKET = 91,
    SDLK_BACKSLASH = 92,
    SDLK_RIGHTBRACKET = 93,
    SDLK_CARET = 94,
    SDLK_UNDERSCORE = 95,
    SDLK_BACKQUOTE = 96,
    SDLK_a = 97,
    SDLK_b = 98,
    SDLK_c = 99,
    SDLK_d = 100,
    SDLK_e = 101,
    SDLK_f = 102,
    SDLK_g = 103,
    SDLK_h = 104,
    SDLK_i = 105,
    SDLK_j = 106,
    SDLK_k = 107,
    SDLK_l = 108,
    SDLK_m = 109,
    SDLK_n = 110,
    SDLK_o = 111,
    SDLK_p = 112,
    SDLK_q = 113,
    SDLK_r = 114,
    SDLK_s = 115,
    SDLK_t = 116,
    SDLK_u = 117,
    SDLK_v = 118,
    SDLK_w = 119,
    SDLK_x = 120,
    SDLK_y = 121,
    SDLK_z = 122,
    SDLK_DELETE = 127,
    SDLK_KP0 = 256,
    SDLK_KP1 = 257,
    SDLK_KP2 = 258,
    SDLK_KP3 = 259,
    SDLK_KP4 = 260,
    SDLK_KP5 = 261,
    SDLK_KP6 = 262,
    SDLK_KP7 = 263,
    SDLK_KP8 = 264,
    SDLK_KP9 = 265,
    SDLK_KP_PERIOD = 266,
    SDLK_KP_DIVIDE = 267,
    SDLK_KP_MULTIPLY = 268,
    SDLK_KP_MINUS = 269,
    SDLK_KP_PLUS = 270,
    SDLK_KP_ENTER = 271,
    SDLK_KP_EQUALS = 272,
    SDLK_UP = 273,
    SDLK_DOWN = 274,
    SDLK_RIGHT = 275,
    SDLK_LEFT = 276,
    SDLK_INSERT = 277,
    SDLK_HOME = 278,
    SDLK_END = 279,
    SDLK_PAGEUP = 280,
    SDLK_PAGEDOWN = 281,
    SDLK_F1 = 282,
    SDLK_F2 = 283,
    SDLK_F3 = 284,
    SDLK_F4 = 285,
    SDLK_F5 = 286,
    SDLK_F6 = 287,
    SDLK_F7 = 288,
    SDLK_F8 = 289,
    SDLK_F9 = 290,
    SDLK_F10 = 291,
    SDLK_F11 = 292,
    SDLK_F12 = 293,
    SDLK_F13 = 294,
    SDLK_F14 = 295,
    SDLK_F15 = 296,
    SDLK_NUMLOCK = 300,
    SDLK_CAPSLOCK = 301,
    SDLK_SCROLLOCK = 302,
    SDLK_RSHIFT = 303,
    SDLK_LSHIFT = 304,
    SDLK_RCTRL = 305,
    SDLK_LCTRL = 306,
    SDLK_RALT = 307,
    SDLK_LALT = 308,
    SDLK_RMETA = 309,
    SDLK_LMETA = 310,
    SDLK_LSUPER = 311,
    SDLK_RSUPER = 312,
    SDLK_MODE = 313,
    SDLK_COMPOSE = 314,
    SDLK_HELP = 315,
    SDLK_PRINT = 316,
    SDLK_SYSREQ = 317,
    SDLK_BREAK = 318,
    SDLK_MENU = 319,
    SDLK_POWER = 320,
    SDLK_EURO = 321,
    SDLK_UNDO = 322,
    SDLK_LAST
} SDLKey;

typedef enum
{
    KMOD_NONE = 0x0000,
    KMOD_LSHIFT = 0x0001,
    KMOD_RSHIFT = 0x0002,
    KMOD_LCTRL = 0x0040,
    KMOD_RCTRL = 0x0080,
    KMOD_LALT = 0x0100,
    KMOD_RALT = 0x0200,
    KMOD_LMETA = 0x0400,
    KMOD_RMETA = 0x0800,
    KMOD_NUM = 0x1000,
    KMOD_CAPS = 0x2000,
    KMOD_MODE = 0x4000,
    KMOD_RESERVED = 0x8000
} SDLMod;

#define KMOD_CTRL (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT (KMOD_LALT | KMOD_RALT)
#define KMOD_META (KMOD_LMETA | KMOD_RMETA)

typedef struct SDL_keysym
{
    Uint8 scancode;
    SDLKey sym;
    SDLMod mod;
    Uint16 unicode;
} SDL_keysym;

char const* SDL_GetKeyName(SDLKey key);
int SDL_EnableUNICODE(int enable);
int SDL_EnableKeyRepeat(int delay, int interval);

/* --- events ------------------------------------------------------------- */

enum
{
    SDL_NOEVENT = 0,
    SDL_ACTIVEEVENT,
    SDL_KEYDOWN,
    SDL_KEYUP,
    SDL_MOUSEMOTION,
    SDL_MOUSEBUTTONDOWN,
    SDL_MOUSEBUTTONUP,
    SDL_JOYAXISMOTION,
    SDL_JOYBALLMOTION,
    SDL_JOYHATMOTION,
    SDL_JOYBUTTONDOWN,
    SDL_JOYBUTTONUP,
    SDL_QUIT,
    SDL_SYSWMEVENT,
    SDL_EVENT_RESERVEDA,
    SDL_EVENT_RESERVEDB,
    SDL_VIDEORESIZE,
    SDL_VIDEOEXPOSE,
    SDL_NUMEVENTS = 32
};

#define SDL_RELEASED 0
#define SDL_PRESSED 1
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_WHEELUP 4
#define SDL_BUTTON_WHEELDOWN 5

typedef struct SDL_ActiveEvent
{
    Uint8 type;
    Uint8 gain;
    Uint8 state;
} SDL_ActiveEvent;

typedef struct SDL_KeyboardEvent
{
    Uint8 type;
    Uint8 which;
    Uint8 state;
    SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent
{
    Uint8 type;
    Uint8 which;
    Uint8 state;
    Uint16 x, y;
    Sint16 xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent
{
    Uint8 type;
    Uint8 which;
    Uint8 button;
    Uint8 state;
    Uint16 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_ResizeEvent
{
    Uint8 type;
    int w, h;
} SDL_ResizeEvent;

typedef struct SDL_QuitEvent
{
    Uint8 type;
} SDL_QuitEvent;

typedef union SDL_Event
{
    Uint8 type;
    SDL_ActiveEvent active;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_ResizeEvent resize;
    SDL_QuitEvent quit;
} SDL_Event;

typedef int (*SDL_EventFilter)(SDL_Event const* event);

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event* event);
int SDL_PushEvent(SDL_Event* event);
void SDL_SetEventFilter(SDL_EventFilter filter);

/* --- threads: never started (rSysDep::StartNetSyncThread returns first) -- */

struct SDL_mutex;
struct SDL_Thread;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_Thread SDL_Thread;
SDL_mutex* SDL_CreateMutex(void);
int SDL_mutexP(SDL_mutex* mutex);
int SDL_mutexV(SDL_mutex* mutex);
void SDL_DestroyMutex(SDL_mutex* mutex);
SDL_Thread* SDL_CreateThread(int (*fn)(void*), void* data);
void SDL_WaitThread(SDL_Thread* thread, int* status);

/* --- audio: always fails to open (NOSOUND is set anyway) ---------------- */

#define AUDIO_U8 0x0008
#define AUDIO_S16LSB 0x8010
#define AUDIO_S16SYS AUDIO_S16LSB

typedef struct SDL_AudioSpec
{
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint16 padding;
    Uint32 size;
    void (*callback)(void* userdata, Uint8* stream, int len);
    void* userdata;
} SDL_AudioSpec;

typedef struct SDL_AudioCVT
{
    int needed;
    Uint16 src_format;
    Uint16 dst_format;
    double rate_incr;
    Uint8* buf;
    int len;
    int len_cvt;
    int len_mult;
    double len_ratio;
} SDL_AudioCVT;

struct SDL_RWops;
typedef struct SDL_RWops SDL_RWops;
SDL_RWops* SDL_RWFromFile(char const* file, char const* mode);
int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops* src, int freesrc, SDL_AudioSpec* spec, Uint8** audio_buf, Uint32* audio_len);
#define SDL_LoadWAV(file, spec, audio_buf, audio_len) SDL_LoadWAV_RW(SDL_RWFromFile(file, "rb"), 1, spec, audio_buf, audio_len)
void SDL_FreeWAV(Uint8* audio_buf);
int SDL_BuildAudioCVT(SDL_AudioCVT* cvt, Uint16 src_format, Uint8 src_channels, int src_rate,
                      Uint16 dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT* cvt);

/* --- SDL_image: PNG through libpng -------------------------------------- */

SDL_Surface* IMG_Load(char const* file);
void IMG_InvertAlpha(int on);

/* --- browser specifics, used by the wasm glue only ---------------------- */

/* backing store size of the canvas after the last (re)size, in device
   pixels; the shim keeps sr_screenWidth/sr_screenHeight in step with it */
void sdlshim_GetCanvasSize(int* width, int* height);
/* the device pixel ratio the backing store uses (clamped to 1..2) */
double sdlshim_GetDevicePixelRatio(void);
/* once per frame: re-sizes the backing store when the pixel ratio changed */
void sdlshim_FrameCheck(void);

/* render resolution: the backing store's pixels per CSS pixel, at most the
   device pixel ratio (0 = the ratio, the default; at least .5); the browser
   scales the canvas up to its CSS size. Get: the density in use. */
void sdlshim_SetRenderDensity(double density);
double sdlshim_GetRenderDensity(void);

/* multisampling of the offscreen frame (WebGL 2 only): the wanted sample
   count (0 off; clamped to GL_MAX_SAMPLES, kept until the context exists) and
   the count in use (0 on WebGL 1, where the setting is ignored) */
void sdlshim_SetMultisample(int samples);
int sdlshim_GetMultisample(void);

/* called right after a key press was queued, still inside the DOM event: the
   game runs a frame from it so the turn goes out now, not at the next
   animation frame */
typedef void (*sdlshim_InputCallback)(void);
void sdlshim_SetInputCallback(sdlshim_InputCallback callback);

/* the key table the shim translates DOM KeyboardEvent.code values with, for
   the page's key binding UI: SDL 1.2 keysym for a code (SDLK_UNKNOWN = 0 if
   the shim would drop the key) and the code for a keysym ("" if none) */
int sdlshim_KeysymFromCode(char const* code);
char const* sdlshim_CodeFromKeysym(int keysym);

/* audio (Web Audio behind SDL_OpenAudio): state 0 no device, 1 suspended
   (waiting for the page's user gesture), 2 running, 3 closed; callbacks is
   the number of mixer callbacks since the last call, nonSilent how many of
   them produced something other than silence (status line) */
void sdlshim_GetAudioStats(int* state, int* callbacks, int* nonSilent);

#endif /* AA_SDLSHIM_H */
