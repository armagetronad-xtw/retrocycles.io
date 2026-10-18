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

// gPlatformMoves.h: the move_players room command (platform-contract/README.md section 3,
// "Moving players"): which connections go where, when, and the players_moved report. Game
// thread only; the effects on the running server are injectable for tests. Only built with
// --enable-platform.

#ifndef ArmageTron_GPLATFORMMOVES_H
#define ArmageTron_GPLATFORMMOVES_H

#include "ePlatform.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// a human player as a move sees it
struct gPlatformMovePlayer
{
    int conn{};        // the connection that owns the player (> 0)
    std::string gid{}; // authenticated global id, empty if none
    bool inTeam{};     // playing (spectators are in no team)
};

// what a move touches in the running server; injectable for tests
struct gPlatformMoveEffects
{
    std::function<std::vector<gPlatformMovePlayer>()> players{}; // humans only
    std::function<bool(int conn)> followsMoves{};               // announced the "move" capability
    std::function<bool(int conn)> browser{};                    // a browser client (WebRTC transport)
    std::function<bool(int conn, ePlatformMoveNotice const&)> sendMove{};
    std::function<void(int conn, std::string const& host, int port, std::string const& reason)> redirect{};
    std::function<void(int conn, std::string const& reason)> disconnect{};
    std::function<bool()> matchRunning{}; // a match is being played (by humans in teams)
    std::function<void(std::string const& data)> report{}; // the players_moved event's data (JSON)
    std::function<void(std::string const&)> log{};
};

// one move_players command
struct gPlatformMove
{
    std::string moveId{};
    bool atMatchEnd{true}; // when: "match_end" (default) or "now"
    std::string reason{"admin"};
    std::string targetServerId{};
    std::string targetName{};
    std::string targetSignalUrl{};
    std::string udpHost{}; // empty: desktop clients stay
    int udpPort{};
    bool everyone{};                 // gids: null
    std::vector<std::string> gids{}; // otherwise
    std::int64_t expiresAt{};        // Unix seconds, 0: never

    static std::size_t const MaxIdLength = 64;
    static std::size_t const MaxGids = 64;

    // the command's data object; false and a reason if it is unusable
    static bool TryParse(std::string const& data, gPlatformMove& move, std::string& error);
};

class gPlatformMoves
{
public:
    using Clock = std::chrono::steady_clock;

    explicit gPlatformMoves(gPlatformMoveEffects effects, std::chrono::milliseconds grace = std::chrono::milliseconds(10000));

    // a command arrived; it runs at the next Tick() that finds it due
    void Add(gPlatformMove move);

    // the running match ended (match_ended was reported): the moves waiting for it are due
    void MatchEnded() noexcept { matchEnded_ = true; }

    // runs the moves that are due, drops expired ones, ends the connections whose grace is over
    void Tick(Clock::time_point now, std::int64_t unixNow);

    // a connection went away (it may have left for the target)
    void UserLoggedOut(int conn);

    std::size_t Waiting() const noexcept { return waiting_.size(); }
    std::size_t Leaving() const noexcept { return leaving_.size(); }

    // what players see as the reason they move
    static std::string ReasonText(std::string const& reason, std::string const& targetName);

private:
    void Run(gPlatformMove const& move, Clock::time_point now);
    void ReportExpired(gPlatformMove const& move);

    gPlatformMoveEffects effects_;
    std::chrono::milliseconds grace_;
    std::vector<gPlatformMove> waiting_{};
    bool matchEnded_{};

    // connections that got a move and should leave by themselves: when to end them, and why
    struct Leaver
    {
        Clock::time_point until{};
        std::string reason{};
    };
    std::map<int, Leaver> leaving_{};
};

#endif // ArmageTron_GPLATFORMMOVES_H
