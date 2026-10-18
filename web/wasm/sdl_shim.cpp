/* SDL 1.2 shaped shim for the browser client, see sdlshim/SDL.h.

   Display: one WebGL 2 (or 1) context on #canvas for src/render/rGLES2.cpp, the
   canvas backing store sized from its CSS size times the
   device pixel ratio (capped at 2), re-sized from the window's resize event;
   sr_screenWidth/sr_screenHeight follow. On WebGL 2 the game draws into an
   offscreen framebuffer (with a multisampled twin under BROWSER_MSAA,
   sdlshim_SetMultisample) that SDL_GL_SwapBuffers copies into the canvas;
   the animation frame presents.

   Input: html5.h keyboard callbacks on the window, mouse/wheel callbacks on
   the canvas; a window blur releases the keys held down (no
   SDL_ACTIVEEVENT: the browser client keeps rendering without the focus).
   Key events are translated to
   the SDL 1.2 keysym numbers (physical key -> US layout sym, so bindings
   from config files keep working), the unicode field carries the key's
   character in the browser's own layout; only the browser's own shortcuts
   are left to it (BrowserKey). Events pass the filter set with
   SDL_SetEventFilter as they arrive (SDL 1.2 semantics) and queue up for
   SDL_PollEvent.

   Images: IMG_Load decodes PNG through libpng into tightly packed
   1/3/4-byte surfaces (gray, RGB, RGBA; gray+alpha expands to RGBA), the formats
   rSurface::Create maps to GL.

   Audio: SDL_OpenAudio opens a Web Audio context with a ScriptProcessorNode
   whose onaudioprocess runs the game's mixer callback (eSound's fill_audio,
   16 bit stereo at the context's sample rate) into a wasm buffer and
   converts it to floats; it starts suspended until the page's first user
   gesture (aa_api.js resumes it, Module.__aaAudio.unlock). Threads and the
   window manager are stubs. */

#include "sdlshim/SDL.h"

#include "rScreen.h"

// src/render/rGLES2.h declared, not included: that header is the game's GL 1.x
// front end and would clash with the GLES2/GLES3 declarations below
bool sr_GLES2BloomPending();
void sr_GLES2Bloom(unsigned int target, unsigned int depth, int width, int height);
bool sr_GLES2DepthOfFieldPending();
void sr_GLES2DepthOfField(unsigned int target, int width, int height);
void sr_GLES2SetBloom(bool on, float intensity);

#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgl/webgl1.h>
#include <webgl/webgl2.h>
#include <png.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
#include <string>

// true while a text field of the page has the focus, or anything inside an element
// marked data-aa-keys="page" (a page menu that takes the keyboard while it is open,
// like the desktop client's in-game menu): its keys are the page's, not the game's
// (the HTML chat line, console input, settings, the in-game menu)
EM_JS(int, TextFieldFocused, (), {
    var element = document.activeElement;
    if (!element)
        return 0;
    if (element.isContentEditable)
        return 1;
    var tag = element.tagName;
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT")
        return 1;
    return (element.closest && element.closest('[data-aa-keys="page"]')) ? 1 : 0;
});

// true while an element of the page is fullscreen (Fullscreen API): Escape
// then belongs to the browser (it leaves fullscreen) and never reaches the game
EM_JS(int, InFullscreen, (), {
    return (typeof document !== "undefined" && document.fullscreenElement) ? 1 : 0;
});

namespace
{

char const* const s_canvas = "#canvas";

SDL_version const s_version = {1, 2, 15};
std::string s_error;

// --- surfaces ----------------------------------------------------------

SDL_Surface* AllocSurface(int w, int h, int bytesPerPixel, Uint32 rMask, Uint32 gMask, Uint32 bMask, Uint32 aMask)
{
    // pitch and clip_rect are 16 bit; the pixel count is checked in 64 bit
    // so a huge image (a PNG header says what it likes) can't wrap the size
    if (w < 0 || h < 0 || bytesPerPixel < 1 || bytesPerPixel > 4 || w * bytesPerPixel > 0xFFFF || h > 0xFFFF ||
        static_cast<unsigned long long>(w) * h * bytesPerPixel > (256ull << 20))
    {
        s_error = "surface too large";
        return NULL;
    }
    SDL_Surface* s = static_cast<SDL_Surface*>(calloc(1, sizeof(SDL_Surface)));
    if (!s)
        return NULL;
    s->format = &s->format_;
    s->format_.BitsPerPixel = static_cast<Uint8>(bytesPerPixel * 8);
    s->format_.BytesPerPixel = static_cast<Uint8>(bytesPerPixel);
    s->format_.Rmask = rMask;
    s->format_.Gmask = gMask;
    s->format_.Bmask = bMask;
    s->format_.Amask = aMask;
    s->format_.alpha = 255;
    s->w = w;
    s->h = h;
    s->pitch = static_cast<Uint16>(w * bytesPerPixel);
    s->clip_rect.w = static_cast<Uint16>(w);
    s->clip_rect.h = static_cast<Uint16>(h);
    s->refcount = 1;
    size_t const size = static_cast<size_t>(w) * h * bytesPerPixel;
    s->pixels = calloc(size > 0 ? size : 1, 1);
    if (!s->pixels)
    {
        free(s);
        return NULL;
    }
    return s;
}

// --- display -----------------------------------------------------------

EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_context = 0;
SDL_Surface* s_screen = NULL;
SDL_VideoInfo s_videoInfo;
SDL_PixelFormat s_desktopFormat;
int s_canvasWidth = 0;
int s_canvasHeight = 0;
bool s_hasFocus = true;
// the backing store's pixels per CSS pixel the game asked for (0: the device
// pixel ratio); below the ratio the browser scales the canvas up to its CSS
// size (sdlshim_SetRenderDensity, the quality tiers in gBrowserQuality.cpp)
double s_renderDensity = 0;

// the offscreen frame (WebGL 2 only, see sdlshim_InstallLowLatencyContext)
bool s_offscreen = false;
GLuint s_frameFbo = 0;
GLuint s_frameColor = 0;
GLuint s_frameDepth = 0;
int s_frameWidth = 0;
int s_frameHeight = 0;
// its multisampled twin (BROWSER_MSAA): the game draws into this one when it
// exists and the swap resolves it into the single sampled frame first. A
// resolving blit needs identical formats on both sides, which the canvas
// (RGB8 or BGRA8, platform dependent) never guarantees against RGBA8; the
// frame does. s_wantedSamples is what the game asked for, s_frameSamples
// what is in use, s_maxSamples the context's GL_MAX_SAMPLES (-1 until asked).
GLuint s_msFbo = 0;
GLuint s_msColor = 0;
GLuint s_msDepth = 0;
int s_wantedSamples = 0;
int s_frameSamples = 0;
int s_maxSamples = -1;
bool s_msaaIgnoredLogged = false;

int ClampedSamples()
{
    if (s_maxSamples < 0)
        emscripten_glGetIntegerv(GL_MAX_SAMPLES, &s_maxSamples);
    return s_wantedSamples > s_maxSamples ? s_maxSamples : s_wantedSamples;
}

void DeleteMultisampleFrame()
{
    if (!s_msFbo)
        return;
    emscripten_glDeleteFramebuffers(1, &s_msFbo);
    emscripten_glDeleteRenderbuffers(1, &s_msColor);
    emscripten_glDeleteRenderbuffers(1, &s_msDepth);
    s_msFbo = s_msColor = s_msDepth = 0;
    s_frameSamples = 0;
}

// the multisampled frame at the single sampled frame's size, bound for the
// game's drawing when BROWSER_MSAA asks for it
void UpdateMultisampleFrame()
{
    int const samples = ClampedSamples();
    if (samples <= 0)
    {
        DeleteMultisampleFrame();
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_frameFbo);
        return;
    }
    if (!s_msFbo)
    {
        emscripten_glGenFramebuffers(1, &s_msFbo);
        emscripten_glGenRenderbuffers(1, &s_msColor);
        emscripten_glGenRenderbuffers(1, &s_msDepth);
    }
    s_frameSamples = samples;
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, s_msColor);
    emscripten_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, s_frameWidth, s_frameHeight);
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, s_msDepth);
    emscripten_glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, s_frameWidth, s_frameHeight);
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, 0);
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_msFbo);
    emscripten_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_msColor);
    emscripten_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_msDepth);
    if (emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        // this sample count is not supported for these formats after all
        printf("BROWSER: BROWSER_MSAA %d not supported, multisampling off.\n", samples);
        s_wantedSamples = 0;
        DeleteMultisampleFrame();
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_frameFbo);
    }
}

// the offscreen framebuffer at the canvas' size, bound for the game's drawing
void UpdateOffscreenFrame()
{
    if (!s_offscreen)
    {
        if (s_wantedSamples > 0 && !s_msaaIgnoredLogged)
        {
            s_msaaIgnoredLogged = true;
            printf("BROWSER: BROWSER_MSAA ignored: no offscreen frame (WebGL 1).\n");
        }
        return;
    }
    if (s_frameFbo && s_frameWidth == s_canvasWidth && s_frameHeight == s_canvasHeight)
    {
        if (ClampedSamples() != s_frameSamples)
            UpdateMultisampleFrame();
        return;
    }

    if (!s_frameFbo)
    {
        emscripten_glGenFramebuffers(1, &s_frameFbo);
        emscripten_glGenRenderbuffers(1, &s_frameColor);
        emscripten_glGenRenderbuffers(1, &s_frameDepth);
    }
    s_frameWidth = s_canvasWidth;
    s_frameHeight = s_canvasHeight;
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, s_frameColor);
    emscripten_glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, s_frameWidth, s_frameHeight);
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, s_frameDepth);
    emscripten_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, s_frameWidth, s_frameHeight);
    emscripten_glBindRenderbuffer(GL_RENDERBUFFER, 0);
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_frameFbo);
    emscripten_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_frameColor);
    emscripten_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_frameDepth);
    if (emscripten_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        // draw straight into the canvas after all (the hint stays; better a
        // rare flicker than no picture)
        printf("BROWSER: offscreen frame incomplete, drawing to the canvas.\n");
        emscripten_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        emscripten_glDeleteFramebuffers(1, &s_frameFbo);
        emscripten_glDeleteRenderbuffers(1, &s_frameColor);
        emscripten_glDeleteRenderbuffers(1, &s_frameDepth);
        s_frameFbo = s_frameColor = s_frameDepth = 0;
        DeleteMultisampleFrame();
        s_offscreen = false;
        return;
    }
    UpdateMultisampleFrame();
}

// the canvas backing store from its CSS size and the device pixel ratio
void UpdateCanvasSize()
{
    double cssWidth = 0, cssHeight = 0;
    emscripten_get_element_css_size(s_canvas, &cssWidth, &cssHeight);
    double ratio = emscripten_get_device_pixel_ratio();
    if (ratio > 2)
        ratio = 2;
    if (ratio < 1)
        ratio = 1;
    if (s_renderDensity > 0 && ratio > s_renderDensity)
        ratio = s_renderDensity;

    int width = static_cast<int>(cssWidth * ratio + .5);
    int height = static_cast<int>(cssHeight * ratio + .5);
    if (width < 1 || height < 1)
    {
        width = 800;
        height = 600;
    }

    if (width != s_canvasWidth || height != s_canvasHeight)
    {
        emscripten_set_canvas_element_size(s_canvas, width, height);
        s_canvasWidth = width;
        s_canvasHeight = height;
    }
    UpdateOffscreenFrame();

    sr_screenWidth = width;
    sr_screenHeight = height;
    if (s_screen)
    {
        s_screen->w = width;
        s_screen->h = height;
    }
}

EM_BOOL OnResize(int, EmscriptenUiEvent const*, void*)
{
    if (s_context)
        UpdateCanvasSize();
    return EM_FALSE;
}

} // namespace

// the canvas' CSS size or the device pixel ratio changed without a window
// resize (a layout change, the window moved to another screen, zoom)
extern "C" EMSCRIPTEN_KEEPALIVE void sdlshim_canvas_changed(void)
{
    if (s_context)
        UpdateCanvasSize();
}

// a ResizeObserver on the canvas and a matchMedia listener on the resolution
EM_JS(void, sdlshim_InstallSizeObserver, (), {
    var canvas = document.getElementById("canvas");
    if (!canvas)
        return;
    var changed = function () { Module["_sdlshim_canvas_changed"](); };
    if (typeof ResizeObserver === "function")
        new ResizeObserver(changed).observe(canvas);
    var watchRatio = function () {
        var query = matchMedia("(resolution: " + window.devicePixelRatio + "dppx)");
        var listener = function () {
            query.removeEventListener("change", listener);
            changed();
            watchRatio();
        };
        query.addEventListener("change", listener);
    };
    if (typeof matchMedia === "function")
        watchRatio();
});

/* Low latency presentation: "desynchronized" (Chrome, Edge: the canvas
   bypasses the compositor's frame queue, like a native client with vsync
   off; ignored elsewhere) and the discrete GPU on dual GPU laptops. Neither
   is an EmscriptenWebGLContextAttributes field, so they are added to the
   attributes the Emscripten GL library passes to the canvas' getContext.
   On a 144 Hz display it cuts the time from a turn key's frame to its paint
   from 16-24 ms to under 16 ms.
   Chrome implements the hint as front buffer rendering: the canvas is
   scanned out while the game is still drawing into it, and whatever is
   drawn last (trails, HUD) flickers. So the game renders into an offscreen
   framebuffer and SDL_GL_SwapBuffers copies the finished frame into the
   canvas with one glBlitFramebuffer (WebGL 2; the hint is only requested
   when that path is available). With BROWSER_MSAA the game draws into a
   multisampled twin of that frame, resolved into it by a first blit (see the
   declarations above UpdateMultisampleFrame). */
EM_JS(void, sdlshim_InstallLowLatencyContext, (int enabled), {
    var canvas = document.getElementById("canvas");
    if (!canvas)
        return;
    canvas.__aaLowLatency = !!enabled;
    if (canvas.__aaLowLatencyInstalled)
        return;
    canvas.__aaLowLatencyInstalled = true;
    var getContext = canvas.getContext;
    canvas.getContext = function (type, attributes) {
        if ((type === "webgl" || type === "webgl2") && canvas.__aaLowLatency)
        {
            attributes = Object.assign({}, attributes || {});
            attributes.desynchronized = true;
            attributes.powerPreference = "high-performance";
        }
        return getContext.call(this, type, attributes);
    };
});

namespace
{

// --- events ------------------------------------------------------------

std::deque<SDL_Event> s_events;
std::set<SDLKey> s_keysDown;
SDL_EventFilter s_filter = NULL;
sdlshim_InputCallback s_inputCallback = NULL;
bool s_callbacksInstalled = false;
Uint8 s_mouseButtons = 0;
Uint16 s_mouseX = 0, s_mouseY = 0;

void Push(SDL_Event const& event)
{
    if (s_filter && !s_filter(&event))
        return;
    if (s_events.size() < 256)
        s_events.push_back(event);
}

SDLMod Modifiers(EmscriptenKeyboardEvent const* e)
{
    int mod = KMOD_NONE;
    if (e->shiftKey)
        mod |= KMOD_LSHIFT;
    if (e->ctrlKey)
        mod |= KMOD_LCTRL;
    if (e->altKey)
        mod |= KMOD_LALT;
    if (e->metaKey)
        mod |= KMOD_LMETA;
    return static_cast<SDLMod>(mod);
}

struct KeyName
{
    char const* code;
    SDLKey sym;
};

// physical key (KeyboardEvent.code) -> SDL 1.2 keysym of the US layout
KeyName const s_codes[] = {
    {"Escape", SDLK_ESCAPE}, {"Enter", SDLK_RETURN}, {"Tab", SDLK_TAB}, {"Space", SDLK_SPACE},
    {"Backspace", SDLK_BACKSPACE}, {"Delete", SDLK_DELETE}, {"Insert", SDLK_INSERT},
    {"Home", SDLK_HOME}, {"End", SDLK_END}, {"PageUp", SDLK_PAGEUP}, {"PageDown", SDLK_PAGEDOWN},
    {"ArrowUp", SDLK_UP}, {"ArrowDown", SDLK_DOWN}, {"ArrowLeft", SDLK_LEFT}, {"ArrowRight", SDLK_RIGHT},
    {"ShiftLeft", SDLK_LSHIFT}, {"ShiftRight", SDLK_RSHIFT}, {"ControlLeft", SDLK_LCTRL},
    {"ControlRight", SDLK_RCTRL}, {"AltLeft", SDLK_LALT}, {"AltRight", SDLK_RALT},
    {"MetaLeft", SDLK_LMETA}, {"MetaRight", SDLK_RMETA}, {"OSLeft", SDLK_LSUPER}, {"OSRight", SDLK_RSUPER},
    {"CapsLock", SDLK_CAPSLOCK}, {"NumLock", SDLK_NUMLOCK}, {"ScrollLock", SDLK_SCROLLOCK},
    {"Pause", SDLK_PAUSE}, {"PrintScreen", SDLK_PRINT}, {"ContextMenu", SDLK_MENU},
    {"Minus", SDLK_MINUS}, {"Equal", SDLK_EQUALS}, {"BracketLeft", SDLK_LEFTBRACKET},
    {"BracketRight", SDLK_RIGHTBRACKET}, {"Backslash", SDLK_BACKSLASH}, {"Semicolon", SDLK_SEMICOLON},
    {"Quote", SDLK_QUOTE}, {"Backquote", SDLK_BACKQUOTE}, {"Comma", SDLK_COMMA}, {"Period", SDLK_PERIOD},
    {"Slash", SDLK_SLASH}, {"IntlBackslash", SDLK_LESS},
    {"Numpad0", SDLK_KP0}, {"Numpad1", SDLK_KP1}, {"Numpad2", SDLK_KP2}, {"Numpad3", SDLK_KP3},
    {"Numpad4", SDLK_KP4}, {"Numpad5", SDLK_KP5}, {"Numpad6", SDLK_KP6}, {"Numpad7", SDLK_KP7},
    {"Numpad8", SDLK_KP8}, {"Numpad9", SDLK_KP9}, {"NumpadDecimal", SDLK_KP_PERIOD},
    {"NumpadDivide", SDLK_KP_DIVIDE}, {"NumpadMultiply", SDLK_KP_MULTIPLY}, {"NumpadSubtract", SDLK_KP_MINUS},
    {"NumpadAdd", SDLK_KP_PLUS}, {"NumpadEnter", SDLK_KP_ENTER}, {"NumpadEqual", SDLK_KP_EQUALS},
};

SDLKey SymFromCode(char const* code)
{
    size_t const len = strlen(code);
    if (len == 4 && strncmp(code, "Key", 3) == 0 && code[3] >= 'A' && code[3] <= 'Z')
        return static_cast<SDLKey>(SDLK_a + (code[3] - 'A'));
    if (len == 6 && strncmp(code, "Digit", 5) == 0 && code[5] >= '0' && code[5] <= '9')
        return static_cast<SDLKey>(SDLK_0 + (code[5] - '0'));
    if (code[0] == 'F' && len >= 2 && len <= 3)
    {
        int n = atoi(code + 1);
        if (n >= 1 && n <= 15)
            return static_cast<SDLKey>(SDLK_F1 + n - 1);
    }
    for (KeyName const& k : s_codes)
        if (strcmp(k.code, code) == 0)
            return k.sym;
    return SDLK_UNKNOWN;
}

// the character of KeyboardEvent.key (one UTF-16 unit), else the control
// character SDL 1.2 put there
Uint16 UnicodeFromKey(char const* key, SDLKey sym)
{
    // decode the first UTF-8 sequence and require it to be the only one
    unsigned char const* k = reinterpret_cast<unsigned char const*>(key);
    unsigned int cp = 0;
    size_t len = 0;
    if (k[0] < 0x80)
    {
        cp = k[0];
        len = 1;
    }
    else if ((k[0] & 0xE0) == 0xC0)
    {
        cp = ((k[0] & 0x1F) << 6) | (k[1] & 0x3F);
        len = 2;
    }
    else if ((k[0] & 0xF0) == 0xE0)
    {
        cp = ((k[0] & 0x0F) << 12) | ((k[1] & 0x3F) << 6) | (k[2] & 0x3F);
        len = 3;
    }
    if (len > 0 && k[len] == 0 && cp >= 32 && cp < 0x10000)
        return static_cast<Uint16>(cp);

    switch (sym)
    {
    case SDLK_ESCAPE:
    case SDLK_RETURN:
    case SDLK_BACKSPACE:
    case SDLK_TAB:
    case SDLK_DELETE:
        return static_cast<Uint16>(sym);
    case SDLK_KP_ENTER:
        return 13;
    default:
        // a printable key whose DOM key carries no character: "Process" while an IME
        // (Korean, Chinese, Japanese) is on, "Dead" for accent keys, "Unidentified".
        // uInputQueue drops letter, digit and sign keys with no character as bogus,
        // so turn keys bound to letters did nothing; the keysym's own character it is
        if (sym >= 32 && sym < 127)
            return static_cast<Uint16>(sym);
        return 0;
    }
}

/* Keys the browser keeps (not delivered, not preventDefault'ed); everything
   else goes to the game, modifiers included, so Ctrl/Alt/Shift can be bound
   (brake on Ctrl is common) and a turn works while one is held. The reserved
   list, documented in API.md: every Meta/Cmd/Win combination (the OS's), F5,
   F11, F12 (reload, fullscreen, devtools), Escape while an element is
   fullscreen (it leaves fullscreen), Ctrl+L/T/W/N/R/Tab/F4/PageUp/
   PageDown/0-9/-/= (address bar, tabs, windows, reload, zoom) and
   Ctrl+Shift+I/J/C/Delete (devtools, clear data). Ctrl+S/P/F/D/H/O/U and the
   Alt combinations are taken by the game (prevented). */
bool ModifierKey(SDLKey sym)
{
    return sym == SDLK_LCTRL || sym == SDLK_RCTRL || sym == SDLK_LMETA || sym == SDLK_RMETA
        || sym == SDLK_LSUPER || sym == SDLK_RSUPER || sym == SDLK_LALT || sym == SDLK_RALT
        || sym == SDLK_LSHIFT || sym == SDLK_RSHIFT;
}

bool BrowserKey(EmscriptenKeyboardEvent const* e, SDLKey sym)
{
    if (sym == SDLK_F5 || sym == SDLK_F11 || sym == SDLK_F12)
        return true;
    if (sym == SDLK_ESCAPE && InFullscreen())
        return true;
    if (ModifierKey(sym))
        return false;
    if (e->metaKey)
        return true;
    if (!e->ctrlKey)
        return false;
    switch (sym)
    {
    case SDLK_l: case SDLK_t: case SDLK_w: case SDLK_n: case SDLK_r: case SDLK_TAB: case SDLK_F4:
    case SDLK_PAGEUP: case SDLK_PAGEDOWN: case SDLK_MINUS: case SDLK_EQUALS:
    case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4: case SDLK_5: case SDLK_6: case SDLK_7: case SDLK_8: case SDLK_9:
        return true;
    case SDLK_i: case SDLK_j: case SDLK_c: case SDLK_DELETE:
        return e->shiftKey;
    default:
        return false;
    }
}

// keyup for every key the game still holds down (focus loss)
void ReleaseKeys()
{
    std::set<SDLKey> down;
    down.swap(s_keysDown);
    for (SDLKey sym : down)
    {
        SDL_Event event;
        memset(&event, 0, sizeof(event));
        event.type = SDL_KEYUP;
        event.key.state = SDL_RELEASED;
        event.key.keysym.sym = sym;
        Push(event);
    }
}

EM_BOOL OnKey(int type, EmscriptenKeyboardEvent const* e, void*)
{
    bool const down = type == EMSCRIPTEN_EVENT_KEYDOWN;
    SDLKey const sym = SymFromCode(e->code);
    if (BrowserKey(e, sym))
        return EM_FALSE;

    // repeats never reach the game, but a held key keeps typing (or deleting)
    // in a text field: those stay the page's
    if (e->repeat)
        return TextFieldFocused() ? EM_FALSE : EM_TRUE;

    // keyups of keys the game holds are always delivered (a text field may
    // have taken the focus while the key was down); keyups of keys that were
    // never delivered down (pressed while a text field had the focus, or
    // released by a blur) are dropped
    if (!down)
    {
        if (!s_keysDown.erase(sym))
            return TextFieldFocused() ? EM_FALSE : EM_TRUE;
    }
    else
    {
        if (TextFieldFocused())
            return EM_FALSE;
        s_keysDown.insert(sym);
    }

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.scancode = static_cast<Uint8>(e->keyCode);
    event.key.keysym.sym = sym;
    event.key.keysym.mod = Modifiers(e);
    event.key.keysym.unicode = down ? UnicodeFromKey(e->key, sym) : 0;
    Push(event);
    if (down && s_inputCallback)
        s_inputCallback();
    return EM_TRUE;
}

EM_BOOL OnMouseMove(int, EmscriptenMouseEvent const* e, void*)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.state = s_mouseButtons;
    s_mouseX = static_cast<Uint16>(e->targetX < 0 ? 0 : e->targetX);
    s_mouseY = static_cast<Uint16>(e->targetY < 0 ? 0 : e->targetY);
    event.motion.x = s_mouseX;
    event.motion.y = s_mouseY;
    event.motion.xrel = static_cast<Sint16>(e->movementX);
    event.motion.yrel = static_cast<Sint16>(e->movementY);
    Push(event);
    return EM_FALSE;
}

EM_BOOL OnMouseButton(int type, EmscriptenMouseEvent const* e, void*)
{
    Uint8 const button = static_cast<Uint8>(e->button == 0 ? SDL_BUTTON_LEFT : e->button == 1 ? SDL_BUTTON_MIDDLE : SDL_BUTTON_RIGHT);
    bool const down = type == EMSCRIPTEN_EVENT_MOUSEDOWN;
    if (down)
        s_mouseButtons |= static_cast<Uint8>(1 << (button - 1));
    else
        s_mouseButtons &= static_cast<Uint8>(~(1 << (button - 1)));

    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    event.button.button = button;
    event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.button.x = static_cast<Uint16>(e->targetX < 0 ? 0 : e->targetX);
    event.button.y = static_cast<Uint16>(e->targetY < 0 ? 0 : e->targetY);
    Push(event);
    return EM_TRUE;
}

EM_BOOL OnWheel(int, EmscriptenWheelEvent const* e, void*)
{
    if (e->deltaY == 0)
        return EM_FALSE;
    Uint8 const button = e->deltaY < 0 ? SDL_BUTTON_WHEELUP : SDL_BUTTON_WHEELDOWN;
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.button.button = button;
    event.button.x = s_mouseX;
    event.button.y = s_mouseY;
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.state = SDL_PRESSED;
    Push(event);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.state = SDL_RELEASED;
    Push(event);
    return EM_TRUE;
}

/* Focus loss: the native client stops rendering and unloads its textures
   (SDL_ACTIVEEVENT -> Activate(false)); a browser game keeps drawing while
   another window has the focus. What must not survive a blur is the key
   state: the keyup of a key held while the focus went away never arrives, so
   every key that is down gets released here. (Hidden tabs are handled
   separately, see gBrowser.cpp.) */
EM_BOOL OnFocus(int type, EmscriptenFocusEvent const*, void*)
{
    s_hasFocus = type == EMSCRIPTEN_EVENT_FOCUS;
    if (!s_hasFocus)
    {
        size_t const held = s_keysDown.size();
        ReleaseKeys();
        printf("BROWSER: focus lost, %d keys released, rendering continues.\n", static_cast<int>(held));
    }
    return EM_FALSE;
}

void InstallCallbacks()
{
    if (s_callbacksInstalled)
        return;
    s_callbacksInstalled = true;

    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, &OnKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, &OnKey);
    emscripten_set_mousemove_callback(s_canvas, NULL, EM_TRUE, &OnMouseMove);
    emscripten_set_mousedown_callback(s_canvas, NULL, EM_TRUE, &OnMouseButton);
    emscripten_set_mouseup_callback(s_canvas, NULL, EM_TRUE, &OnMouseButton);
    emscripten_set_wheel_callback(s_canvas, NULL, EM_TRUE, &OnWheel);
    // not in the capture phase: focus/blur do not bubble, so with capture
    // off only the window's own focus changes arrive, not every element's
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, &OnFocus);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_FALSE, &OnFocus);
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, &OnResize);
    sdlshim_InstallSizeObserver();
}

// --- images ------------------------------------------------------------

SDL_Surface* LoadPNG(char const* file)
{
    FILE* fp = fopen(file, "rb");
    if (!fp)
    {
        s_error = std::string("Couldn't open ") + file;
        return NULL;
    }

    png_byte header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8) != 0)
    {
        s_error = std::string("Not a PNG file: ") + file;
        fclose(fp);
        return NULL;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info)
    {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        s_error = "libpng init failed";
        return NULL;
    }

    SDL_Surface* surface = NULL;
    png_bytep* rows = NULL;
    if (setjmp(png_jmpbuf(png)))
    {
        free(rows);
        SDL_FreeSurface(surface);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        s_error = std::string("PNG decode failed: ") + file;
        return NULL;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 width = 0, height = 0;
    int bitDepth = 0, colorType = 0;
    png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType, NULL, NULL, NULL);

    // 8 bits per channel; palettes expanded to RGB(A), tRNS to alpha
    if (colorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png);
    if (bitDepth == 16)
        png_set_strip_16(png);
    // gray+alpha becomes RGBA like SDL_image made it (gTextureCycle insists
    // on 4 bytes per pixel); plain gray stays one byte (GL_LUMINANCE)
    if (colorType == PNG_COLOR_TYPE_GRAY_ALPHA || (colorType == PNG_COLOR_TYPE_GRAY && png_get_valid(png, info, PNG_INFO_tRNS)))
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    int const channels = png_get_channels(png, info);
    if (channels < 1 || channels > 4 || width == 0 || height == 0)
        png_error(png, "unsupported layout");

    Uint32 rMask = 0, gMask = 0, bMask = 0, aMask = 0;
    switch (channels)
    {
    case 2:
        aMask = 0xFF00;
        break;
    case 3:
        rMask = 0x0000FF;
        gMask = 0x00FF00;
        bMask = 0xFF0000;
        break;
    case 4:
        rMask = 0x000000FF;
        gMask = 0x0000FF00;
        bMask = 0x00FF0000;
        aMask = 0xFF000000;
        break;
    default:
        break;
    }

    surface = AllocSurface(static_cast<int>(width), static_cast<int>(height), channels, rMask, gMask, bMask, aMask);
    if (!surface)
        png_error(png, "out of memory");

    rows = static_cast<png_bytep*>(malloc(sizeof(png_bytep) * height));
    if (!rows)
        png_error(png, "out of memory");
    for (png_uint_32 y = 0; y < height; ++y)
        rows[y] = static_cast<png_bytep>(surface->pixels) + y * surface->pitch;

    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return surface;
}

} // namespace

// --- version, init, errors, time -------------------------------------------

SDL_version const* SDL_Linked_Version(void)
{
    return &s_version;
}

int SDL_Init(Uint32)
{
    return 0;
}

int SDL_InitSubSystem(Uint32)
{
    return 0;
}

void SDL_QuitSubSystem(Uint32)
{
}

Uint32 SDL_WasInit(Uint32 flags)
{
    return flags & (SDL_INIT_VIDEO | SDL_INIT_AUDIO);
}

void SDL_Quit(void)
{
}

char const* SDL_GetError(void)
{
    return s_error.c_str();
}

void SDL_ClearError(void)
{
    s_error.clear();
}

void SDL_Delay(Uint32)
{
    // the animation frame paces everything; sleeping would busy wait
}

Uint32 SDL_GetTicks(void)
{
    return static_cast<Uint32>(emscripten_get_now());
}

// --- surfaces ----------------------------------------------------------------

SDL_Surface* SDL_CreateRGBSurface(Uint32, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
{
    return AllocSurface(width, height, (depth + 7) / 8, Rmask, Gmask, Bmask, Amask);
}

// only the copy case (same format) is needed: rSurface::CopyFrom
SDL_Surface* SDL_ConvertSurface(SDL_Surface* src, SDL_PixelFormat* fmt, Uint32)
{
    if (!src || !fmt)
        return NULL;
    SDL_Surface* dst = AllocSurface(src->w, src->h, fmt->BytesPerPixel, fmt->Rmask, fmt->Gmask, fmt->Bmask, fmt->Amask);
    if (!dst)
        return NULL;
    if (fmt->BytesPerPixel == src->format->BytesPerPixel)
        memcpy(dst->pixels, src->pixels, static_cast<size_t>(src->w) * src->h * fmt->BytesPerPixel);
    else if (fmt->BytesPerPixel == 4 && src->format->BytesPerPixel == 3)
    {
        Uint8 const* in = static_cast<Uint8 const*>(src->pixels);
        Uint8* out = static_cast<Uint8*>(dst->pixels);
        for (int i = src->w * src->h - 1; i >= 0; --i)
        {
            out[4 * i] = in[3 * i];
            out[4 * i + 1] = in[3 * i + 1];
            out[4 * i + 2] = in[3 * i + 2];
            out[4 * i + 3] = 255;
        }
    }
    return dst;
}

void SDL_FreeSurface(SDL_Surface* surface)
{
    if (!surface || surface == s_screen)
        return;
    free(surface->pixels);
    free(surface);
}

int SDL_SaveBMP(SDL_Surface*, char const*)
{
    s_error = "screenshots are not written in the browser build";
    return -1;
}

// --- video -------------------------------------------------------------------

SDL_VideoInfo const* SDL_GetVideoInfo(void)
{
    memset(&s_desktopFormat, 0, sizeof(s_desktopFormat));
    s_desktopFormat.BitsPerPixel = 32;
    s_desktopFormat.BytesPerPixel = 4;
    s_desktopFormat.Rmask = 0x00FF0000;
    s_desktopFormat.Gmask = 0x0000FF00;
    s_desktopFormat.Bmask = 0x000000FF;
    memset(&s_videoInfo, 0, sizeof(s_videoInfo));
    s_videoInfo.vfmt = &s_desktopFormat;
    double cssWidth = 800, cssHeight = 600;
    emscripten_get_element_css_size(s_canvas, &cssWidth, &cssHeight);
    s_videoInfo.current_w = static_cast<int>(cssWidth);
    s_videoInfo.current_h = static_cast<int>(cssHeight);
    return &s_videoInfo;
}

SDL_Rect** SDL_ListModes(SDL_PixelFormat*, Uint32)
{
    // "any mode"
    return reinterpret_cast<SDL_Rect**>(-1);
}

int SDL_VideoModeOK(int, int, int bpp, Uint32)
{
    return bpp;
}

SDL_Surface* SDL_SetVideoMode(int, int, int, Uint32)
{
    if (!s_context)
    {
        EmscriptenWebGLContextAttributes attributes;
        emscripten_webgl_init_context_attributes(&attributes);
        attributes.alpha = EM_FALSE;
        attributes.depth = EM_TRUE;
        attributes.stencil = EM_FALSE;
        attributes.antialias = EM_FALSE;
        attributes.preserveDrawingBuffer = EM_FALSE;
        attributes.enableExtensionsByDefault = EM_TRUE;
        // rGLES2 runs on either version (GLSL ES 1.00 shaders); WebGL 2 is
        // preferred for mipmaps on non power of two (moviepack) textures
        attributes.majorVersion = 2;
        attributes.minorVersion = 0;
        sdlshim_InstallLowLatencyContext(1);
        s_context = emscripten_webgl_create_context(s_canvas, &attributes);
        s_offscreen = s_context > 0;
        if (s_context <= 0)
        {
            // WebGL 1: no glBlitFramebuffer, so no offscreen frame and no
            // low latency hint (it would flicker)
            sdlshim_InstallLowLatencyContext(0);
            attributes.majorVersion = 1;
            s_context = emscripten_webgl_create_context(s_canvas, &attributes);
        }
        if (s_context <= 0)
        {
            s_error = "Couldn't create a WebGL context";
            s_context = 0;
            return NULL;
        }
        emscripten_webgl_make_context_current(s_context);
    }

    if (!s_screen)
        s_screen = AllocSurface(1, 1, 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);

    UpdateCanvasSize();
    InstallCallbacks();

    return s_screen;
}

int SDL_GL_SetAttribute(SDL_GLattr, int)
{
    return 0;
}

int SDL_GL_GetAttribute(SDL_GLattr, int* value)
{
    if (value)
        *value = 0;
    return 0;
}

void SDL_GL_SwapBuffers(void)
{
    // the finished frame into the canvas in one copy (two with BROWSER_MSAA:
    // the multisampled frame is resolved into the single sampled one first,
    // the same rectangle on both sides as a resolve requires); the browser
    // presents the canvas when the current task returns (rSysDep::SwapGL has
    // flushed rGLES2's batch with glFlush before this)
    if (!s_offscreen || !s_frameFbo)
    {
        // no frame to post process: bloom is a WebGL 2 feature like the frame
        // itself, and the geometry the layer is collecting would pile up
        if (sr_GLES2BloomPending())
        {
            printf("BROWSER: BROWSER_BLOOM ignored: no offscreen frame (WebGL 1).\n");
            sr_GLES2SetBloom(false, -1);
        }
        return;
    }
    // bloom (BROWSER_BLOOM) is added to the resolved frame before it goes to
    // the canvas; its emission pass tests against the frame's depth, so the
    // resolve has to bring the multisampled depth down with the color
    bool const bloom = sr_GLES2BloomPending();
    // the depth of field (the attract scene's) reads the frame's depth too
    bool const dof = sr_GLES2DepthOfFieldPending();
    if (s_msFbo)
    {
        emscripten_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_msFbo);
        emscripten_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_frameFbo);
        emscripten_glBlitFramebuffer(0, 0, s_frameWidth, s_frameHeight, 0, 0, s_frameWidth, s_frameHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (bloom || dof)
            emscripten_glBlitFramebuffer(0, 0, s_frameWidth, s_frameHeight, 0, 0, s_frameWidth, s_frameHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
    if (bloom)
        sr_GLES2Bloom(s_frameFbo, s_frameDepth, s_frameWidth, s_frameHeight);
    if (dof)
        sr_GLES2DepthOfField(s_frameFbo, s_frameWidth, s_frameHeight);
    emscripten_glBindFramebuffer(GL_READ_FRAMEBUFFER, s_frameFbo);
    emscripten_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    emscripten_glBlitFramebuffer(0, 0, s_frameWidth, s_frameHeight, 0, 0, s_canvasWidth, s_canvasHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    emscripten_glBindFramebuffer(GL_FRAMEBUFFER, s_msFbo ? s_msFbo : s_frameFbo);
}

void* SDL_GL_GetProcAddress(char const*)
{
    return NULL;
}

// the play page owns the tab's title ("Retrocycles"); the game's window caption is not shown
void SDL_WM_SetCaption(char const*, char const*)
{
}

void SDL_WM_SetIcon(SDL_Surface*, Uint8*)
{
}

int SDL_WM_IconifyWindow(void)
{
    return 0;
}

int SDL_ShowCursor(int)
{
    return 1;
}

void SDL_WarpMouse(Uint16, Uint16)
{
}

Uint8 SDL_GetAppState(void)
{
    // always active and with the mouse: the game would wait for activation otherwise
    return SDL_APPACTIVE | SDL_APPMOUSEFOCUS | (s_hasFocus ? SDL_APPINPUTFOCUS : 0);
}

void sdlshim_GetCanvasSize(int* width, int* height)
{
    if (width)
        *width = s_canvasWidth;
    if (height)
        *height = s_canvasHeight;
}

double sdlshim_GetDevicePixelRatio(void)
{
    double ratio = emscripten_get_device_pixel_ratio();
    return ratio > 2 ? 2 : ratio < 1 ? 1 : ratio;
}

void sdlshim_FrameCheck(void)
{
    // a pixel ratio change without a resize event or a media query change
    // (emulated devices, some zoom implementations): one property read per frame
    static double lastRatio = 0;
    double const ratio = sdlshim_GetDevicePixelRatio();
    if (ratio != lastRatio)
    {
        lastRatio = ratio;
        if (s_context)
            UpdateCanvasSize();
    }
}

void sdlshim_SetRenderDensity(double density)
{
    // stored before the context exists, like the sample count
    s_renderDensity = density <= 0 ? 0 : density < .5 ? .5 : density;
    if (s_context)
        UpdateCanvasSize();
}

double sdlshim_GetRenderDensity(void)
{
    double cssWidth = 0, cssHeight = 0;
    emscripten_get_element_css_size(s_canvas, &cssWidth, &cssHeight);
    return cssWidth > 0 ? s_canvasWidth / cssWidth : 1;
}

void sdlshim_SetMultisample(int samples)
{
    // stored before the context exists (the setting loads first) and
    // applied with the frame; the resize path recreates the renderbuffers
    s_wantedSamples = samples < 0 ? 0 : samples;
    if (s_context)
        UpdateOffscreenFrame();
}

int sdlshim_GetMultisample(void)
{
    return s_offscreen ? s_frameSamples : 0;
}

void sdlshim_SetInputCallback(sdlshim_InputCallback callback)
{
    s_inputCallback = callback;
}

int sdlshim_KeysymFromCode(char const* code)
{
    return static_cast<int>(SymFromCode(code));
}

char const* sdlshim_CodeFromKeysym(int keysym)
{
    static char name[16];
    if (keysym >= SDLK_a && keysym <= SDLK_z)
    {
        snprintf(name, sizeof(name), "Key%c", 'A' + (keysym - SDLK_a));
        return name;
    }
    if (keysym >= SDLK_0 && keysym <= SDLK_9)
    {
        snprintf(name, sizeof(name), "Digit%d", keysym - SDLK_0);
        return name;
    }
    if (keysym >= SDLK_F1 && keysym <= SDLK_F15)
    {
        snprintf(name, sizeof(name), "F%d", keysym - SDLK_F1 + 1);
        return name;
    }
    for (KeyName const& k : s_codes)
        if (k.sym == keysym)
            return k.code;
    return "";
}

// --- keys ----------------------------------------------------------------------

char const* SDL_GetKeyName(SDLKey key)
{
    static char name[32];
    if (key >= SDLK_SPACE && key <= SDLK_z)
    {
        name[0] = static_cast<char>(key);
        name[1] = 0;
        return name;
    }
    for (KeyName const& k : s_codes)
        if (k.sym == key)
            return k.code;
    if (key >= SDLK_F1 && key <= SDLK_F15)
    {
        snprintf(name, sizeof(name), "F%d", key - SDLK_F1 + 1);
        return name;
    }
    switch (key)
    {
    case SDLK_BACKSPACE: return "backspace";
    case SDLK_TAB: return "tab";
    case SDLK_RETURN: return "return";
    case SDLK_ESCAPE: return "escape";
    case SDLK_DELETE: return "delete";
    default: break;
    }
    return "unknown key";
}

int SDL_EnableUNICODE(int)
{
    return 1;
}

int SDL_EnableKeyRepeat(int, int)
{
    return 0;
}

// --- events ------------------------------------------------------------------

void SDL_PumpEvents(void)
{
    // the browser delivers events between frames on its own
}

int SDL_PollEvent(SDL_Event* event)
{
    if (s_events.empty())
        return 0;
    if (event)
        *event = s_events.front();
    s_events.pop_front();
    return 1;
}

int SDL_PushEvent(SDL_Event* event)
{
    if (!event)
        return -1;
    Push(*event);
    return 0;
}

void SDL_SetEventFilter(SDL_EventFilter filter)
{
    s_filter = filter;
}

// --- threads -----------------------------------------------------------------

SDL_mutex* SDL_CreateMutex(void)
{
    static int dummy;
    return reinterpret_cast<SDL_mutex*>(&dummy);
}

int SDL_mutexP(SDL_mutex*)
{
    return 0;
}

int SDL_mutexV(SDL_mutex*)
{
    return 0;
}

void SDL_DestroyMutex(SDL_mutex*)
{
}

SDL_Thread* SDL_CreateThread(int (*)(void*), void*)
{
    return NULL;
}

void SDL_WaitThread(SDL_Thread*, int*)
{
}

// --- audio -------------------------------------------------------------------

namespace
{
SDL_AudioSpec s_audioSpec;
bool s_audioOpen = false;
bool s_audioPaused = true;
Uint8* s_audioBuffer = NULL;
int s_audioBufferLen = 0;
int s_audioCallbacks = 0;
int s_audioNonSilent = 0;
bool s_audioFailed = false;
}

/* The Web Audio side. A ScriptProcessorNode (deprecated but universal; an
   AudioWorklet would need a separate script file and a message hop) asks for
   `frames` frames at a time on the main thread, between the game's frames;
   the wasm side mixes 16 bit stereo into its buffer (sdlshim_audio_fill
   returns the pointer, 0 while paused) and this converts to floats. The
   context starts suspended in every browser until a user gesture;
   Module.__aaAudio.unlock() resumes it (aa_api.js does that from the first
   key/pointer event, aa.unlockAudio() from the page). Returns the sample rate
   or 0. */
EM_JS(int, sdlshim_AudioOpen, (int frames, int channels), {
    var Context = window.AudioContext || window.webkitAudioContext;
    if (!Context)
        return 0;
    var context;
    try {
        context = new Context();
    } catch (e) {
        return 0;
    }
    var node = context.createScriptProcessor(frames, 0, channels);
    var audio = Module["__aaAudio"] = {
        context: context,
        node: node,
        unlock: function () {
            if (context.state === "suspended")
                context.resume().then(function () {
                    console.log("BROWSER: audio running, " + context.sampleRate + " Hz, " + frames + " frames per callback");
                }, function () {});
        }
    };
    node.onaudioprocess = function (event) {
        var out = event.outputBuffer;
        var ptr = Module["_sdlshim_audio_fill"]();
        var n = out.length;
        for (var c = 0; c < out.numberOfChannels; ++c) {
            var data = out.getChannelData(c);
            if (!ptr) {
                data.fill(0);
                continue;
            }
            var base = (ptr >> 1) + c;
            var step = out.numberOfChannels;
            for (var i = 0; i < n; ++i)
                data[i] = HEAP16[base + i * step] / 32768;
        }
    };
    node.connect(context.destination);
    return context.sampleRate;
});

EM_JS(void, sdlshim_AudioClose, (), {
    var audio = Module["__aaAudio"];
    if (!audio)
        return;
    try { audio.node.disconnect(); } catch (e) {}
    try { audio.context.close(); } catch (e) {}
    Module["__aaAudio"] = null;
});

EM_JS(int, sdlshim_AudioState, (), {
    var audio = Module["__aaAudio"];
    if (!audio || !audio.context)
        return 0;
    switch (audio.context.state) {
    case "suspended": return 1;
    case "running": return 2;
    default: return 3;
    }
});

extern "C" EMSCRIPTEN_KEEPALIVE Uint8* sdlshim_audio_fill(void)
{
    if (!s_audioOpen || s_audioPaused || !s_audioSpec.callback || s_audioFailed)
        return NULL;
    memset(s_audioBuffer, 0, s_audioBufferLen);
    try
    {
        s_audioSpec.callback(s_audioSpec.userdata, s_audioBuffer, s_audioBufferLen);
    }
    catch (...)
    {
        // a sound file that fails to load throws from inside the mixer; the
        // exception must not reach the audio event
        s_audioFailed = true;
        printf("BROWSER: audio mixer threw, sound disabled.\n");
        return NULL;
    }
    ++s_audioCallbacks;
    // did anything get mixed (the status line reports it)
    Sint16 const* samples = reinterpret_cast<Sint16 const*>(s_audioBuffer);
    for (int i = 0; i < s_audioBufferLen / 2; ++i)
        if (samples[i] != 0)
        {
            ++s_audioNonSilent;
            break;
        }
    return s_audioBuffer;
}

// the RWops is just the file name; SDL_LoadWAV_RW reads it directly
SDL_RWops* SDL_RWFromFile(char const* file, char const*)
{
    return reinterpret_cast<SDL_RWops*>(file ? strdup(file) : NULL);
}

int SDL_OpenAudio(SDL_AudioSpec* desired, SDL_AudioSpec* obtained)
{
    if (s_audioOpen || !desired || !desired->callback)
    {
        s_error = "audio already open or no callback";
        return -1;
    }
    // the mixer writes 16 bit stereo whatever it was asked; the sample rate
    // is the context's (the mixer resamples its sources by spec.freq/audio.freq)
    int const frames = 1024;
    int const rate = sdlshim_AudioOpen(frames, 2);
    if (rate <= 0)
    {
        s_error = "no Web Audio";
        return -1;
    }
    s_audioSpec = *desired;
    s_audioSpec.freq = rate;
    s_audioSpec.format = AUDIO_S16SYS;
    s_audioSpec.channels = 2;
    s_audioSpec.silence = 0;
    s_audioSpec.samples = static_cast<Uint16>(frames);
    s_audioSpec.size = static_cast<Uint32>(frames * 2 * 2);
    s_audioBufferLen = static_cast<int>(s_audioSpec.size);
    s_audioBuffer = static_cast<Uint8*>(calloc(s_audioBufferLen, 1));
    s_audioOpen = true;
    s_audioPaused = true;
    s_audioFailed = false;
    if (obtained)
        *obtained = s_audioSpec;
    printf("BROWSER: audio device opened, %d Hz, %d frames per callback, waiting for a user gesture.\n", rate, frames);
    return 0;
}

void SDL_CloseAudio(void)
{
    if (!s_audioOpen)
        return;
    s_audioPaused = true;
    s_audioOpen = false;
    sdlshim_AudioClose();
    free(s_audioBuffer);
    s_audioBuffer = NULL;
    s_audioBufferLen = 0;
}

void SDL_PauseAudio(int pause)
{
    s_audioPaused = pause != 0;
}

void sdlshim_GetAudioStats(int* state, int* callbacks, int* nonSilent)
{
    if (state)
        *state = s_audioOpen ? sdlshim_AudioState() : 0;
    if (callbacks)
        *callbacks = s_audioCallbacks;
    if (nonSilent)
        *nonSilent = s_audioNonSilent;
    s_audioCallbacks = 0;
    s_audioNonSilent = 0;
}

void SDL_LockAudio(void)
{
}

void SDL_UnlockAudio(void)
{
}

/* Canonical RIFF/WAVE PCM (8 or 16 bit) only; eWavData needs the files to
   load even though nothing is ever mixed (the audio device never opens). */
SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops* src, int freesrc, SDL_AudioSpec* spec, Uint8** audio_buf, Uint32* audio_len)
{
    char* file = reinterpret_cast<char*>(src);
    if (!file || !spec || !audio_buf || !audio_len)
        return NULL;

    SDL_AudioSpec* result = NULL;
    FILE* fp = fopen(file, "rb");
    if (fp)
    {
        Uint8 header[12];
        if (fread(header, 1, 12, fp) == 12 && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0)
        {
            memset(spec, 0, sizeof(*spec));
            bool haveFormat = false;
            Uint8 chunk[8];
            while (fread(chunk, 1, 8, fp) == 8)
            {
                Uint32 const size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | (static_cast<Uint32>(chunk[7]) << 24);
                if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16)
                {
                    Uint8 fmt[16];
                    if (fread(fmt, 1, 16, fp) != 16)
                        break;
                    int const tag = fmt[0] | (fmt[1] << 8);
                    int const bits = fmt[14] | (fmt[15] << 8);
                    spec->channels = fmt[2];
                    spec->freq = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
                    spec->format = bits == 8 ? AUDIO_U8 : AUDIO_S16LSB;
                    haveFormat = tag == 1 && (bits == 8 || bits == 16) && spec->channels >= 1;
                    if (size > 16)
                        fseek(fp, static_cast<long>(size - 16), SEEK_CUR);
                }
                else if (memcmp(chunk, "data", 4) == 0)
                {
                    if (!haveFormat)
                        break;
                    Uint8* data = static_cast<Uint8*>(malloc(size > 0 ? size : 1));
                    if (data && fread(data, 1, size, fp) == size)
                    {
                        *audio_buf = data;
                        *audio_len = size;
                        result = spec;
                    }
                    else
                        free(data);
                    break;
                }
                else
                    fseek(fp, static_cast<long>(size + (size & 1)), SEEK_CUR);
            }
        }
        fclose(fp);
    }

    if (!result)
        s_error = std::string("Couldn't load WAV ") + file;
    if (freesrc)
        free(file);
    return result;
}

void SDL_FreeWAV(Uint8* audio_buf)
{
    free(audio_buf);
}

int SDL_BuildAudioCVT(SDL_AudioCVT*, Uint16, Uint8, int, Uint16, Uint8, int)
{
    return -1;
}

int SDL_ConvertAudio(SDL_AudioCVT*)
{
    return -1;
}

// --- SDL_image -----------------------------------------------------------------

SDL_Surface* IMG_Load(char const* file)
{
    return LoadPNG(file);
}

void IMG_InvertAlpha(int)
{
    // SDL_image 1.2 quirk the SDL2 native build skips as well
}
