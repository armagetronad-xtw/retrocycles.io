/*
SPDX-License-Identifier: GPL-2.0-or-later

This file is part of the browser and platform additions to Armagetron Advanced.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef ArmageTron_GBROWSER_H
#define ArmageTron_GBROWSER_H

/* Browser (Emscripten) client glue: the session lifecycle behind the page's
   command API (gBrowserApi.cpp, web/wasm/aa_api.js, contract in
   web/wasm/API.md). Only compiled into the wasm build (web/wasm/Makefile). */

#ifdef __EMSCRIPTEN__

#include <string>

class tOutput;

//! the page facing session state (API.md, "State machine")
enum gBrowserState
{
    gBrowser_Idle = 0,          //!< nothing running, connect() allowed
    gBrowser_Connecting = 1,    //!< DataChannel handshake or login
    gBrowser_Syncing = 2,       //!< logged in, syncing objects / waiting for the game
    gBrowser_Playing = 3,       //!< in the game loop
    gBrowser_Disconnected = 4,  //!< the last session ended (reason available), connect() allowed
    gBrowser_Fetching = 5,      //!< in the game, but the round's map is being downloaded
    gBrowser_Local = 6,         //!< the local game against the AI runs (standalone)
    gBrowser_Scene = 7          //!< the attract scene runs (standalone, no players)
};

/* called from main() after the display is up: registers the animation frame
   loop (emscripten_set_main_loop) in the idle state and, if the page's
   location fragment holds a "#ws://host:port/" URL, connects to it right
   away. Does not return. */
void gBrowserStart();

/* the command API. Connect fails (false) while a session is running or when
   the handshake cannot be started. Disconnect is a no-op when idle.
   iceConfig: "" or a JSON object {"iceServers": [...], "iceTransportPolicy":
   "relay"} for the browser's RTCPeerConnection (STUN/TURN servers the page got
   from its platform or configuration; web/wasm/API.md "ICE servers"). It holds
   TURN credentials and is never logged. */
bool gBrowserConnect( std::string const & signallingUrl, std::string const & iceConfig = std::string() );
void gBrowserDisconnect();

/* platform login (platform-contract/README.md): hands a login token to the
   running network session (connecting/syncing/playing); ePlatform sends it
   to the server once the connection is logged in, once. False without a
   session or with an empty token. The token is never logged; a token not
   sent when the session ends is dropped. */
bool gBrowserPlatformLogin( std::string const & token );

/* the local game: the desktop client's "Local Game" (standalone, AI opponents
   from aiplayers.cfg), driven frame by frame like a network session. Ends
   through gBrowserStopLocal() or gBrowserDisconnect(); the page state is then
   "disconnected" with reason "requested". */
bool gBrowserStartLocal();
void gBrowserStopLocal();

/* the attract scene (gScene.cpp): standalone, no player, four scripted states.
   gBrowserScene(k) starts it and plays towards state k, gBrowserSceneStop()
   takes it down and returns to idle. */
bool gBrowserScene( int state );
void gBrowserSceneStop();

gBrowserState gBrowserGetState();
std::string const & gBrowserLastReason();   //!< why the last session ended
std::string const & gBrowserUrl();          //!< the signalling URL of the current/last session
//! the selected ICE candidate pair of the current/last network session as a JSON object
//! ({"local":"relay","remote":"host","protocol":"udp","relayProtocol":"tcp"}), "" if none yet
std::string const & gBrowserIceRoute();

//! stdout, "BROWSER: " prefix; the in-game console only with
//! BROWSER_CONSOLE_LOG 1 (tConsole::BrowserLog)
void gBrowserLog( std::string const & line );

//! tConsole's message callback in the browser build: a "message" event, no modal loop
bool gBrowserMessage( tOutput const & title, tOutput const & body, float timeout );

/* config persistence: the user's var/ directory is an IDBFS mount (set up by
   web/wasm/aa_api.js before the runtime starts). Restore seeds an empty mount
   with the browser defaults before the configuration is loaded, Save writes
   user.cfg and flushes the mount to IndexedDB. */
void gBrowserPersistRestore();
void gBrowserPersistSave();
//! the settings gBrowserPersistSave writes to var/autoexec.cfg (tSettingItems the page edits,
//! not part of user.cfg), as "NAME value" lines; exportConfig appends them
std::string gBrowserSettingsText();

// gBrowserApi.cpp: hooks the loop calls
void gBrowserApiInit();                 //!< installs the console/chat/message hooks
void gBrowserApiReady();                //!< emits the "ready" event: queries may be called now
void gBrowserApiStateChanged();         //!< emits the "state" event
void gBrowserApiFrame();                //!< per frame: the round rhythm ("round" event), player list / vote change detection
void gBrowserApiRequest( char const * event ); //!< emits an event without data (a key asked for a page UI element)
void gBrowserApiScene( int state, bool transitioning ); //!< emits the "scene" event (gScene.cpp)
void gBrowserApiHandshake( char const * step ); //!< emits the "handshake" event: "signalling" / "ice" reached

#endif // __EMSCRIPTEN__

#endif // ArmageTron_GBROWSER_H
