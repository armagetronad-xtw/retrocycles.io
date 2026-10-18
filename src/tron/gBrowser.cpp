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

#include "gBrowser.h"
#include "gBrowserQuality.h"

#ifdef __EMSCRIPTEN__

#include "gGame.h"
#include "gScene.h"
#include "ePlatform.h"
#include "ePlayer.h"
#include "eTimer.h"
#include "nAuthentication.h"
#include "nNetObject.h"
#include "nNetwork.h"
#include "nServerInfo.h"
#include "nSocket.h"
#include "nTransport.h"
#include "nTransportRtcJs.h"
#include "rRender.h"
#ifdef AA_GLES2
#include "rGLES2.h"
#endif
#include "rScreen.h"
#include "rSDL.h"
#include "rSysdep.h"
#include "rTexture.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tDirectories.h"
#include "tException.h"
#include "tLocale.h"
#include "tSysTime.h"

#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>

//! seconds to wait for the DataChannel to open
static REAL const sg_browserHandshakeSeconds = 20;

//! seconds between two status lines
static REAL const sg_browserStatusInterval = 5;

// gGame.cpp
void sg_Receive();

void gBrowserLog( std::string const & line )
{
    tConsole::BrowserLog( line );
}

// what drives the frame BrowserFrame runs (the uncapped tick and the key wake set it around their call)
static gBrowserFrameKind sg_browserFrameKind = gFrame_Display;

namespace
{

void BrowserLog( std::string const & line )
{
    gBrowserLog( line );
}

//! the page's "#ws://host:port" fragment
std::string BrowserUrlFromLocation()
{
    char * hash = static_cast< char * >( EM_ASM_PTR({
        if (typeof location === "undefined" || !location.hash)
            return 0;
        return stringToNewUTF8(location.hash.substring(1));
    }) );

    if ( !hash )
        return std::string();

    std::string url( hash );
    free( hash );

    return url;
}

//! host part of a ws:// URL
std::string BrowserHost( std::string const & url )
{
    std::string rest = url;

    size_t const scheme = rest.find( "://" );
    if ( scheme != std::string::npos )
        rest = rest.substr( scheme + 3 );

    size_t const end = rest.find_first_of( ":/" );
    if ( end != std::string::npos )
        rest = rest.substr( 0, end );

    return rest;
}

//! "host:port" of a ws(s):// URL, the port from the URL or the scheme's
//! default: what the login's pharming check compares with SERVER_DNS
std::string BrowserHostAndPort( std::string const & url )
{
    std::string rest = url;
    std::string port = rest.compare( 0, 6, "wss://" ) == 0 ? "443" : "80";

    size_t const scheme = rest.find( "://" );
    if ( scheme != std::string::npos )
        rest = rest.substr( scheme + 3 );

    size_t const end = rest.find_first_of( "/?#" );
    if ( end != std::string::npos )
        rest = rest.substr( 0, end );

    size_t const colon = rest.rfind( ':' );
    if ( colon != std::string::npos && rest.find( ']' ) == std::string::npos )
    {
        port = rest.substr( colon + 1 );
        rest = rest.substr( 0, colon );
    }

    return rest + ":" + port;
}

//! dotted quad in network byte order, 127.0.0.1 for anything else. The server
//! only sees the virtual address; the IP is what its log will print.
unsigned int BrowserIPv4( std::string const & host )
{
    unsigned int bytes[4] = {0, 0, 0, 0};
    size_t pos = 0;

    for ( int i = 0; i < 4; ++i )
    {
        size_t const start = pos;
        unsigned int value = 0;
        while ( pos < host.size() && host[pos] >= '0' && host[pos] <= '9' )
            value = value * 10 + static_cast< unsigned int >( host[pos++] - '0' );

        if ( pos == start || value > 255 )
            return htonl( 0x7f000001 );

        bytes[i] = value;

        if ( i < 3 )
        {
            if ( pos >= host.size() || host[pos] != '.' )
                return htonl( 0x7f000001 );
            ++pos;
        }
    }

    if ( pos != host.size() )
        return htonl( 0x7f000001 );

    return htonl( ( bytes[0] << 24 ) | ( bytes[1] << 16 ) | ( bytes[2] << 8 ) | bytes[3] );
}

//! server info carrying the transport's virtual address instead of a DNS name
class gBrowserServerInfo : public nServerInfoBase
{
public:
    gBrowserServerInfo( std::string const & host, nAddress const & address )
    {
        SetConnectionName( tString( host.c_str() ) );
        SetPort( address.GetPort() );
        SetAddress( address );
    }
};

//! frames that reached the buffer swap since the last status line
static int sg_browserSwaps = 0;
void BrowserCountSwap()
{
    ++sg_browserSwaps;
}
static rPerFrameTask sg_browserSwapTask( &BrowserCountSwap );

/* Antialiasing (BROWSER_MSAA <samples>, 0 off): multisampled renderbuffers
   for the shim's offscreen frame, resolved by its blit into the canvas
   (web/wasm/sdl_shim.cpp); the shim clamps to GL_MAX_SAMPLES and ignores
   the value on WebGL 1, where there is no offscreen frame. A tSettingItem,
   so it is persisted through var/autoexec.cfg (gBrowserSettingsText) and
   applied by the frame when it changed. */
static int sg_browserMsaa = 0;
static tSettingItem< int > sg_browserMsaaConf( "BROWSER_MSAA", sg_browserMsaa );

static void BrowserApplyMsaa()
{
    static int applied = -1;
    if ( sg_browserMsaa == applied )
        return;
    applied = sg_browserMsaa;
    sdlshim_SetMultisample( sg_browserMsaa );
}

/* Selective bloom (BROWSER_BLOOM 0/1, BROWSER_BLOOM_INTENSITY 0..1): the GL
   layer remembers the draws that declare GL_EMISSION under per pixel lighting
   (the cycle trim of CYCLE_LIGHTING 1, the only emissive material in the game)
   and adds a blurred copy of just those to the finished frame
   (src/render/rGLES2.cpp, "selective bloom"). Walls, the floor, trails, the
   HUD and the console never enter the mask. tSettingItems like BROWSER_MSAA
   above: persisted through var/autoexec.cfg and applied by the frame. */
static int sg_browserBloom = 0;
static tSettingItem< int > sg_browserBloomConf( "BROWSER_BLOOM", sg_browserBloom );
static REAL sg_browserBloomIntensity = .35;
static tSettingItem< REAL > sg_browserBloomIntensityConf( "BROWSER_BLOOM_INTENSITY", sg_browserBloomIntensity );

/* The bloom's shape (both 0 = the plain quarter resolution blur, as before):
   BROWSER_BLOOM_WIDE <weight> adds a second level blurred at a sixteenth of
   the frame - the long soft halo a bright trail throws - and
   BROWSER_BLOOM_SPILL <amount> lets strong glow burn towards white instead of
   clipping per channel (additive amber over amber stops at yellow otherwise).
   Look settings like the arena's (gCycle.cpp), not persisted. */
static REAL sg_browserBloomWide = 0;
static tSettingItem< REAL > sg_browserBloomWideConf( "BROWSER_BLOOM_WIDE", sg_browserBloomWide );
static REAL sg_browserBloomSpill = 0;
static tSettingItem< REAL > sg_browserBloomSpillConf( "BROWSER_BLOOM_SPILL", sg_browserBloomSpill );

static void BrowserApplyBloom()
{
    static int appliedOn = -1;
    static REAL appliedIntensity = -1;
    static REAL appliedWide = -1, appliedSpill = -1;
#ifdef AA_GLES2
    if ( sg_browserBloomWide != appliedWide || sg_browserBloomSpill != appliedSpill )
    {
        appliedWide = sg_browserBloomWide;
        appliedSpill = sg_browserBloomSpill;
        sr_GLES2SetBloomShape( static_cast< float >( sg_browserBloomWide ), static_cast< float >( sg_browserBloomSpill ) );
    }
#endif
    if ( sg_browserBloom == appliedOn && sg_browserBloomIntensity == appliedIntensity )
        return;
    appliedOn = sg_browserBloom;
    appliedIntensity = sg_browserBloomIntensity;
#ifdef AA_GLES2
    sr_GLES2SetBloom( sg_browserBloom > 0, static_cast< float >( sg_browserBloomIntensity ) );
#endif
}

/* Hidden tab: requestAnimationFrame stops, so the frame callback would not
   run and the connection would time out. The page side (BrowserInstallVisibility
   below) starts a timer while document.hidden and calls aa_background_tick()
   from it: a full frame without rendering (sr_glOut off; the game already
   knows how to run blind, it is the native "window deactivated" path minus
   the texture unload). Timers of a hidden tab are throttled to 1 Hz on the
   main thread and, after a few minutes, to one per minute; a Worker's are
   not, so the ticks come from a Worker when the browser has one and from
   setInterval otherwise. */
static bool sg_browserHidden = false;
static int sg_browserBackgroundTicks = 0;
static int sg_browserBackgroundTicksTotal = 0;
static double sg_browserHiddenSince = 0;

void BrowserVisibility( bool hidden )
{
    if ( hidden == sg_browserHidden )
        return;
    sg_browserHidden = hidden;
    if ( hidden )
    {
        sg_browserHiddenSince = tRealSysTimeFloat();
        sg_browserBackgroundTicksTotal = 0;
        sr_glOut = false;
        BrowserLog( "visibility hidden, ticking from a timer without rendering." );
    }
    else
    {
        sr_glOut = true;
        BrowserLog( "visibility visible after " + std::to_string( tRealSysTimeFloat() - sg_browserHiddenSince )
                    + " s, " + std::to_string( sg_browserBackgroundTicksTotal ) + " background ticks." );
    }
}

// installs the visibilitychange handler and the background ticker on the page
EM_JS(void, BrowserInstallVisibility, (), {
    if (typeof document === "undefined")
        return;
    var ticker = null;
    var interval = 100; // ms between background ticks: 10 simulation steps per second
    function tick() {
        if (document.hidden)
            Module["_aa_background_tick"]();
    }
    function start() {
        if (ticker)
            return;
        try {
            var source = "setInterval(function(){postMessage(0)}," + interval + ")";
            var worker = new Worker(URL.createObjectURL(new Blob([source], { type: "text/javascript" })));
            worker.onmessage = tick;
            ticker = { stop: function() { worker.terminate(); } };
        } catch (e) {
            var id = setInterval(tick, interval);
            ticker = { stop: function() { clearInterval(id); } };
        }
    }
    function stop() {
        if (ticker)
            ticker.stop();
        ticker = null;
    }
    document.addEventListener("visibilitychange", function() {
        var hidden = document.hidden ? 1 : 0;
        if (hidden)
            start();
        else
            stop();
        Module["_aa_visibility"](hidden);
    });
    if (document.hidden) {
        start();
        Module["_aa_visibility"](1);
    }
});

//! one status line every few seconds, from the animation frame callback:
//! whether the game state arrived and how fast frames come
void BrowserStatus()
{
    static double next = 0;
    static double last = 0;
    static int frames = 0;
    ++frames;

    double const now = tRealSysTimeFloat();
    if ( now < next )
        return;
    next = now + sg_browserStatusInterval;

    std::string line = "status netstate=" + std::to_string( static_cast< int >( sn_GetNetState() ) );
    if ( se_mainGameTimer )
    {
        line += " gametime=" + std::to_string( se_GameTime() );
        line += " fps=" + std::to_string( se_mainGameTimer->FPS() );
        line += " synced=" + std::to_string( se_mainGameTimer->IsSynced() ? 1 : 0 );
    }
    else
    {
        line += " gametime=none";
    }
    if ( sn_GetNetState() == nCLIENT )
        line += " ping=" + std::to_string( sn_Connections[0].ping.GetPing() * 1000 ) + "ms";

    // animation frames since the last line and their average wall clock
    // duration (the number the FPS() sample above only approximates), and how
    // many of them rendered
    if ( last > 0 && frames > 0 )
        line += " frames=" + std::to_string( frames ) + " avg_ms=" + std::to_string( ( now - last ) * 1000 / frames )
              + " swaps=" + std::to_string( sg_browserSwaps ) + " glout=" + std::to_string( sr_glOut ? 1 : 0 )
              + " hidden=" + std::to_string( sg_browserHidden ? 1 : 0 ) + " bgticks=" + std::to_string( sg_browserBackgroundTicks );
    {
        // canvas backing store (device pixels) and the pixel ratio behind it
        int width = 0, height = 0;
        sdlshim_GetCanvasSize( &width, &height );
        char ratio[16];
        snprintf( ratio, sizeof( ratio ), "%.2f", sdlshim_GetDevicePixelRatio() );
        line += " size=" + std::to_string( width ) + "x" + std::to_string( height ) + " dpr=" + ratio;
        // the offscreen frame's sample count (BROWSER_MSAA as applied, 0 on WebGL 1)
        line += " msaa=" + std::to_string( sdlshim_GetMultisample() );
        // BROWSER_BLOOM as applied, with its intensity
        char bloom[16];
        snprintf( bloom, sizeof( bloom ), "%d/%.2f", sg_browserBloom, static_cast< double >( sg_browserBloomIntensity ) );
        line += " bloom=";
        line += bloom;
        line += gBrowserQualityStatus();
    }
    {
        // audio: device state (0 none, 1 suspended, 2 running, 3 closed), mixer
        // callbacks in the window and how many of them were not silence
        int audioState = 0, audioCallbacks = 0, audioNonSilent = 0;
        sdlshim_GetAudioStats( &audioState, &audioCallbacks, &audioNonSilent );
        line += " audio=" + std::to_string( audioState ) + "/" + std::to_string( audioCallbacks ) + "/" + std::to_string( audioNonSilent );
    }
#ifdef AA_GLES2
    // the GL layer's counters of the last rendered frame (src/render/rGLES2.h)
    {
        rGLES2Stats const & stats = sr_GLES2LastFrameStats();
        line += " draws=" + std::to_string( stats.draws ) + " blocks=" + std::to_string( stats.begins )
              + " verts=" + std::to_string( stats.vertices )
              + " breaks=" + std::to_string( stats.stateBreaks ) + " glcalls=" + std::to_string( stats.immediateCalls )
              + " webgl=" + std::to_string( sr_GLES2ContextVersion() );
    }
#endif
    last = now;
    frames = 0;
    sg_browserSwaps = 0;
    sg_browserBackgroundTicks = 0;

    BrowserLog( line );
}

/* The event driven main loop. main() hands over to gBrowserStart(), which
   registers BrowserFrame() as the animation frame callback and never returns;
   every wait of the old blocking path is a state here or in
   sg_BrowserConnectPoll (gGame.cpp), polled once per frame. Packets are read
   as they arrive (BrowserNetworkWake, from the DataChannel's onmessage), not
   only at the next frame. A session ends in BrowserEnd(): one
   "BROWSER: disconnected <reason>" line, cleanup, back to idle; the page may
   then connect again. */

enum gBrowserLoopState
{
    Loop_Idle,          // no session; connect() starts one
    Loop_Handshake,     // waiting for the DataChannel to open
    Loop_Connecting,    // login, sync, game: sg_BrowserConnectPoll
    Loop_Local,         // the local game against the AI: sg_BrowserLocalPoll
    Loop_Scene          // the attract scene: gSceneFrame (gScene.cpp)
};

struct gBrowserLoop
{
    gBrowserLoopState state = Loop_Idle;
    bool ended = false;             // a session ran and ended (state Disconnected)
    bool loopRegistered = false;
    std::string url;
    std::string host;
    std::string reason;
    std::string route;              // the selected ICE candidate pair (JSON) of the current/last session
    int progress = 0;               // handshake steps reported to the page (the transport's GetProgress())
    nTransportRtcJs * transport = NULL;
    gBrowserServerInfo * server = NULL;
    nTimeAbsolute handshakeTimeout = 0;
};

gBrowserLoop sg_loop;

gBrowserState BrowserPublicState()
{
    gBrowserLoop const & loop = sg_loop;
    switch ( loop.state )
    {
    case Loop_Handshake:
        return gBrowser_Connecting;
    case Loop_Connecting:
        switch ( sg_BrowserConnectPhase() )
        {
        case 0: // idle: the poll has not run yet or the tail ran
        case 1: // login
            return gBrowser_Connecting;
        case 5: // game
            return sg_BrowserFetching() ? gBrowser_Fetching : gBrowser_Playing;
        default: // syncs, waiting for the game object
            return gBrowser_Syncing;
        }
    case Loop_Local:
        return gBrowser_Local;
    case Loop_Scene:
        return gBrowser_Scene;
    case Loop_Idle:
    default:
        return loop.ended ? gBrowser_Disconnected : gBrowser_Idle;
    }
}

//! the page facing state, tracked so transitions inside gGame.cpp's state
//! machine (login -> sync -> game) produce events too
gBrowserState sg_lastPublicState = gBrowser_Idle;
void BrowserNotifyState()
{
    gBrowserState const now = BrowserPublicState();
    if ( now == sg_lastPublicState )
        return;
    sg_lastPublicState = now;
    gBrowserApiStateChanged();
}

//! "relay" out of {"local":"relay",...}: the value of a flat JSON string member, "" if absent
std::string BrowserJsonMember( std::string const & json, char const * name )
{
    std::string const key = std::string( "\"" ) + name + "\":\"";
    std::string::size_type begin = json.find( key );
    if ( begin == std::string::npos )
        return std::string();
    begin += key.size();
    std::string::size_type const end = json.find( '"', begin );
    return end == std::string::npos ? std::string() : json.substr( begin, end - begin );
}

/* the selected candidate pair, once the browser's stats have it (a few ms after the
   channel opened): kept for the state object's "ice" field and logged when it is
   first known or changes */
void BrowserPollRoute()
{
    gBrowserLoop & loop = sg_loop;
    if ( !loop.transport )
        return;
    std::string const & route = loop.transport->GetRoute();
    if ( route.empty() || route == loop.route )
        return;
    loop.route = route;
    std::string const relay = BrowserJsonMember( route, "relayProtocol" );
    BrowserLog( "ice route local=" + BrowserJsonMember( route, "local" ) + ( relay.empty() ? std::string() : "/" + relay )
                + " remote=" + BrowserJsonMember( route, "remote" ) + " protocol=" + BrowserJsonMember( route, "protocol" ) + "." );
}

/* the handshake steps the transport reached since the last frame, as "handshake" events
   (signalling answered, then the data channel open), each once and in order */
void BrowserPollProgress()
{
    gBrowserLoop & loop = sg_loop;
    if ( !loop.transport )
        return;
    int const progress = loop.transport->GetProgress();
    while ( loop.progress < progress )
    {
        ++loop.progress;
        gBrowserApiHandshake( loop.progress == 1 ? "signalling" : "ice" );
    }
}

void BrowserEnd( std::string const & reason )
{
    gBrowserLoop & loop = sg_loop;
    if ( loop.state != Loop_Handshake && loop.state != Loop_Connecting )
        return;

    BrowserPollRoute();

    // an exception may have left gGame.cpp's state machine running: run its
    // tail now (sn_SetNetState(nSTANDALONE), object cleanup)
    if ( sg_BrowserConnectPhase() != 0 )
    {
        try
        {
            sg_BrowserDisconnect();
            tString ignored;
            sg_BrowserConnectPoll( ignored );
        }
        catch ( ... )
        {
        }
    }

    loop.state = Loop_Idle;
    loop.ended = true;
    loop.reason = reason;

    // a login form the page still shows answers nothing any more
    nAuthentication::CancelPasswordRequest();

    // a platform login token that did not go out belongs to this session only
    ePlatform::SendLoginToken( tString() );

    BrowserLog( "disconnected " + reason );
    BrowserLog( "connection ended." );

    delete loop.server;
    loop.server = NULL;
    if ( loop.transport )
    {
        loop.transport->SetDeliverCallback( NULL );
        nTransport::Unregister( loop.transport );
        loop.transport->Close();
        delete loop.transport;
        loop.transport = NULL;
    }

    // what main() does after the menus return, minus the display teardown:
    // the page keeps the canvas and may connect again
    for ( int i = se_PlayerNetIDs.Len()-1; i>=0; --i )
        se_PlayerNetIDs(i)->RemoveFromGame();
    nNetObject::ClearAll();
    gBrowserPersistSave();

    BrowserNotifyState();
}

/* the local game's end: no transport and no network objects to tear down, but
   the same "a session ran and ended" result the page sees after a disconnect */
void BrowserEndLocal( std::string const & reason )
{
    gBrowserLoop & loop = sg_loop;
    if ( loop.state != Loop_Local )
        return;

    loop.state = Loop_Idle;
    loop.ended = true;
    loop.reason = reason;

    BrowserLog( "local game ended " + reason );

    for ( int i = se_PlayerNetIDs.Len()-1; i>=0; --i )
        se_PlayerNetIDs(i)->RemoveFromGame();
    nNetObject::ClearAll();
    gBrowserPersistSave();

    BrowserNotifyState();
}

//! ends whatever runs (network session, local game or scene)
void BrowserEndAny( std::string const & reason )
{
    switch ( sg_loop.state )
    {
    case Loop_Local:
        BrowserEndLocal( reason );
        break;
    case Loop_Scene:
        BrowserLog( "scene: " + reason );
        gBrowserSceneStop();
        break;
    default:
        BrowserEnd( reason );
        break;
    }
}

//! one animation frame
void BrowserFrame()
{
    gBrowserLoop & loop = sg_loop;

    static double lastEnd = 0;
    double const start = emscripten_get_now();
    if ( lastEnd > 0 && start - lastEnd > 1000 )
        BrowserLog( "frame gap " + std::to_string( start - lastEnd ) + " ms" );
    struct EndStamp { double & lastEnd; double start; ~EndStamp() { lastEnd = emscripten_get_now(); if ( lastEnd - start > 200 ) BrowserLog( "slow frame " + std::to_string( lastEnd - start ) + " ms" ); } } endStamp{ lastEnd, start };

    try
    {
        tAdvanceFrame();
        sr_BrowserFrameBegin();
        sdlshim_FrameCheck();
        gBrowserQualityFrame( sr_glOut && !sg_browserHidden, sg_browserFrameKind );
        BrowserApplyMsaa();
        BrowserApplyBloom();
        BrowserStatus();

        switch ( loop.state )
        {
        case Loop_Idle:
            break;

        case Loop_Handshake:
            BrowserPollProgress();
            if ( loop.transport->GetState() == nTransportRtcJs::Rtc_Open )
            {
                BrowserLog( "data channel open, connecting." );
                loop.server = new gBrowserServerInfo( loop.host, loop.transport->GetServerAddress() );
                loop.state = Loop_Connecting;
                sg_BrowserConnectBegin( loop.server );
            }
            else if ( loop.transport->GetState() != nTransportRtcJs::Rtc_Connecting || tSysTimeFloat() > loop.handshakeTimeout )
            {
                BrowserEnd( "data channel did not open (state " + std::to_string( static_cast< int >( loop.transport->GetState() ) ) + ")" );
            }
            break;

        case Loop_Connecting:
            {
                // cheap (a short string copy); a pair that changes later is logged again
                BrowserPollRoute();
                tString reason;
                if ( sg_BrowserConnectPoll( reason ) )
                    BrowserEnd( std::string( reason ) );
            }
            break;

        case Loop_Local:
            {
                tString reason;
                if ( sg_BrowserLocalPoll( reason ) )
                    BrowserEndLocal( std::string( reason ) );
            }
            break;

        case Loop_Scene:
            gSceneFrame();
            break;
        }

        gBrowserApiFrame();
        BrowserNotifyState();
    }
    catch ( tException const & e )
    {
        BrowserEndAny( std::string( "exception " ) + std::string( e.GetName() ) + ": " + std::string( e.GetDescription() ) );
    }
    catch ( ... )
    {
        BrowserEndAny( "unknown exception" );
    }
}

/* Runs inside the DataChannel's onmessage task, between two frames: read the
   packet now and send what it triggers (acks above all), so the round trip
   the server sees is the link's and not the link plus a frame. */
void BrowserNetworkWake()
{
    if ( sg_loop.state != Loop_Connecting )
        return;

    try
    {
        tAdvanceFrame();
        sg_Receive();
        sn_SendPlanned();
    }
    catch ( tException const & e )
    {
        BrowserLog( std::string( "wake exception " ) + std::string( e.GetName() ) );
    }
}

/* Runs inside the DOM key event: a frame right now, so the turn the key
   triggers is simulated and sent at once instead of at the next animation
   frame (up to 16 ms later). Keys are rare, the extra frame is cheap. */
void BrowserInputWake()
{
    if ( sg_loop.state == Loop_Connecting || sg_loop.state == Loop_Local )
    {
        gBrowserFrameKind const kind = sg_browserFrameKind;
        sg_browserFrameKind = gFrame_Input;
        BrowserFrame();
        sg_browserFrameKind = kind;
    }
}

bool FileExists( std::string const & path )
{
    struct stat st;
    return stat( path.c_str(), &st ) == 0;
}

}

// *******************************************************************************************
// the public session API
// *******************************************************************************************

/* "ICE servers: 3 URLs (turn 2, turns 1, stun 0), policy relay" for the log: counted
   from the JSON text, whose "urls" values are the only strings that start with a
   scheme; usernames and credentials are never printed */
std::string BrowserDescribeIce( std::string const & ice )
{
    if ( ice.empty() )
        return "no ICE servers";
    if ( ice.find( "\"transport\":\"websocket\"" ) != std::string::npos )
        return "game packets over the signalling WebSocket";
    auto count = [&]( char const * prefix ) {
        int n = 0;
        for ( std::string::size_type at = ice.find( prefix ); at != std::string::npos; at = ice.find( prefix, at + 1 ) )
            ++n;
        return n;
    };
    int const turn = count( "\"turn:" ), turns = count( "\"turns:" ), stun = count( "\"stun:" ) + count( "\"stuns:" );
    bool const relay = ice.find( "\"iceTransportPolicy\":\"relay\"" ) != std::string::npos;
    return "ICE servers: " + std::to_string( turn + turns + stun ) + " URLs (turn " + std::to_string( turn ) + ", turns "
           + std::to_string( turns ) + ", stun " + std::to_string( stun ) + "), policy " + ( relay ? "relay" : "all" );
}

bool gBrowserConnect( std::string const & url, std::string const & iceConfig )
{
    gBrowserLoop & loop = sg_loop;

    // the attract scene is a backdrop, not a session: connecting replaces it
    if ( loop.state == Loop_Scene )
        gBrowserSceneStop();

    if ( loop.state != Loop_Idle )
    {
        BrowserLog( "connect refused, a session is running." );
        return false;
    }
    if ( url.empty() )
    {
        BrowserLog( "connect refused, no URL." );
        return false;
    }

    loop.url = url;
    loop.host = BrowserHost( url );
    loop.reason.clear();
    loop.route.clear();
    loop.progress = 0;
    loop.ended = false;

    BrowserLog( "signalling " + loop.url + ", server host " + loop.host + ", " + BrowserDescribeIce( iceConfig ) + "." );

    loop.transport = new nTransportRtcJs;
    if ( !loop.transport->TryOpen( loop.url, BrowserIPv4( loop.host ), iceConfig ) )
    {
        BrowserLog( "FAIL, could not start the handshake." );
        delete loop.transport;
        loop.transport = NULL;
        loop.ended = true;
        loop.reason = "handshake not started";
        BrowserNotifyState();
        return false;
    }

    nTransport::Register( loop.transport );
    loop.transport->SetDeliverCallback( &BrowserNetworkWake );
    nAuthentication::SetClientServerAddress( tString( BrowserHostAndPort( loop.url ).c_str() ) );
    loop.handshakeTimeout = tSysTimeFloat() + sg_browserHandshakeSeconds;
    loop.state = Loop_Handshake;

    BrowserNotifyState();
    return true;
}

bool gBrowserPlatformLogin( std::string const & token )
{
    gBrowserLoop const & loop = sg_loop;
    if ( loop.state != Loop_Handshake && loop.state != Loop_Connecting )
    {
        BrowserLog( "platform login refused, no network session." );
        return false;
    }
    if ( token.empty() )
        return false;

    // queued; ePlatform::Drain (sg_Receive) sends it once the login is through
    ePlatform::SendLoginToken( tString( token.c_str() ) );
    BrowserLog( "platform login token queued." );
    return true;
}

void gBrowserDisconnect()
{
    gBrowserLoop & loop = sg_loop;
    switch ( loop.state )
    {
    case Loop_Idle:
        return;
    case Loop_Handshake:
        BrowserEnd( "requested" );
        return;
    case Loop_Connecting:
        // the state machine ends on its next poll; BrowserEnd runs from the frame
        sg_BrowserDisconnect();
        return;
    case Loop_Local:
        // same: sg_BrowserLocalPoll runs the tail next frame
        sg_BrowserLocalStop();
        return;
    case Loop_Scene:
        gBrowserSceneStop();
        return;
    }
}

// *******************************************************************************************
// the local game (gGame.cpp's sg_BrowserLocal*) and the attract scene (gScene.cpp)
// *******************************************************************************************

bool gBrowserStartLocal()
{
    gBrowserLoop & loop = sg_loop;

    // the scene is a backdrop, not a session: starting a game replaces it
    if ( loop.state == Loop_Scene )
        gBrowserSceneStop();

    if ( loop.state != Loop_Idle )
    {
        BrowserLog( "local game refused, a session is running." );
        return false;
    }

    if ( !sg_BrowserLocalBegin() )
    {
        BrowserLog( "local game refused, the game state machine is busy." );
        return false;
    }

    loop.url.clear();
    loop.reason.clear();
    loop.route.clear();
    loop.ended = false;
    loop.state = Loop_Local;

    BrowserLog( "local game started." );
    BrowserNotifyState();
    return true;
}

void gBrowserStopLocal()
{
    if ( sg_loop.state != Loop_Local )
        return;
    sg_BrowserLocalStop();
}

bool gBrowserScene( int state )
{
    gBrowserLoop & loop = sg_loop;

    if ( loop.state == Loop_Scene )
        return gSceneStart( state );

    if ( loop.state != Loop_Idle )
    {
        BrowserLog( "scene refused, a session is running." );
        return false;
    }

    if ( !gSceneStart( state ) )
        return false;

    loop.state = Loop_Scene;
    BrowserNotifyState();
    return true;
}

void gBrowserSceneStop()
{
    if ( sg_loop.state != Loop_Scene )
        return;
    gSceneStop();
    sg_loop.state = Loop_Idle;
    BrowserNotifyState();
}

gBrowserState gBrowserGetState()
{
    return BrowserPublicState();
}

std::string const & gBrowserLastReason()
{
    return sg_loop.reason;
}

std::string const & gBrowserUrl()
{
    return sg_loop.url;
}

std::string const & gBrowserIceRoute()
{
    return sg_loop.route;
}

/* Uncapped frame rate (UNCAPPED_FPS 1): what a native client does with vsync
   off. requestAnimationFrame runs once per display refresh; with the low
   latency canvas (sdl_shim.cpp) every frame the game draws reaches the screen
   as soon as it is drawn, so frames between two refreshes are not wasted: the
   scanout picks up the newest one, like a native front buffer. The frames
   come from a MessageChannel loop (a macrotask with no timer clamp; a
   setTimeout carries the wait when MAX_FPS asks for one) while a game runs
   and the tab is visible; the animation frame callback stands aside
   meanwhile and restarts the loop when it has ended (setting turned off,
   tab hidden - the background ticker takes over - or no game).
   The loop also keeps its own animation frame chain: a loop with no wait
   (MAX_FPS unlimited, or frames slower than MAX_FPS asks) never left the
   browser a turn for its refresh, so the animation frames came late (144 ->
   30 a second on a 144 Hz display) and so did the page's own drawing; where
   there is no low latency canvas (Firefox, Safari) the canvas reaches the
   screen only on those refreshes, so fewer frames were shown than without
   UNCAPPED_FPS. When a refresh is overdue (a quarter of an interval past
   due), the loop waits for that animation frame and goes on from it. */
static bool sg_browserUncapped = false;
static tConfItem<bool> sg_browserUncappedConf( "UNCAPPED_FPS", sg_browserUncapped );
static bool sg_browserUncappedRunning = false;
static double sg_browserUncappedLast = 0;

static bool BrowserUncappedWanted()
{
    return sg_browserUncapped && !sg_browserHidden &&
           ( sg_loop.state == Loop_Connecting || sg_loop.state == Loop_Local );
}

EM_JS(void, BrowserStartUncappedLoop, (), {
    if (Module.__aaUncapped)
        return;
    var channel = new MessageChannel();
    // the refresh: the last animation frame's time and the interval, the
    // shortest one seen (late frames only stretch it slowly back up, so a
    // starved stretch does not teach the loop to yield less)
    var lastFrame = performance.now();
    var interval = 1000 / 60;
    var waiting = false;
    function onFrame(t) {
        if (Module.__aaUncapped !== channel)
            return;
        var d = t - lastFrame;
        lastFrame = t;
        if (d > 1 && d < interval)
            interval = d;
        else if (d > interval && d < 100)
            interval += (d - interval) * .02;
        requestAnimationFrame(onFrame);
        if (waiting)
        {
            waiting = false;
            step();
        }
    }
    function schedule(ms) {
        if (ms > 1.5)
            setTimeout(step, ms);
        else if (performance.now() - lastFrame > interval * 1.25)
            waiting = true;
        else
            channel.port2.postMessage(0);
    }
    function step() {
        if (Module.__aaUncapped !== channel)
            return;
        var wait = Module["_aa_uncapped_tick"]();
        if (wait < 0)
        {
            Module.__aaUncapped = null;
            return;
        }
        schedule(wait);
    }
    channel.port1.onmessage = step;
    Module.__aaUncapped = channel;
    requestAnimationFrame(onFrame);
    schedule(0);
});

//! the animation frame callback
static void BrowserAnimationFrame()
{
    if ( sg_browserUncappedRunning )
        return;
    if ( BrowserUncappedWanted() )
    {
        sg_browserUncappedRunning = true;
        sg_browserUncappedLast = 0;
        BrowserLog( "uncapped frames on (MAX_FPS " + std::to_string( sr_maxFPS ) + ")." );
        BrowserStartUncappedLoop();
        return;
    }
    BrowserFrame();
}

extern "C"
{
//! one step of the uncapped loop: the ms to wait before the next call, or -1
//! when the loop is to end (the animation frame callback takes over again)
EMSCRIPTEN_KEEPALIVE double aa_uncapped_tick()
{
    if ( !BrowserUncappedWanted() )
    {
        sg_browserUncappedRunning = false;
        BrowserLog( "uncapped frames off." );
        return -1;
    }
    double const now = emscripten_get_now();
    if ( sr_maxFPS > 0 )
    {
        double const due = sg_browserUncappedLast + 1000.0 / sr_maxFPS;
        if ( now < due - .05 )
            return due - now;
    }
    sg_browserUncappedLast = now;
    sg_browserFrameKind = gFrame_Uncapped;
    BrowserFrame();
    sg_browserFrameKind = gFrame_Display;
    return 0;
}

//! from the page's visibilitychange handler (BrowserInstallVisibility)
EMSCRIPTEN_KEEPALIVE void aa_visibility( int hidden )
{
    BrowserVisibility( hidden != 0 );
}

//! one frame from the background timer while the tab is hidden
EMSCRIPTEN_KEEPALIVE void aa_background_tick()
{
    if ( !sg_browserHidden )
        return;
    ++sg_browserBackgroundTicks;
    ++sg_browserBackgroundTicksTotal;
    BrowserFrame();
}
}

void gBrowserStart()
{
    gBrowserLoop & loop = sg_loop;
    if ( loop.loopRegistered )
        return;
    loop.loopRegistered = true;

    gBrowserApiInit();
    sdlshim_SetInputCallback( &BrowserInputWake );
    BrowserInstallVisibility();
    gBrowserApiReady();

    // a direct connect: "#ws://host:port/" in the page's location
    // (with ?transport=websocket the game packets go over the signalling socket)
    std::string const url = BrowserUrlFromLocation();
    bool const websocket = EM_ASM_INT({
        return typeof location !== "undefined" && new URLSearchParams(location.search).get("transport") === "websocket" ? 1 : 0;
    });
    if ( !url.empty() )
        gBrowserConnect( url, websocket ? "{\"iceServers\":[],\"transport\":\"websocket\"}" : "" );
    else
        BrowserLog( "idle, waiting for connect()." );

    gBrowserApiStateChanged();

    // one frame per animation frame (or the uncapped loop, above); unwinds
    // main() and keeps the runtime alive
    emscripten_set_main_loop( &BrowserAnimationFrame, 0, 1 );
}

bool gBrowserMessage( tOutput const & title, tOutput const & body, float timeout )
{
    // gBrowserApi.cpp emits the event; here only the log line
    tString t( title ), b( body );
    BrowserLog( "message " + std::string( tColoredString::RemoveColors( t ) ) );
    con << title << ":\n" << body << "\n";
    return true;
}

// *******************************************************************************************
// config persistence
// *******************************************************************************************

void gBrowserPersistRestore()
{
    // the console text over the game view is on by default; natively the
    // first-use menus set it (sr_LoadDefaultConfig), which the browser skips.
    // user.cfg's TEXT_OUT (the page's "hide game log" button) overrides it.
    sr_textOut = true;

    // the var/ directory is the IDBFS mount (aa_api.js); an empty one is the
    // first run: seed it with the browser defaults so they load as user.cfg
    tString const userCfg = tDirectories::Var().GetWritePath( "user.cfg" );
    if ( userCfg.Len() <= 1 || FileExists( static_cast< char const * >( userCfg ) ) )
        return;

    tString const defaults = tDirectories::Config().GetReadPath( "browser_user.cfg" );
    if ( defaults.Len() <= 1 )
        return;

    std::ifstream in( static_cast< char const * >( defaults ), std::ios::binary );
    std::ofstream out( static_cast< char const * >( userCfg ), std::ios::binary );
    out << in.rdbuf();
    std::cout << "BROWSER: first run, user.cfg seeded from " << defaults << "\n";
}

// tSettingItems (settings_*.cfg values, the custom camera among them) are not
// part of user.cfg (Save() false); natively the user puts them into
// var/autoexec.cfg by hand. The browser page edits these, so they are written
// there on every save (loaded after user.cfg at startup, same IDBFS mount).
static char const * sg_browserSettingNames[] =
{
    "CAMERA_CUSTOM_BACK", "CAMERA_CUSTOM_RISE", "CAMERA_CUSTOM_PITCH",
    "CAMERA_CUSTOM_TURN_SPEED", "CAMERA_CUSTOM_BACK_FROMSPEED", "CAMERA_CUSTOM_RISE_FROMSPEED",
    "CAMERA_CUSTOM_ZOOM", "CAMERA_CUSTOM_TURN_SPEED_180",
    // the glance and the internal camera (the Settings sheet's CAMERA tab)
    "CAMERA_GLANCE_BACK", "CAMERA_GLANCE_RISE", "CAMERA_GLANCE_PITCH",
    "CAMERA_GLANCE_BACK_FROMSPEED", "CAMERA_GLANCE_RISE_FROMSPEED",
    "CAMERA_IN_TURN_SPEED",
    // the attract scene (gScene.cpp): the page tunes these too
    "SCENE_CAMERA_X", "SCENE_CAMERA_Y", "SCENE_CAMERA_Z", "SCENE_CAMERA_PITCH",
    "SCENE_CAMERA_DIR", "SCENE_CAMERA_FOV", "SCENE_TRANSITION_TIME",
    "SCENE_WALLS_LENGTH", "SCENE_ARENA_SIZE", "SCENE_RIM", "SCENE_FLOOR_DETAIL",
    "SCENE_REPLAY", "SCENE_REPLAY_X", "SCENE_REPLAY_Y", "SCENE_REPLAY_SPEED", "SCENE_REPLAY_ZONE_HEIGHT",
    "SCENE_REPLAY_CAMERA_X", "SCENE_REPLAY_CAMERA_Y", "SCENE_REPLAY_CAMERA_Z",
    "SCENE_REPLAY_CAMERA_PITCH", "SCENE_REPLAY_CAMERA_DIR", "SCENE_REPLAY_CAMERA_FOV",
    "SCENE_REPLAY_CAMERA_MODE", "SCENE_REPLAY_CAMERA_ORBIT", "SCENE_REPLAY_CAMERA_SHOT",
    // the display (sdl_shim.cpp, rGLES2.cpp)
    "BROWSER_MSAA", "BROWSER_BLOOM", "BROWSER_BLOOM_INTENSITY",
    // the quality tiers (gBrowserQuality.cpp)
    "BROWSER_QUALITY", "BROWSER_RENDER_SCALE", "BROWSER_QUALITY_LEVEL",
    // a tier's own, the player's while the quality is Custom (after the tier lines: a fixed
    // tier writes it again on its first frame, Custom keeps it)
    "TRAIL_GLASS",
    0
};

std::string gBrowserSettingsText()
{
    std::string text;
    tConfItemBase::tConfItemMap const & map = tConfItemBase::GetConfItemMap();
    for ( char const ** name = sg_browserSettingNames; *name; ++name )
    {
        tConfItemBase::tConfItemMap::const_iterator found = map.find( tString( *name ) );
        if ( found == map.end() )
            continue;
        std::ostringstream value;
        found->second->WriteVal( value );
        text += *name;
        text += " ";
        text += value.str();
        text += "\n";
    }
    return text;
}

void gBrowserPersistSave()
{
    // the attract scene forces TEXT_OUT, SHOW_HUD, the sky and the overlays off
    // while it is the backdrop; user.cfg gets the player's values, not those
    gSceneSuspendLook();
    st_SaveConfig();
    gSceneResumeLook();

    {
        // closed before the sync below (an open stream's buffer is not on the mount yet)
        std::ofstream autoexec;
        if ( tDirectories::Var().Open( autoexec, "autoexec.cfg", std::ios::out, true ) )
            autoexec << "# written by the browser client on every save: settings that are not part of user.cfg\n"
                     << gBrowserSettingsText();
    }

    // flush the IDBFS mount; a no-op on a plain MEMFS directory
    EM_ASM({
        if (typeof FS === "undefined" || !FS.syncfs)
            return;
        FS.syncfs(false, function(err) {
            if (err)
                console.log("BROWSER: persist sync failed: " + err);
        });
    });
}

#endif // __EMSCRIPTEN__
