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

// gPlatformSocial.h: friends and direct messages for desktop players in an official room
// (platform-contract/README.md section 3, "Friends in the rooms"). The game
// thread side: /f [name], /msg to a name that matches nobody in the room (it falls through to
// a friend of that name anywhere), /r and /join are never broadcast; one request per player at
// a time goes to the platform, which runs the rules and words the answer; the lines come back
// on the player's console. Messages and the unread note arrive as `player_message` room
// commands (Deliver), printed to that player only and never to a browser client (its page has
// the platform's event stream). No network and no game objects (gPlatformSocialEffects);
// gPlatformHttp does the HTTP on a worker thread. Only built with --enable-platform.

#ifndef ArmageTron_GPLATFORMSOCIAL_H
#define ArmageTron_GPLATFORMSOCIAL_H

#include "gPlatformModeration.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

// one command on its way to the platform
struct gPlatformSocialRequest
{
    std::uint64_t id{};
    int playerId{};
    std::string gid{};     // the player's authenticated global id
    std::string command{}; // friend, friends, message, reply, join
    std::string target{};  // a name or handle as typed
    int netId{-1};         // friend: the room's player the name matched, -1 for none
    std::string text{};    // message, reply
};

// what came back; status 0: no HTTP answer at all
struct gPlatformSocialAnswer
{
    std::uint64_t id{};
    int status{};
    std::string result{};
    std::vector<std::string> lines{}; // the platform's words for the player (UTF-8)
};

// what the social commands do to the running server (all game thread)
struct gPlatformSocialEffects
{
    std::function<void(int conn, std::string const& text)> tell{};               // a console line to one connection
    std::function<int(int playerId)> connOf{};                                   // the player's connection now, 0 if gone
    std::function<int(int requesterId, std::string const& name)> inRoom{};       // the room's player a name matches (/msg's rule), -1 for none
    std::function<std::vector<int>(std::string const& gid)> connsOfGid{};        // the connections of players logged in as gid
    std::function<bool(int conn)> isBrowser{};                                   // a browser client (its page has the event stream)
    std::function<void(gPlatformSocialRequest const&)> send{};                   // hand the request to the worker
    std::function<void(std::string const&)> log{};                               // server console
};

/* /f, /msg to a friend elsewhere, /r, /join, and the lines that come back; game thread only. */
class gPlatformSocial
{
public:
    using Clock = std::chrono::steady_clock;

    static constexpr int CommandsPerMinute = 12; // per player, whatever came of them
    static constexpr std::size_t MaxTextLength = 240; // characters (code points), not bytes

    explicit gPlatformSocial(gPlatformSocialEffects effects);

    // true: the line was one of ours and is social's now (never delivered); false: not ours (a
    // /msg to someone in the room, any other line), the room goes on as before
    bool Submit(gPlatformChatPlayer const& player, std::string const& line, Clock::time_point now);

    // the worker's answer: the player is told
    void Answer(gPlatformSocialAnswer const& answer);

    // a `player_message` room command: kind "message" (a direct message, in the whisper's pink)
    // or "notice"; to the desktop connections logged in as gid
    void Deliver(std::string const& gid, std::string const& kind, std::string const& text);

    std::size_t Pending() const noexcept { return pending_.size(); }

    // the command word (lower case) and the rest, trimmed; empty word for a line that is not a command
    static void SplitCommand(std::string const& line, std::string& word, std::string& rest);

    // UTF-8 from the platform as the game's Latin-1 (what Latin-1 cannot hold becomes ?), no control characters
    static std::string ToLatin1(std::string const& utf8);

    // JSON (contract section 3)
    static std::string RequestJson(std::string const& bootId, gPlatformSocialRequest const& request);
    static bool TryParseAnswer(std::string const& body, gPlatformSocialAnswer& answer, std::string& error);

private:
    bool Send(gPlatformChatPlayer const& player, gPlatformSocialRequest request, Clock::time_point now);

    gPlatformSocialEffects effects_;
    std::uint64_t nextId_{1};
    std::map<std::uint64_t, gPlatformSocialRequest> pending_{};
    std::map<int, std::deque<Clock::time_point>> commands_{}; // per player
};

#endif // ArmageTron_GPLATFORMSOCIAL_H
