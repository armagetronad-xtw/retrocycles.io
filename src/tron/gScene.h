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

#ifndef ArmageTron_GSCENE_H
#define ArmageTron_GSCENE_H

/* The browser client's attract scene: the game's own renderer as the play
   page's background (standalone, no player, no HUD, no console, no sky, a
   fixed low scene camera) with a small director that spawns cycles and issues
   turns on a timeline. Four states, see web/wasm/API.md ("aa.scene").

   Only compiled into the wasm build (web/wasm/Makefile); nothing in the
   native build refers to it. */

#ifdef __EMSCRIPTEN__

//! start the scene (if needed) and play forward/backward to that state, 1..4.
//! Returns false when the state is out of range or a session is running.
bool gSceneStart( int state );

//! tear the scene down; the client is idle again and connect() may follow
void gSceneStop();

bool gSceneRunning();          //!< is the scene up?
void gSceneSuspendLook();      //!< the player's TEXT_OUT/SHOW_HUD/... back while the scene runs (around a config save)
void gSceneResumeLook();       //!< the backdrop's switches again
int  gSceneState();            //!< the state reached, 0 while not running
int  gSceneTarget();           //!< the state being played towards
bool gSceneTransitioning();    //!< true while a transition is playing

//! one animation frame while the scene runs (director, simulation, render)
void gSceneFrame();

#endif // __EMSCRIPTEN__

#endif // ArmageTron_GSCENE_H
