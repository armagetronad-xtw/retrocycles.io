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

// gPlatformLink.h: /link CODE (platform-contract/README.md section 3, "Linking old global
// ids"): a player logged in with an old global id (name@forums) links it to their platform
// account with the one-time code the account showed. The game thread side: the command is
// never broadcast, the checks the room can make itself, one request per player at a time,
// and the answer on the player's console. No network and no game objects (the server is
// reached through gPlatformLinkEffects); gPlatformHttp does the HTTP on a worker thread.
// Only built with --enable-platform.

#ifndef ArmageTron_GPLATFORMLINK_H
#define ArmageTron_GPLATFORMLINK_H

#include "gPlatformModeration.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>

// one /link on its way to the platform
struct gPlatformLinkRequest
{
    std::uint64_t id{};
    int playerId{};
    std::string gid{};  // the player's authenticated global id
    std::string name{}; // without colour codes
    std::string code{}; // as typed
};

// what came back; status 0: no HTTP answer at all
struct gPlatformLinkAnswer
{
    std::uint64_t id{};
    int status{};
    std::string result{};  // linked, already_linked, invalid_code, expired, used, linked_elsewhere, ...
    std::string message{}; // the platform's words, for results the room does not know
    std::string gid{};
    std::string handle{};  // the account (linked, already_linked)
};

// what linking does to the running server (all game thread)
struct gPlatformLinkEffects
{
    std::function<void(int conn, std::string const& text)> tell{};  // a console line to one connection
    std::function<int(int playerId)> connOf{};                      // the player's connection now, 0 if gone
    std::function<void(gPlatformLinkRequest const&)> send{};        // hand the request to the worker
    std::function<void(std::string const&)> log{};                  // server console
};

/* /link, game thread only. */
class gPlatformLink
{
public:
    using Clock = std::chrono::steady_clock;

    static int const AttemptsPerMinute = 3; // per player, whatever came of them
    static std::size_t const MaxCodeLength = 32;

    // authority: PLATFORM_AUTHORITY (the platform's own ids are not linked)
    gPlatformLink(std::string authority, gPlatformLinkEffects effects);

    // true: the line was /link and is linking's now (never delivered); false: not ours
    bool Submit(gPlatformChatPlayer const& player, std::string const& line, Clock::time_point now);

    // the worker's answer: the player is told
    void Answer(gPlatformLinkAnswer const& answer);

    std::size_t Pending() const noexcept { return pending_.size(); }

    // "/link" (any case) alone or followed by a blank; args gets the rest, trimmed
    static bool IsLinkCommand(std::string const& line, std::string& args);

    // the authority of user@authority, empty for anything else (a local account: user@)
    static std::string AuthorityOf(std::string const& gid);

    // the platform's own authority, also in its long form (<authority>.authentication.armagetronad.net)
    static bool IsOwnAuthority(std::string const& authority, std::string const& own);

    // what the player reads for an answer
    static std::string TextFor(gPlatformLinkAnswer const& answer);

    // JSON (contract section 3)
    static std::string RequestJson(std::string const& bootId, gPlatformLinkRequest const& request);
    static bool TryParseAnswer(std::string const& body, gPlatformLinkAnswer& answer, std::string& error);

private:
    std::string authority_;
    gPlatformLinkEffects effects_;
    std::uint64_t nextId_{1};
    std::map<std::uint64_t, gPlatformLinkRequest> pending_{};
    std::map<int, std::deque<Clock::time_point>> attempts_{}; // per player
};

#endif // ArmageTron_GPLATFORMLINK_H
