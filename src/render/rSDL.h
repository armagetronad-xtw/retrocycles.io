#ifndef AT_SDL_H
#define AT_SDL_H

#include "config.h"

#ifndef DEDICATED
#include <SDL.h>
#include <SDL_version.h>

#if SDL_VERSION_ATLEAST(2,0,0)
/* SDL2 compatibility layer (--enable-sdl2 / the Emscripten build).

   The client keeps the SDL 1.2 shape: key events are translated in
   sr_TranslateEvent to the SDL 1.2 keysym numbering so keymap[] indices,
   config files (KEYBOARD 273 ...) and recordings stay compatible. The SDLK_*
   names the code compares against are redefined to those numbers below. */

#define SDL_OPENGL SDL_WINDOW_OPENGL

typedef SDL_Keysym SDL_keysym;
typedef SDL_Keycode SDLKey;
typedef Uint16 SDLMod;      // SDL_Keysym::mod is a plain Uint16 in SDL2

// SDL 1.2 keysym range, plus the mouse pseudo keys uInput.h adds on top
#define SDLK_LAST 323

// SDL 1.2 values of the keysyms the code names explicitly
#define SDLK_UNKNOWN 0
#define SDLK_BACKSPACE 8
#define SDLK_RETURN 13
#define SDLK_ESCAPE 27
#define SDLK_SPACE 32
#define SDLK_DELETE 127
#define SDLK_KP_ENTER 271
#define SDLK_UP 273
#define SDLK_DOWN 274
#define SDLK_RIGHT 275
#define SDLK_LEFT 276
#define SDLK_HOME 278
#define SDLK_END 279
#define SDLK_NUMLOCK 300
#define SDLK_COMPOSE 314

#define KMOD_META KMOD_GUI

// SDL 1.2 application state, see sr_GetAppState
#define SDL_APPMOUSEFOCUS 0x01
#define SDL_APPINPUTFOCUS 0x02
#define SDL_APPACTIVE     0x04
#define SDL_GetAppState sr_GetAppState
Uint8 sr_GetAppState();

//! the unicode character an SDL 1.2 keysym carried; stored in the unused
//! word of the SDL2 keysym by sr_TranslateEvent
inline Uint16 sr_KeysymUnicode( SDL_Keysym const & k ){ return static_cast< Uint16 >( k.unused ); }
inline void sr_SetKeysymUnicode( SDL_Keysym & k, Uint16 unicode ){ k.unused = unicode; }

//! rewrites an SDL2 event in place to SDL 1.2 conventions; false if it should be dropped
bool sr_TranslateEvent( SDL_Event & event );

//! the SDL2 keycode behind an SDL 1.2 keysym number, for SDL_GetKeyName
SDL_Keycode sr_KeycodeFromKeysym( int sym );

struct SDL_Window;
extern SDL_Window * sr_window;

#else // SDL 1.2

inline Uint16 sr_KeysymUnicode( SDL_keysym const & k ){ return k.unicode; }
inline void sr_SetKeysymUnicode( SDL_keysym & k, Uint16 unicode ){ k.unicode = unicode; }

#endif // SDL_VERSION_ATLEAST(2,0,0)

#else // DEDICATED
// no SDL at all; the SDL2 branches guarded with this stay off
#define SDL_VERSION_ATLEAST(a,b,c) 0
#define SDLK_LAST 1024
typedef int SDL_keysym;
typedef int SDL_Event;
typedef unsigned char Uint8;
typedef unsigned int Uint32;
typedef int SDL_AudioSpec;
#endif

#endif
