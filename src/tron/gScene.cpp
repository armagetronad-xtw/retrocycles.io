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

/* The browser client's attract scene (contract: web/wasm/API.md, "aa.scene").

   The play page's background is the game's own renderer: a standalone grid
   built from the default map (sg_BrowserSceneGridInit, gGame.cpp), a fixed low
   camera that follows nothing, and a director that spawns player-less cycles
   and issues their turns on a timeline. No gGame, no round logic, no players,
   no HUD, no console text and no sky.

   The browser build cannot run a nested loop (no Asyncify), so this is driven
   exactly like the network session in gBrowser.cpp: gSceneStart() sets it up
   and gSceneFrame() does one step per animation frame.

   Only compiled into the wasm build (web/wasm/Makefile). */

#include "gScene.h"

#ifdef __EMSCRIPTEN__

#include "rSDL.h"

#include "eAdvWall.h"
#include "eCamera.h"
#include "eFloor.h"
#include "eGameObject.h"
#include "eGrid.h"
#include "ePlayer.h"
#include "eRectangle.h"
#include "eSound.h"
#include "eTimer.h"
#include "gArena.h"
#include "gBrowser.h"
#include "gCycle.h"
#include "gGame.h"
#include "gHud.h"
#include "gSceneReplayData.h"
#include "gStadium.h"
#include "gWall.h"
#include "gWinZone.h"
#include "rConsole.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "rSysdep.h"
#include "rViewport.h"
#include "tConfiguration.h"
#include "tSysTime.h"
#include "tException.h"
#include "tMemManager.h"
#include "uInputQueue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

// *******************************************************************************************
// settings
// *******************************************************************************************

/* The scene camera. X/Y are fractions of the arena's half size, measured from
   its centre, so the defaults work whatever REAL_ARENA_SIZE_FACTOR and the map
   make the arena; Z is the height above the floor in grid units and PITCH the
   slope of the view (dz per unit driven forward, negative looks down). The
   defaults put the camera low, just inside the west rim, looking east across
   the whole grid towards the horizon. */
static REAL sg_sceneCameraX = -0.88;
static REAL sg_sceneCameraY = 0;
static REAL sg_sceneCameraZ = 6;
static REAL sg_sceneCameraPitch = -0.18;
static REAL sg_sceneCameraDir = 0;      // degrees, 0 = along +x
static REAL sg_sceneCameraFov = 80;
static REAL sg_sceneTransition = 3.0;   // seconds one state change takes
static REAL sg_sceneWallsLength = 70;   // grid units of trail kept behind a cycle
/* the arena size multiplier the scene builds its grid with (the stock
   REAL_ARENA_SIZE_FACTOR: 500 map units times this). Pinned so the backdrop
   looks the same whatever a game before it left behind. */
static REAL sg_sceneArenaSize = 0.5;
/* the look of the floor while the scene runs: the rim wall (off: an open
   floor to a black horizon) and FLOOR_DETAIL (0 off, 1 lines, 2 the grid
   texture, 3 two textures; the game's own defaults are restored afterwards) */
static bool sg_sceneRim = false;
static int sg_sceneFloorDetail = rFLOOR_TWOTEXTURE;
static int sg_sceneStadium = 1;         // the stadium around the arena (gStadium.cpp), STADIUM values
/* The replay (on by default): instead of the four scripted states, the floor
   plays recorded Team Sumo rounds (gSceneReplayData.h, made by
   web/wasm/scene_replay.py) one after the other, whatever state the page asks
   for. X/Y place the round's arena centre in normalized arena coordinates (the
   default camera sits at x .06 looking along +x), SPEED is the playback rate.
   Read when the scene starts. */
static bool sg_sceneReplay = true;
static REAL sg_sceneReplayX = .4;
static REAL sg_sceneReplayY = .5;
static REAL sg_sceneReplaySpeed = 1;
static REAL sg_sceneReplayZoneHeight = .5;  // the zones' ZONE_HEIGHT (5 in a game): a low ring, not a fence
/* the camera while the replay plays, the SCENE_CAMERA_* settings' meaning: higher
   and closer than the states' camera, over the rim of the recorded zone looking
   into it (a sumo crowd seen from the floor is a thin band) */
static REAL sg_sceneReplayCameraX = -0.45;
static REAL sg_sceneReplayCameraY = 0;
static REAL sg_sceneReplayCameraZ = 22;
static REAL sg_sceneReplayCameraPitch = -0.4;
static REAL sg_sceneReplayCameraDir = 0;
static REAL sg_sceneReplayCameraFov = 70;
static bool sg_sceneCameraReplay = false;   // the scene runs the replay: its camera settings apply
/* the replay's camera (SCENE_REPLAY_CAMERA_MODE):
   0 fixed: stands where SCENE_REPLAY_CAMERA_X/_Y/_Z/_PITCH/_DIR say;
   1 orbit: watches the whole crowd from above, circling it once every ORBIT s
     (0: no orbit);
   2 chase: rides behind and above one cycle, a shot of SHOT s each, then cuts to
     another;
   3 cinematic: the chase, slowly rising and falling (SCENE_REPLAY_CINEMA_*); the
     default.
   4 player: the player's own custom camera (CAMERA_CUSTOM_*, START_FOV_1) on one cycle,
     the game's eCamera doing the work, so the play page's Settings sheet can show a
     change to it as it will look in a game; no timed cuts, a new cycle
     only when the followed one has died. SCENE_PLAYER_GLANCE (0 none, 1 left, 2 right,
     3 back) holds a glance, as the glance keys do (the scene takes no input).
   _FOV applies to all but the player camera. */
enum { SceneCameraFixed = 0, SceneCameraOrbit = 1, SceneCameraChase = 2, SceneCameraCinema = 3, SceneCameraPlayer = 4 };
static int sg_scenePlayerGlance = 0;
// SCENE_PLAYER_CAMERA 1: the player camera whatever SCENE_REPLAY_CAMERA_MODE says, and not
// saved (that one is: the page turns this on while its CAMERA tab is open, and a save then
// must not keep the scene on it)
static bool sg_scenePlayerCamera = false;
/* The player camera takes the replay at a real round's pace: the recordings are a fast room's
   and play at SCENE_REPLAY_SPEED (1.5), about 58 m/s, and the custom camera pulls back and rises
   with speed (_FROMSPEED), so at the recorded pace it stood twice as far back as in a game. While
   it runs, the replay cycles report their speed scaled so the clip's mean is SCENE_PLAYER_SPEED
   (m/s; the stock CYCLE_SPEED, which the page's rig shows "at speed" too). Only Speed() is
   scaled, what the camera reads; the cycles still move as recorded. */
static REAL sg_scenePlayerSpeed = 30;
/* SCENE_PLAYER_COURSE: instead of the recorded rounds, the player camera follows one cycle in the
   player's colour on a course made for judging a camera: 0 off (the replay), 1 a mixed
   loop, 2 steady straights, 3 S-bends (one turn each way), 4 reversals (two quick turns), 5 a brake
   to a crawl and back up to speed. The play page picks the part that shows the focused row. The
   course is a track generated a few seconds ahead of the cycle and driven like a recorded one (no
   physics: the clock sets the speed), turning towards the middle of the stadium's floor when the
   floor runs out ahead. Its trail is short (SCENE_PLAYER_COURSE_WALLS). Not saved. */
static int sg_scenePlayerCourse = 0;
static REAL sg_scenePlayerCourseWalls = 40;
static bool sg_sceneCourseActive = false; // the course runs now (the camera and the look read it)
static REAL sg_scenePlayerSpeedScale = 1;
static int sg_sceneReplayCameraMode = SceneCameraCinema;
static int SceneReplayCameraMode()
{
    return sg_scenePlayerCamera ? SceneCameraPlayer : sg_sceneReplayCameraMode;
}
static REAL sg_sceneReplayCameraOrbit = 70;
static REAL sg_sceneReplayCameraShot = 7;

/* SCENE_VIEW 1: a camera for looking at the whole stadium, over any other scene
   camera. It circles the arena's centre at SCENE_VIEW_DISTANCE times half the
   arena's larger side, SCENE_VIEW_HEIGHT above the floor, starting at
   SCENE_VIEW_ANGLE degrees and turning SCENE_VIEW_SPEED degrees a second (0
   stands still; wall clock), and looks at the
   point SCENE_VIEW_LOOK_HEIGHT above the centre. */
static bool sg_sceneView = false;
static REAL sg_sceneViewDistance = 1.55;
static REAL sg_sceneViewHeight = 104;
static REAL sg_sceneViewAngle = 200;
static REAL sg_sceneViewSpeed = 3;
static REAL sg_sceneViewLookHeight = 0;
static REAL sg_sceneViewFov = 75;
static tSettingItem<bool> sg_sceneViewConf( "SCENE_VIEW", sg_sceneView );
static tSettingItem<REAL> sg_sceneViewDistanceConf( "SCENE_VIEW_DISTANCE", sg_sceneViewDistance );
static tSettingItem<REAL> sg_sceneViewHeightConf( "SCENE_VIEW_HEIGHT", sg_sceneViewHeight );
static tSettingItem<REAL> sg_sceneViewAngleConf( "SCENE_VIEW_ANGLE", sg_sceneViewAngle );
static tSettingItem<REAL> sg_sceneViewSpeedConf( "SCENE_VIEW_SPEED", sg_sceneViewSpeed );
static tSettingItem<REAL> sg_sceneViewLookHeightConf( "SCENE_VIEW_LOOK_HEIGHT", sg_sceneViewLookHeight );
static tSettingItem<REAL> sg_sceneViewFovConf( "SCENE_VIEW_FOV", sg_sceneViewFov );

// the explosions' rays in the scene, a fraction of the stock length
// (EXPLOSION_RAY_LENGTH, gExplosion.cpp; saved and restored with the look)
extern REAL sg_explosionRayLength;
static REAL sg_sceneExplosionRays = .06;
static tSettingItem<REAL> sg_sceneExplosionRaysConf( "SCENE_EXPLOSION_RAYS", sg_sceneExplosionRays );

// the trails' reflection in the floor while the scene runs (FLOOR_REFLECT,
// gCycle.cpp; off in every quality tier for play); < 0 leaves the player's
extern REAL sg_floorReflect, sg_floorReflectSpread;
static REAL sg_sceneFloorReflect = .25;
static tSettingItem<REAL> sg_sceneFloorReflectConf( "SCENE_FLOOR_REFLECT", sg_sceneFloorReflect );
// how far down the mirror image reaches (FLOOR_REFLECT_SPREAD; short: the
// reflection fades out close to the wall's foot, as on a dark wet floor)
static REAL sg_sceneFloorReflectSpread = .8;
static tSettingItem<REAL> sg_sceneFloorReflectSpreadConf( "SCENE_FLOOR_REFLECT_SPREAD", sg_sceneFloorReflectSpread );

/* SCENE_DOF 1: depth of field with bokeh (rGLES2.cpp, sr_GLES2SetDepthOfField),
   where the bloom runs (Medium tier and up) on WebGL 2. The focus is
   SCENE_DOF_FOCUS grid units from the eye, or with 0 the scene camera's target
   (SCENE_VIEW: its look point; the follow camera: its cycle; else where the view
   meets the floor). SCENE_DOF_BAND is the in-focus band (as 1 - focus/distance),
   SCENE_DOF_STRENGTH the largest blur in pixels at 1536 wide (the aperture),
   SCENE_DOF_THRESHOLD / _GAIN which pixels turn into bright bokeh discs and how
   bright, SCENE_DOF_SHAPE the disc's corners (0 round, 5..8 a polygon) and
   SCENE_DOF_RINGS its quality (1..4 rings of taps). The focus follows the camera
   without easing, so no focus pull is ever animated. */
static bool sg_sceneDof = true;
static REAL sg_sceneDofFocus = 0;
static REAL sg_sceneDofBand = .3;
static REAL sg_sceneDofStrength = 7;
static REAL sg_sceneDofNear = .35;   // the blur in front of the focus, relative (the play area nearer the eye)
static REAL sg_sceneDofThreshold = .55;
static REAL sg_sceneDofGain = 6;
static int sg_sceneDofShape = 6;
static int sg_sceneDofRings = 4;
static tSettingItem<bool> sg_sceneDofConf( "SCENE_DOF", sg_sceneDof );
static tSettingItem<REAL> sg_sceneDofFocusConf( "SCENE_DOF_FOCUS", sg_sceneDofFocus );
static tSettingItem<REAL> sg_sceneDofBandConf( "SCENE_DOF_BAND", sg_sceneDofBand );
static tSettingItem<REAL> sg_sceneDofStrengthConf( "SCENE_DOF_STRENGTH", sg_sceneDofStrength );
static tSettingItem<REAL> sg_sceneDofThresholdConf( "SCENE_DOF_THRESHOLD", sg_sceneDofThreshold );
static tSettingItem<REAL> sg_sceneDofGainConf( "SCENE_DOF_GAIN", sg_sceneDofGain );
static tSettingItem<int> sg_sceneDofShapeConf( "SCENE_DOF_SHAPE", sg_sceneDofShape );
static tSettingItem<int> sg_sceneDofRingsConf( "SCENE_DOF_RINGS", sg_sceneDofRings );
static tSettingItem<REAL> sg_sceneDofNearConf( "SCENE_DOF_NEAR", sg_sceneDofNear );

/* the replay's chase camera: grid units behind its cycle and above the floor, how
   far ahead of the cycle it looks (sideways only), and where the cycle sits in the
   frame: SCENE_REPLAY_CHASE_FRAME is its height from the top (0..1); the tilt
   follows from it. A lower, farther camera keeps more of the stands above the
   horizon. SCENE_REPLAY_CHASE_BACK 6, _HEIGHT 4, _LEAD 1.5 is close enough to see a
   cycle up close. */
static REAL sg_sceneChaseBack = 16.5;
static REAL sg_sceneChaseHeight = 6.5;
static REAL sg_sceneChaseLead = 8;
static REAL sg_sceneChaseFrame = .62;
// smoothing on a turn (ReplayChase): the path's averaging time, the heading's
// spring and the position's spring (critically damped; seconds, 1/omega)
static REAL sg_sceneChasePath = 1.2;
static REAL sg_sceneChaseTurn = .8;
static REAL sg_sceneChaseFollow = .15;
static tSettingItem<REAL> sg_sceneChasePathConf( "SCENE_REPLAY_CHASE_PATH", sg_sceneChasePath );
static tSettingItem<REAL> sg_sceneChaseTurnConf( "SCENE_REPLAY_CHASE_TURN", sg_sceneChaseTurn );
static tSettingItem<REAL> sg_sceneChaseFollowConf( "SCENE_REPLAY_CHASE_FOLLOW", sg_sceneChaseFollow );
static REAL sg_sceneChaseTilt = .4;
static REAL sg_sceneChaseFollowAlong = .1;
/* the safe zone the followed cycle is kept in (fractions of the frame from the
   left and the top: clear of the page's menu column and wordmark), see ReplayChase */
static bool sg_sceneFrameZone = true;
static REAL sg_sceneFrameXMin = .42, sg_sceneFrameXMax = .8, sg_sceneFrameYMin = .45, sg_sceneFrameYMax = .8;
static REAL sg_sceneFrameX = .6;         // where across the cycle rests (from the left)
static REAL sg_sceneFrameMargin = .12;   // the smoothed cycle stays this far inside the zone
static REAL sg_sceneFrameSmooth = .3;    // seconds the cycle's place is smoothed over
static REAL sg_sceneFrameRecenter = 1;   // seconds to drift back to the aim inside the zone
static tSettingItem<REAL> sg_sceneFrameXConf( "SCENE_REPLAY_FRAME_X", sg_sceneFrameX );
static tSettingItem<REAL> sg_sceneFrameMarginConf( "SCENE_REPLAY_FRAME_MARGIN", sg_sceneFrameMargin );
static REAL sg_sceneFrameCorrect = .4;   // the view's turn follows its target on this spring (seconds)
static tSettingItem<bool> sg_sceneFrameZoneConf( "SCENE_REPLAY_FRAME_ZONE", sg_sceneFrameZone );
static tSettingItem<REAL> sg_sceneFrameXMinConf( "SCENE_REPLAY_FRAME_X_MIN", sg_sceneFrameXMin );
static tSettingItem<REAL> sg_sceneFrameXMaxConf( "SCENE_REPLAY_FRAME_X_MAX", sg_sceneFrameXMax );
static tSettingItem<REAL> sg_sceneFrameYMinConf( "SCENE_REPLAY_FRAME_Y_MIN", sg_sceneFrameYMin );
static tSettingItem<REAL> sg_sceneFrameYMaxConf( "SCENE_REPLAY_FRAME_Y_MAX", sg_sceneFrameYMax );
static tSettingItem<REAL> sg_sceneFrameSmoothConf( "SCENE_REPLAY_FRAME_SMOOTH", sg_sceneFrameSmooth );
static tSettingItem<REAL> sg_sceneFrameRecenterConf( "SCENE_REPLAY_FRAME_RECENTER", sg_sceneFrameRecenter );
static tSettingItem<REAL> sg_sceneFrameCorrectConf( "SCENE_REPLAY_FRAME_CORRECT", sg_sceneFrameCorrect );
static tSettingItem<REAL> sg_sceneChaseFollowAlongConf( "SCENE_REPLAY_CHASE_FOLLOW_ALONG", sg_sceneChaseFollowAlong );
static tSettingItem<REAL> sg_sceneChaseTiltConf( "SCENE_REPLAY_CHASE_TILT", sg_sceneChaseTilt );
static tSettingItem<REAL> sg_sceneChaseFrameConf( "SCENE_REPLAY_CHASE_FRAME", sg_sceneChaseFrame );
/* SCENE_REPLAY_CAMERA_MODE 3, the cinematic follow: the chase camera, but over
   SCENE_REPLAY_CINEMA_PERIOD seconds it rises from _HEIGHT_LOW/_BACK_LOW (close,
   low) to _HEIGHT_HIGH/_BACK_HIGH (high and far, over the arena and up into the
   stands) and eases back down, each clip starting low; the cycle stays at
   SCENE_REPLAY_CHASE_FRAME. SCENE_REDUCED_MOTION 1 (the page's prefers-reduced-motion,
   aa_api.js) holds it at _HOLD (0 low .. 1 high). */
static REAL sg_sceneCinemaPeriod = 15;
static REAL sg_sceneCinemaHeightLow = 5;
static REAL sg_sceneCinemaHeightHigh = 24;
static REAL sg_sceneCinemaBackLow = 12;
static REAL sg_sceneCinemaBackHigh = 34;
static REAL sg_sceneCinemaFrameHigh = .72;  // the cycle a little lower in the frame up there: the stands show over it
static tSettingItem<REAL> sg_sceneCinemaFrameHighConf( "SCENE_REPLAY_CINEMA_FRAME_HIGH", sg_sceneCinemaFrameHigh );
static REAL sg_sceneCinemaHold = .35;
static bool sg_sceneReducedMotion = false;
static tSettingItem<REAL> sg_sceneCinemaPeriodConf( "SCENE_REPLAY_CINEMA_PERIOD", sg_sceneCinemaPeriod );
static tSettingItem<REAL> sg_sceneCinemaHeightLowConf( "SCENE_REPLAY_CINEMA_HEIGHT_LOW", sg_sceneCinemaHeightLow );
static tSettingItem<REAL> sg_sceneCinemaHeightHighConf( "SCENE_REPLAY_CINEMA_HEIGHT_HIGH", sg_sceneCinemaHeightHigh );
static tSettingItem<REAL> sg_sceneCinemaBackLowConf( "SCENE_REPLAY_CINEMA_BACK_LOW", sg_sceneCinemaBackLow );
static tSettingItem<REAL> sg_sceneCinemaBackHighConf( "SCENE_REPLAY_CINEMA_BACK_HIGH", sg_sceneCinemaBackHigh );
static tSettingItem<REAL> sg_sceneCinemaHoldConf( "SCENE_REPLAY_CINEMA_HOLD", sg_sceneCinemaHold );
static tSettingItem<bool> sg_sceneReducedMotionConf( "SCENE_REDUCED_MOTION", sg_sceneReducedMotion );
static tSettingItem<REAL> sg_sceneChaseBackConf( "SCENE_REPLAY_CHASE_BACK", sg_sceneChaseBack );
static tSettingItem<REAL> sg_sceneChaseHeightConf( "SCENE_REPLAY_CHASE_HEIGHT", sg_sceneChaseHeight );
static tSettingItem<REAL> sg_sceneChaseLeadConf( "SCENE_REPLAY_CHASE_LEAD", sg_sceneChaseLead );
static REAL sg_sceneFocus = 60;    // the camera's target distance this frame (Place)

//! where the follow camera is this frame (the replay director sets it)
struct gSceneFollowView
{
    bool valid = false;
    eCoord pos, dir;
    REAL z = 10, rise = -.5;
};
static gSceneFollowView sg_sceneFollowView;

static tSettingItem<REAL> sg_sceneCameraXConf( "SCENE_CAMERA_X", sg_sceneCameraX );
static tSettingItem<REAL> sg_sceneCameraYConf( "SCENE_CAMERA_Y", sg_sceneCameraY );
static tSettingItem<REAL> sg_sceneCameraZConf( "SCENE_CAMERA_Z", sg_sceneCameraZ );
static tSettingItem<REAL> sg_sceneCameraPitchConf( "SCENE_CAMERA_PITCH", sg_sceneCameraPitch );
static tSettingItem<REAL> sg_sceneCameraDirConf( "SCENE_CAMERA_DIR", sg_sceneCameraDir );
static tSettingItem<REAL> sg_sceneCameraFovConf( "SCENE_CAMERA_FOV", sg_sceneCameraFov );
static tSettingItem<REAL> sg_sceneTransitionConf( "SCENE_TRANSITION_TIME", sg_sceneTransition );
static tSettingItem<REAL> sg_sceneWallsLengthConf( "SCENE_WALLS_LENGTH", sg_sceneWallsLength );
static tSettingItem<REAL> sg_sceneArenaSizeConf( "SCENE_ARENA_SIZE", sg_sceneArenaSize );
static tSettingItem<bool> sg_sceneRimConf( "SCENE_RIM", sg_sceneRim );
static tSettingItem<int> sg_sceneFloorDetailConf( "SCENE_FLOOR_DETAIL", sg_sceneFloorDetail );
static tSettingItem<int> sg_sceneStadiumConf( "SCENE_STADIUM", sg_sceneStadium );
// the stands around the area the replay uses, SCENE_STADIUM_MARGIN grid units out
// from its recorded corners (SceneFitStadium); SCENE_STADIUM_FIT 0: around the whole arena. Read when
// the scene starts.
static bool sg_sceneStadiumFit = true;
static REAL sg_sceneStadiumMargin = 16;
static tSettingItem<bool> sg_sceneStadiumFitConf( "SCENE_STADIUM_FIT", sg_sceneStadiumFit );
static tSettingItem<REAL> sg_sceneStadiumMarginConf( "SCENE_STADIUM_MARGIN", sg_sceneStadiumMargin );
static tSettingItem<bool> sg_sceneReplayConf( "SCENE_REPLAY", sg_sceneReplay );
static tSettingItem<REAL> sg_sceneReplayXConf( "SCENE_REPLAY_X", sg_sceneReplayX );
static tSettingItem<REAL> sg_sceneReplayYConf( "SCENE_REPLAY_Y", sg_sceneReplayY );
static tSettingItem<REAL> sg_sceneReplaySpeedConf( "SCENE_REPLAY_SPEED", sg_sceneReplaySpeed );
static tSettingItem<REAL> sg_sceneReplayZoneHeightConf( "SCENE_REPLAY_ZONE_HEIGHT", sg_sceneReplayZoneHeight );
static tSettingItem<REAL> sg_sceneReplayCameraXConf( "SCENE_REPLAY_CAMERA_X", sg_sceneReplayCameraX );
static tSettingItem<REAL> sg_sceneReplayCameraYConf( "SCENE_REPLAY_CAMERA_Y", sg_sceneReplayCameraY );
static tSettingItem<REAL> sg_sceneReplayCameraZConf( "SCENE_REPLAY_CAMERA_Z", sg_sceneReplayCameraZ );
static tSettingItem<REAL> sg_sceneReplayCameraPitchConf( "SCENE_REPLAY_CAMERA_PITCH", sg_sceneReplayCameraPitch );
static tSettingItem<REAL> sg_sceneReplayCameraDirConf( "SCENE_REPLAY_CAMERA_DIR", sg_sceneReplayCameraDir );
static tSettingItem<REAL> sg_sceneReplayCameraFovConf( "SCENE_REPLAY_CAMERA_FOV", sg_sceneReplayCameraFov );
static tSettingItem<int> sg_sceneReplayCameraModeConf( "SCENE_REPLAY_CAMERA_MODE", sg_sceneReplayCameraMode );
static tSettingItem<REAL> sg_sceneReplayCameraOrbitConf( "SCENE_REPLAY_CAMERA_ORBIT", sg_sceneReplayCameraOrbit );
static tSettingItem<REAL> sg_sceneReplayCameraShotConf( "SCENE_REPLAY_CAMERA_SHOT", sg_sceneReplayCameraShot );
static tSettingItem<int> sg_scenePlayerGlanceConf( "SCENE_PLAYER_GLANCE", sg_scenePlayerGlance );
static tSettingItem<bool> sg_scenePlayerCameraConf( "SCENE_PLAYER_CAMERA", sg_scenePlayerCamera );
static tSettingItem<REAL> sg_scenePlayerSpeedConf( "SCENE_PLAYER_SPEED", sg_scenePlayerSpeed );
static tSettingItem<int> sg_scenePlayerCourseConf( "SCENE_PLAYER_COURSE", sg_scenePlayerCourse );
static tSettingItem<REAL> sg_scenePlayerCourseWallsConf( "SCENE_PLAYER_COURSE_WALLS", sg_scenePlayerCourseWalls );

namespace
{

// *******************************************************************************************
// the scene camera: watches nothing, sits where the settings put it
// *******************************************************************************************

/* CAMERA_FREE is the one mode whose Render() uses the camera position as it
   is (eCamera::Glance is the identity there), and eCamera::Bound() does
   nothing without a center object, so the view stays exactly where Place()
   puts it. */
class gSceneCamera : public eCamera
{
public:
    gSceneCamera( eGrid * grid, rViewport * viewport )
        : eCamera( grid, viewport, NULL, NULL, CAMERA_FREE )
    {
        center = NULL;      // eCamera::MyInit picked an arbitrary game object
        Place();
    }

    //! applies the SCENE_CAMERA_* settings; called every timestep so the page
    //! can move the camera while the scene runs
    void Place()
    {
        if ( sg_sceneView )
        {
            // around the stadium's floor (its fit, gStadiumSetFit), which may be smaller than the arena
            eRectangle const bounds = gStadiumFloor();
            eCoord const middle = ( bounds.GetLow() + bounds.GetHigh() ) * .5;
            eCoord const size = bounds.GetHigh() - bounds.GetLow();
            REAL const half = ( size.x > size.y ? size.x : size.y ) * .5;
            REAL const angle = ( sg_sceneViewAngle + sg_sceneViewSpeed * tSysTimeFloat() ) * M_PI / 180;
            eCoord const out( cos( angle ), sin( angle ) );
            // a negative distance puts the camera on the other side of the
            // centre, still facing the same way: close to a stand, looking at it
            // measured from the floor's edge in half arenas, so the camera keeps its
            // place among the stands (which keep the arena's scale) when they are
            // fitted in closer: 1 + d is d half arenas out from the floor's edge
            eRectangle const & rim = eWallRim::GetBounds();
            eCoord const rimSize = rim.GetHigh() - rim.GetLow();
            REAL const rimHalf = std::max( REAL( ( rimSize.x > rimSize.y ? rimSize.x : rimSize.y ) * .5 ), REAL( half ) );
            // the floor's edge in the camera's direction (a fitted floor need not be square)
            REAL edge = half;
            if ( fabs( out.x ) > 1E-3 )
                edge = std::min( edge, REAL( size.x * .5 / fabs( out.x ) ) );
            if ( fabs( out.y ) > 1E-3 )
                edge = std::min( edge, REAL( size.y * .5 / fabs( out.y ) ) );
            REAL const along = std::max( REAL( edge + ( fabs( sg_sceneViewDistance ) - 1 ) * rimHalf ), REAL( 1 ) );
            REAL distance = along * ( sg_sceneViewDistance < 0 ? -1 : 1 );
            if ( fabs( distance ) < 1 )
                distance = distance < 0 ? -1 : 1;
            pos = middle + out * distance;
            dir = out * -1;
            top = eCoord( 0, 0 );
            z = sg_sceneViewHeight;
            rise = ( sg_sceneViewLookHeight - sg_sceneViewHeight ) / fabs( distance );
            fov = sg_sceneViewFov;
            lastPos = pos;
            {
                // in focus: the look point (a close-up looks at the stand behind it)
                REAL const dz = sg_sceneViewLookHeight - sg_sceneViewHeight;
                sg_sceneFocus = sqrt( distance * distance + dz * dz );
            }
            return;
        }

        eRectangle const & bounds = eWallRim::GetBounds();
        eCoord const low = bounds.GetLow();
        eCoord const high = bounds.GetHigh();
        eCoord const middle = ( low + high ) * .5;

        bool const replay = sg_sceneCameraReplay;
        REAL const angle = ( replay ? sg_sceneReplayCameraDir : sg_sceneCameraDir ) * M_PI / 180;

        pos = middle + eCoord( ( replay ? sg_sceneReplayCameraX : sg_sceneCameraX ) * ( high.x - low.x ) * .5,
                               ( replay ? sg_sceneReplayCameraY : sg_sceneCameraY ) * ( high.y - low.y ) * .5 );
        dir = eCoord( cos( angle ), sin( angle ) );
        top = eCoord( 0, 0 );
        z = replay ? sg_sceneReplayCameraZ : sg_sceneCameraZ;
        rise = replay ? sg_sceneReplayCameraPitch : sg_sceneCameraPitch;
        fov = replay ? sg_sceneReplayCameraFov : sg_sceneCameraFov;

        if ( replay && SceneReplayCameraMode() != SceneCameraFixed && sg_sceneFollowView.valid )
        {
            pos = sg_sceneFollowView.pos;
            dir = sg_sceneFollowView.dir;
            z = sg_sceneFollowView.z;
            rise = sg_sceneFollowView.rise;
        }
        lastPos = pos;
        // in focus: where the view meets the floor (the cameras look at the action)
        sg_sceneFocus = rise < -.02 ? z / -rise * sqrt( 1 + rise * rise ) : 120;
    }

    //! the player camera (SCENE_REPLAY_CAMERA_MODE 4) runs eCamera's own; the rest Place()
    virtual void Timestep( REAL ts );

    //! as the game's camera has it (gCamera::CenterCycleDir): the direction the cycle's
    //! body faces, which turns smoothly, not its driving direction, which snaps
    virtual eCoord CenterCycleDir() const
    {
        gCycle * cycle = dynamic_cast< gCycle * >( Center() );
        return cycle ? cycle->CamDir() : CenterDir();
    }
};

// *******************************************************************************************
// the scene's cycles
// *******************************************************************************************

/* A recorded cycle's track in grid coordinates and scene time: the corners of
   its wall (the first is the spawn), the heading leaving each, the distance
   driven up to each, and the clock (distance driven against time) that moves
   it along them. Built by the replay director from gSceneReplayData.h. */
struct gSceneTrack
{
    std::vector< eCoord > corners;
    std::vector< eCoord > headings;
    std::vector< REAL > cornerDistance;
    std::vector< REAL > clockTime;
    std::vector< REAL > clockDistance;
    REAL death = -1;                  //!< scene time of the death, < 0: never

    //! distance driven at scene time t
    REAL DistanceAt( REAL t ) const
    {
        size_t const n = clockTime.size();
        if ( n == 0 || t <= clockTime[0] )
            return n ? clockDistance[0] : 0;
        if ( t >= clockTime[n-1] )
            return clockDistance[n-1];
        size_t const hi = std::upper_bound( clockTime.begin(), clockTime.end(), t ) - clockTime.begin();
        REAL const span = clockTime[hi] - clockTime[hi-1];
        REAL const f = span > 0 ? ( t - clockTime[hi-1] ) / span : 1;
        return clockDistance[hi-1] + ( clockDistance[hi] - clockDistance[hi-1] ) * f;
    }

    //! the first scene time the cycle has driven distance d
    REAL TimeAt( REAL d ) const
    {
        size_t const n = clockDistance.size();
        if ( n == 0 || d <= clockDistance[0] )
            return n ? clockTime[0] : 0;
        if ( d >= clockDistance[n-1] )
            return clockTime[n-1];
        size_t const hi = std::lower_bound( clockDistance.begin(), clockDistance.end(), d ) - clockDistance.begin();
        REAL const span = clockDistance[hi] - clockDistance[hi-1];
        REAL const f = span > 0 ? ( d - clockDistance[hi-1] ) / span : 1;
        return clockTime[hi-1] + ( clockTime[hi] - clockTime[hi-1] ) * f;
    }

    //! the point at distance d, on the leg that starts at corner k
    eCoord PointAt( REAL d, size_t k ) const
    {
        return corners[k] + headings[k] * ( d - cornerDistance[k] );
    }
};

/* A cycle without a player that cannot die on its own and can be parked.
   Nothing is ever dangerous to it (EdgeIsDangerous), so no wall, trail or rim
   can kill it: the director alone decides where it goes. Parking freezes it in
   place without stopping the simulation clock, which leaves its trail standing
   exactly where it is.

   A replayed cycle (Follow) is not simulated at all: every frame it is placed
   where its recording says it was, passing its wall's corners exactly (one
   dropped wall per corner, as a turn makes), and it explodes where and when the
   recording's death line says. */
class gSceneCycle : public gCycle
{
public:
    gSceneCycle( eGrid * grid, eCoord const & position, eCoord const & direction )
        : gCycle( grid, position, direction, NULL ), parked_( false ), replay_( false ), corner_( 0 )
    {
    }

    void Park()   { parked_ = true;  }
    void Unpark() { parked_ = false; }

    //! the axis the cycle drives on (Direction() is animated during a turn)
    eCoord DriveDirection() const { return dirDrive; }

    //! drive the recorded track from now on
    void Follow( gSceneTrack const & track )
    {
        track_ = track;
        replay_ = true;
        corner_ = 0;
    }

    //! scene time of the recorded death, < 0: none
    REAL ReplayDeath() const { return replay_ ? track_.death : -1; }

    //! the track it follows, for the course to extend ahead of it
    gSceneTrack & Track() { return track_; }

    //! the speed as recorded (Speed() is the player camera's, see sg_scenePlayerSpeed)
    REAL RecordedSpeed() const { return gCycle::Speed(); }

    virtual REAL Speed() const { return gCycle::Speed() * sg_scenePlayerSpeedScale; }

    //! remove the cycle with its walls at once; a dead cycle's walls would
    //! otherwise stand for WALLS_STAY_UP_DELAY
    void Vanish()
    {
        RemoveFromGame();
        deathTime = -1E+10;
    }

    virtual bool EdgeIsDangerous( eWall const *, REAL, REAL ) const { return false; }

    virtual bool Timestep( REAL currentTime )
    {
        if ( replay_ )
            return Replay( currentTime );
        if ( parked_ )
        {
            if ( currentTime > lastTime )
                lastTime = currentTime;
            return false;
        }
        return gCycle::Timestep( currentTime );
    }

protected:
    virtual void CalculateAcceleration()
    {
        gCycle::CalculateAcceleration();
    }

private:
    bool Replay( REAL currentTime )
    {
        // dead: die completely and leave the object lists (the walls stay)
        if ( !Alive() )
            return gCycle::Timestep( currentTime );

        if ( currentTime < lastTime )
            return false;
        REAL const frame = currentTime - lastTime;

        BrowserSceneExtendWall();  // the first wall

        REAL const now = track_.death >= 0 && currentTime > track_.death ? track_.death : currentTime;
        REAL const driven = track_.DistanceAt( now );

        // every corner passed since the last frame, at the time it was passed
        size_t const corners = track_.corners.size();
        while ( corner_ + 1 < corners && track_.cornerDistance[corner_+1] <= driven )
        {
            ++corner_;
            Advance( track_.corners[corner_], track_.TimeAt( track_.cornerDistance[corner_] ) );
            if ( ( track_.headings[corner_] - dirDrive ).NormSquared() > EPS )
                TurnTo( track_.headings[corner_] );
        }
        Advance( track_.PointAt( driven, corner_ ), currentTime );
        BrowserSceneExtendWall();

        Animate( frame );

        if ( track_.death >= 0 && currentTime >= track_.death )
            Kill();

        return false;
    }

    //! move straight on to `to`, arriving at `time`
    void Advance( eCoord const & to, REAL time )
    {
        if ( time < lastTime )
            time = lastTime;
        REAL const step = sqrt( ( to - pos ).NormSquared() );
        if ( time > lastTime + EPS )
            verletSpeed_ = step / ( time - lastTime );
        Move( to, lastTime, time );
        distance += step;
        lastTime = time;
        predictPosition_ = pos;
    }

    //! a turn at the current position: the wall drops here, as gCycle::DoTurn's does
    void TurnTo( eCoord const & heading )
    {
        lastDirDrive = dirDrive;
        dirDrive = heading;
        lastTurnPos_ = pos;
        ++turns;
        FindCurrentFace();
        DropWall();
    }

    //! what gCycle::TimestepCore animates: the wheels and the body swinging
    //! round to the heading
    void Animate( REAL frame )
    {
        if ( frame > .2 )
            frame = .2;
        RotateWheel( rotationFrontWheel, 2 * verletSpeed_ * frame / .43 );
        RotateWheel( rotationRearWheel, 2 * verletSpeed_ * frame / .73 );

        dir = dir + dirDrive * frame * verletSpeed_ * 3;
        eCoord const away = dir - dirDrive;
        REAL const awaySquared = away.NormSquared();
        REAL const maxAway = .8;
        if ( awaySquared > maxAway )
            dir = dirDrive + away * ( 1 / sqrt( awaySquared / maxAway ) );
        dir = dir * ( 1 / sqrt( dir.NormSquared() ) );
    }

    static void RotateWheel( eCoord & r, REAL angle )
    {
        REAL const x = r.x;
        r.x += r.y * angle;
        r.y -= x * angle;
        r = r * ( 1 / sqrt( r.NormSquared() ) );
    }

    bool parked_;
    bool replay_;
    gSceneTrack track_;
    size_t corner_;              //!< the corner the current leg starts at
};

/* A zone drawn exactly as the game draws one (gZone), sized by the replay
   director every frame. It touches nothing: gZone reacts to no cycle. It fades
   in from its creation, as the game's zones do. */
class gSceneZone : public gZone
{
public:
    gSceneZone( eGrid * grid, eCoord const & center, REAL r, REAL g, REAL b )
        : gZone( grid, center )
    {
        color_.r = r;
        color_.g = g;
        color_.b = b;
        createTime_ = referenceTime_ = lastTime = se_GameTime();
        SetExpansionSpeed( 0 );
    }

    void Size( REAL radius ) { SetRadius( radius ); }
    void Spin( REAL omega ) { SetRotationSpeed( omega ); }
};

// *******************************************************************************************
// the director
// *******************************************************************************************

enum gSceneStepKind
{
    Step_Turn,      //!< turn left (+1) or right (-1)
    Step_Park,      //!< stop here, keep the trail
    Step_Loop       //!< start the step list over (a closed course)
};

struct gSceneStep
{
    REAL distance;  //!< grid units to drive before the step happens
    int kind;
    int turn;       //!< +1 right, -1 left
    size_t to;      //!< Step_Loop: the step to start over at
};

struct gSceneActor
{
    tJUST_CONTROLLED_PTR< gSceneCycle > cycle;
    int level;                        //!< the scene state this actor belongs to
    std::vector< gSceneStep > steps;
    size_t step;
    eCoord mark;                      //!< where the current step started
    eCoord markDir;                   //!< the direction driven since then
    bool leaving;                     //!< retired, removed when the transition ends
    gRealColor trail;                 //!< the trail colour it was spawned with
};

struct gSceneColor
{
    REAL r, g, b;
};

// the cycle colours of state 3, taken from the AI palette in gAIBase.cpp
gSceneColor const sg_sceneExtraColors[4] =
{
    { .2, 1, .2 }, { .2, .6, 1 }, { 1, .6, .2 }, { .6, .2, 1 }
};

gSceneColor const sg_sceneRed = { 1, .2, .2 };
gSceneColor const sg_sceneCyan = { .2, 1, 1 };    // the rival when the player is red
gSceneColor const sg_sceneBot = { .8, .8, .8 };   // the AI grey

struct gScene
{
    bool running = false;
    int state = 0;               //!< the state reached
    int target = 0;              //!< the state being played towards
    bool transitioning = false;
    REAL transitionEnd = 0;

    tJUST_CONTROLLED_PTR< eGrid > grid;
    gSceneCamera * camera = NULL;
    std::vector< gSceneActor > actors;

    REAL width = 250;            //!< arena extents, for the choreography
    REAL height = 250;
    eCoord low;

    // the replay (SCENE_REPLAY, read at the start)
    bool replay = false;
    int replayClip = -1;         //!< the clip playing, -1 before the first
    REAL replayBegin = 0;        //!< scene time the clip's first second is shown at
    REAL replayEnd = 0;          //!< scene time the next clip starts at
    REAL replaySpeed = 1;
    eCoord replayOrigin;         //!< where the recorded arena centre lies on the scene's floor
    std::vector< tJUST_CONTROLLED_PTR< gSceneCycle > > replayCycles;
    std::vector< tJUST_CONTROLLED_PTR< gSceneZone > > replayZones;

    // the follow camera, smoothed
    eCoord viewTarget;
    REAL viewSpread = 20;
    REAL viewAngle = 0;
    REAL viewTime = 0;
    int viewSnap = 0;            //!< frames left to jump straight to the action (a clip starts)

    // the chase camera
    int chaseSubject = -1;       //!< index into replayCycles, -1: pick one
    REAL chaseShotEnd = 0;       //!< scene time the shot ends (a cut to another cycle)
    REAL chaseHoldUntil = -1;    //!< the subject died: hold on it until then, < 0: alive
    eCoord chaseDir;             //!< the subject's heading, smoothed (sumo turns all the time)
    eCoord chaseLastPos;
    eCoord chaseCamera;          //!< the camera's floor position, smoothed
    eCoord chasePath;            //!< the subject's position smoothed over SCENE_REPLAY_CHASE_PATH s
    eCoord chaseHead, chaseHeadVel;  //!< the subject's position followed by a spring (SCENE_REPLAY_CHASE_FOLLOW)
    REAL chaseYaw = 0, chaseYawVel = 0; //!< the camera's heading, a spring (SCENE_REPLAY_CHASE_TURN)
    REAL chaseLead = 0;          //!< the lead in force, easing to 0 while holding on a death
    REAL chasePitch = 0, chasePitchVel = 0; //!< the tilt, a spring (SCENE_REPLAY_CHASE_TILT)
    REAL chaseAlong = 0, chaseAlongVel = 0; //!< the cycle ahead of chaseHead along the heading, a spring
    REAL frameDown = 0, frameBearing = 0;   //!< the cycle's place seen from the camera, smoothed (SCENE_REPLAY_FRAME_*)
    REAL framePitchGoal = 0, frameBiasGoal = 0; //!< the tilt and the turn of the view the safe zone asks for
    REAL frameBias = 0, frameBiasVel = 0;   //!< the view's turn off the camera's heading, a spring
    bool chaseCut = true;        //!< jump, don't glide, to the next view

    // what the scene changes and puts back
    bool savedTextOut = false;
    bool savedAutoDisplayAtSwap = false;
    bool savedFullscreenConsole = false;
    bool savedShowHud = true;
    bool savedFPSOut = false;
    bool savedShowTime = false;
    bool savedUpperSky = false;
    bool savedLowerSky = false;
    REAL savedWallsLength = -1;
    REAL savedWallsStayUpDelay = -1;
    REAL savedArenaSize = .5;
    REAL savedExplosionRadius = 4;
    REAL savedZoneHeight = 5;
    REAL savedExplosionRays = 1;
    REAL savedFloorReflect = 0;
    REAL savedFloorReflectSpread = 2.5;
    bool savedRenderRim = true;
    int savedFloorDetail = rFLOOR_TWOTEXTURE;
};

gScene sg_scene;

//! a point of the arena in normalized coordinates (0,0 = one corner, 1,1 = the other)
eCoord ScenePoint( REAL fx, REAL fy )
{
    return sg_scene.low + eCoord( fx * sg_scene.width, fy * sg_scene.height );
}

gSceneStep MakeTurn( REAL distance, int turn )
{
    gSceneStep step;
    step.distance = distance;
    step.kind = Step_Turn;
    step.turn = turn;
    step.to = 0;
    return step;
}

gSceneStep MakePark( REAL distance )
{
    gSceneStep step;
    step.distance = distance;
    step.kind = Step_Park;
    step.turn = 0;
    step.to = 0;
    return step;
}

//! start the step list over, at step p to (steps before it are a lead-in)
gSceneStep MakeLoop( size_t to = 0 )
{
    gSceneStep step;
    step.distance = 0;
    step.kind = Step_Loop;
    step.turn = 0;
    step.to = to;
    return step;
}

void SceneAdd( int level, eCoord const & position, eCoord const & direction,
               gSceneColor const & color, std::vector< gSceneStep > const & steps )
{
    // a cycle bakes its colour into its body texture when it is created
    sg_BrowserSceneCycleColor( color.r, color.g, color.b );
    gSceneActor actor;
    actor.cycle = new gSceneCycle( sg_scene.grid, position, direction );
    sg_BrowserSceneCycleColorClear();

    actor.level = level;
    actor.steps = steps;
    actor.step = 0;
    actor.mark = position;
    actor.markDir = direction;
    actor.leaving = false;
    actor.trail = actor.cycle->trailColor_;

    sg_scene.actors.push_back( actor );
}

//! the local player's cycle colour (COLOR_R/G/B_1, default yellow)
gSceneColor SceneLocalColor()
{
    gSceneColor color = { 1, 1, .2 };
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( config )
    {
        color.r = config->rgb[0] / 15.0;
        color.g = config->rgb[1] / 15.0;
        color.b = config->rgb[2] / 15.0;
    }
    return color;
}

//! the rival of state 2: red, unless the local player is red himself
gSceneColor SceneRivalColor()
{
    gSceneColor const mine = SceneLocalColor();
    REAL const distance = ( mine.r - sg_sceneRed.r ) * ( mine.r - sg_sceneRed.r )
                        + ( mine.g - sg_sceneRed.g ) * ( mine.g - sg_sceneRed.g )
                        + ( mine.b - sg_sceneRed.b ) * ( mine.b - sg_sceneRed.b );
    return distance < .25 ? sg_sceneCyan : sg_sceneRed;
}

/* The choreography. All positions are normalized arena coordinates; the camera
   sits at the west edge (x small) looking east, so +y is screen left and -y is
   screen right. The tracks are laid out so that no cycle ever crosses another
   cycle's trail and none of them reaches the rim during a transition. */
void SceneSpawn( int level )
{
    REAL const w = sg_scene.width;
    REAL const h = sg_scene.height;

    switch ( level )
    {
    case 1:
        {
            // state 1: the local player's colour drives in from the left and
            // stops mid floor, its trail behind it
            std::vector< gSceneStep > steps;
            steps.push_back( MakePark( .34 * h ) );
            SceneAdd( 1, ScenePoint( .18, .84 ), eCoord( 0, -1 ), SceneLocalColor(), steps );
        }
        break;

    case 2:
        {
            // state 2: a red cycle pulls alongside on a parallel track
            std::vector< gSceneStep > steps;
            steps.push_back( MakePark( .34 * h ) );
            SceneAdd( 2, ScenePoint( .245, .84 ), eCoord( 0, -1 ), SceneRivalColor(), steps );
        }
        break;

    case 3:
        {
            // state 3: four cycles come in from the horizon and fan out. The
            // outer two turn further away than the inner two and everyone
            // turns well before the trails of states 1 and 2, so no track ever
            // crosses another.
            REAL const entryOuter = .18 * w, entryInner = .24 * w;
            REAL const runOuter = .10 * h, runInner = .08 * h;
            REAL const y[4] = { .24, .36, .64, .76 };
            // Turn(+1) is a right turn; a cycle coming towards the camera
            // (heading -x) then swings towards +y. The two on the low y side
            // fan further that way, the two on the high y side the other.
            int const turn[4] = { -1, -1, 1, 1 };
            for ( int i = 0; i < 4; ++i )
            {
                bool const outer = ( i == 0 || i == 3 );
                std::vector< gSceneStep > steps;
                steps.push_back( MakeTurn( outer ? entryOuter : entryInner, turn[i] ) );
                steps.push_back( MakePark( outer ? runOuter : runInner ) );
                SceneAdd( 3, ScenePoint( .60, y[i] ), eCoord( -1, 0 ), sg_sceneExtraColors[i], steps );
            }
        }
        break;

    case 4:
        {
            // state 4: two AI coloured bots circle a square each, clear of the
            // remaining cycle's trail and of one another. The square is longer
            // than SCENE_WALLS_LENGTH, so the loop never closes on itself.
            // Turn(+1) is a right turn, so a square started heading +x runs
            // towards -y; the two squares straddle the camera's axis.
            REAL const side = .12 * w;
            REAL const start[2][2] = { { .22, .64 }, { .38, .52 } };
            for ( int i = 0; i < 2; ++i )
            {
                std::vector< gSceneStep > steps;
                for ( int corner = 0; corner < 4; ++corner )
                    steps.push_back( MakeTurn( side, 1 ) );
                steps.push_back( MakeLoop() );
                SceneAdd( 4, ScenePoint( start[i][0], start[i][1] ), eCoord( 1, 0 ), sg_sceneBot, steps );
            }
        }
        break;
    }
}

// *******************************************************************************************
// the replay: recorded Team Sumo rounds (gSceneReplayData.h)
// *******************************************************************************************

int const sg_sceneReplayClipCount = sizeof( sg_sceneReplayClips ) / sizeof( sg_sceneReplayClips[0] );

//! scene time of a second of the playing clip's round
REAL ReplaySceneTime( REAL roundTime )
{
    gSceneReplayClip const & clip = sg_sceneReplayClips[sg_scene.replayClip];
    return sg_scene.replayBegin + ( roundTime - clip.start ) / sg_scene.replaySpeed;
}

//! a recorded position on the scene's floor
eCoord ReplayPoint( float x, float y )
{
    return sg_scene.replayOrigin + eCoord( x - SG_SCENE_REPLAY_CENTER_X, y - SG_SCENE_REPLAY_CENTER_Y ) * SG_SCENE_REPLAY_SCALE;
}

//! an axis-aligned unit vector along `delta` (zero when there is none)
eCoord ReplayHeading( eCoord const & delta )
{
    if ( fabs( delta.x ) < EPS && fabs( delta.y ) < EPS )
        return eCoord( 0, 0 );
    if ( fabs( delta.x ) >= fabs( delta.y ) )
        return eCoord( delta.x > 0 ? 1 : -1, 0 );
    return eCoord( 0, delta.y > 0 ? 1 : -1 );
}

gSceneTrack ReplayTrack( gSceneReplayCycle const & cycle )
{
    gSceneTrack track;
    for ( int i = 0; i < cycle.corners; ++i )
    {
        float const * corner = sg_sceneReplayCorners + 2 * ( cycle.corner + i );
        track.corners.push_back( ReplayPoint( corner[0], corner[1] ) );
    }

    // the heading leaving each corner: towards the next corner that is not on
    // top of it (the last keeps the one before)
    size_t const n = track.corners.size();
    track.headings.assign( n, eCoord( 1, 0 ) );
    for ( size_t i = n; i > 0; --i )
    {
        size_t const k = i - 1;
        eCoord heading = k + 1 < n ? ReplayHeading( track.corners[k+1] - track.corners[k] ) : eCoord( 0, 0 );
        if ( heading.NormSquared() < .5 )
            heading = k + 1 < n ? track.headings[k+1] : ( k > 0 ? ReplayHeading( track.corners[k] - track.corners[k-1] ) : eCoord( 1, 0 ) );
        track.headings[k] = heading;
    }

    track.cornerDistance.assign( n, 0 );
    for ( size_t k = 1; k < n; ++k )
        track.cornerDistance[k] = track.cornerDistance[k-1] + sqrt( ( track.corners[k] - track.corners[k-1] ).NormSquared() );

    for ( int i = 0; i < cycle.clocks; ++i )
    {
        float const * tick = sg_sceneReplayClock + 2 * ( cycle.clock + i );
        track.clockTime.push_back( ReplaySceneTime( tick[0] ) );
        track.clockDistance.push_back( tick[1] * SG_SCENE_REPLAY_SCALE );
    }
    track.death = cycle.death >= 0 ? ReplaySceneTime( cycle.death ) : -1;
    return track;
}

void ReplayClear()
{
    for ( size_t i = 0; i < sg_scene.replayCycles.size(); ++i )
        sg_scene.replayCycles[i]->Vanish();
    sg_scene.replayCycles.clear();
    for ( size_t i = 0; i < sg_scene.replayZones.size(); ++i )
        if ( sg_scene.replayZones[i] )
            sg_scene.replayZones[i]->RemoveFromGame();
    sg_scene.replayZones.clear();
}

/* Start a clip at scene time `now`. Its cycles spawn where the round started
   them and their first timestep drives them, in no time, to where they were at
   the clip's first second: the walls on the floor are the round's own. */
void ReplayStart( int index, REAL now )
{
    ReplayClear();

    gSceneReplayClip const & clip = sg_sceneReplayClips[index];
    sg_scene.replayClip = index;
    sg_scene.replaySpeed = sg_sceneReplaySpeed > .05 ? sg_sceneReplaySpeed : .05;
    sg_scene.replayBegin = now;
    sg_scene.replayEnd = now + ( clip.end - clip.start ) / sg_scene.replaySpeed;
    sg_scene.replayOrigin = ScenePoint( sg_sceneReplayX, sg_sceneReplayY );
    sg_scene.viewSnap = 2;   // the cycles reach the clip's first second on their first timestep
    sg_scene.chaseSubject = -1;

    // Team Sumo stacks every team's zone on the same circle and zones draw
    // additively: at full colour four of them burn to white, so each gets its
    // share and the stack shows the teams' mixed colour
    REAL const share = clip.zones > 1 ? 1.5 / clip.zones : 1;
    for ( int i = 0; i < clip.zones; ++i )
    {
        gSceneReplayZone const & zone = sg_sceneReplayZones[clip.zone + i];
        sg_scene.replayZones.push_back( new gSceneZone( sg_scene.grid, sg_scene.replayOrigin, zone.r * share, zone.g * share, zone.b * share ) );
    }

    for ( int i = 0; i < clip.cycles; ++i )
    {
        gSceneReplayCycle const & data = sg_sceneReplayCycles[clip.cycle + i];
        gSceneTrack const track = ReplayTrack( data );
        if ( track.corners.empty() )
            continue;
        sg_BrowserSceneCycleColor( data.r, data.g, data.b );
        gSceneCycle * cycle = new gSceneCycle( sg_scene.grid, track.corners[0], track.headings[0] );
        sg_BrowserSceneCycleColorClear();
        cycle->Follow( track );
        sg_scene.replayCycles.push_back( cycle );
    }
}

//! the zones of the playing clip: Team Sumo's shrinking disc, a conquered
//! team's zone collapsing to nothing in SG_SCENE_REPLAY_ZONE_COLLAPSE seconds
void ReplayZones( REAL now )
{
    gSceneReplayClip const & clip = sg_sceneReplayClips[sg_scene.replayClip];
    REAL const t = clip.start + ( now - sg_scene.replayBegin ) * sg_scene.replaySpeed;
    for ( size_t i = 0; i < sg_scene.replayZones.size(); ++i )
    {
        if ( !sg_scene.replayZones[i] )
            continue;
        gSceneReplayZone const & zone = sg_sceneReplayZones[clip.zone + i];
        REAL radius = SG_SCENE_REPLAY_ZONE_RADIUS - SG_SCENE_REPLAY_ZONE_SHRINK * t;
        if ( zone.fall >= 0 && t > zone.fall )
        {
            REAL const atFall = SG_SCENE_REPLAY_ZONE_RADIUS - SG_SCENE_REPLAY_ZONE_SHRINK * zone.fall;
            radius = atFall * ( 1 - ( t - zone.fall ) / SG_SCENE_REPLAY_ZONE_COLLAPSE );
        }
        if ( radius <= 0 )
        {
            sg_scene.replayZones[i]->RemoveFromGame();
            sg_scene.replayZones[i] = NULL;
            continue;
        }
        sg_scene.replayZones[i]->Size( radius * SG_SCENE_REPLAY_SCALE );

        // the spin of gBaseZoneHack: .3 rad/s, plus up to ten segments a second
        // as the conquest nears 1; once fallen it slows by .4 of that per second
        REAL const maxSpin = 10 * 2 * M_PI / 11;
        REAL omega;
        if ( zone.fall >= 0 && t > zone.fall )
        {
            REAL const atFall = .3 + maxSpin;
            omega = atFall * ( 1 - .4 * ( t - zone.fall ) );
        }
        else
        {
            REAL conquered = 0;
            if ( zone.conquests > 0 )
            {
                REAL const k = t / SG_SCENE_REPLAY_CONQUEST_STEP;
                int const lo = (int)k;
                float const * level = sg_sceneReplayConquest + zone.conquest;
                if ( lo + 1 < zone.conquests )
                    conquered = level[lo] + ( level[lo+1] - level[lo] ) * ( k - lo );
                else
                    conquered = level[zone.conquests - 1];
            }
            omega = .3 + conquered * conquered * maxSpin;
        }
        sg_scene.replayZones[i]->Spin( omega );
    }
}

/* The orbit camera: it looks at the live cycles' centre, a cycle that is
   about to die weighing four times as much (the recording knows who dies
   next, so the camera is already there), backs off and rises with how far
   the crowd is spread, and orbits it slowly. Everything is smoothed, so it
   glides; only a new clip makes it jump. */
void ReplayOrbit( REAL now, REAL dt )
{
    eCoord sum( 0, 0 );
    REAL weight = 0;
    for ( size_t i = 0; i < sg_scene.replayCycles.size(); ++i )
    {
        gSceneCycle const & cycle = *sg_scene.replayCycles[i];
        if ( !cycle.Alive() )
            continue;
        REAL const death = cycle.ReplayDeath();
        REAL const w = death >= 0 && death - now < 2.5 ? 4 : 1;
        sum = sum + cycle.Position() * w;
        weight += w;
    }

    if ( weight > 0 )
    {
        eCoord const target = sum * ( 1 / weight );
        // how far the crowd reaches: the farthest cycle but one once there are
        // four (a straggler at the rim should not pull the whole view back)
        std::vector< REAL > reach;
        for ( size_t i = 0; i < sg_scene.replayCycles.size(); ++i )
            if ( sg_scene.replayCycles[i]->Alive() )
                reach.push_back( sqrt( ( sg_scene.replayCycles[i]->Position() - target ).NormSquared() ) );
        std::sort( reach.begin(), reach.end() );
        REAL spread = reach.empty() ? 0 : reach[ reach.size() >= 4 ? reach.size() - 2 : reach.size() - 1 ];
        spread = std::min( std::max( spread, REAL( 8 ) ), REAL( 35 ) );

        if ( sg_scene.viewSnap > 0 )
        {
            --sg_scene.viewSnap;
            sg_scene.viewTarget = target;
            sg_scene.viewSpread = spread;
        }
        else
        {
            REAL const follow = 1 - exp( -dt / 1.0 );
            REAL const zoom = 1 - exp( -dt / 1.5 );
            sg_scene.viewTarget = sg_scene.viewTarget + ( target - sg_scene.viewTarget ) * follow;
            sg_scene.viewSpread += ( spread - sg_scene.viewSpread ) * zoom;
        }
    }

    if ( sg_sceneReplayCameraOrbit > 0 )
        sg_scene.viewAngle += 2 * M_PI * dt / sg_sceneReplayCameraOrbit;

    REAL const distance = 12 + sg_scene.viewSpread * 1.2;
    REAL const height = 7.2 + sg_scene.viewSpread * .6;
    eCoord const dir( cos( sg_scene.viewAngle ), sin( sg_scene.viewAngle ) );
    sg_sceneFollowView.pos = sg_scene.viewTarget - dir * distance;
    sg_sceneFollowView.dir = dir;
    sg_sceneFollowView.z = height;
    sg_sceneFollowView.rise = -height / distance;   // aimed at the target on the floor
    sg_sceneFollowView.valid = true;
}

/* Who the chase camera follows next: a live cycle in the thick of it (the
   number of others within 20 units), better still one that dies within the
   coming shot (the shot then ends on its explosion), never one that dies in
   the next two seconds (no time to settle) and preferably not the last one. */
int ChasePick( REAL now, int previous )
{
    int best = -1;
    REAL bestScore = -1E+10;
    for ( size_t i = 0; i < sg_scene.replayCycles.size(); ++i )
    {
        gSceneCycle const & cycle = *sg_scene.replayCycles[i];
        if ( !cycle.Alive() )
            continue;
        REAL const death = cycle.ReplayDeath();
        if ( death >= 0 && death - now < 2 )
            continue;
        REAL score = 0;
        for ( size_t j = 0; j < sg_scene.replayCycles.size(); ++j )
            if ( j != i && sg_scene.replayCycles[j]->Alive()
                 && ( sg_scene.replayCycles[j]->Position() - cycle.Position() ).NormSquared() < 20 * 20 )
                score += 1;
        if ( death >= 0 && death - now < sg_sceneReplayCameraShot + 1 )
            score += 3;
        if ( (int)i == previous )
            score -= 2;
        if ( score > bestScore )
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

/* The chase camera: behind and above its subject, looking a little ahead of
   it along its smoothed path (a sumo cycle turns several times a second; the
   raw heading would whip the view around; see the springs below). Shots
   cut, the camera glides within one. When the subject dies the camera holds
   on the explosion for a moment before cutting to the next. */
void ReplayChase( REAL now, REAL dt )
{
    gScene & scene = sg_scene;
    if ( scene.viewSnap > 0 )
    {
        // the cycles are still at their spawns; pick once they are in place
        --scene.viewSnap;
        scene.chaseSubject = -1;
        return;
    }

    int const count = scene.replayCycles.size();
    gSceneCycle * subject = scene.chaseSubject >= 0 && scene.chaseSubject < count ? (gSceneCycle *)scene.replayCycles[scene.chaseSubject] : NULL;
    if ( subject && !subject->Alive() && scene.chaseHoldUntil < 0 )
        scene.chaseHoldUntil = now + 1.5;

    bool const holding = subject && !subject->Alive();
    if ( !subject || ( holding && now >= scene.chaseHoldUntil ) || ( !holding && now >= scene.chaseShotEnd ) )
    {
        int const next = ChasePick( now, scene.chaseSubject );
        if ( next >= 0 )
        {
            scene.chaseSubject = next;
            scene.chaseShotEnd = now + sg_sceneReplayCameraShot;
            scene.chaseHoldUntil = -1;
            scene.chaseCut = true;
            subject = scene.replayCycles[next];
        }
    }
    if ( !subject )
        return;

    /* Smooth on a turn: a sumo cycle turns 90 degrees several times a second.
       The camera follows the direction of its smoothed path (where it is
       against where it was over the last SCENE_REPLAY_CHASE_PATH s), its heading
       a critically damped spring on that (SCENE_REPLAY_CHASE_TURN: no step in the
       turning rate), and a spring-followed copy of the cycle's position
       (SCENE_REPLAY_CHASE_FOLLOW: the cycle's own 90 degree changes of velocity
       do not jerk the camera sideways). Camera and look point sit on one line
       through the followed position, so the view's heading is the spring's. */
    eCoord const head = subject->Position();
    bool const cutNow = scene.chaseCut;
    if ( scene.chaseCut )
    {
        eCoord const heading = subject->Direction();
        scene.chaseYaw = atan2( heading.y, heading.x );
        scene.chaseYawVel = 0;
        scene.chasePath = head - heading * .5;
        scene.chaseHead = head;
        scene.chaseHeadVel = eCoord( 0, 0 );
        scene.chaseAlong = 0;
        scene.chaseAlongVel = 0;
        scene.chaseLead = sg_sceneChaseLead;
        scene.chaseLastPos = head;
    }
    else if ( dt > 0 )
    {
        // the critically damped spring's exact step towards `goal`
        auto spring = [dt]( REAL & x, REAL & v, REAL goal, REAL tau )
        {
            REAL const omega = 1 / ( tau > .01 ? tau : .01 );
            REAL const e = exp( -omega * dt );
            REAL const d = x - goal;
            REAL const k = ( v + omega * d ) * dt;
            x = goal + ( d + k ) * e;
            v = ( v - omega * k ) * e;
        };
        if ( !holding )
        {
            REAL const pathTau = sg_sceneChasePath > .01 ? sg_sceneChasePath : .01;
            scene.chasePath = scene.chasePath + ( head - scene.chasePath ) * ( 1 - exp( -dt / pathTau ) );
            eCoord const travel = head - scene.chasePath;
            if ( travel.NormSquared() > .01 )
            {
                REAL goal = atan2( travel.y, travel.x );
                while ( goal - scene.chaseYaw > M_PI )
                    goal -= 2 * M_PI;
                while ( goal - scene.chaseYaw < -M_PI )
                    goal += 2 * M_PI;
                spring( scene.chaseYaw, scene.chaseYawVel, goal, sg_sceneChaseTurn );
            }
        }
        else
        {
            // holding on the explosion: the turn settles, the lead eases out
            spring( scene.chaseYaw, scene.chaseYawVel, scene.chaseYaw, sg_sceneChaseTurn );
        }
        spring( scene.chaseHead.x, scene.chaseHeadVel.x, head.x, sg_sceneChaseFollow );
        spring( scene.chaseHead.y, scene.chaseHeadVel.y, head.y, sg_sceneChaseFollow );
        // along the camera's heading the cycle is followed more closely (FOLLOW_ALONG)
        spring( scene.chaseAlong, scene.chaseAlongVel, eCoord::F( scene.chaseDir, head - scene.chaseHead ), sg_sceneChaseFollowAlong );
        REAL const leadGoal = holding ? 0 : sg_sceneChaseLead;
        scene.chaseLead += ( leadGoal - scene.chaseLead ) * ( 1 - exp( -dt / .6 ) );
        scene.chaseLastPos = head;
    }
    scene.chaseDir = eCoord( cos( scene.chaseYaw ), sin( scene.chaseYaw ) );

    // high enough to see the subject over the walls around it (they stand ~4 tall)
    REAL back = sg_sceneChaseBack, height = sg_sceneChaseHeight, frameAt = sg_sceneChaseFrame;
    REAL const lead = sg_sceneChaseLead;
    if ( SceneReplayCameraMode() == SceneCameraCinema )
    {
        // 0 low and close .. 1 high and far: a cosine from the clip's start, so
        // every clip eases in from low; held still for reduced motion
        REAL s = sg_sceneCinemaHold;
        if ( !sg_sceneReducedMotion && sg_sceneCinemaPeriod > 1 )
            s = .5 - .5 * cos( 2 * M_PI * ( now - scene.replayBegin ) / sg_sceneCinemaPeriod );
        s = s < 0 ? 0 : s > 1 ? 1 : s;
        height = sg_sceneCinemaHeightLow + ( sg_sceneCinemaHeightHigh - sg_sceneCinemaHeightLow ) * s;
        back = sg_sceneCinemaBackLow + ( sg_sceneCinemaBackHigh - sg_sceneCinemaBackLow ) * s;
        frameAt = sg_sceneChaseFrame + ( sg_sceneCinemaFrameHigh - sg_sceneChaseFrame ) * s;
    }
    if ( height < 1 )
        height = 1;
    (void)lead;
    /* The point the camera rides behind: sideways the spring-followed position
       (the zigzag of a sumo cycle, left-right-left several times a second, is
       smoothed out over FOLLOW), along the camera's heading a second, quicker
       spring (FOLLOW_ALONG) on the cycle's own place, so the camera stays close
       to BACK behind it: the distance, and with it the tilt or the cycle's
       height on screen, no longer swings by several units on every change of
       course, while the camera does not jerk back and forth with each leg. */
    eCoord const anchor = scene.chaseHead + scene.chaseDir * scene.chaseAlong;
    {
        eCoord wanted = anchor - scene.chaseDir * back;
        // never into the stands: inside the stadium's floor
        {
            eRectangle const area = gStadiumFloor();
            REAL const inset = 1.5;
            wanted.x = std::min( std::max( wanted.x, REAL( area.GetLow().x + inset ) ), REAL( area.GetHigh().x - inset ) );
            wanted.y = std::min( std::max( wanted.y, REAL( area.GetLow().y + inset ) ), REAL( area.GetHigh().y - inset ) );
        }
        scene.chaseCamera = wanted;
    }
    scene.chaseCut = false;

    // look a little ahead of the subject (the lead eases out while holding on its death)
    eCoord const look = anchor + scene.chaseDir * scene.chaseLead;
    eCoord toLook = look - scene.chaseCamera;
    REAL distance = sqrt( toLook.NormSquared() );
    if ( distance < 1 )
    {
        toLook = scene.chaseDir;
        distance = 1;
    }
    sg_sceneFollowView.pos = scene.chaseCamera;
    sg_sceneFollowView.z = height;
    {
        /* Framing. The view looks along the camera's heading (toLook), turned by a
           small bias, and is tilted so the cycle sits SCENE_REPLAY_CHASE_FRAME down
           the frame. With SCENE_REPLAY_FRAME_ZONE the cycle is kept inside a safe
           zone of the frame (clear of the page's menu and wordmark): its position,
           smoothed over SCENE_REPLAY_FRAME_SMOOTH, sets the range of bias and tilt
           that keeps it inside; the targets stay where they are while it is inside
           (a dead zone), drift back to the plain framing over
           SCENE_REPLAY_FRAME_RECENTER, and are nudged just enough when it would
           leave. Bias and tilt then follow their targets on critically damped
           springs (SCENE_REPLAY_FRAME_CORRECT, SCENE_REPLAY_CHASE_TILT). The field
           of view per rViewport::Perspective: tan(fov/2) across, wider than 1.5:1
           stretched, up and down that over the aspect. */
        REAL const aspect = sr_screenHeight > 0 ? REAL( sr_screenWidth ) / REAL( sr_screenHeight ) : 1.5;
        REAL const halfAcross = ( aspect > 1.5 ? aspect / 1.5 : 1 ) * tan( sg_sceneReplayCameraFov * M_PI / 360 );
        REAL const halfUp = halfAcross / ( aspect > .1 ? aspect : .1 );
        REAL const baseYaw = atan2( toLook.y, toLook.x );

        eCoord const toCycle = head - scene.chaseCamera;
        REAL const across = std::max( REAL( sqrt( toCycle.NormSquared() ) ), REAL( 1 ) );
        REAL const down = atan2( height - .5, across );
        REAL bearing = atan2( toCycle.y, toCycle.x ) - baseYaw;
        while ( bearing > M_PI )
            bearing -= 2 * M_PI;
        while ( bearing < -M_PI )
            bearing += 2 * M_PI;

        REAL const frame = frameAt < .05 ? .05 : frameAt > .95 ? .95 : frameAt;
        REAL const below = atan( ( 2 * frame - 1 ) * halfUp );
        bool const zone = sg_sceneFrameZone && sg_sceneFrameXMax > sg_sceneFrameXMin && sg_sceneFrameYMax > sg_sceneFrameYMin;
        bool const reset = cutNow || dt <= 0;

        // the cycle's place, smoothed (the zone looks at this, never the raw one)
        if ( reset )
        {
            scene.frameDown = down;
            scene.frameBearing = bearing;
        }
        else
        {
            REAL const smooth = 1 - exp( -dt / ( sg_sceneFrameSmooth > .01 ? sg_sceneFrameSmooth : .01 ) );
            scene.frameDown += ( down - scene.frameDown ) * smooth;
            scene.frameBearing += ( bearing - scene.frameBearing ) * smooth;
        }

        REAL pitchGoal = down - below, biasGoal = 0;
        if ( zone )
        {
            // the smoothed cycle is held inside the zone shrunk by SCENE_REPLAY_FRAME_MARGIN
            // (its weaving around that stays inside the zone itself), and drifts back to
            // SCENE_REPLAY_FRAME_X across and SCENE_REPLAY_CHASE_FRAME down
            auto clampTo = []( REAL x, REAL lo, REAL hi ) { return x < lo ? lo : x > hi ? hi : x; };
            REAL const margin = sg_sceneFrameMargin > 0 ? sg_sceneFrameMargin : 0;
            REAL const yMin = std::min( REAL( sg_sceneFrameYMin + margin ), frame ), yMax = std::max( REAL( sg_sceneFrameYMax - margin ), frame );
            REAL const xAim = clampTo( sg_sceneFrameX, sg_sceneFrameXMin, sg_sceneFrameXMax );
            REAL const xMin = std::min( REAL( sg_sceneFrameXMin + margin ), xAim ), xMax = std::max( REAL( sg_sceneFrameXMax - margin ), xAim );
            REAL const pitchLo = scene.frameDown - atan( ( 2 * yMax - 1 ) * halfUp );
            REAL const pitchHi = scene.frameDown - atan( ( 2 * yMin - 1 ) * halfUp );
            REAL const biasLo = scene.frameBearing - atan( ( 1 - 2 * xMin ) * halfAcross );
            REAL const biasHi = scene.frameBearing - atan( ( 1 - 2 * xMax ) * halfAcross );
            REAL const pitchAim = scene.frameDown - below;
            REAL const biasAim = scene.frameBearing - atan( ( 1 - 2 * xAim ) * halfAcross );
            if ( reset )
            {
                scene.framePitchGoal = pitchAim;
                scene.frameBiasGoal = biasAim;
            }
            else
            {
                // inside: drift slowly back to the aim (the dead zone is calm)
                REAL const ease = 1 - exp( -dt / ( sg_sceneFrameRecenter > .1 ? sg_sceneFrameRecenter : .1 ) );
                scene.framePitchGoal += ( pitchAim - scene.framePitchGoal ) * ease;
                scene.frameBiasGoal += ( biasAim - scene.frameBiasGoal ) * ease;
            }
            // outside: just far enough to bring it back
            scene.framePitchGoal = clampTo( scene.framePitchGoal, pitchLo, pitchHi );
            scene.frameBiasGoal = clampTo( scene.frameBiasGoal, biasLo, biasHi );
            pitchGoal = scene.framePitchGoal;
            biasGoal = scene.frameBiasGoal;
        }
        if ( pitchGoal > 1.3 )
            pitchGoal = 1.3;
        if ( pitchGoal < -.3 )
            pitchGoal = -.3;

        auto spring = [dt]( REAL & x, REAL & v, REAL goal, REAL tau )
        {
            REAL const omega = 1 / ( tau > .01 ? tau : .01 );
            REAL const e = exp( -omega * dt );
            REAL const d = x - goal;
            REAL const k = ( v + omega * d ) * dt;
            x = goal + ( d + k ) * e;
            v = ( v - omega * k ) * e;
        };
        /* The tilt on its own critically damped spring (SCENE_REPLAY_CHASE_TILT):
           solved every frame from the live camera-to-cycle distance, it bobbed on
           every turn. 0 solves it per frame. */
        if ( reset || sg_sceneChaseTilt <= 0 )
        {
            scene.chasePitch = pitchGoal;
            scene.chasePitchVel = 0;
        }
        else
            spring( scene.chasePitch, scene.chasePitchVel, pitchGoal, sg_sceneChaseTilt );
        if ( reset || !zone )
        {
            scene.frameBias = biasGoal;
            scene.frameBiasVel = 0;
        }
        else
            spring( scene.frameBias, scene.frameBiasVel, biasGoal, sg_sceneFrameCorrect );

        REAL const yaw = baseYaw + scene.frameBias;
        sg_sceneFollowView.dir = eCoord( cos( yaw ), sin( yaw ) );
        sg_sceneFollowView.rise = -tan( scene.chasePitch );
    }
    sg_sceneFollowView.valid = true;
}

/* The player camera's subject: the cycle that lives longest from now (one that survives the
   clip best), in the thick of it as the tie break. It is kept until it dies, then held 0.8 s
   on the explosion (eCamera itself would switch to an arbitrary object after a second). */
int PlayerPick( REAL now )
{
    int best = -1;
    REAL bestScore = -1E+10;
    for ( size_t i = 0; i < sg_scene.replayCycles.size(); ++i )
    {
        gSceneCycle const & cycle = *sg_scene.replayCycles[i];
        if ( !cycle.Alive() )
            continue;
        REAL const death = cycle.ReplayDeath();
        REAL score = death < 0 ? 1000 : death - now;
        for ( size_t j = 0; j < sg_scene.replayCycles.size(); ++j )
            if ( j != i && sg_scene.replayCycles[j]->Alive()
                 && ( sg_scene.replayCycles[j]->Position() - cycle.Position() ).NormSquared() < 20 * 20 )
                score += .5;
        if ( score > bestScore )
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

void ReplayPlayer( REAL now )
{
    gScene & scene = sg_scene;
    sg_sceneFollowView.valid = false;
    if ( scene.viewSnap > 0 )
    {
        // the cycles are still at their spawns; pick once they are in place
        --scene.viewSnap;
        scene.chaseSubject = -1;
        return;
    }
    int const count = scene.replayCycles.size();
    gSceneCycle * subject = scene.chaseSubject >= 0 && scene.chaseSubject < count ? (gSceneCycle *)scene.replayCycles[scene.chaseSubject] : NULL;
    if ( subject && !subject->Alive() && scene.chaseHoldUntil < 0 )
        scene.chaseHoldUntil = now + .8;
    if ( !subject || ( !subject->Alive() && now >= scene.chaseHoldUntil ) )
    {
        int const next = PlayerPick( now );
        if ( next >= 0 )
        {
            scene.chaseSubject = next;
            scene.chaseHoldUntil = -1;
            scene.chaseCut = true;
        }
    }
}

void ReplayCamera( REAL now )
{
    REAL const dt = now > sg_scene.viewTime ? now - sg_scene.viewTime : 0;
    sg_scene.viewTime = now;
    if ( SceneReplayCameraMode() == SceneCameraPlayer )
        ReplayPlayer( now );
    else if ( SceneReplayCameraMode() == SceneCameraChase || SceneReplayCameraMode() == SceneCameraCinema )
        ReplayChase( now, dt );
    else
        ReplayOrbit( now, dt );
}

// a setting's value by name (the custom camera's numbers live in eCamera.cpp, out of reach)
static REAL SceneConfigReal( char const * name, REAL fallback )
{
    tConfItemBase::tConfItemMap const & map = tConfItemBase::GetConfItemMap();
    tConfItemBase::tConfItemMap::const_iterator found = map.find( tString( name ) );
    if ( found == map.end() )
        return fallback;
    std::ostringstream out;
    found->second->WriteVal( out );
    std::istringstream in( out.str() );
    REAL value = fallback;
    in >> value;
    return in.fail() ? fallback : value;
}

void gSceneCamera::Timestep( REAL ts )
{
    gScene & scene = sg_scene;
    gSceneCycle * subject = NULL;
    if ( sg_sceneCameraReplay && SceneReplayCameraMode() == SceneCameraPlayer && !sg_sceneView
         && scene.chaseSubject >= 0 && scene.chaseSubject < (int)scene.replayCycles.size() )
        subject = scene.replayCycles[scene.chaseSubject];

    if ( !subject )
    {
        sg_scenePlayerSpeedScale = 1;
        if ( mode != CAMERA_FREE )
        {
            mode = CAMERA_FREE;
            center = NULL;
            glancingLeft = glancingRight = glancingBack = false;
        }
        Place();
        return;
    }

    ePlayer * player = ePlayer::PlayerConfig( 0 );
    fov = player ? player->startFOV : 90;
    glancingLeft = sg_scenePlayerGlance == 1;
    glancingRight = sg_scenePlayerGlance == 2;
    glancingBack = sg_scenePlayerGlance == 3;

    if ( mode != CAMERA_CUSTOM || Center() != static_cast< eGameObject * >( subject ) || scene.chaseCut )
    {
        // the clip's pace: the mean recorded speed of the cycles alive now
        REAL sum = 0;
        int alive = 0;
        for ( size_t i = 0; i < scene.replayCycles.size(); ++i )
            if ( scene.replayCycles[i]->Alive() )
            {
                sum += scene.replayCycles[i]->RecordedSpeed();
                ++alive;
            }
        REAL const mean = alive > 0 ? sum / alive : 0;
        sg_scenePlayerSpeedScale = mean > 1 && sg_scenePlayerSpeed > 0 ? sg_scenePlayerSpeed / mean : 1;
        if ( sg_sceneCourseActive )
            sg_scenePlayerSpeedScale = 1; // the course is driven at a real round's pace

        /* a cut: straight to where the custom camera settles behind the new subject, its
           speed seeded, so the first second is not a glide from the old place (the camera
           reads CAMERA_CUSTOM_BACK/_RISE + speed * _FROMSPEED; eCamera.cpp) */
        mode = CAMERA_CUSTOM;
        center = subject;
        REAL const speed = subject->Speed();
        centerSpeedSmooth = speed;
        dir = subject->CamDir();
        REAL const back = SceneConfigReal( "CAMERA_CUSTOM_BACK", 6 ) + speed * SceneConfigReal( "CAMERA_CUSTOM_BACK_FROMSPEED", 0 );
        REAL const up = SceneConfigReal( "CAMERA_CUSTOM_RISE", 4 ) + speed * SceneConfigReal( "CAMERA_CUSTOM_RISE_FROMSPEED", 0 );
        pos = subject->Position() - dir * back;
        lastPos = pos;
        z = subject->CamZ() + up;
        rise = SceneConfigReal( "CAMERA_CUSTOM_PITCH", -.58 );
        glanceSmooth = glanceSmoothAbs = 0;
        scene.chaseCut = false;
    }

    eCamera::Timestep( ts );
    // in focus: the followed cycle
    sg_sceneFocus = ( subject->Position() - pos ).Norm();
}

void SceneTeardown();

/* A fresh grid for the next clip, as the game makes one for every round: one
   grid kept for the whole menu visit would take every clip's trails into its
   triangulation and degrade over time. The scene clock carries on from `now`. */
bool ReplayFreshGrid( REAL now )
{
    gScene & scene = sg_scene;
    if ( !scene.actors.empty() )
        return true; // scripted actors live on this grid: keep it

    ReplayClear();
    delete scene.camera;
    scene.camera = NULL;

    // exit_game_objects and init_game_grid touch the console and input
    // prefetch and stop the game timer: the scene keeps its own
    bool const fullscreen = sr_con.fullscreen;
    bool const prefetch = su_prefetchInput;
    sg_BrowserSceneGridExit( scene.grid );
    scene.grid = tNEW(eGrid());
    bool ok = true;
    try
    {
        sg_BrowserSceneGridInit( scene.grid );
    }
    catch ( tException const & e )
    {
        gBrowserLog( std::string( "scene: fresh grid failed, " ) + std::string( e.GetName() ) );
        ok = false;
    }
    sr_con.fullscreen = fullscreen;
    su_prefetchInput = prefetch;
    se_ResetGameTimer( now );
    se_PauseGameTimer( false, eTIMER_PAUSE_GAME );
    se_SyncGameTimer();
    if ( !ok )
        return false;
    scene.camera = new gSceneCamera( scene.grid, rViewportConfiguration::CurrentViewport( 0 ) );
    return true;
}

// *******************************************************************************************
// the camera course (SCENE_PLAYER_COURSE)
// *******************************************************************************************

struct gSceneCourse
{
    bool active = false;
    int lastClip = -1;       //!< the clip to go on with afterwards
    int section = 0;         //!< the mixed loop's next part
    eCoord pos, heading;     //!< where the track ends now
    REAL distance = 0, time = 0, speed = 0;
};
static gSceneCourse sg_course;

static bool CourseWanted()
{
    return sg_scene.replay && sg_scenePlayerCourse > 0 && SceneReplayCameraMode() == SceneCameraPlayer
           && !sg_sceneView;
}

static gSceneCycle * CourseCycle()
{
    return sg_scene.replayCycles.empty() ? NULL : (gSceneCycle *)sg_scene.replayCycles[0];
}

//! drive `length` straight on from speed v0 to v1 at a steady rate of change
static void CourseLeg( gSceneTrack & track, REAL length, REAL v1 )
{
    gSceneCourse & c = sg_course;
    if ( length <= 0 )
        return;
    REAL const v0 = c.speed;
    int const pieces = fabs( v1 - v0 ) > .5 ? 8 : 1;
    REAL driven = 0, before = v0;
    for ( int i = 1; i <= pieces; ++i )
    {
        REAL const s = length * i / pieces;
        // constant acceleration: v^2 grows linearly with the distance
        REAL const v2 = v0 * v0 + ( v1 * v1 - v0 * v0 ) * s / length;
        REAL const v = sqrt( v2 > 1 ? v2 : 1 );
        REAL const ds = s - driven;
        c.time += 2 * ds / ( before + v );
        c.distance += ds;
        track.clockTime.push_back( c.time );
        track.clockDistance.push_back( c.distance );
        driven = s;
        before = v;
    }
    c.pos = c.pos + c.heading * length;
    c.speed = v1;
}

//! turn here, left (+1) or right (-1)
static void CourseTurn( gSceneTrack & track, int side )
{
    gSceneCourse & c = sg_course;
    c.heading = c.heading.Turn( 0, side );
    track.corners.push_back( c.pos );
    track.headings.push_back( c.heading );
    track.cornerDistance.push_back( c.distance );
}

//! free floor straight ahead of p along h, to the floor's edge less a margin
static REAL CourseRoom( eCoord const & p, eCoord const & h )
{
    eRectangle const floor = gStadiumFloor();
    REAL const margin = 12;
    REAL room = 1E+10;
    if ( h.x > .5 )  room = floor.GetHigh().x - margin - p.x;
    if ( h.x < -.5 ) room = p.x - floor.GetLow().x - margin;
    if ( h.y > .5 )  room = floor.GetHigh().y - margin - p.y;
    if ( h.y < -.5 ) room = p.y - floor.GetLow().y - margin;
    return room;
}

//! the side towards the middle of the floor
static int CourseInward()
{
    gSceneCourse & c = sg_course;
    eRectangle const floor = gStadiumFloor();
    eCoord const middle = ( floor.GetLow() + floor.GetHigh() ) * .5;
    eCoord const left = c.heading.Turn( 0, 1 );
    return eCoord::F( left, middle - c.pos ) >= 0 ? 1 : -1;
}

//! one part of the course, turning in first when the floor ahead is too short for it
static void CourseSection( gSceneTrack & track, int kind )
{
    gSceneCourse & c = sg_course;
    REAL const fast = sg_scenePlayerSpeed > 5 ? sg_scenePlayerSpeed : 30;
    REAL const slow = fast * .25;
    REAL const needs[] = { 0, 0, 90, 70, 65, 120 };
    REAL const need = needs[kind];
    if ( CourseRoom( c.pos, c.heading ) < need )
    {
        // a short run to the turn, at speed, then round towards the middle
        CourseLeg( track, 4, fast );
        CourseTurn( track, CourseInward() );
    }
    REAL const room = CourseRoom( c.pos, c.heading );
    REAL const k = room < need ? std::max( REAL( .4 ), room / need ) : 1;
    int const side = CourseInward();
    switch ( kind )
    {
    case 2: // steady straight
        CourseLeg( track, 90 * k, fast );
        break;
    case 3: // an S-bend: one turn each way
        CourseLeg( track, 30 * k, fast );
        CourseTurn( track, side );
        CourseLeg( track, 14, fast );
        CourseTurn( track, -side );
        CourseLeg( track, 26 * k, fast );
        break;
    case 4: // a reversal: two quick turns the same way
        CourseLeg( track, 30 * k, fast );
        CourseTurn( track, side );
        CourseLeg( track, 3, fast );
        CourseTurn( track, side );
        CourseLeg( track, 32 * k, fast );
        break;
    default: // 5: brake to a crawl, then back up to speed
        CourseLeg( track, 30 * k, fast );
        CourseLeg( track, 22 * k, slow );
        CourseLeg( track, 8 * k, slow );
        CourseLeg( track, 60 * k, fast );
        break;
    }
}

//! the course's track reaches `ahead` seconds past now
static void CourseExtend( REAL now, REAL ahead )
{
    gSceneCycle * cycle = CourseCycle();
    if ( !cycle )
        return;
    gSceneTrack & track = cycle->Track();
    int guard = 0;
    while ( sg_course.time < now + ahead && guard++ < 8 )
    {
        int kind = sg_scenePlayerCourse;
        if ( kind < 2 || kind > 5 )
        {
            static int const loop[] = { 5, 3, 2, 4 };
            kind = loop[sg_course.section++ % 4];
        }
        CourseSection( track, kind );
    }
}

static void CourseStart( REAL now )
{
    gScene & scene = sg_scene;
    sg_course.lastClip = scene.replayClip;
    if ( !ReplayFreshGrid( now ) )
        return;
    ReplayClear();

    eRectangle const floor = gStadiumFloor();
    eCoord const middle = ( floor.GetLow() + floor.GetHigh() ) * .5;
    eCoord const size = floor.GetHigh() - floor.GetLow();
    gSceneCourse & c = sg_course;
    c = gSceneCourse();
    c.active = true;
    sg_sceneCourseActive = true;
    c.lastClip = scene.replayClip;
    // along the floor's long side, from a third of the way in
    c.heading = size.x >= size.y ? eCoord( 1, 0 ) : eCoord( 0, 1 );
    c.pos = middle - c.heading * ( ( size.x >= size.y ? size.x : size.y ) * .3 );
    c.time = now;
    c.speed = sg_scenePlayerSpeed > 5 ? sg_scenePlayerSpeed * .6 : 18;

    gSceneTrack track;
    track.corners.push_back( c.pos );
    track.headings.push_back( c.heading );
    track.cornerDistance.push_back( 0 );
    track.clockTime.push_back( c.time );
    track.clockDistance.push_back( 0 );

    // the player's own colour (0..15 in the game)
    ePlayer * player = ePlayer::PlayerConfig( 0 );
    if ( player )
        sg_BrowserSceneCycleColor( player->rgb[0] / 15.0, player->rgb[1] / 15.0, player->rgb[2] / 15.0 );
    gSceneCycle * cycle = new gSceneCycle( scene.grid, c.pos, c.heading );
    sg_BrowserSceneCycleColorClear();
    cycle->Follow( track );
    scene.replayCycles.push_back( cycle );
    for ( size_t i = 0; i < scene.replayZones.size(); ++i )
        if ( scene.replayZones[i] )
            scene.replayZones[i]->RemoveFromGame();
    scene.replayZones.clear();

    gCycle::SetWallsLength( sg_scenePlayerCourseWalls > 1 ? sg_scenePlayerCourseWalls : 1 );
    scene.viewSnap = 0;
    scene.chaseSubject = -1;
    scene.chaseCut = true;
    CourseExtend( now, 4 );
}

//! back to the recorded rounds, at the clip after the one the course interrupted
static void CourseStop( REAL now )
{
    gScene & scene = sg_scene;
    sg_course.active = false;
    sg_sceneCourseActive = false;
    gCycle::SetWallsLength( SG_SCENE_REPLAY_WALLS_LENGTH );
    if ( !ReplayFreshGrid( now ) )
        return;
    int const next = sg_course.lastClip < 0 ? 0 : ( sg_course.lastClip + 1 ) % sg_sceneReplayClipCount;
    ReplayStart( next, now );
    scene.chaseCut = true;
}

void ReplayDirector( REAL now )
{
    if ( CourseWanted() )
    {
        if ( !sg_course.active )
            CourseStart( now );
        CourseExtend( now, 3 );
        ReplayCamera( now );
        return;
    }
    if ( sg_course.active )
        CourseStop( now );

    if ( sg_scene.replayClip < 0 )
        ReplayStart( 0, now );
    else if ( now >= sg_scene.replayEnd )
    {
        if ( !ReplayFreshGrid( now ) )
        {
            SceneTeardown();
            return;
        }
        ReplayStart( ( sg_scene.replayClip + 1 ) % sg_sceneReplayClipCount, now );
    }
    ReplayZones( now );
    ReplayCamera( now );
}

//! the next state on the way to the target, one step at a time
int SceneNextState( int state, int target )
{
    return state + ( target > state ? 1 : -1 );
}

//! the actor levels a scene state shows
bool SceneLevelWanted( int state, int level )
{
    switch ( state )
    {
    case 1: return level == 1;
    case 2: return level == 1 || level == 2;
    case 3: return level == 1 || level == 2 || level == 3;
    case 4: return level == 1 || level == 4;
    }
    return false;
}

bool SceneHasLevel( int level )
{
    for ( size_t i = 0; i < sg_scene.actors.size(); ++i )
        if ( sg_scene.actors[i].level == level && !sg_scene.actors[i].leaving )
            return true;
    return false;
}

/* Retiring is the reverse choreography: the cycle is un-parked and keeps
   driving the way it was going until the transition is over, then it and its
   trail are removed. It is not a time reversal (nobody drives backwards). */
void SceneRetire( gSceneActor & actor )
{
    actor.leaving = true;
    actor.steps.clear();
    actor.step = 0;
    if ( actor.cycle )
        actor.cycle->Unpark();
}

void SceneDropLeaving()
{
    for ( size_t i = sg_scene.actors.size(); i > 0; --i )
    {
        gSceneActor & actor = sg_scene.actors[i-1];
        if ( !actor.leaving )
            continue;
        if ( actor.cycle )
            actor.cycle->RemoveFromGame();
        actor.cycle = NULL;
        sg_scene.actors.erase( sg_scene.actors.begin() + ( i - 1 ) );
    }
}

void SceneEnter( int state )
{
    // the replay plays whatever the state; the page still gets its events
    if ( sg_scene.replay )
    {
        sg_scene.state = state;
        sg_scene.transitioning = true;
        sg_scene.transitionEnd = se_GameTime();
        gBrowserApiScene( state, true );
        return;
    }

    // whatever this state does not show drives off ...
    for ( size_t i = 0; i < sg_scene.actors.size(); ++i )
        if ( !SceneLevelWanted( state, sg_scene.actors[i].level ) )
            SceneRetire( sg_scene.actors[i] );

    // ... and whatever it shows and is not there yet drives in
    for ( int level = 1; level <= 4; ++level )
        if ( SceneLevelWanted( state, level ) && !SceneHasLevel( level ) )
            SceneSpawn( level );

    sg_scene.state = state;
    sg_scene.transitioning = true;
    sg_scene.transitionEnd = se_GameTime() + sg_sceneTransition;
    gBrowserApiScene( state, true );
}

void SceneDirector( REAL time )
{
    if ( sg_scene.replay )
        ReplayDirector( time );

    for ( size_t i = 0; i < sg_scene.actors.size(); ++i )
    {
        gSceneActor & actor = sg_scene.actors[i];
        if ( !actor.cycle || actor.step >= actor.steps.size() )
            continue;

        gSceneStep const & step = actor.steps[actor.step];
        REAL driven = eCoord::F( actor.markDir, actor.cycle->Position() - actor.mark );
        if ( step.kind == Step_Turn && driven < step.distance )
        {
            // a turn falls due during this frame: advance the cycle to the
            // exact corner first. Turning at whatever position the frame left
            // it at overshoots by up to a frame's drive (1.5 grid units at 20
            // fps), different for every cycle, which drifts the squares.
            REAL const speed = actor.cycle->Speed();
            REAL const last = actor.cycle->LastTime();
            REAL const remaining = step.distance - driven;
            if ( speed > 0 && time > last && remaining < speed * ( time - last ) )
            {
                actor.cycle->Timestep( last + remaining / speed );
                driven = eCoord::F( actor.markDir, actor.cycle->Position() - actor.mark );
            }
            if ( driven < step.distance - .01 )
                continue;
        }
        else if ( driven < step.distance )
            continue;

        switch ( step.kind )
        {
        case Step_Turn:
            // the cycle refuses turns that come too fast after the last one;
            // just try again next frame
            if ( !actor.cycle->Turn( step.turn ) )
                continue;
            actor.mark = actor.cycle->Position();
            actor.markDir = actor.cycle->DriveDirection();
            ++actor.step;
            break;

        case Step_Park:
            actor.cycle->Park();
            ++actor.step;
            break;

        case Step_Loop:
            actor.step = step.to;
            break;
        }
    }

    if ( !sg_scene.transitioning )
        return;

    if ( time < sg_scene.transitionEnd )
        return;

    SceneDropLeaving();
    sg_scene.transitioning = false;
    gBrowserApiScene( sg_scene.state, false );

    // play the next transition of a multi step jump
    if ( sg_scene.state != sg_scene.target )
        SceneEnter( SceneNextState( sg_scene.state, sg_scene.target ) );
}

//! SCENE_RIM, SCENE_FLOOR_DETAIL and the replay's zone height, re-read every
//! frame like the camera
void SceneApplyLook()
{
    eWallRim::SetRenderRim( sg_sceneRim );
    sr_floorDetail = sg_sceneFloorDetail;
    gStadiumSetOverride( sg_sceneStadium );
    sg_explosionRayLength = sg_sceneExplosionRays;
    if ( sg_sceneFloorReflect >= 0 )
    {
        sg_floorReflect = sg_sceneFloorReflect;
        sg_floorReflectSpread = sg_sceneFloorReflectSpread;
    }
#ifdef AA_GLES2
    sr_GLES2SetDepthOfField( sg_sceneDof, sg_sceneDofFocus > 0 ? sg_sceneDofFocus : sg_sceneFocus, sg_sceneDofBand,
                             sg_sceneDofStrength, sg_sceneDofThreshold, sg_sceneDofGain, sg_sceneDofShape, sg_sceneDofRings,
                             sg_sceneDofNear );
#endif
    if ( sg_scene.replay )
        sg_BrowserSetZoneHeight( sg_sceneReplayZoneHeight );
}

void SceneRender()
{
    SceneApplyLook();

    if ( !sr_glOut )
    {
        rSysDep::SwapGL();
        return;
    }

    sr_ResetRenderState();

    sg_ArenaHazeBegin();

    rViewportConfiguration * conf = rViewportConfiguration::CurrentViewportConfiguration();
    conf->Select( 0 );
    if ( sg_scene.camera )
        sg_scene.camera->Render();

    sg_ArenaHazeEnd();

    rSysDep::SwapGL();
    rSysDep::ClearGL();
}

//! the scene ignores the keyboard; drop what the shim queued for the game
void SceneDrainInput()
{
    SDL_Event event;
    while ( SDL_PollEvent( &event ) )
    {
    }
}

// the player's values back (scene end, and around a config save: the
// backdrop's switches must not land in user.cfg)
void SceneRestoreLook()
{
    gScene & scene = sg_scene;
    sr_textOut = scene.savedTextOut;
    sr_con.autoDisplayAtSwap = scene.savedAutoDisplayAtSwap;
    sr_con.fullscreen = scene.savedFullscreenConsole;
    subby_ShowHUD = scene.savedShowHud;
    sr_FPSOut = scene.savedFPSOut;
    showTime = scene.savedShowTime;
    sr_upperSky = scene.savedUpperSky;
    sr_lowerSky = scene.savedLowerSky;
    gCycle::SetWallsLength( scene.savedWallsLength );
    gCycle::SetWallsStayUpDelay( scene.savedWallsStayUpDelay );
    gCycle::SetExplosionRadius( scene.savedExplosionRadius );
    sg_BrowserSetZoneHeight( scene.savedZoneHeight );
    sg_explosionRayLength = scene.savedExplosionRays;
    sg_floorReflect = scene.savedFloorReflect;
    sg_floorReflectSpread = scene.savedFloorReflectSpread;
#ifdef AA_GLES2
    sr_GLES2SetDepthOfField( false, 1, 0, 0, 0, 0, 0, 1 );
#endif
    gArena::SetSizeMultiplier( scene.savedArenaSize );
    eWallRim::SetRenderRim( scene.savedRenderRim );
    sr_floorDetail = scene.savedFloorDetail;
    gStadiumSetOverride( -1 );
    gStadiumSetFit( NULL );
}

void SceneFitStadium();

// the backdrop's switches: no console lines, no HUD, no sky, its trails and arena
void SceneOverrideLook()
{
    SceneApplyLook();
    if ( sg_scene.grid )
        SceneFitStadium(); // resuming: the stands come back in (SceneRestoreLook dropped the fit)
    sr_textOut = false;
    sr_con.autoDisplayAtSwap = false;
    sr_con.fullscreen = false;
    subby_ShowHUD = false;
    sr_FPSOut = false;
    showTime = false;
    sr_upperSky = false;
    sr_lowerSky = false;
    if ( sg_scene.replay )
    {
        // the recorded rounds' own walls: long, a dead cycle's stays a while,
        // and a death blows the rounds' hole into them
        // (the camera course keeps a short trail)
        gCycle::SetWallsLength( sg_sceneCourseActive ? ( sg_scenePlayerCourseWalls > 1 ? sg_scenePlayerCourseWalls : 1 ) : SG_SCENE_REPLAY_WALLS_LENGTH );
        gCycle::SetWallsStayUpDelay( SG_SCENE_REPLAY_WALLS_STAY_UP );
        gCycle::SetExplosionRadius( SG_SCENE_REPLAY_HOLE_RADIUS * SG_SCENE_REPLAY_SCALE );
    }
    else
    {
        gCycle::SetWallsLength( sg_sceneWallsLength );
        gCycle::SetWallsStayUpDelay( .25 );
    }
    gArena::SetSizeMultiplier( sg_sceneArenaSize );
}

/* The stadium around the area the replay uses (SCENE_STADIUM_FIT): every clip's
   recorded corners plus SCENE_STADIUM_MARGIN, clipped to the arena. The rounds
   were recorded on a fixed map, so the map keeps its size; the stands come in
   (keeping the arena's scale) instead of standing around a mostly empty floor.
   Without the replay (the scripted states) the stands stay at the rim. */
void SceneFitStadium()
{
    gScene & scene = sg_scene;
    if ( !scene.replay || !sg_sceneStadiumFit || sg_sceneReplayClipCount <= 0 )
    {
        gStadiumSetFit( NULL );
        return;
    }
    scene.replayOrigin = ScenePoint( sg_sceneReplayX, sg_sceneReplayY );
    eRectangle used;
    bool any = false;
    for ( int c = 0; c < sg_sceneReplayClipCount; ++c )
    {
        gSceneReplayClip const & clip = sg_sceneReplayClips[c];
        for ( int i = 0; i < clip.cycles; ++i )
        {
            gSceneReplayCycle const & data = sg_sceneReplayCycles[clip.cycle + i];
            gSceneTrack const track = ReplayTrack( data );
            size_t const n = track.corners.size();
            if ( n == 0 || data.clocks <= 0 )
                continue;
            // the stretch of the track that stands as a wall while the clip plays:
            // from WALLS_LENGTH behind where the cycle is at the clip's start to
            // where it is at its end (the round's earlier walls are gone by then)
            float const * clock = sg_sceneReplayClock + 2 * data.clock;
            auto distanceAt = [&]( float t )
            {
                if ( t <= clock[0] )
                    return clock[1];
                for ( int k = 1; k < data.clocks; ++k )
                    if ( t <= clock[2 * k] )
                    {
                        float const t0 = clock[2 * k - 2], t1 = clock[2 * k];
                        float const f = t1 > t0 ? ( t - t0 ) / ( t1 - t0 ) : 1;
                        return clock[2 * k - 1] + ( clock[2 * k + 1] - clock[2 * k - 1] ) * f;
                    }
                return clock[2 * data.clocks - 1];
            };
            REAL const from = distanceAt( clip.start ) * SG_SCENE_REPLAY_SCALE - SG_SCENE_REPLAY_WALLS_LENGTH;
            REAL const to = distanceAt( clip.end ) * SG_SCENE_REPLAY_SCALE;
            for ( size_t k = 0; k < n; ++k )
            {
                REAL const d = track.cornerDistance[k];
                REAL const next = k + 1 < n ? track.cornerDistance[k + 1] : d;
                if ( next < from || d > to )
                    continue;
                // the segment k -> k+1 clipped to [from, to]
                eCoord const a = track.corners[k], b = k + 1 < n ? track.corners[k + 1] : a;
                REAL const length = next - d;
                REAL const f0 = length > 0 ? std::max( (REAL)0, ( from - d ) / length ) : 0;
                REAL const f1 = length > 0 ? std::min( (REAL)1, ( to - d ) / length ) : 0;
                used.Include( a + ( b - a ) * f0 );
                used.Include( a + ( b - a ) * f1 );
                any = true;
            }
        }
    }
    if ( !any )
    {
        gStadiumSetFit( NULL );
        return;
    }
    REAL const margin = sg_sceneStadiumMargin > 0 ? sg_sceneStadiumMargin : 0;
    eRectangle fit( used.GetLow() - eCoord( margin, margin ), used.GetHigh() + eCoord( margin, margin ) );
    gStadiumSetFit( &fit );
    eRectangle const floor = gStadiumFloor();
    gBrowserLog( "scene: stadium fitted to the replay, floor "
                 + std::to_string( (int)( floor.GetHigh().x - floor.GetLow().x ) ) + "x"
                 + std::to_string( (int)( floor.GetHigh().y - floor.GetLow().y ) ) + " from "
                 + std::to_string( (int)floor.GetLow().x ) + "," + std::to_string( (int)floor.GetLow().y )
                 + " (recorded corners " + std::to_string( (int)used.GetLow().x ) + "," + std::to_string( (int)used.GetLow().y )
                 + " to " + std::to_string( (int)used.GetHigh().x ) + "," + std::to_string( (int)used.GetHigh().y )
                 + ", margin " + std::to_string( (int)margin ) + ")" );
}

bool SceneSetup()
{
    gScene & scene = sg_scene;

    scene.savedTextOut = sr_textOut;
    scene.savedAutoDisplayAtSwap = sr_con.autoDisplayAtSwap;
    scene.savedFullscreenConsole = sr_con.fullscreen;
    scene.savedShowHud = subby_ShowHUD;
    scene.savedFPSOut = sr_FPSOut;
    scene.savedShowTime = showTime;
    scene.savedUpperSky = sr_upperSky;
    scene.savedLowerSky = sr_lowerSky;
    scene.savedWallsLength = gCycle::WallsLength();
    scene.savedWallsStayUpDelay = gCycle::WallsStayUpDelay();
    scene.savedExplosionRadius = gCycle::ExplosionRadius();
    scene.savedZoneHeight = sg_BrowserZoneHeight();
    scene.savedExplosionRays = sg_explosionRayLength;
    scene.savedFloorReflect = sg_floorReflect;
    scene.savedFloorReflectSpread = sg_floorReflectSpread;
    scene.savedArenaSize = gArena::GetSizeMultiplier();
    scene.savedRenderRim = eWallRim::GetRenderRim();
    scene.savedFloorDetail = sr_floorDetail;

    // a backdrop: no console lines, no HUD, no scores, no sky. The floor is
    // the game's own, its detail and the rim wall per SCENE_FLOOR_DETAIL and
    // SCENE_RIM (SceneApplyLook). Trails stay as long as the choreography
    // needs and vanish quickly once a cycle has driven off; the replay's are
    // the recorded rounds'.
    scene.replay = sg_sceneReplay && sg_sceneReplayClipCount > 0;
    scene.replayClip = -1;
    sg_sceneCameraReplay = scene.replay;
    sg_sceneFollowView.valid = false;
    scene.viewTime = 0;
    SceneOverrideLook();
    se_UserShowScores( false );

    se_MakeGameTimer();

    scene.grid = tNEW(eGrid());
    try
    {
        sg_BrowserSceneGridInit( scene.grid );
    }
    catch ( tException const & e )
    {
        gBrowserLog( std::string( "scene: map failed to load, " ) + std::string( e.GetName() ) );
        sg_BrowserSceneGridExit( scene.grid );
        scene.grid = NULL;
        se_KillGameTimer();
        return false;
    }

    eRectangle const & bounds = eWallRim::GetBounds();
    scene.low = bounds.GetLow();
    scene.width = bounds.GetHigh().x - bounds.GetLow().x;
    scene.height = bounds.GetHigh().y - bounds.GetLow().y;

    SceneFitStadium();

    rViewport::Update( 1 );
    scene.camera = new gSceneCamera( scene.grid, rViewportConfiguration::CurrentViewport( 0 ) );

    se_ResetGameTimer( 0 );
    se_PauseGameTimer( false, eTIMER_PAUSE_GAME );
    se_SyncGameTimer();

    scene.running = true;
    scene.state = 0;
    scene.target = 0;
    scene.transitioning = false;

    gBrowserLog( "scene: standalone backdrop up, arena "
                 + std::to_string( (int)scene.width ) + "x" + std::to_string( (int)scene.height )
                 + " from " + std::to_string( (int)scene.low.x ) + "," + std::to_string( (int)scene.low.y )
                 + ", camera at " + std::to_string( (int)scene.camera->CameraPos().x ) + ","
                 + std::to_string( (int)scene.camera->CameraPos().y )
                 + " z " + std::to_string( (int)sg_sceneCameraZ )
                 + ", floor detail " + std::to_string( scene.savedFloorDetail ) + " -> "
                 + std::to_string( sg_sceneFloorDetail ) + ", rim " + ( sg_sceneRim ? "on" : "off" ) + "." );
    return true;
}

void SceneTeardown()
{
    gScene & scene = sg_scene;
    if ( !scene.running )
        return;

    // a camera course running when the scene goes: the next scene starts with the replay
    sg_course = gSceneCourse();
    sg_sceneCourseActive = false;

    for ( size_t i = 0; i < scene.actors.size(); ++i )
    {
        if ( scene.actors[i].cycle )
            scene.actors[i].cycle->RemoveFromGame();
        scene.actors[i].cycle = NULL;
    }
    scene.actors.clear();
    ReplayClear();
    scene.replay = false;
    scene.replayClip = -1;
    sg_sceneCameraReplay = false;

    delete scene.camera;
    scene.camera = NULL;

    sg_BrowserSceneGridExit( scene.grid );
    scene.grid = NULL;

    se_KillGameTimer();

    SceneRestoreLook();

    scene.running = false;
    scene.state = 0;
    scene.target = 0;
    scene.transitioning = false;

    gBrowserLog( "scene: stopped." );
}

}

// *******************************************************************************************
// the public interface (gScene.h)
// *******************************************************************************************

bool gSceneStart( int state )
{
    if ( state < 1 || state > 4 )
        return false;

    if ( !sg_scene.running && !SceneSetup() )
        return false;

    sg_scene.target = state;

    // a transition that is still playing finishes first; SceneDirector picks
    // the next one up
    if ( !sg_scene.transitioning )
    {
        if ( sg_scene.state != sg_scene.target )
            SceneEnter( SceneNextState( sg_scene.state, sg_scene.target ) );
        else
            // already there and standing still: say so, a page waiting for the
            // event should not wait forever
            gBrowserApiScene( sg_scene.state, false );
    }

    return true;
}

void gSceneStop()
{
    SceneTeardown();
}

bool gSceneRunning()
{
    return sg_scene.running;
}

void gSceneSuspendLook()
{
    if ( sg_scene.running )
        SceneRestoreLook();
}

void gSceneResumeLook()
{
    if ( sg_scene.running )
        SceneOverrideLook();
}

int gSceneState()
{
    return sg_scene.running ? sg_scene.state : 0;
}

int gSceneTarget()
{
    return sg_scene.running ? sg_scene.target : 0;
}

bool gSceneTransitioning()
{
    return sg_scene.running && sg_scene.transitioning;
}

void gSceneFrame()
{
    if ( !sg_scene.running )
        return;

    SceneDrainInput();

    se_SyncGameTimer();
    REAL const time = se_GameTime();

    sg_scene.grid->SimplifyAll( 10 );

    SceneDirector( time );
    if ( !sg_scene.running )
        return; // the next clip's grid failed to build

    {
        gNetPlayerWall::s_CopyIntoGrid();
        eSoundLocker locker;
        eGameObject::s_Timestep( sg_scene.grid, time, 0 );
        eCamera::s_Timestep( sg_scene.grid, time );
    }

    SceneRender();
}

#endif // __EMSCRIPTEN__
