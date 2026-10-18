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

#include "gPlatformEvents.h"

#include "ePlayer.h"
#include "eTeam.h"
#include "eTimer.h"
#include "eNetGameObject.h"
#include "gCycle.h"
#include "gGame.h"

#include <algorithm>
#include <limits>

#ifdef ENABLE_PLATFORM
#include "gPlatformHttp.h"
#endif

// *************************************************************
// MatchTracker: the bookkeeping, no game objects
// *************************************************************

void gPlatformEvents::MatchTracker::MatchStarted(int limitRounds, int limitMinutes, int limitScore)
{
    if (open_)
    {
        // gGame::StartNewMatchNow() runs again at the first round of the same match
        if (roundsEnded_ == 0)
        {
            if (!startSent_)
            {
                start_.limitRounds = limitRounds;
                start_.limitMinutes = limitMinutes;
                start_.limitScore = limitScore;
            }
            return;
        }

        // a new match before this one was decided (START_NEW_MATCH, a vote, a new game)
        Close(false);
    }

    ++match_;
    open_ = true;
    roundsEnded_ = 0;
    decided_ = false;
    winner_.clear();
    order_.clear();
    participants_.clear();
    lastLines_.clear();
    deathTimes_.clear();

    startSent_ = false;
    start_ = ePlatformMatchStart();
    start_.match = match_;
    start_.limitRounds = limitRounds;
    start_.limitMinutes = limitMinutes;
    start_.limitScore = limitScore;
}

void gPlatformEvents::MatchTracker::SendStart()
{
    if (startSent_)
        return;
    startSent_ = true;
    ePlatform::MatchStarted(start_);
}

void gPlatformEvents::MatchTracker::MatchDecided(std::string const& winner)
{
    if (!open_)
        return;
    decided_ = true;
    winner_ = winner;
}

void gPlatformEvents::MatchTracker::Death(std::string const& victimKey, ePlatformPlayerRef const& victim, double time,
                                          ePlatformPlayerRef const* killer, char const* cause)
{
    // deaths after a match's last round and before the next match belong to no round
    if (!open_)
        return;

    SendStart();
    deathTimes_[victimKey] = time;

    ePlatformDeath death;
    death.match = match_;
    death.round = roundsEnded_ + 1;
    death.victim = victim;
    death.hasKiller = killer != nullptr;
    if (killer)
        death.killer = *killer;
    death.cause = cause;
    ePlatform::Death(death);
}

std::vector<int> gPlatformEvents::MatchTracker::RoundPlaces(std::vector<Line> const& lines, std::map<std::string, double> const& deathTimes)
{
    // survivors rank above every death, deaths without a known time below all others
    double const survived = std::numeric_limits<double>::infinity();
    double const unknown = -std::numeric_limits<double>::infinity();
    std::vector<double> rank;
    rank.reserve(lines.size());
    for (auto const& line : lines)
    {
        if (line.alive)
            rank.push_back(survived);
        else
        {
            auto const time = deathTimes.find(line.key);
            rank.push_back(time == deathTimes.end() ? unknown : time->second);
        }
    }

    std::vector<int> places;
    places.reserve(lines.size());
    for (std::size_t i = 0; i < rank.size(); ++i)
    {
        int better = 0;
        for (std::size_t j = 0; j < rank.size(); ++j)
            if (rank[j] > rank[i])
                ++better;
        places.push_back(better + 1);
    }
    return places;
}

void gPlatformEvents::MatchTracker::RoundEnded(int gameRounds, int seconds, std::vector<Line> const& lines)
{
    if (!open_)
        return;

    SendStart();
    ++roundsEnded_;

    ePlatformRoundResult result;
    result.match = match_;
    result.round = roundsEnded_;
    result.seconds = seconds < 0 ? 0 : seconds;
    std::vector<int> const places = RoundPlaces(lines, deathTimes_);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        Line const& line = lines[i];
        ePlatformRoundPlayer player;
        player.name = line.player.name;
        player.gid = line.player.gid;
        player.team = line.player.team;
        player.bot = line.player.bot;
        player.score = line.score;
        player.alive = line.alive;
        player.place = places[i];
        result.players.push_back(player);

        auto found = participants_.find(line.key);
        if (found == participants_.end())
        {
            order_.push_back(line.key);
            found = participants_.emplace(line.key, Participant{}).first;
        }
        found->second.player = line.player;
        found->second.score = line.score;
        ++found->second.rounds;
    }
    ePlatform::RoundEnded(result);

    lastLines_ = lines;
    deathTimes_.clear();

    // gGame::rounds is reset to -100 as soon as a match is decided or restarted: this
    // was its last round
    if (gameRounds < 0)
        Close(decided_);
}

void gPlatformEvents::MatchTracker::Close(bool completed)
{
    ePlatformMatchResult result;
    result.match = match_;
    result.completed = completed;
    result.rounds = roundsEnded_;
    if (completed)
        result.winner = winner_;

    // places of the teams at the end, by score; equal scores share a place
    std::map<std::string, int> teamScores;
    for (auto const& line : lastLines_)
        teamScores[line.teamKey] = line.teamScore;

    std::map<std::string, Line const*> present;
    for (auto const& line : lastLines_)
        present[line.key] = &line;

    for (auto const& key : order_)
    {
        Participant const& participant = participants_[key];
        ePlatformMatchPlayer player;
        player.player = participant.player;
        player.score = participant.score;
        player.rounds = participant.rounds;

        auto const line = present.find(key);
        if (line == present.end())
            player.leftEarly = true;
        else
        {
            int better = 0;
            for (auto const& team : teamScores)
                if (team.second > line->second->teamScore)
                    ++better;
            player.place = better + 1;
        }
        result.players.push_back(player);
    }

    open_ = false;
    ePlatform::MatchEnded(result);
}

// *************************************************************
// the game side
// *************************************************************

namespace
{
gPlatformEvents::MatchTracker& sg_Tracker()
{
    static gPlatformEvents::MatchTracker tracker;
    return tracker;
}

// the death KillAt() credited, waiting for the cycle's Die() in Kill()
struct gPendingCredit
{
    gCycle const* cycle{};
    REAL time{};
    bool hasKiller{};
    ePlatformPlayerRef killer{};
    char const* cause{"other"};
};
gPendingCredit sg_pendingCredit;

ePlatformPlayerRef sg_Ref(ePlayerNetID const& player)
{
    ePlatformPlayerRef ref;
    ref.name = static_cast<char const*>(player.GetName());
#ifdef KRAWALL_SERVER
    if (player.IsHuman() && player.IsAuthenticated())
        ref.gid = static_cast<char const*>(player.GetRawAuthenticatedName());
#endif
    eTeam const* team = player.CurrentTeam();
    if (team)
        ref.team = static_cast<char const*>(tColoredString::RemoveColors(team->Name()));
    ref.bot = !player.IsHuman();
    return ref;
}

std::string sg_Key(ePlayerNetID const& player, ePlatformPlayerRef const& ref)
{
    if (!ref.gid.empty())
        return ref.gid;
    return "#" + std::to_string(player.ID()) + ":" + ref.name;
}

// the players in teams (spectators do not play the round)
std::vector<gPlatformEvents::MatchTracker::Line> sg_Lines()
{
    std::vector<gPlatformEvents::MatchTracker::Line> lines;
    for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
    {
        ePlayerNetID const* player = se_PlayerNetIDs(i);
        if (!player)
            continue;

        eTeam const* team = player->CurrentTeam();
        if (!team)
            continue;

        gPlatformEvents::MatchTracker::Line line;
        line.player = sg_Ref(*player);
        line.key = sg_Key(*player, line.player);
        line.score = player->Score();
        eNetGameObject const* object = player->Object();
        line.alive = object && object->Alive();
        // teams of one: every player places on their own (the AI players of MIN_PLAYERS
        // and NUM_AIS share one AI team whatever TEAM_MAX_PLAYERS says)
        bool const freeForAll = eTeam::maxPlayers <= 1;
        line.teamKey = freeForAll ? line.key : std::to_string(team->ID());
        line.teamScore = freeForAll ? line.score : team->Score();
        lines.push_back(line);
    }
    return lines;
}

void sg_StartMatch()
{
    gGameSettings const* settings = sg_currentSettings;
    sg_Tracker().MatchStarted(settings ? settings->limitRounds : 0, settings ? settings->limitTime : 0, settings ? settings->limitScore : 0);
}
} // namespace

void gPlatformEvents::ServerStarted()
{
#ifdef ENABLE_PLATFORM
    gPlatformHttp::StartIfConfigured();
#endif
    ePlatform::ServerStarted();
}

void gPlatformEvents::Shutdown()
{
#ifdef ENABLE_PLATFORM
    gPlatformHttp::Stop();
#endif
}

namespace
{
bool sg_matchRestartWish = false;
}

void gPlatformEvents::RequestMatchRestart()
{
    sg_matchRestartWish = true;
}

bool gPlatformEvents::TakeMatchRestart()
{
    bool const wish = sg_matchRestartWish;
    sg_matchRestartWish = false;
    return wish;
}

bool gPlatformEvents::MatchRunning()
{
    if (!sg_Tracker().Open())
        return false;
    for (int i = se_PlayerNetIDs.Len() - 1; i >= 0; --i)
    {
        ePlayerNetID const* player = se_PlayerNetIDs(i);
        if (player && player->IsHuman() && player->CurrentTeam())
            return true;
    }
    return false;
}

void gPlatformEvents::MatchStarted()
{
    if (!ePlatform::Instance())
        return;
    sg_StartMatch();
}

void gPlatformEvents::MatchDecided()
{
    if (!ePlatform::Instance())
        return;

    // Analysis() sorted the teams by score just before
    std::string winner;
    if (eTeam::teams.Len() > 0 && eTeam::teams(0))
        winner = static_cast<char const*>(tColoredString::RemoveColors(eTeam::teams(0)->Name()));
    sg_Tracker().MatchDecided(winner);
}

ePlatformRoundResult gPlatformEvents::CollectRoundResult(int round)
{
    ePlatformRoundResult result;
    result.round = round;

    std::vector<MatchTracker::Line> const lines = sg_Lines();
    std::vector<int> const places = MatchTracker::RoundPlaces(lines, std::map<std::string, double>());
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        ePlatformRoundPlayer player;
        player.name = lines[i].player.name;
        player.gid = lines[i].player.gid;
        player.team = lines[i].player.team;
        player.bot = lines[i].player.bot;
        player.score = lines[i].score;
        player.alive = lines[i].alive;
        player.place = places[i];
        result.players.push_back(player);
    }

    return result;
}

void gPlatformEvents::RoundEnded(int gameRounds)
{
    // build nothing without a platform
    if (!ePlatform::Instance())
        return;

    // a platform that came up during a match reports it from here on
    if (!sg_Tracker().Open())
        sg_StartMatch();

    sg_Tracker().RoundEnded(gameRounds, static_cast<int>(se_GameTime()), sg_Lines());
}

void gPlatformEvents::DeathCredited(gCycle const* victim, ePlayerNetID const* hunter)
{
    sg_pendingCredit = gPendingCredit();
    if (!ePlatform::Instance() || !victim)
        return;

    ePlayerNetID const* prey = victim->Player();
    sg_pendingCredit.cycle = victim;
    sg_pendingCredit.time = victim->LastTime();
    if (!prey || !hunter || hunter == prey)
    {
        sg_pendingCredit.cause = "suicide";
        return;
    }

    sg_pendingCredit.hasKiller = true;
    sg_pendingCredit.killer = sg_Ref(*hunter);
    sg_pendingCredit.cause = prey->CurrentTeam() != hunter->CurrentTeam() ? "frag" : "teamkill";
}

void gPlatformEvents::CycleDied(gCycle const* cycle)
{
    // the credit belongs to this death only if KillAt() made it for this cycle just now
    gPendingCredit const credit = sg_pendingCredit;
    sg_pendingCredit = gPendingCredit();

    if (!ePlatform::Instance() || !cycle)
        return;
    ePlayerNetID const* player = cycle->Player();
    if (!player)
        return;

    bool const credited = credit.cycle == cycle && credit.time == cycle->LastTime();
    ePlatformPlayerRef const victim = sg_Ref(*player);
    sg_Tracker().Death(sg_Key(*player, victim), victim, cycle->LastTime(),
                       credited && credit.hasKiller ? &credit.killer : nullptr,
                       credited ? credit.cause : "other");
}
