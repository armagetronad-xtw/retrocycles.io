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

// ePlatform.h: optional in-process link between a game server and a platform service
// (server events, platform login tokens, room commands, chat checks and sanctions).
// Wire format: platform-contract/README.md. Real implementation: src/tron/gPlatformHttp.h.

#ifndef ArmageTron_EPLATFORM_H
#define ArmageTron_EPLATFORM_H

#include "tString.h"
#include "nKrawall.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class ePlayerNetID;

// the network message carrying a platform login token from client to server
// (descriptor 380, "platform_login_token", one raw string)
unsigned short const ePLATFORM_LOGIN_TOKEN_DESCRIPTOR = 380;

// the client's capabilities, client to server once per connection (381, "platform_client_caps",
// one raw string: words separated by blanks), and a move to another room, server to client
// (382, "platform_move", five raw strings; only to clients that announced "move")
unsigned short const ePLATFORM_CLIENT_CAPS_DESCRIPTOR = 381;
unsigned short const ePLATFORM_MOVE_DESCRIPTOR = 382;

// the capability a client announces when it follows platform_move
char const* const ePLATFORM_CAPABILITY_MOVE = "move";

// where a room sends a client (contract section 3, "Moving players")
struct ePlatformMoveNotice
{
    std::string moveId{};
    std::string serverId{};
    std::string name{};      // may carry colour codes
    std::string signalUrl{};
    std::string reason{};    // merge, drain, closer, admin
};

// a player as every event names it
struct ePlatformPlayerRef
{
    std::string name{}; // without color codes
    std::string gid{};  // authenticated global id, empty if none (never for a bot)
    std::string team{}; // team name without color codes, empty if none
    bool bot{};         // an AI player
};

// one player's line in a round result
struct ePlatformRoundPlayer
{
    std::string name{}; // without color codes
    std::string gid{};  // authenticated global id, empty if none
    std::string team{}; // team name without color codes, empty if none
    int score{};
    bool alive{};
    bool bot{};
    int place{}; // in the round: 1 for the survivors (or the last to die), then by time of death
};

// what a round end reports
struct ePlatformRoundResult
{
    int match{};   // the match the round belongs to (1-based per server run), 0 if unknown
    int round{};   // 1-based number of the round within its match
    int seconds{}; // length of the round in game time
    std::vector<ePlatformRoundPlayer> players{};
};

// what a match start reports: the limits it is played to
struct ePlatformMatchStart
{
    int match{};
    int limitRounds{};
    int limitMinutes{};
    int limitScore{};
};

// a cycle's death (contract: "death")
struct ePlatformDeath
{
    int match{};
    int round{}; // the round being played
    ePlatformPlayerRef victim{};
    bool hasKiller{};
    ePlatformPlayerRef killer{};
    std::string cause{}; // "frag", "teamkill", "suicide" or "other"
};

// one player's line in a match result
struct ePlatformMatchPlayer
{
    ePlatformPlayerRef player{};
    int score{};
    int place{};  // the team's place at the end, 1 best; 0 if the player left early
    int rounds{}; // rounds of the match that listed the player
    bool leftEarly{};
};

// what a match end reports
struct ePlatformMatchResult
{
    int match{};
    bool completed{};     // a winner was declared by the limits; otherwise it was aborted
    int rounds{};         // rounds reported for the match
    std::string winner{}; // the winning team's name without color codes (completed only)
    std::vector<ePlatformMatchPlayer> players{};
};

// who is on the server, for room heartbeats
struct ePlatformPlayerCounts
{
    int humans{};   // human players, spectators included
    int bots{};     // AI players
    int capacity{}; // connection limit (MAX_CLIENTS)
    int clients{};  // connections holding one of those slots (sn_NumUsers(): players, spectators,
                    // logins still in progress; a desktop client with two players is one)

    bool operator==(ePlatformPlayerCounts const& other) const noexcept
    {
        return humans == other.humans && bots == other.bots && capacity == other.capacity && clients == other.clients;
    }
    bool operator!=(ePlatformPlayerCounts const& other) const noexcept { return !(*this == other); }
};

// one connected human in a room heartbeat's roster (bots are never listed)
struct ePlatformRosterPlayer
{
    int netId{};       // the player's network object id (what every client knows it by)
    std::string gid{}; // authenticated global id, empty if none
    int ping{};        // the server's current estimate of the connection's round trip, ms
    bool spectator{};  // watching: spectator mode, or not in a team
    bool browser{};    // its connection came over the WebRTC transport (our page), not from a desktop client

    bool operator==(ePlatformRosterPlayer const& other) const noexcept
    {
        return netId == other.netId && gid == other.gid && ping == other.ping && spectator == other.spectator && browser == other.browser;
    }
};

// one serialized outbound event on its way from the game thread to a worker thread
struct ePlatformEvent
{
    std::string type{};
    std::int64_t time{}; // Unix seconds
    std::string body{};  // serialization chosen by the implementation
};

/* Bounded, thread-safe FIFO of outbound events. The game thread pushes, one worker
   thread pops. When full, the oldest event is dropped (and counted): a slow or absent
   platform must never make the server grow without bound. */
class ePlatformEventQueue
{
public:
    explicit ePlatformEventQueue(std::size_t capacity = 10000) : capacity_{capacity} {}

    void Push(ePlatformEvent event);

    // moves up to max events to the end of out; returns how many
    std::size_t PopBatch(std::vector<ePlatformEvent>& out, std::size_t max);

    // blocks until an event is queued, Wake() is called or the deadline passes;
    // returns false on timeout
    bool WaitUntil(std::chrono::steady_clock::time_point deadline);

    // wakes a waiting worker (for reasons other than new events)
    void Wake();

    std::size_t Size() const;

    // number of events dropped because the queue was full since the last call
    std::size_t TakeDropped();

private:
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::deque<ePlatformEvent> events_{};
    std::size_t capacity_;
    std::size_t dropped_{};
    bool woken_{};
};

/* The platform seam. Game code calls the static hooks; without a registered platform
   they do nothing, so the server behaves exactly as stock Armagetron.

   Threading (same rules as nTransport): hooks run on the game thread and must return
   quickly. Implementations do I/O on their own threads, hand outbound events over
   through an ePlatformEventQueue, and hand results back with PostToGameThread();
   Drain() runs them on the game thread once per main loop pass.

   Decisions get a static entry point whose null answer is today's behaviour
   (ChatSubmitted: deliver as usual), events a plain On... hook (as Death and MatchEnded are).
   Joins are decided where the player is known: at login (TryCompleteLogin's caller and
   PlayerAuthenticated), see gPlatformHttp.
*/
class ePlatform
{
public:
    virtual ~ePlatform();

    /* Registry. At most one platform is active. */
    static void Register(ePlatform* platform);
    static void Unregister(ePlatform* platform);
    static ePlatform* Instance() noexcept { return s_instance_; }

    /* Hooks, game thread only. */

    // the server is up (reported once per registered platform)
    static void ServerStarted();

    // a player's authentication succeeded; call right after ePlayerNetID::Authenticate().
    // The method is "token" while TryCompleteLogin() runs, "krawall" otherwise.
    static void PlayerAuthenticated(ePlayerNetID* player, tString const& gid);

    // a round ended (the result is built by gPlatformEvents in src/tron)
    static void RoundEnded(ePlatformRoundResult const& result);

    // a match started, a cycle died, a match ended (built by gPlatformEvents in src/tron)
    static void MatchStarted(ePlatformMatchStart const& start);
    static void Death(ePlatformDeath const& death);
    static void MatchEnded(ePlatformMatchResult const& result);

    /* Chat (server only). */

    // a chat line arrived from player (after the owner check, before any handling): true if
    // the platform took it (held for a verdict, dropped, answered); it delivers held lines
    // later through DeliverChat(). false: handle it as usual, right away.
    static bool ChatSubmitted(ePlayerNetID* player, tColoredString const& line);

    // the server's normal handling of a chat line (set by ePlayer: se_HandleChatLine)
    typedef void ChatDeliverCallback(ePlayerNetID* player, tColoredString line);
    static void SetChatDeliverCallback(ChatDeliverCallback* callback);

    // hands a line the platform held to the normal handling, as if it had just arrived from
    // the player's connection; false if there is no handler or the player is gone
    static bool DeliverChat(ePlayerNetID* player, tColoredString const& line);

    // runs work queued for the game thread; call once per main loop pass. Commands from the
    // platform (room heartbeats and commands) are applied here too.
    static void Drain();

    // the players on this server right now (game thread)
    static ePlatformPlayerCounts CountPlayers();

    // the connected humans with their ping, in the order of the player list (game thread)
    static std::vector<ePlatformRosterPlayer> RosterPlayers();

    /* Login tokens. */

    // client: sends a platform login token to the server once connected (queued until then,
    // sent once, dropped if no connection comes up within a minute)
    static void SendLoginToken(tString const& token);

    // server: the connection's human player that is not authenticated yet, or null
    static ePlayerNetID* FindLoginCandidate(int userID);

    // server: authenticates player as the verified global id gid through the same path a
    // successful armathentication login takes, at accessLevel (a tAccessLevel from the token's
    // roles, never above Admin; -1: Authenticated); true if the player is authenticated after
    static bool TryCompleteLogin(ePlayerNetID* player, tString const& gid, int accessLevel = -1);

    // the login result handler of the regular login path (set by ePlayer)
    typedef void LoginResultCallback(nKrawall::nCheckResult const& result);
    static void SetLoginResultCallback(LoginResultCallback* callback);

    // "token" while a platform token login is being completed, "krawall" otherwise
    static char const* CurrentLoginMethod() noexcept;

    // server: a login of the connection newOwner finds its account already logged in on
    // oldOwner. A fresh platform token replaces a session on another connection (the old
    // one is a reloaded page or another device); anything else is refused as a duplicate.
    static bool LoginReplacesOld(int newOwner, int oldOwner, char const* method) noexcept;

    // server: called by the network handler when a client sent a login token
    void HandleLoginToken(int userID, tString const& token) { OnLoginToken(userID, token); }

    /* Moves to another room (contract section 3, "Moving players"). */

    // client: the capabilities announced to every server once per connection (message 381),
    // words separated by blanks; empty (the default): nothing is announced
    static void SetClientCapabilities(tString const& capabilities);

    // client: called when the server sends this client elsewhere (message 382)
    typedef void MoveCallback(ePlatformMoveNotice const& notice);
    static void SetMoveCallback(MoveCallback* callback);

    // server: whether the connection announced capability (forgotten when it logs out)
    static bool ClientHasCapability(int userID, char const* capability);

    // server: the connection's announced capabilities as received (for tests and logs)
    static std::string ClientCapabilities(int userID);

    // server: sends the move to the connection (only if it announced "move"); false if not sent
    static bool SendMove(int userID, ePlatformMoveNotice const& notice);

    // whether a blank separated word list contains word (exactly, case sensitive)
    static bool HasWord(std::string const& words, char const* word);

protected:
    ePlatform();

    // queues a task for the game thread; safe from any thread
    void PostToGameThread(std::function<void()> task);

    // runs the tasks queued so far (Drain() does this for the registered platform)
    void RunGameThreadTasks();

private:
    virtual void OnServerStarted() {}
    virtual void OnPlayerAuthenticated(ePlayerNetID& player, tString const& gid, char const* method) {}
    virtual void OnRoundEnded(ePlatformRoundResult const& result) {}
    virtual void OnMatchStarted(ePlatformMatchStart const& start) {}
    virtual void OnDeath(ePlatformDeath const& death) {}
    virtual void OnMatchEnded(ePlatformMatchResult const& result) {}
    virtual void OnLoginToken(int userID, tString const& token) {}
    virtual bool OnChatSubmitted(ePlayerNetID& player, tColoredString const& line) { return false; }
    virtual void OnDrain() {}

    std::mutex tasksMutex_{};
    std::deque<std::function<void()>> tasks_{};
    bool serverStartedReported_{};

    static ePlatform* s_instance_;

    // no copying, the registry holds raw pointers
    ePlatform(ePlatform const&) = delete;
    ePlatform& operator=(ePlatform const&) = delete;
};

#endif // ArmageTron_EPLATFORM_H
