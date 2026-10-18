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

// gPlatformEvents.h: game level platform hooks (ePlatform.h). Each call site in an
// upstream file is a single line; the event building lives here.

#ifndef ArmageTron_GPLATFORMEVENTS_H
#define ArmageTron_GPLATFORMEVENTS_H

#include "ePlatform.h"

#include <map>
#include <string>
#include <vector>

class ePlayerNetID;
class gCycle;

class gPlatformEvents
{
public:
    // sg_HostGame(): starts the platform if one is configured (and built in), then
    // reports the server start; repeated calls do nothing more
    static void ServerStarted();

    // gGame::StartNewMatchNow(): a match starts (the game calls it again at the first
    // round of the same match; that is recognised and not reported twice)
    static void MatchStarted();

    // gGame::Analysis(), where the match winner is declared (absolute_winner = 1): the
    // match ends with the round being played
    static void MatchDecided();

    // gGame::Analysis(), right before a finished round is counted: gameRounds is
    // gGame::rounds at that point (negative once the match was decided or restarted)
    static void RoundEnded(int gameRounds);

    // gCycle::KillAt(), right before Kill(): the player the game credits with the death
    // (the victim's own player for a suicide)
    static void DeathCredited(gCycle const* victim, ePlayerNetID const* hunter);

    // gCycle::Kill(), right after Die(): the cycle is dead
    static void CycleDied(gCycle const* cycle);

    // a private room's rematch (gPlatformRematch): the match starts over at the next analysis, the
    // round being driven ended at once (as when the number of human teams changes)
    static void RequestMatchRestart();
    // gGame::Analysis(): true once per request
    static bool TakeMatchRestart();

    // a match is being played: one was started and has not ended, and a human plays it (in a
    // team); a room where nobody plays has no match end to wait for (moves, gPlatformMoves)
    static bool MatchRunning();

    // main(), on the way out: flushes briefly and stops the platform
    static void Shutdown();

    // the round result for the current players (exposed for tests)
    static ePlatformRoundResult CollectRoundResult(int round);

    /* The match bookkeeping behind the events, free of game objects so it can be tested:
       numbers matches and rounds, remembers who played which round and when players
       died, and reports through the ePlatform hooks, in the contract's order
       (match_started, per round its deaths and round_ended, one match_ended). */
    class MatchTracker
    {
    public:
        // one player in a team at the end of a round
        struct Line
        {
            std::string key{}; // stable within a match: the gid, else net id and name
            ePlatformPlayerRef player{};
            int score{};
            bool alive{};
            std::string teamKey{}; // same team, same key (in a free for all: the player's own)
            int teamScore{};
        };

        // a match starts; one already running is ended as aborted first, unless no
        // round of it ended yet (then it simply goes on, with these limits). The event
        // goes out with the match's first death or round end, so it carries the limits
        // the first round is played to (the game creates a match before it knows how
        // many humans play, which decides between its two settings sets)
        void MatchStarted(int limitRounds, int limitMinutes, int limitScore);

        // the running match has a winner; it ends with the next round end
        void MatchDecided(std::string const& winner);

        // a death in the running round (ignored outside a match); killer may be null
        void Death(std::string const& victimKey, ePlatformPlayerRef const& victim, double time,
                   ePlatformPlayerRef const* killer, char const* cause);

        // a round ended with these players in teams; gameRounds < 0 ends the match
        void RoundEnded(int gameRounds, int seconds, std::vector<Line> const& lines);

        bool Open() const noexcept { return open_; }
        int Match() const noexcept { return match_; }

        // the places of a round: survivors 1, then by time of death, later is better
        static std::vector<int> RoundPlaces(std::vector<Line> const& lines, std::map<std::string, double> const& deathTimes);

    private:
        struct Participant
        {
            ePlatformPlayerRef player{};
            int score{};
            int rounds{};
        };

        void Close(bool completed);
        void SendStart();

        int match_{};
        bool open_{};
        bool startSent_{};
        ePlatformMatchStart start_{};
        int roundsEnded_{};
        bool decided_{};
        std::string winner_{};
        std::vector<std::string> order_{}; // participants in order of appearance
        std::map<std::string, Participant> participants_{};
        std::vector<Line> lastLines_{};
        std::map<std::string, double> deathTimes_{}; // of the running round
    };
};

#endif // ArmageTron_GPLATFORMEVENTS_H
