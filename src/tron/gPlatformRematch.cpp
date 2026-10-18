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

#include "gPlatformRematch.h"

#include <utility>

namespace
{
std::string sg_TrimLower(std::string const& line)
{
    std::size_t begin = 0, end = line.size();
    while (begin < end && (line[begin] == ' ' || line[begin] == '\t'))
        ++begin;
    while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t' || line[end - 1] == '\r' || line[end - 1] == '\n'))
        --end;
    std::string out = line.substr(begin, end - begin);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return out;
}
} // namespace

std::string gPlatformRematch::AskText(long seconds)
{
    return "Rematch? Every player: say /rematch within " + std::to_string(seconds) + " seconds, or the room closes.";
}

std::string gPlatformRematch::WantsText(std::string const& name)
{
    return name + " wants a rematch.";
}

std::string const gPlatformRematch::StartText = "Rematch: a new match starts.";
std::string const gPlatformRematch::ClosedText = "The 1v1 is over: nobody asked for a rematch in time.";

gPlatformRematch::gPlatformRematch(gPlatformRematchEffects effects, std::chrono::seconds window)
    : effects_(std::move(effects)), window_(window)
{
}

void gPlatformRematch::MatchEnded(Clock::time_point now)
{
    if (window_.count() <= 0)
        return;
    open_ = true;
    until_ = now + window_;
    yes_.clear();
    if (effects_.announce)
        effects_.announce(AskText(static_cast<long>(window_.count())));
}

bool gPlatformRematch::Submit(int conn, std::string const& line, Clock::time_point now)
{
    (void)now;
    if (sg_TrimLower(line) != "/rematch")
        return false;
    if (!open_)
    {
        if (effects_.tell)
            effects_.tell(conn, "There is no match to rematch right now: it is asked for when a match ends.");
        return true;
    }
    std::vector<gPlatformRematchPlayer> const players = effects_.players ? effects_.players() : std::vector<gPlatformRematchPlayer>{};
    std::string name;
    bool playing = false;
    for (auto const& p : players)
        if (p.conn == conn && p.inTeam)
        {
            playing = true;
            name = p.name;
        }
    if (!playing)
    {
        if (effects_.tell)
            effects_.tell(conn, "Only the players can ask for a rematch.");
        return true;
    }
    if (!yes_.insert(conn).second)
    {
        if (effects_.tell)
            effects_.tell(conn, "You asked for a rematch: waiting for the other player.");
        return true;
    }
    if (effects_.announce)
        effects_.announce(WantsText(name));

    // everyone playing said yes (two at least: a rematch needs an opponent)
    std::size_t inTeam = 0, agreed = 0;
    for (auto const& p : players)
        if (p.inTeam)
        {
            ++inTeam;
            if (yes_.count(p.conn))
                ++agreed;
        }
    if (inTeam >= 2 && agreed == inTeam)
        Restart();
    return true;
}

void gPlatformRematch::Restart()
{
    open_ = false;
    yes_.clear();
    if (effects_.announce)
        effects_.announce(StartText);
    if (effects_.log)
        effects_.log("Rematch: everyone agreed, the match starts over.");
    if (effects_.restart)
        effects_.restart();
}

void gPlatformRematch::Tick(Clock::time_point now)
{
    if (!open_ || now < until_)
        return;
    open_ = false;
    yes_.clear();
    std::set<int> conns;
    if (effects_.players)
        for (auto const& p : effects_.players())
            if (p.conn > 0)
                conns.insert(p.conn);
    if (effects_.log)
        effects_.log("Rematch: nobody agreed in time, " + std::to_string(conns.size()) + " connection(s) closed.");
    // a disconnect ends the connection, which calls UserLoggedOut(): the set is a copy
    for (int conn : conns)
        if (effects_.disconnect)
            effects_.disconnect(conn, ClosedText);
}

void gPlatformRematch::UserLoggedOut(int conn)
{
    yes_.erase(conn);
}
