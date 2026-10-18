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

// gPlatformRematch.h: a private room's end of match: the duel's
// room does not just roll on into the next match. When a match is decided it waits
// PLATFORM_PRIVATE_REMATCH_SECONDS (30) for every player to say /rematch: then the match starts
// over at once (scores back to zero, the round being driven ended, gPlatformEvents
// RequestMatchRestart); otherwise everyone is disconnected and the room is free again (the
// platform ends its claim once it is empty). Game thread only; the effects on the running server
// are injectable for tests. Only built with --enable-platform.

#ifndef ArmageTron_GPLATFORMREMATCH_H
#define ArmageTron_GPLATFORMREMATCH_H

#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <vector>

// a human in the room as the rematch sees them
struct gPlatformRematchPlayer
{
    int conn{};         // the connection that owns the player (> 0)
    std::string name{}; // as the game shows it, colour codes stripped
    bool inTeam{};      // playing (spectators are in no team)
};

// what the rematch touches in the running server; injectable for tests
struct gPlatformRematchEffects
{
    std::function<std::vector<gPlatformRematchPlayer>()> players{}; // humans only
    std::function<void(std::string const& line)> announce{};         // a console line to everyone
    std::function<void(int conn, std::string const& line)> tell{};   // a console line to one connection
    std::function<void()> restart{};                                 // a new match now, the round ended
    std::function<void(int conn, std::string const& reason)> disconnect{};
    std::function<void(std::string const&)> log{};
};

class gPlatformRematch
{
public:
    using Clock = std::chrono::steady_clock;

    // window 0: off (matches roll on as in any room)
    explicit gPlatformRematch(gPlatformRematchEffects effects, std::chrono::seconds window = std::chrono::seconds(30));

    void SetWindow(std::chrono::seconds window) noexcept { window_ = window; }

    // a match was decided: the window opens (announced with how to answer)
    void MatchEnded(Clock::time_point now);

    // a chat line from a player; true when it was /rematch (taken: never said in the room)
    bool Submit(int conn, std::string const& line, Clock::time_point now);

    // the window is over: everyone is disconnected
    void Tick(Clock::time_point now);

    // a connection went away: its answer goes with it
    void UserLoggedOut(int conn);

    bool Open() const noexcept { return open_; }
    std::size_t Answers() const noexcept { return yes_.size(); }

    // what the room says; the page reads the first two (DuelRoom.tsx)
    static std::string AskText(long seconds); // "Rematch? Every player: say /rematch within 30 seconds, or the room closes."
    static std::string WantsText(std::string const& name); // "<name> wants a rematch."
    static std::string const StartText;   // "Rematch: a new match starts."
    static std::string const ClosedText;  // the disconnect reason

private:
    void Restart();

    gPlatformRematchEffects effects_;
    std::chrono::seconds window_;
    bool open_{};
    Clock::time_point until_{};
    std::set<int> yes_{};
};

#endif // ArmageTron_GPLATFORMREMATCH_H
