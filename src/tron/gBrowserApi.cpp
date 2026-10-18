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

/* The command/query API the page uses instead of the menus. Plain
   C exports (EMSCRIPTEN_KEEPALIVE, called through cwrap from
   web/wasm/aa_api.js) and one EM_JS event sink. Contract: web/wasm/API.md.

   Conventions: every query returns a JSON document in a static buffer that is
   valid until the next call (cwrap copies it at once); commands return 1 on
   success, 0 on failure and say why through an "error" event. Strings cross
   the boundary as UTF-8; the game's own strings are Latin-1 (its font has 256
   glyphs), so bytes above 0x7f are written as \u00xx escapes and UTF-8 input
   is folded to Latin-1 where the game reads it. Only compiled into the wasm
   build (web/wasm/Makefile). */

#include "gBrowser.h"

#ifdef __EMSCRIPTEN__

#include "gGame.h"
#include "gScene.h"
#include "gCycle.h"
#include "eCamera.h"
#include "ePlayer.h"
#include "ePlatform.h"
#include "eTeam.h"
#include "eSound.h"
#include "eTimer.h"
#include "eVoter.h"
#include "nAuthentication.h"
#include "nNetwork.h"
#include "rSDL.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tDirectories.h"
#include "tResourceManager.h"
#include "tLocale.h"
#include "tString.h"
#include "tSysTime.h"
#include "uInput.h"

#include <emscripten.h>

#include <fstream>
#include <sstream>
#include <string>

EM_JS_DEPS(gBrowserApi, "$UTF8ToString");

// the one way into JS: Module.__aaEmit(type, jsonText), installed by aa_api.js
EM_JS(void, gBrowserApiEmitJs, (char const * type, char const * json), {
    if (typeof Module !== "undefined" && typeof Module.__aaEmit === "function")
        Module.__aaEmit(UTF8ToString(type), UTF8ToString(json));
});

namespace
{

// --- JSON helpers --------------------------------------------------------------------------

std::string JsonString( char const * text )
{
    std::string out = "\"";
    for ( unsigned char const * c = reinterpret_cast< unsigned char const * >( text ); *c; ++c )
    {
        switch ( *c )
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ( *c < 0x20 || *c >= 0x80 )
            {
                // control characters and the game's Latin-1 upper half
                char buffer[8];
                snprintf( buffer, sizeof( buffer ), "\\u%04x", static_cast< unsigned int >( *c ) );
                out += buffer;
            }
            else
                out += static_cast< char >( *c );
        }
    }
    out += "\"";
    return out;
}

std::string JsonString( std::string const & text )
{
    return JsonString( text.c_str() );
}

std::string JsonString( tString const & text )
{
    return JsonString( static_cast< char const * >( text ) );
}

std::string JsonBool( bool b )
{
    return b ? "true" : "false";
}

//! UTF-8 from the page -> the game's Latin-1 (other characters become '?')
tString FromUtf8( char const * text )
{
    tString out;
    unsigned char const * c = reinterpret_cast< unsigned char const * >( text );
    while ( *c )
    {
        unsigned int code = *c;
        int extra = 0;
        if ( code >= 0xf0 ) { code &= 0x07; extra = 3; }
        else if ( code >= 0xe0 ) { code &= 0x0f; extra = 2; }
        else if ( code >= 0xc0 ) { code &= 0x1f; extra = 1; }
        ++c;
        for ( int i = 0; i < extra && *c; ++i, ++c )
            code = ( code << 6 ) | ( *c & 0x3f );
        if ( code < 0x100 )
            out << static_cast< char >( code );
        else
            out << '?';
    }
    return out;
}

//! the query results live here until the next query
std::string & Result()
{
    static std::string result;
    return result;
}

char const * Return( std::string const & json )
{
    Result() = json;
    return Result().c_str();
}

void Emit( char const * type, std::string const & json )
{
    gBrowserApiEmitJs( type, json.c_str() );
}

void EmitError( std::string const & what )
{
    Emit( "error", "{\"text\":" + JsonString( what ) + "}" );
}

// --- state ---------------------------------------------------------------------------------

char const * StateName( gBrowserState state )
{
    switch ( state )
    {
    case gBrowser_Idle: return "idle";
    case gBrowser_Connecting: return "connecting";
    case gBrowser_Syncing: return "syncing";
    case gBrowser_Playing: return "playing";
    case gBrowser_Disconnected: return "disconnected";
    case gBrowser_Fetching: return "fetching";
    case gBrowser_Local: return "local";
    case gBrowser_Scene: return "scene";
    }
    return "idle";
}

std::string PlayerJson( ePlayerNetID const * p )
{
    std::string json = "{";
    json += "\"id\":" + std::to_string( p->ID() );
    json += ",\"name\":" + JsonString( p->GetName() );
    json += ",\"coloredName\":" + JsonString( p->GetColoredName() );
    json += ",\"color\":[" + std::to_string( p->r ) + "," + std::to_string( p->g ) + "," + std::to_string( p->b ) + "]";
    json += ",\"score\":" + std::to_string( p->Score() );
    eTeam const * team = p->CurrentTeam();
    json += ",\"team\":" + ( team ? JsonString( team->Name() ) : std::string( "null" ) );
    json += ",\"teamId\":" + ( team ? std::to_string( team->ID() ) : std::string( "null" ) );
    eTeam const * next = p->NextTeam();
    json += ",\"nextTeamId\":" + ( next ? std::to_string( next->ID() ) : std::string( "null" ) );
    json += ",\"ping\":" + std::to_string( static_cast< int >( p->ping * 1000 ) );
    json += ",\"local\":" + JsonBool( p->Owner() == sn_myNetID );
    // an AI player (gAIPlayer has its own network class, so clients know it too)
    json += ",\"bot\":" + JsonBool( !p->IsHuman() );
    // riding now (the score table's Alive column): false while dead, waiting to respawn, or spectating
    eNetGameObject * object = p->Object();
    json += ",\"alive\":" + JsonBool( object && object->Alive() );
    json += ",\"spectating\":" + JsonBool( p->IsSpectating() );
    json += ",\"suspended\":" + JsonBool( const_cast< ePlayerNetID * >( p )->IsSuspended() );
    json += ",\"chatting\":" + JsonBool( p->IsChatting() );
    json += ",\"active\":" + JsonBool( p->IsActive() );
    json += "}";
    return json;
}

std::string PlayersJson()
{
    std::string json = "[";
    for ( int i = 0; i < se_PlayerNetIDs.Len(); ++i )
    {
        if ( i )
            json += ",";
        json += PlayerJson( se_PlayerNetIDs(i) );
    }
    json += "]";
    return json;
}

// the cycle the first local player's camera follows (its own, or the one it watches while dead or
// spectating), as the HUD's gauges read it: speed in m/s, the rubber it used and the most it may
// use (CYCLE_RUBBER), the rubber malus (extra use from grinding, 0 without), the brake reservoir
// (1 full, 0 empty) and whether it brakes; null without one
std::string CycleJson()
{
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( !config )
        return "null";
    gCycle const * cycle = NULL;
    if ( config->cam )
        cycle = dynamic_cast< gCycle const * >( config->cam->Center() );
    eNetGameObject const * own = config->netPlayer ? config->netPlayer->Object() : NULL;
    if ( !cycle )
        cycle = dynamic_cast< gCycle const * >( own );
    if ( !cycle )
        return "null";
    std::string json = "{";
    json += "\"own\":" + JsonBool( own && own == cycle );
    json += ",\"alive\":" + JsonBool( cycle->Alive() );
    json += ",\"speed\":" + std::to_string( cycle->Speed() );
    json += ",\"rubber\":" + std::to_string( cycle->GetRubber() );
    json += ",\"rubberMax\":" + std::to_string( sg_rubberCycle );
    json += ",\"rubberMalus\":" + std::to_string( cycle->GetRubberMalus() );
    json += ",\"brake\":" + std::to_string( cycle->GetBrakingReservoir() );
    json += ",\"braking\":" + JsonBool( cycle->GetBraking() != 0 );
    REAL respawnIn = 0;
    json += ",\"respawnIn\":" + ( sg_RespawnIn( config->netPlayer, respawnIn ) ? std::to_string( respawnIn ) : std::string( "null" ) );
    json += "}";
    return json;
}

std::string StateJson()
{
    gBrowserState const state = gBrowserGetState();
    std::string json = "{";
    json += "\"state\":" + JsonString( StateName( state ) );
    json += ",\"reason\":" + JsonString( gBrowserLastReason() );
    json += ",\"url\":" + JsonString( gBrowserUrl() );
    // the selected candidate pair, JSON.stringify'd by the transport glue from the browser's stats
    json += ",\"ice\":" + ( gBrowserIceRoute().empty() ? std::string( "null" ) : gBrowserIceRoute() );
    json += ",\"resource\":" + ( state == gBrowser_Fetching ? JsonString( tResourceManager::BrowserPrefetchFile() ) : std::string( "null" ) );
    json += ",\"netstate\":" + std::to_string( static_cast< int >( sn_GetNetState() ) );
    bool const client = sn_GetNetState() == nCLIENT;
    json += ",\"ping\":" + ( client ? std::to_string( static_cast< int >( sn_Connections[0].ping.GetPing() * 1000 ) ) : std::string( "null" ) );
    if ( se_mainGameTimer )
    {
        json += ",\"gametime\":" + std::to_string( se_GameTime() );
        json += ",\"fps\":" + std::to_string( static_cast< int >( se_mainGameTimer->FPS() ) );
        json += ",\"synced\":" + JsonBool( se_mainGameTimer->IsSynced() );
    }
    else
    {
        json += ",\"gametime\":null,\"fps\":null,\"synced\":false";
    }
    // the seconds until a respawn game's held first cycle comes (sg_JoinIn), null when none is due
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    REAL joinIn = 0;
    json += ",\"joinIn\":" + ( config && sg_JoinIn( config->netPlayer, joinIn ) ? std::to_string( joinIn ) : std::string( "null" ) );
    json += ",\"authPending\":" + JsonBool( nAuthentication::PasswordRequestPending() );
    json += ",\"sceneState\":" + std::to_string( gSceneState() );
    json += ",\"sceneTransitioning\":" + JsonBool( gSceneTransitioning() );
    json += ",\"players\":" + PlayersJson();
    json += "}";
    return json;
}

// --- votes ---------------------------------------------------------------------------------

void VoteVisitor( int id, tString const & description, tString const & details, void * context )
{
    std::string & json = *static_cast< std::string * >( context );
    if ( json.size() > 1 )
        json += ",";
    json += "{\"id\":" + std::to_string( id ) + ",\"description\":" + JsonString( description )
          + ",\"details\":" + JsonString( details ) + "}";
}

std::string VotesJson()
{
    std::string json = "[";
    if ( sn_GetNetState() == nCLIENT )
        se_BrowserVisitVotes( &VoteVisitor, &json );
    json += "]";
    return json;
}

// --- hooks ---------------------------------------------------------------------------------

void ConsoleLine( tString const & line )
{
    Emit( "console", "{\"line\":" + JsonString( line ) + ",\"text\":" + JsonString( tColoredString::RemoveColors( line ) ) + "}" );
}

void ChatLine( ePlayerNetID * p, tString const & line )
{
    // the line as the console shows it ("name: text\n" with colors); the name
    // is separate so the page can format it its own way
    std::string trimmed( static_cast< char const * >( line ) );
    while ( !trimmed.empty() && trimmed[trimmed.size()-1] == '\n' )
        trimmed.erase( trimmed.size()-1 );
    std::string json = "{\"line\":" + JsonString( trimmed ) + ",\"text\":" + JsonString( tColoredString::RemoveColors( trimmed.c_str() ) );
    if ( p )
        json += ",\"playerId\":" + std::to_string( p->ID() ) + ",\"name\":" + JsonString( p->GetName() ) + ",\"local\":" + JsonBool( p->Owner() == sn_myNetID );
    else
        json += ",\"playerId\":null,\"name\":null,\"local\":false";
    json += "}";
    Emit( "chat", json );
}

void ChatOpen( tString const & prefill )
{
    Emit( "chatOpen", "{\"prefill\":" + JsonString( prefill ) + "}" );
}

// the server asked for a password (ePlayer.cpp's PasswordCallback deferred
// the answer): the page shows its login form. No password crosses here.
void AuthRequest( tString const & username, tString const & message, tString const & method, bool failed )
{
    gBrowserLog( "login request for user " + std::string( username ) + ( failed ? " (last attempt failed)" : "" ) );
    Emit( "authRequest", "{\"username\":" + JsonString( username ) + ",\"serverMessage\":" + JsonString( tColoredString::RemoveColors( message ) )
        + ",\"method\":" + JsonString( method ) + ",\"failed\":" + JsonBool( failed ) + "}" );
}

bool Message( tOutput const & title, tOutput const & body, REAL timeout )
{
    tString t( title ), b( body );
    Emit( "message", "{\"title\":" + JsonString( tColoredString::RemoveColors( t ) ) + ",\"body\":" + JsonString( tColoredString::RemoveColors( b ) )
        + ",\"timeout\":" + std::to_string( timeout ) + "}" );
    return gBrowserMessage( title, body, timeout );
}

//! the local player's network object, NULL when not in a game
ePlayerNetID * LocalPlayer()
{
    // the configured first player's object is the authoritative answer; the
    // scan below is the fallback while it is being (re)created
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( config && bool( config->netPlayer ) && config->netPlayer->Owner() == sn_myNetID )
        return config->netPlayer;
    for ( int i = 0; i < se_PlayerNetIDs.Len(); ++i )
    {
        ePlayerNetID * p = se_PlayerNetIDs(i);
        if ( p->Owner() == sn_myNetID && p->pID >= 0 )
            return p;
    }
    return NULL;
}

// --- change detection ----------------------------------------------------------------------

std::string sg_lastPlayers, sg_lastVotes;
double sg_nextCheck = 0;

// the `round` event: whether the last round event said a round is in play
bool sg_roundInPlay = false;

// every frame: the game's entries into and exits from play (queued by gGame::SetState, so a
// standalone game that sets up the next round within one frame is not missed) become
// `round` events while the session runs, one per transition, alternating; a session that
// ends drops what is queued and ends silently
void RoundFrame()
{
    bool const session = sg_BrowserConnectPhase() == 5 || sg_BrowserLocalRunning();
    if ( !session )
    {
        sg_BrowserClearRoundEvents();
        sg_roundInPlay = false;
        return;
    }
    bool started = false;
    while ( sg_BrowserTakeRoundEvent( started ) )
    {
        if ( started == sg_roundInPlay )
            continue;
        sg_roundInPlay = started;
        Emit( "round", started ? "{\"state\":\"started\"}" : "{\"state\":\"ended\"}" );
    }
}

}

// *******************************************************************************************
// hooks the loop calls (gBrowser.cpp)
// *******************************************************************************************

// the server moves this client to another room (contract section 3, "Moving players"): the
// page leaves and follows; the client itself does nothing else
static void MoveNotice( ePlatformMoveNotice const & notice )
{
    Emit( "move", "{\"moveId\":" + JsonString( notice.moveId ) + ",\"serverId\":" + JsonString( notice.serverId )
        + ",\"name\":" + JsonString( notice.name ) + ",\"signalUrl\":" + JsonString( notice.signalUrl )
        + ",\"reason\":" + JsonString( notice.reason ) + "}" );
}

void gBrowserApiInit()
{
    tConsole::RegisterLineCallback( &ConsoleLine );
    tConsole::RegisterMessageCallback( &Message );
    se_browserChatHook = &ChatLine;
    se_browserChatOpenHook = &ChatOpen;
    se_browserAuthHook = &AuthRequest;

    // the page follows moves: every server hears so once per connection (message 381)
    ePlatform::SetMoveCallback( &MoveNotice );
    ePlatform::SetClientCapabilities( tString( ePLATFORM_CAPABILITY_MOVE ) );
}

void gBrowserApiReady()
{
    Emit( "ready", "{}" );
}

void gBrowserApiRequest( char const * event )
{
    Emit( event, "{}" );
}

void gBrowserApiStateChanged()
{
    Emit( "state", StateJson() );
}

// the attract scene reached a state, or started playing towards one
void gBrowserApiScene( int state, bool transitioning )
{
    Emit( "scene", "{\"state\":" + std::to_string( state ) + ",\"transitioning\":" + JsonBool( transitioning ) + "}" );
}

// a network session's handshake reached a step: "signalling" (the server answered over the
// signalling socket) or "ice" (the data channel is open); the page times its connect stages
// with these
void gBrowserApiHandshake( char const * step )
{
    Emit( "handshake", "{\"step\":" + JsonString( step ) + "}" );
}

void gBrowserApiFrame()
{
    RoundFrame();
    sg_ReadyFrame();

    double const now = tRealSysTimeFloat();
    if ( now < sg_nextCheck )
        return;
    sg_nextCheck = now + 0.5;

    std::string players = PlayersJson();
    if ( players != sg_lastPlayers )
    {
        sg_lastPlayers = players;
        Emit( "players", "{\"players\":" + players + "}" );
    }

    std::string votes = VotesJson();
    if ( votes != sg_lastVotes )
    {
        sg_lastVotes = votes;
        Emit( "votes", "{\"votes\":" + votes + "}" );
    }
}

// *******************************************************************************************
// the exports (API.md)
// *******************************************************************************************

extern "C"
{

// ice: "" / NULL or {"iceServers": [...], "iceTransportPolicy": "relay"} (aa_api.js checks the shape)
EMSCRIPTEN_KEEPALIVE int aa_connect( char const * url, char const * ice )
{
    if ( gBrowserConnect( url ? url : "", ice ? ice : "" ) )
        return 1;
    EmitError( "connect: refused (a session is running, no URL, or the handshake could not start)" );
    return 0;
}

EMSCRIPTEN_KEEPALIVE void aa_disconnect()
{
    gBrowserDisconnect();
}

// the desktop client's "Local Game": standalone, AI opponents from aiplayers.cfg
EMSCRIPTEN_KEEPALIVE int aa_start_local()
{
    if ( gBrowserStartLocal() )
        return 1;
    EmitError( "startLocal: refused (a session is running)" );
    return 0;
}

EMSCRIPTEN_KEEPALIVE void aa_stop_local()
{
    gBrowserStopLocal();
}

// the attract scene: 1..4, played forward or backward one state at a time
EMSCRIPTEN_KEEPALIVE int aa_scene( int state )
{
    if ( gBrowserScene( state ) )
        return 1;
    EmitError( "scene: refused (a session is running, the state is out of range 1-5, or the map failed to load)" );
    return 0;
}

EMSCRIPTEN_KEEPALIVE void aa_scene_stop()
{
    gBrowserSceneStop();
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_state()
{
    return Return( StateJson() );
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_players()
{
    return Return( PlayersJson() );
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_cycle()
{
    return Return( CycleJson() );
}

// turns the first local player's cycle `count` times (1 a single turn, 2 a double: two turns the
// cycle queues as it would two quick presses), left for dir < 0, right otherwise, exactly as its
// turn keys do whatever they are bound to (ePlayer::Act: nothing during chat, before the round or
// without a living cycle); the page's touch buttons. Returns how many turns were taken
EMSCRIPTEN_KEEPALIVE int aa_turn( int dir, int count )
{
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( !config )
        return 0;
    uActionPlayer * action = dir < 0 ? &eGameObject::se_turnLeft : &eGameObject::se_turnRight;
    int taken = 0;
    for ( int i = 0; i < count && i < 2; ++i )
        if ( config->Act( action, 1 ) )
            ++taken;
    return taken;
}

// holds (on) or lets go of (off) the first local player's brake, as its brake key's press and
// release do (ePlayer::Act); the page's touch brake. Returns whether the cycle took it
EMSCRIPTEN_KEEPALIVE int aa_brake( int on )
{
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( !config )
        return 0;
    return config->Act( &gCycle::s_brake, on ? 1 : 0 ) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int aa_chat( char const * text )
{
    ePlayerNetID * me = LocalPlayer();
    if ( !me || sn_GetNetState() != nCLIENT )
    {
        EmitError( "chat: not in a game" );
        return 0;
    }
    // UTF-8 as typed in a room that carries it (setChatUtf8), else folded to Latin-1
    tString say = se_chatWritesUtf8 ? tString( text ? text : "" ) : FromUtf8( text ? text : "" );
    if ( say.Len() <= 1 )
        return 0;
    me->Chat( say );
    return 1;
}

// the page says whether the room carries UTF-8 chat (an official room): chat() then
// sends what was typed as UTF-8, so Korean, Chinese and Japanese get through; off, it is folded
// to Latin-1 (characters above U+00FF become '?') as every 0.2.9 server expects
EMSCRIPTEN_KEEPALIVE void aa_set_chat_utf8( int on )
{
    se_chatWritesUtf8 = on != 0;
}

// the page draws the score table: the canvas one is no longer drawn, and every show or hide
// (the SCORE key, a round's end, a new round) is a "scores" event instead
static void ScoresShown( bool show )
{
    Emit( "scores", "{\"show\":" + JsonBool( show ) + "}" );
}

EMSCRIPTEN_KEEPALIVE void aa_set_page_scores( int on )
{
    se_browserScoresHook = on ? &ScoresShown : NULL;
}

EMSCRIPTEN_KEEPALIVE void aa_set_chatting( int chatting )
{
    // what the native chat line does while open: the other players see it
    se_ChatState( ePlayerNetID::ChatFlags_Chat, chatting != 0 );
}

EMSCRIPTEN_KEEPALIVE int aa_console( char const * line )
{
    if ( !line || !*line )
        return 0;
    // like "/console" in the chat line: owner level, one or more lines
    tCurrentAccessLevel level( tAccessLevel_Owner, true );
    tString text = FromUtf8( line );
    std::stringstream s( static_cast< char const * >( text ) );
    tConfItemBase::LoadAll( s );
    if ( sn_GetNetState() == nCLIENT )
        ePlayerNetID::Update();
    return 1;
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_config( char const * name )
{
    tString key( name ? name : "" );
    tToUpper( key );
    tConfItemBase::tConfItemMap const & map = tConfItemBase::GetConfItemMap();
    tConfItemBase::tConfItemMap::const_iterator found = map.find( key );
    if ( found == map.end() )
        return Return( "null" );

    tConfItemBase * item = found->second;
    std::ostringstream value;
    item->WriteVal( value );
    std::string json = "{\"name\":" + JsonString( key ) + ",\"value\":" + JsonString( value.str() );
    json += ",\"savable\":" + JsonBool( item->Save() && item->CanSave() );
    json += ",\"writable\":" + JsonBool( item->Writable() );
    json += ",\"requiredLevel\":" + JsonString( tCurrentAccessLevel::GetName( item->GetRequiredLevel() ) );
    json += "}";
    return Return( json );
}

EMSCRIPTEN_KEEPALIVE int aa_set_config( char const * name, char const * value )
{
    tString key( name ? name : "" );
    tToUpper( key );
    tConfItemBase::tConfItemMap const & map = tConfItemBase::GetConfItemMap();
    if ( map.find( key ) == map.end() )
    {
        EmitError( "setConfig: unknown item " + std::string( key ) );
        return 0;
    }
    // the sound device settings: the native sound menu reopens the device when
    // it closes (se_SoundMenu); without the same here a change from the page
    // only took effect at the next start
    bool const sound = key == "SOUND_QUALITY" || key == "SOUND_CHANNELS";
    tConfItemBase * item = map.find( key )->second;
    std::ostringstream before;
    item->WriteVal( before );
    tCurrentAccessLevel level( tAccessLevel_Owner, true );
    tString line( key );
    line << " " << FromUtf8( value ? value : "" ) << "\n";
    std::stringstream s( static_cast< char const * >( line ) );
    tConfItemBase::LoadLine( s );
    std::ostringstream after;
    item->WriteVal( after );
    bool const changed = after.str() != before.str();
    if ( sound && changed )
    {
        se_SoundExit();
        se_SoundInit();
    }
    // what the player menu does on exit: name/color changes go out now, not at
    // the next round. Only for a change, and only in the game: during the login or
    // the sync (a page handler of a state event) it re-entered the login, which then
    // froze the page until the server timed the player out; the game's start sends
    // the players anyway
    gBrowserState const state = gBrowserGetState();
    if ( changed && sn_GetNetState() == nCLIENT && ( state == gBrowser_Playing || state == gBrowser_Fetching ) )
        ePlayerNetID::Update();
    return 1;
}

EMSCRIPTEN_KEEPALIVE char const * aa_list_config( char const * prefix )
{
    tString key( prefix ? prefix : "" );
    tToUpper( key );
    std::string json = "[";
    bool first = true;
    tConfItemBase::tConfItemMap const & map = tConfItemBase::GetConfItemMap();
    for ( tConfItemBase::tConfItemMap::const_iterator i = map.begin(); i != map.end(); ++i )
    {
        if ( key.Len() > 1 && strncmp( i->first, key, key.Len()-1 ) != 0 )
            continue;
        tConfItemBase * item = i->second;
        if ( !item->CanSave() )
            continue; // commands (tConfItemFunc), not values
        std::ostringstream value;
        item->WriteVal( value );
        if ( !first )
            json += ",";
        first = false;
        json += "{\"name\":" + JsonString( i->first ) + ",\"value\":" + JsonString( value.str() )
              + ",\"savable\":" + JsonBool( item->Save() ) + ",\"writable\":" + JsonBool( item->Writable() ) + "}";
    }
    json += "]";
    return Return( json );
}

EMSCRIPTEN_KEEPALIVE void aa_save_config()
{
    gBrowserPersistSave();
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_bindings()
{
    std::string json = "[";
    bool first = true;
    for ( int sym = 0; sym < SDLK_NEWLAST; ++sym )
    {
        uBind * bind = keymap[sym];
        if ( !bind || !bind->act )
            continue;
        int player = -1;
        for ( int p = 0; p <= uMAX_PLAYERS; ++p )
            if ( bind->CheckPlayer( p ) )
            {
                player = p;
                break;
            }
        if ( !first )
            json += ",";
        first = false;
        json += "{\"keysym\":" + std::to_string( sym ) + ",\"keyName\":" + JsonString( sym < SDLK_LAST ? SDL_GetKeyName( static_cast< SDLKey >( sym ) ) : "mouse" )
              + ",\"action\":" + JsonString( bind->act->internalName ) + ",\"player\":" + std::to_string( player ) + "}";
    }
    json += "]";
    return Return( json );
}

namespace
{
void ActionVisitor( uAction * action, void * context )
{
    std::string & json = *static_cast< std::string * >( context );
    char const * kind = "global";
    if ( dynamic_cast< uActionPlayer * >( action ) )
        kind = "player";
    else if ( dynamic_cast< uActionCamera * >( action ) )
        kind = "camera";
    if ( json.size() > 1 )
        json += ",";
    json += "{\"name\":" + JsonString( action->internalName ) + ",\"kind\":" + JsonString( kind )
          + ",\"description\":" + JsonString( tString( action->description ) ) + ",\"help\":" + JsonString( tString( action->helpText ) )
          + ",\"analog\":" + JsonBool( action->type == uAction::uINPUT_ANALOG ) + "}";
}
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_actions()
{
    std::string json = "[";
    su_BrowserVisitActions( &ActionVisitor, &json );
    json += "]";
    return Return( json );
}

EMSCRIPTEN_KEEPALIVE int aa_set_binding( char const * actionName, int player, int sym )
{
    uAction * action = uAction::Find( actionName ? actionName : "" );
    if ( !action )
    {
        EmitError( "setBinding: unknown action " + std::string( actionName ? actionName : "" ) );
        return 0;
    }
    if ( sym <= 0 || sym >= SDLK_NEWLAST || player < 0 || player > uMAX_PLAYERS )
    {
        EmitError( "setBinding: key or player out of range" );
        return 0;
    }
    // what the KEYBOARD config line does
    keymap[sym] = NULL;
    keymap[sym] = uBindPlayer::NewBind( action, player );
    return 1;
}

EMSCRIPTEN_KEEPALIVE int aa_unbind( int sym )
{
    if ( sym < 0 || sym >= SDLK_NEWLAST )
        return 0;
    keymap[sym] = NULL;
    return 1;
}

EMSCRIPTEN_KEEPALIVE int aa_keysym_from_code( char const * code )
{
    return sdlshim_KeysymFromCode( code ? code : "" );
}

EMSCRIPTEN_KEEPALIVE char const * aa_code_from_keysym( int sym )
{
    return Return( sdlshim_CodeFromKeysym( sym ) );
}

EMSCRIPTEN_KEEPALIVE char const * aa_get_votes()
{
    return Return( VotesJson() );
}

EMSCRIPTEN_KEEPALIVE int aa_vote( int id, int accept )
{
    if ( sn_GetNetState() != nCLIENT || !se_BrowserVote( id, accept != 0 ) )
    {
        EmitError( "vote: no such vote " + std::to_string( id ) );
        return 0;
    }
    return 1;
}

// --- teams (what gTeam.cpp's team menu does) ---------------------------------------------

EMSCRIPTEN_KEEPALIVE char const * aa_get_teams()
{
    ePlayerNetID const * me = LocalPlayer();
    std::string json = "[";
    for ( int i = 0; i < eTeam::teams.Len(); ++i )
    {
        eTeam * team = eTeam::teams(i);
        if ( i )
            json += ",";
        json += "{\"id\":" + std::to_string( team->ID() );
        json += ",\"name\":" + JsonString( team->Name() );
        json += ",\"coloredName\":" + JsonString( team->GetColoredName() );
        json += ",\"color\":[" + std::to_string( team->R() ) + "," + std::to_string( team->G() ) + "," + std::to_string( team->B() ) + "]";
        json += ",\"players\":" + std::to_string( team->NumPlayers() );
        json += ",\"humans\":" + std::to_string( team->NumHumanPlayers() );
        json += ",\"maxPlayers\":" + std::to_string( team->MaxPlayers() );
        json += ",\"locked\":" + JsonBool( team->IsLocked() );
        json += ",\"joinable\":" + JsonBool( me && team != me->NextTeam() && team->PlayerMayJoin( me ) );
        json += ",\"current\":" + JsonBool( me && me->CurrentTeam() == team );
        json += ",\"next\":" + JsonBool( me && me->NextTeam() == team );
        json += "}";
    }
    json += "]";
    return Return( json );
}

// the local player's spectator switch (gTeam.cpp SetSpectator / TOGGLE_SPECTATOR)
EMSCRIPTEN_KEEPALIVE int aa_set_spectating( int spectate )
{
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( !config )
        return 0;
    bool const want = spectate != 0;
    if ( config->spectate != want )
    {
        config->spectate = want;
        con << tOutput( want ? "$player_toggle_spectator_on" : "$player_toggle_spectator_off", config->name );
        if ( sn_GetNetState() == nCLIENT )
            ePlayerNetID::Update();
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE int aa_join_team( int id )
{
    ePlayerNetID * me = LocalPlayer();
    if ( !me || sn_GetNetState() != nCLIENT )
    {
        EmitError( "joinTeam: not in a game" );
        return 0;
    }
    eTeam * team = NULL;
    for ( int i = 0; i < eTeam::teams.Len(); ++i )
        if ( eTeam::teams(i)->ID() == id )
            team = eTeam::teams(i);
    if ( !team )
    {
        EmitError( "joinTeam: no team with id " + std::to_string( id ) );
        return 0;
    }
    aa_set_spectating( 0 );
    me->SetTeamWish( team );
    return 1;
}

EMSCRIPTEN_KEEPALIVE int aa_create_team()
{
    ePlayerNetID * me = LocalPlayer();
    if ( !me || sn_GetNetState() != nCLIENT )
    {
        EmitError( "createTeam: not in a game" );
        return 0;
    }
    aa_set_spectating( 0 );
    me->CreateNewTeamWish();
    return 1;
}

// --- authentication -------------------------------------------------------------------

// the native in-game menu's "Login" item: USER_1 (user@authority, "@" alone
// or user@ for the server's local accounts) and ePlayer::LogIn()
EMSCRIPTEN_KEEPALIVE int aa_login( char const * globalId )
{
    ePlayer * config = ePlayer::PlayerConfig( 0 );
    if ( !config || sn_GetNetState() != nCLIENT || !bool( config->netPlayer ) )
    {
        EmitError( "login: not in a game" );
        return 0;
    }
    if ( globalId && *globalId )
        config->globalID = FromUtf8( globalId );
    ePlayer::LogIn();
    return 1;
}

// answers the pending password request. The password is folded to Latin-1,
// handed to the scrambler and wiped; it is never logged or emitted.
EMSCRIPTEN_KEEPALIVE int aa_auth( char const * username, char const * password )
{
    if ( !nAuthentication::PasswordRequestPending() )
    {
        EmitError( "auth: no login request pending" );
        return 0;
    }
    tString user = FromUtf8( username ? username : "" );
    if ( user.Len() <= 1 )
        user = nAuthentication::PendingPasswordUsername();
    tString secret = FromUtf8( password ? password : "" );
    bool const ok = ePlayer::BrowserAuthAnswer( user, secret, false );
    for ( int i = secret.Len()-2; i >= 0; --i )
        secret(i) = 'a';
    gBrowserLog( "login answer sent for user " + std::string( user ) );
    return ok ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE int aa_auth_cancel()
{
    if ( !nAuthentication::PasswordRequestPending() )
        return 0;
    ePlayer::BrowserAuthAnswer( nAuthentication::PendingPasswordUsername(), tString(), true );
    gBrowserLog( "login request cancelled" );
    return 1;
}

// --- platform login ---------------------------------------------------------------------

// a platform login token (platform-contract/README.md) for the running network
// session: sent to the server once the connection is logged in. Never logged
// or emitted; the page fetches a fresh one per connection.
EMSCRIPTEN_KEEPALIVE int aa_platform_login( char const * token )
{
    if ( !token || !*token )
    {
        EmitError( "platformLogin: no token" );
        return 0;
    }
    if ( !gBrowserPlatformLogin( token ) )
    {
        EmitError( "platformLogin: no network session" );
        return 0;
    }
    return 1;
}

// --- configuration export/import --------------------------------------------------------

// the text of user.cfg as the game saves it (everything savable, KEYBOARD
// lines included), minus the PASSWORD lines (stored password hashes)
EMSCRIPTEN_KEEPALIVE char const * aa_export_config()
{
    // the player's values, not the attract scene's overrides (gBrowserPersistSave)
    gSceneSuspendLook();
    st_SaveConfig();
    gSceneResumeLook();
    std::string text;
    tString const path = tDirectories::Var().GetReadPath( "user.cfg" );
    if ( path.Len() > 1 )
    {
        std::ifstream in( static_cast< char const * >( path ) );
        std::string line;
        while ( std::getline( in, line ) )
        {
            std::string::size_type const start = line.find_first_not_of( " \t" );
            if ( start != std::string::npos && line.compare( start, 8, "PASSWORD" ) == 0
                 && ( line.size() == start + 8 || line[start+8] == ' ' || line[start+8] == '\t' ) )
                continue;
            text += line;
            text += "\n";
        }
    }
    // the var/autoexec.cfg settings (custom camera; not in user.cfg)
    text += gBrowserSettingsText();
    return Return( text );
}

// applies configuration text line by line (like console()) and saves
EMSCRIPTEN_KEEPALIVE int aa_import_config( char const * text )
{
    if ( !text || !*text )
        return 0;
    if ( !aa_console( text ) )
        return 0;
    gBrowserPersistSave();
    return 1;
}

}

#endif // __EMSCRIPTEN__
