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

#ifndef ArmageTron_GBROWSERQUALITY_H
#define ArmageTron_GBROWSERQUALITY_H

/* Graphics quality tiers and the Auto controller of the browser client
   (BROWSER_QUALITY, BROWSER_RENDER_SCALE; gBrowserQuality.cpp). */

#ifdef __EMSCRIPTEN__

#include <string>

//! what drove a frame: Auto judges the display's frames against the refresh
//! interval, uncapped ones against 60 fps, and leaves a key's extra frame out
enum gBrowserFrameKind { gFrame_Display, gFrame_Uncapped, gFrame_Input };

//! once per frame, before the look settings are applied;
//! rendering false while the frame is not drawn (hidden tab)
void gBrowserQualityFrame( bool rendering, gBrowserFrameKind kind );

//! the status line's part: " quality=auto/3:medium slow=0.02 density=1.00"
std::string gBrowserQualityStatus();

#endif

#endif
