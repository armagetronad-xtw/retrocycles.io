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

/* Graphics quality tiers for the browser client.

   BROWSER_QUALITY picks a tier: 0 Auto (the default), 1 Low, 2 Medium,
   3 High, 4 Ultra, -1 Custom. A tier is a set of values for the look
   settings that cost GPU time (bloom, MSAA, the floor reflection, the
   stadium's lights; gCycle.cpp, gBrowser.cpp, gStadium.cpp). Custom leaves
   every one of those settings to the player.

   The resolution is the player's alone, whatever the quality: the device's
   full resolution (the render density, the backing store's pixels per CSS
   pixel, sdl_shim.cpp sdlshim_SetRenderDensity, uncapped), or that times
   BROWSER_RENDER_SCALE (the page's RESOLUTION row). Nothing lowers it on its
   own: the HUD's text and the cycles are drawn into the same frame, and a lower
   density stretches them soft.

   Auto walks the tiers from the measured frame interval: two seconds mostly
   over budget step down, eight seconds comfortably under it try the next step
   up; a step that fails within a few seconds of being tried is tried again
   only after twice the wait (up to five minutes). The first step comes from
   the GPU's name and is remembered across sessions (BROWSER_QUALITY_LEVEL). */

#include "gBrowserQuality.h"

#ifdef __EMSCRIPTEN__

#include "gBrowser.h"
#include "gMenus.h"
#include "rScreen.h"
#include "tConfiguration.h"
#include "uMenu.h"

#include <emscripten.h>
#include <SDL.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

static int sg_quality = 0;
static tSettingItem< int > sg_qualityConf( "BROWSER_QUALITY", sg_quality );
static REAL sg_renderScale = 1;
static tSettingItem< REAL > sg_renderScaleConf( "BROWSER_RENDER_SCALE", sg_renderScale );
// Auto's current step, kept so the next session starts where this one ended (-1: not yet chosen)
static int sg_autoLevel = -1;
static tSettingItem< int > sg_autoLevelConf( "BROWSER_QUALITY_LEVEL", sg_autoLevel );

enum { Tier_Low = 1, Tier_Medium, Tier_High, Tier_Ultra };

/* The tiers' values. Every tier sets every name, so switching is clean.
   Every tier draws the game's own walls (WALL_STYLE 0) with TRAIL_GLASS 1,
   which leaves out their glow (BROWSER_WALL_GLOW, WALL_CONTACT_GLOW). */
struct gQualityValue { char const * name; char const * low, * medium, * high, * ultra; };
static gQualityValue const sg_tierValues[] =
{
    //  name                       low    medium high   ultra
    { "BROWSER_MSAA",             "0",   "2",   "4",   "4" },
    { "BROWSER_BLOOM",            "0",   "1",   "1",   "1" },
    { "BROWSER_BLOOM_INTENSITY",  ".45", ".45", ".45", ".45" },
    { "BROWSER_BLOOM_WIDE",       "0",   "0",   ".4",  ".6" },
    { "BROWSER_BLOOM_SPILL",      "0",   "0",   ".2",  ".3" },
    { "BROWSER_WALL_GLOW",        "0",   "1",   "1",   "1" },
    { "CYCLE_LIGHTING",           "1",   "1",   "1",   "1" },
    { "WALL_STYLE",               "0",   "0",   "0",   "0" },
    { "WALL_EMIT",                "0",   "0",   "0",   "0" },
    { "WALL_CONTACT_GLOW",        ".8",  ".8",  ".8",  ".8" },
    { "WALL_CONTACT_HEIGHT",      ".06", ".06", ".06", ".06" },
    // the floor mirror is off in every tier
    { "FLOOR_REFLECT",            "0",   "0",   "0",   "0" },
    { "FLOOR_REFLECT_SPREAD",     "4",   "4",   "4",   "4" },
    { "FLOOR_REFLECT_FADE",       "0",   "0",   "0",   "0" },
    { "TRAIL_GLASS",              "1",   "1",   "1",   "1" },
    // the stadium's light cones: one wall on Low (gStadium.cpp)
    { "STADIUM_CONE_WALLS",       "1",   "2",   "2",   "2" },
    // the floodlights' cones, pools and halos left out of games on Low (the menu keeps them)
    { "STADIUM_GAME_LIGHTS",      "0",   "1",   "1",   "1" },
    { 0, 0, 0, 0, 0 }
};

// Auto's ladder, cheapest first: the tiers (the resolution is not Auto's to change)
struct gQualityStep { int tier; };
static gQualityStep const sg_ladder[] = { { Tier_Low }, { Tier_Medium }, { Tier_High }, { Tier_Ultra } };
static int const sg_ladderMedium = 1, sg_ladderHigh = 2, sg_ladderUltra = 3;
static int const sg_ladderSize = sizeof( sg_ladder ) / sizeof( sg_ladder[0] );

static char const * TierName( int tier )
{
    switch ( tier )
    {
    case Tier_Low: return "low";
    case Tier_Medium: return "medium";
    case Tier_High: return "high";
    case Tier_Ultra: return "ultra";
    default: return "custom";
    }
}

EM_JS_DEPS( gBrowserQuality, "$stringToNewUTF8" );

// the unmasked GPU name, lower case ("" where the browser hides it)
EM_JS( char *, gQualityRendererName, (), {
    var name = "";
    try {
        if ( typeof GLctx !== "undefined" && GLctx ) {
            var info = GLctx.getExtension( "WEBGL_debug_renderer_info" );
            name = String( GLctx.getParameter( info ? info.UNMASKED_RENDERER_WEBGL : GLctx.RENDERER ) || "" );
        }
    } catch ( e ) {}
    return stringToNewUTF8( name.toLowerCase() );
} );

static std::string sg_renderer;

// Auto's first step on a machine it has not seen
static int GuessLevel()
{
    char * name = gQualityRendererName();
    sg_renderer = name ? name : "";
    free( name );
    auto has = []( char const * word ) { return sg_renderer.find( word ) != std::string::npos; };
    if ( has( "swiftshader" ) || has( "llvmpipe" ) || has( "software" ) )
        return 0;
    if ( has( "nvidia" ) || has( "geforce" ) || has( "radeon" ) || has( "amd" ) )
        return sg_ladderUltra;
    // Chrome on Apple silicon names the chip ("ANGLE Metal Renderer: Apple M1")
    if ( has( "apple m" ) )
        return sg_ladderHigh;
    // Safari says "Apple GPU" on every Mac and iPhone, Intel Macs included: Medium
    if ( has( "apple" ) )
        return sg_ladderMedium;
    // integrated and mobile GPUs: Intel, Mali, Adreno, PowerVR, unknown. Low: antialiasing
    // and bloom are what cost at high pixel densities, and a step down only comes
    // after two slow seconds; a device that holds Low tries Medium after eight good ones
    return 0;
}

// the values of one tier, pushed through the config parser without the
// "X changed" console lines
static void ApplyTier( int tier )
{
    std::ostringstream lines;
    for ( gQualityValue const * value = sg_tierValues; value->name; ++value )
    {
        char const * text = tier == Tier_Low ? value->low : tier == Tier_Medium ? value->medium
                          : tier == Tier_High ? value->high : value->ultra;
        lines << value->name << " " << text << "\n";
    }
    tCurrentAccessLevel level( tAccessLevel_Owner, true );
    bool const printChange = tConfItemBase::printChange;
    tConfItemBase::printChange = false;
    std::istringstream in( lines.str() );
    tConfItemBase::LoadAll( in );
    tConfItemBase::printChange = printChange;
}

// --- the Auto controller ---

struct gQualityAuto
{
    int level = -1;              // the ladder step in use
    double windowStart = 0;      // the current one second window
    int frames = 0, slowFrames = 0;
    std::vector< double > intervals; // the display's frames of the window (not uncapped ones)
    double lastFrame = 0;
    double refreshMs = 0;        // the display's frame interval, as measured (0: not yet)
    double refreshSeen[ 5 ] = {};// the last windows' own measures, newest at refreshNext - 1
    int refreshNext = 0;
    double graceUntil = 0;       // no judging until then (a step just changed)
    int slowWindows = 0, fastWindows = 0;
    double changedAt = 0;
    int probing = -1;            // the step being tried, while it is on trial
    double wait[ sg_ladderSize ] = {};
    double lastSlowFraction = 0;
};
static gQualityAuto sg_auto;

static double const sg_waitMin = 8000, sg_waitMax = 300000;

// the applied state: what the frame last pushed into the settings and the shim
static int sg_appliedTier = -2;
static double sg_appliedDensity = -1;

static void Apply( int tier )
{
    if ( tier != sg_appliedTier )
    {
        sg_appliedTier = tier;
        if ( tier >= Tier_Low )
            ApplyTier( tier );
    }
}

static void ApplyDensity( double density )
{
    if ( density != sg_appliedDensity )
    {
        sg_appliedDensity = density;
        sdlshim_SetRenderDensity( density );
    }
}

static void SetLevel( int level, double now, char const * why )
{
    gQualityAuto & a = sg_auto;
    level = std::max( 0, std::min( sg_ladderSize - 1, level ) );
    if ( level == a.level )
        return;
    a.level = level;
    sg_autoLevel = level;
    a.changedAt = now;
    a.graceUntil = now + 1500;
    a.slowWindows = a.fastWindows = 0;
    Apply( sg_ladder[ level ].tier );
    gBrowserLog( std::string( "quality: auto step " ) + std::to_string( level ) + " (" + TierName( sg_ladder[ level ].tier ) + "): " + why );
}

/* The budget a frame is judged by. A frame the display drives (the animation frame) is slow when
   it took 1.5 refresh intervals: a missed refresh shows as two, and the half interval of slack takes
   the callbacks' own jitter (at 240 Hz a millisecond or two). So a GPU that cannot keep up with a
   144 Hz display steps down to the tier that can: smooth motion over looks. At most 144 fps is asked
   for, so a 240 or 360 Hz display does not push a laptop to Low. Uncapped frames (UNCAPPED_FPS) come
   as fast as the CPU allows and say nothing about the display: a 60 fps rule judges them. */
static double const sg_maxAskedFps = 144;

static double Budget( gQualityAuto const & a, gBrowserFrameKind kind )
{
    if ( kind == gFrame_Uncapped || a.refreshMs <= 0 )
        return 16.7 * 1.2;
    return std::max( a.refreshMs, 1000 / sg_maxAskedFps ) * 1.5;
}

/* The refresh interval, from the display's frames only: each window's 10th percentile (the fast
   end: a GPU that misses refreshes mixes one and two interval frames, and the one interval frames
   are the display's), the smallest of the last five windows. A window moved to a slower display
   takes the new interval after five seconds; meanwhile its frames count as slow, which can cost
   one step down that the next probe takes back. */
static void LearnRefresh( gQualityAuto & a )
{
    if ( a.intervals.size() < 20 )
        return;
    std::sort( a.intervals.begin(), a.intervals.end() );
    int const count = sizeof( a.refreshSeen ) / sizeof( a.refreshSeen[0] );
    a.refreshSeen[ a.refreshNext ] = a.intervals[ a.intervals.size() / 10 ];
    a.refreshNext = ( a.refreshNext + 1 ) % count;
    double best = 0;
    for ( double seen : a.refreshSeen )
        if ( seen > 0 && ( best <= 0 || seen < best ) )
            best = seen;
    a.refreshMs = best;
}

static void AutoFrame( double now, bool rendering, gBrowserFrameKind kind )
{
    gQualityAuto & a = sg_auto;
    // a key's extra frame (BrowserInputWake) is neither the display's nor slow: left out, and the
    // next frame's interval still runs from the frame before it
    if ( kind == gFrame_Input )
        return;
    for ( double & wait : a.wait )
        if ( wait <= 0 )
            wait = sg_waitMin;
    if ( a.level < 0 )
    {
        bool const known = sg_autoLevel >= 0 && sg_autoLevel < sg_ladderSize;
        SetLevel( known ? sg_autoLevel : GuessLevel(), now, known ? "remembered" : ( "first guess for \"" + sg_renderer + "\"" ).c_str() );
    }

    double const interval = a.lastFrame > 0 ? now - a.lastFrame : 0;
    a.lastFrame = now;
    // a hidden tab, a stall (loading, a breakpoint) or a fresh step: nothing to learn
    if ( !rendering || interval <= 0 || interval > 250 || now < a.graceUntil )
    {
        a.windowStart = now;
        a.frames = a.slowFrames = 0;
        a.intervals.clear();
        return;
    }

    if ( kind == gFrame_Display )
        a.intervals.push_back( interval );
    ++a.frames;
    if ( interval > Budget( a, kind ) )
        ++a.slowFrames;
    if ( now - a.windowStart < 1000 )
        return;

    // the window's verdict
    LearnRefresh( a );
    double const slowFraction = double( a.slowFrames ) / a.frames;
    a.lastSlowFraction = slowFraction;
    a.windowStart = now;
    a.frames = a.slowFrames = 0;
    a.intervals.clear();

    if ( slowFraction > .25 )
    {
        a.fastWindows = 0;
        if ( ++a.slowWindows >= 2 && a.level > 0 )
        {
            // a step that fails while on trial waits twice as long for the next try
            if ( a.probing == a.level )
                a.wait[ a.level ] = std::min( a.wait[ a.level ] * 2, sg_waitMax );
            a.probing = -1;
            SetLevel( a.level - 1, now, "frames over budget" );
        }
        return;
    }
    a.slowWindows = 0;
    if ( a.probing == a.level && now - a.changedAt > 6000 )
        a.probing = -1; // the trial passed
    if ( slowFraction < .03 )
        ++a.fastWindows;
    else
        a.fastWindows = 0;
    int const next = a.level + 1;
    if ( next < sg_ladderSize && a.fastWindows >= 8 && now - a.changedAt >= a.wait[ next ] )
    {
        a.probing = next;
        SetLevel( next, now, "headroom, trying the next step" );
    }
}

static void BuildMenu();

void gBrowserQualityFrame( bool rendering, gBrowserFrameKind kind )
{
    BuildMenu();
    double const now = emscripten_get_now();
    // the player's resolution, in every mode: the device's full one unless BROWSER_RENDER_SCALE says less
    ApplyDensity( sg_renderScale > 0 && sg_renderScale < 1 ? sdlshim_GetDevicePixelRatio() * sg_renderScale : 0 );
    if ( sg_quality == 0 )
    {
        AutoFrame( now, rendering, kind );
        return;
    }
    sg_auto.level = -1; // back to Auto starts from the remembered step
    Apply( sg_quality < 0 ? -1 : std::min( (int)Tier_Ultra, sg_quality ) );
}

std::string gBrowserQualityStatus()
{
    char density[16];
    snprintf( density, sizeof( density ), "%.2f", sdlshim_GetRenderDensity() );
    std::string text = std::string( " quality=" ) + ( sg_quality == 0 ? "auto" : TierName( sg_quality ) );
    if ( sg_quality == 0 && sg_auto.level >= 0 )
    {
        char slow[16];
        snprintf( slow, sizeof( slow ), "%.2f", sg_auto.lastSlowFraction );
        text += "/" + std::to_string( sg_auto.level ) + ":" + TierName( sg_ladder[ sg_auto.level ].tier ) + " slow=" + slow;
        char refresh[16];
        snprintf( refresh, sizeof( refresh ), "%.2f", sg_auto.refreshMs );
        text += std::string( " refresh=" ) + refresh;
    }
    return text + " density=" + density;
}

// --- the menu: Display Settings, Graphics Quality ---

class gQualityMenuItem : public uMenuItemSelection< int >
{
public:
    gQualityMenuItem( uMenu * menu )
        : uMenuItemSelection< int >( menu, "$browser_quality_text", "$browser_quality_help", sg_quality )
    {
        NewChoice( "$browser_quality_auto_text", "$browser_quality_auto_help", 0 );
        NewChoice( "$browser_quality_low_text", "$browser_quality_low_help", Tier_Low );
        NewChoice( "$browser_quality_medium_text", "$browser_quality_medium_help", Tier_Medium );
        NewChoice( "$browser_quality_high_text", "$browser_quality_high_help", Tier_High );
        NewChoice( "$browser_quality_ultra_text", "$browser_quality_ultra_help", Tier_Ultra );
        NewChoice( "$browser_quality_custom_text", "$browser_quality_custom_help", -1 );
    }
};

// built on the first frame: a menu item constructed statically could run
// before sg_screenMenu (another translation unit) exists
static void BuildMenu()
{
    static bool built = false;
    if ( built )
        return;
    built = true;
    new gQualityMenuItem( &sg_screenMenu );
}

#endif // __EMSCRIPTEN__
