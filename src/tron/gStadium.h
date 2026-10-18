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

#ifndef ArmageTron_STADIUM_H
#define ArmageTron_STADIUM_H

//! A stadium around the arena: stands with a crowd, light rigs, scoreboards
//! and their reflections on the floor, built procedurally from the rim walls'
//! bounds (no assets). Client side decoration only; STADIUM turns it on.

//! overrides STADIUM while a caller (the browser's attract scene) wants its
//! own value; -1 hands control back to the setting
void gStadiumSetOverride( int mode );

class eRectangle;
//! the floor the stands are fitted around instead of the rim walls' bounds
//! (clipped to them; the bowl keeps the arena's scale), NULL for the rim's.
//! The attract scene fits it to the area its replay uses.
void gStadiumSetFit( eRectangle const * floor );
//! the floor the stands stand around now (the fit, else the rim walls' bounds)
eRectangle gStadiumFloor();

#endif
