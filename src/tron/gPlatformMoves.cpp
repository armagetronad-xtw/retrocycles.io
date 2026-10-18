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

#include <nlohmann/json.hpp>

#include "gPlatformMoves.h"

#include <algorithm>
#include <set>
#include <utility>

namespace
{
std::string sg_Lower(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// printable ASCII only, at most max bytes (ids, URLs, addresses)
bool sg_PrintableAscii(std::string const& text, std::size_t max)
{
    if (text.empty() || text.size() > max)
        return false;
    for (char c : text)
    {
        auto const byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte >= 0x7f)
            return false;
    }
    return true;
}

// UTF-8 (JSON) to the game's Latin-1, '?' for what does not fit; control characters dropped
std::string sg_GameText(std::string const& text, std::size_t max)
{
    std::string out;
    for (std::size_t i = 0; i < text.size() && out.size() < max;)
    {
        auto const c = static_cast<unsigned char>(text[i]);
        unsigned int cp = c;
        std::size_t len = 1;
        if (c >= 0xc0 && c < 0xe0 && i + 1 < text.size())
        {
            cp = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3fu);
            len = 2;
        }
        else if (c >= 0xe0 && c < 0xf0)
        {
            cp = 0x10000; // beyond Latin-1
            len = 3;
        }
        else if (c >= 0xf0)
        {
            cp = 0x10000;
            len = 4;
        }
        i += len;
        if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp < 0xa0))
            continue;
        out.push_back(cp <= 0xff ? static_cast<char>(cp) : '?');
    }
    return out;
}

std::string sg_Text(nlohmann::json const& object, char const* member)
{
    auto const value = object.find(member);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string();
}

nlohmann::ordered_json sg_GidOrNull(std::string const& gid)
{
    return gid.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(gid);
}
} // namespace

std::size_t const gPlatformMove::MaxIdLength;
std::size_t const gPlatformMove::MaxGids;

bool gPlatformMove::TryParse(std::string const& data, gPlatformMove& move, std::string& error)
{
    move = gPlatformMove();
    nlohmann::json const json = nlohmann::json::parse(data.empty() ? std::string("{}") : data, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "data is not an object";
        return false;
    }

    move.moveId = sg_Text(json, "moveId");
    if (!sg_PrintableAscii(move.moveId, MaxIdLength))
    {
        error = "no usable moveId";
        return false;
    }

    std::string const when = sg_Text(json, "when");
    if (when == "now")
        move.atMatchEnd = false;
    else if (when.empty() || when == "match_end")
        move.atMatchEnd = true;
    else
    {
        error = "unknown when '" + sg_GameText(when, 20) + "'";
        return false;
    }

    std::string const reason = sg_Text(json, "reason");
    if (reason == "merge" || reason == "drain" || reason == "closer" || reason == "admin" || reason == "join")
        move.reason = reason;
    else
        move.reason = "admin";

    auto const target = json.find("target");
    if (target == json.end() || !target->is_object())
    {
        error = "no target";
        return false;
    }
    move.targetServerId = sg_Text(*target, "serverId");
    move.targetSignalUrl = sg_Text(*target, "signalUrl");
    move.targetName = sg_GameText(sg_Text(*target, "name"), 80);
    if (!sg_PrintableAscii(move.targetServerId, 128) || !sg_PrintableAscii(move.targetSignalUrl, 512))
    {
        error = "target without serverId or signalUrl";
        return false;
    }
    if (move.targetName.empty())
        move.targetName = move.targetServerId;

    // host:port; anything unusable means no desktop redirect
    std::string const udp = sg_Text(*target, "udp");
    std::size_t const colon = udp.rfind(':');
    if (sg_PrintableAscii(udp, 255) && colon != std::string::npos && colon > 0 && colon + 1 < udp.size())
    {
        std::string const port = udp.substr(colon + 1);
        if (port.size() <= 5 && std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
        {
            int const value = std::stoi(port);
            if (value > 0 && value < 65536)
            {
                move.udpHost = udp.substr(0, colon);
                move.udpPort = value;
            }
        }
    }

    auto const gids = json.find("gids");
    if (gids == json.end() || gids->is_null())
        move.everyone = true;
    else if (gids->is_array())
    {
        for (auto const& gid : *gids)
            if (gid.is_string() && sg_PrintableAscii(gid.get<std::string>(), 128) && move.gids.size() < MaxGids)
                move.gids.push_back(gid.get<std::string>());
    }
    else
    {
        error = "gids is neither a list nor null";
        return false;
    }

    auto const expires = json.find("expiresAt");
    if (expires != json.end() && expires->is_number_integer() && expires->get<std::int64_t>() > 0)
        move.expiresAt = expires->get<std::int64_t>();

    return true;
}

gPlatformMoves::gPlatformMoves(gPlatformMoveEffects effects, std::chrono::milliseconds grace) : effects_(std::move(effects)), grace_(grace)
{
    if (!effects_.players)
        effects_.players = [] { return std::vector<gPlatformMovePlayer>(); };
    if (!effects_.followsMoves)
        effects_.followsMoves = [](int) { return false; };
    if (!effects_.browser)
        effects_.browser = [](int) { return false; };
    if (!effects_.sendMove)
        effects_.sendMove = [](int, ePlatformMoveNotice const&) { return false; };
    if (!effects_.redirect)
        effects_.redirect = [](int, std::string const&, int, std::string const&) {};
    if (!effects_.disconnect)
        effects_.disconnect = [](int, std::string const&) {};
    if (!effects_.matchRunning)
        effects_.matchRunning = [] { return false; };
    if (!effects_.report)
        effects_.report = [](std::string const&) {};
    if (!effects_.log)
        effects_.log = [](std::string const&) {};
}

std::string gPlatformMoves::ReasonText(std::string const& reason, std::string const& targetName)
{
    if (reason == "merge")
        return "Rooms merged: moving everyone to " + targetName + ".";
    if (reason == "drain")
        return "This server is closing: moving to " + targetName + ".";
    if (reason == "closer")
        return "Moving to " + targetName + ", a server closer to you.";
    if (reason == "join")
        return "Joining your friend in " + targetName + ".";
    return "Moving to " + targetName + ".";
}

void gPlatformMoves::Add(gPlatformMove move)
{
    effects_.log("Platform: move " + move.moveId + " to " + move.targetServerId + (move.atMatchEnd ? " at the match end" : " now") + " (" +
                 (move.everyone ? std::string("everyone") : std::to_string(move.gids.size()) + " player(s)") + ", " + move.reason + ").");
    waiting_.push_back(std::move(move));
}

void gPlatformMoves::UserLoggedOut(int conn)
{
    leaving_.erase(conn);
}

void gPlatformMoves::ReportExpired(gPlatformMove const& move)
{
    nlohmann::ordered_json data;
    data["moveId"] = move.moveId;
    data["outcome"] = "expired";
    data["target"] = move.targetServerId;
    data["sent"] = nlohmann::ordered_json::array();
    data["redirected"] = nlohmann::ordered_json::array();
    nlohmann::ordered_json skipped = nlohmann::ordered_json::array();
    for (auto const& gid : move.gids)
        skipped.push_back({{"gid", gid}, {"why", "expired"}});
    data["skipped"] = std::move(skipped);
    effects_.log("Platform: move " + move.moveId + " expired before its match ended.");
    effects_.report(data.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace));
}

void gPlatformMoves::Tick(Clock::time_point now, std::int64_t unixNow)
{
    if (!waiting_.empty())
    {
        // a move ends connections, which may change the waiting list's surroundings: work on a copy
        std::vector<gPlatformMove> moves;
        moves.swap(waiting_);
        bool const matchOver = matchEnded_ || !effects_.matchRunning();
        for (auto& move : moves)
        {
            if (move.expiresAt > 0 && unixNow >= move.expiresAt)
                ReportExpired(move);
            else if (!move.atMatchEnd || matchOver)
                Run(move, now);
            else
                waiting_.push_back(std::move(move));
        }
    }
    matchEnded_ = false;

    if (leaving_.empty())
        return;
    std::vector<std::pair<int, std::string>> due;
    for (auto const& leaver : leaving_)
        if (now >= leaver.second.until)
            due.emplace_back(leaver.first, leaver.second.reason);
    for (auto const& leaver : due)
    {
        leaving_.erase(leaver.first);
        effects_.log("Platform: connection " + std::to_string(leaver.first) + " did not leave after its move; disconnecting.");
        effects_.disconnect(leaver.first, leaver.second);
    }
}

void gPlatformMoves::Run(gPlatformMove const& move, Clock::time_point now)
{
    std::vector<gPlatformMovePlayer> const players = effects_.players();

    std::set<std::string> wanted;
    for (auto const& gid : move.gids)
        wanted.insert(sg_Lower(gid));

    // the connections that move, each with all of its human players, in order of appearance
    std::vector<int> conns;
    std::map<int, std::vector<std::string>> gidsOf;
    std::set<std::string> found;
    for (auto const& player : players)
    {
        if (player.conn <= 0)
            continue;
        std::string const key = sg_Lower(player.gid);
        bool const named = !player.gid.empty() && wanted.count(key) > 0;
        if (named)
            found.insert(key);
        if (!move.everyone && !named)
            continue;
        if (leaving_.count(player.conn))
            continue; // on its way already (an earlier move)
        if (std::find(conns.begin(), conns.end(), player.conn) == conns.end())
            conns.push_back(player.conn);
    }
    for (auto const& player : players)
        if (std::find(conns.begin(), conns.end(), player.conn) != conns.end())
            gidsOf[player.conn].push_back(player.gid);

    ePlatformMoveNotice notice;
    notice.moveId = move.moveId;
    notice.serverId = move.targetServerId;
    notice.name = move.targetName;
    notice.signalUrl = move.targetSignalUrl;
    notice.reason = move.reason;
    std::string const reasonText = ReasonText(move.reason, move.targetName);

    nlohmann::ordered_json sent = nlohmann::ordered_json::array();
    nlohmann::ordered_json redirected = nlohmann::ordered_json::array();
    nlohmann::ordered_json skipped = nlohmann::ordered_json::array();
    auto skip = [&](std::string const& gid, char const* why) { skipped.push_back({{"gid", sg_GidOrNull(gid)}, {"why", why}}); };

    for (int conn : conns)
    {
        std::vector<std::string> const& gids = gidsOf[conn];
        if (effects_.followsMoves(conn) && effects_.sendMove(conn, notice))
        {
            for (auto const& gid : gids)
                sent.push_back(sg_GidOrNull(gid));
            leaving_[conn] = Leaver{now + grace_, "Moved to " + move.targetName + "."};
        }
        else if (effects_.browser(conn))
        {
            for (auto const& gid : gids)
                skip(gid, "no_support");
        }
        else if (!move.udpHost.empty())
        {
            for (auto const& gid : gids)
                redirected.push_back(sg_GidOrNull(gid));
            effects_.redirect(conn, move.udpHost, move.udpPort, reasonText);
        }
        else
        {
            for (auto const& gid : gids)
                skip(gid, "no_udp");
        }
    }

    for (auto const& gid : move.gids)
        if (!found.count(sg_Lower(gid)))
            skip(gid, "not_here");

    effects_.log("Platform: move " + move.moveId + " to " + move.targetServerId + ": sent " + std::to_string(sent.size()) + ", redirected " +
                 std::to_string(redirected.size()) + ", skipped " + std::to_string(skipped.size()) + ".");

    nlohmann::ordered_json data;
    data["moveId"] = move.moveId;
    data["outcome"] = "done";
    data["target"] = move.targetServerId;
    data["sent"] = std::move(sent);
    data["redirected"] = std::move(redirected);
    data["skipped"] = std::move(skipped);
    effects_.report(data.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace));
}
