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

// gPlatformModeration.h: the game thread side of moderation (platform-contract/README.md
// section 5): chat lines held for the platform's verdict, the local word filter, the sanctions
// a server holds, and in-game reports. No network and no game objects: the server is reached
// through gPlatformModerationEffects, so all of it runs in unit tests. gPlatformHttp owns one
// and does the HTTP. Only built with --enable-platform.

#ifndef ArmageTron_GPLATFORMMODERATION_H
#define ArmageTron_GPLATFORMMODERATION_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <string>
#include <vector>

// a player as moderation sees it (built by the server from an ePlayerNetID)
struct gPlatformChatPlayer
{
    int playerId{};       // identifies the player while connected (the network object id)
    int conn{};           // network connection (Owner()), 1-based
    std::string name{};   // without colour codes
    std::string gid{};    // authenticated global id, empty if not logged in
    std::string userId{}; // the login token's sub, empty if unknown
    std::string ip{};     // the connection's address
    std::string team{};   // team name without colour codes, empty if none

    bool LoggedIn() const noexcept { return !gid.empty(); }
};

/* The local word filter (contract section 5, "The local word filter"). */
class gPlatformChatFilter
{
public:
    struct Lists
    {
        std::string version{};
        std::vector<std::string> mask{};
        std::vector<std::string> block{};
        std::vector<std::string> allow{};
        std::string notice{}; // told to the sender of a line the filter drops; empty: DefaultNotice
    };

    // what the sender of a dropped line is told when the platform names nothing (the platform's default)
    static char const* const DefaultNotice;

    // the built-in lists (a short English profanity list, game talk allowed)
    gPlatformChatFilter();
    explicit gPlatformChatFilter(Lists const& lists);

    static Lists BuiltIn();

    // text with every match replaced by '*' (one per character); masked tells whether any was,
    // blocked whether a block term was among them (a server drops such a line, contract section 5)
    std::string Apply(std::string const& text, bool* masked = nullptr, bool* blocked = nullptr) const;

    // false when a player name contains a mask or block term, also with every separator between
    // its letters removed ("f_uck", "Fu.Ck"), as the platform checks names (contract section 5, "Names")
    bool NameAllowed(std::string const& name) const;

    std::string const& Version() const noexcept { return version_; }
    std::string const& Notice() const noexcept { return notice_; }
    std::size_t TermCount() const noexcept { return terms_.size(); }

private:
    struct Word
    {
        std::string text{};
        bool prefix{}; // word*
        bool infix{};  // *word*
    };
    using Term = std::vector<Word>;

    static void Parse(std::vector<std::string> const& in, std::vector<Term>& out);

    std::string version_{};
    std::string notice_{};
    std::vector<Term> terms_{}; // mask terms, then block terms (from blockFrom_ on)
    std::size_t blockFrom_{};
    std::vector<Term> allow_{};
};

/* A sanction (contract section 5, "Sanctions"). */
struct gPlatformSanction
{
    std::string id{};
    std::string kind{};  // mute, kick, ban
    std::string scope{}; // all, room
    std::string serverId{};
    std::string targetType{}; // account, connection, address
    std::string userId{};
    std::vector<std::string> gids{}; // lowercase
    std::string bootId{};
    int conn{};
    std::string ip{};
    std::string name{};
    std::int64_t until{}; // Unix seconds; meaningless when permanent
    bool permanent{};
    std::string reason{};

    // parses a contract sanction object (JSON text); false if unusable
    static bool TryParse(std::string const& json, gPlatformSanction& out, std::string& error);
};

// what a chat line came to
enum class gPlatformChatOutcome
{
    AsWritten,
    Masked,
    Dropped,
    Muted
};

// a held line on its way to the platform
struct gPlatformOutboundLine
{
    std::uint64_t id{};
    std::int64_t time{};
    std::string channel{};
    std::string text{};
    std::string to{};
    gPlatformChatPlayer player{};
};

// the platform's verdict on one line
struct gPlatformChatVerdict
{
    std::uint64_t id{};
    std::string action{}; // allow, replace, drop
    std::string text{};   // replace
    std::string notice{}; // drop
};

// what moderation does to the running server (all game thread)
struct gPlatformModerationEffects
{
    std::function<std::vector<gPlatformChatPlayer>()> players{};                // the human players now
    std::function<void(int conn, std::string const& text)> tell{};               // a console line to one connection
    std::function<void(int conn, std::string const& reason)> kick{};             // end a connection, logged like KICK
    std::function<bool(std::string const& gid, std::string const& name)> rename{}; // RENAME the player logged in as gid
    std::function<bool(std::string const& name, int requesterPlayerId, gPlatformChatPlayer& found)> find{}; // /msg-style lookup
    std::function<void(std::string const& data)> report{};                      // a report event's data (JSON)
    std::function<void(std::string const&)> log{};                              // server console
    std::function<std::int64_t()> clock{};                                       // Unix seconds
};

/* Chat hold, sanctions and reports; game thread only. */
class gPlatformModeration
{
public:
    using Clock = std::chrono::steady_clock;
    using Deliver = std::function<void(std::string const& line)>;

    struct Config
    {
        bool chatFilter{};  // PLATFORM_CHAT_FILTER
        bool sanctions{};   // PLATFORM_SANCTIONS
        bool reports{};     // PLATFORM_REPORTS
        std::chrono::milliseconds timeout{150};   // PLATFORM_CHAT_TIMEOUT
        std::chrono::seconds ipBanGrace{15};      // PLATFORM_IP_BAN_GRACE
        std::chrono::seconds statsInterval{60};   // how often the hold times are logged
        std::string serverId{};
        std::string bootId{};
    };

    // how chat lines are treated
    enum class Mode
    {
        Hold,  // wait for the verdict (up to the timeout)
        Async, // the platform is failing: local filter at once, lines still sent
        Off    // the platform does no moderation: local filter at once, nothing sent
    };

    struct Stats
    {
        std::size_t checked{};  // lines held and released
        std::size_t timeouts{}; // decided by the local filter after the timeout
        std::size_t late{};     // verdicts that came after the timeout
        std::size_t lateDrops{}; // of those, drops of a line the local filter had let through
        std::size_t localDrops{}; // lines the local filter dropped (block terms)
        std::vector<double> holdMs{}; // per released held line
    };

    static std::size_t const MaxWindow = 50;        // chat lines kept for reports
    static std::int64_t const WindowSeconds = 600;
    static std::size_t const MaxOutbound = 1000;
    static int const ReportsPerMinute = 3;
    static std::size_t const MaxReplayLog = 1000;

    gPlatformModeration(Config config, gPlatformModerationEffects effects);

    Config const& Settings() const noexcept { return config_; }

    /* Chat. */

    // true: the line is moderation's now (held, dropped, answered or delivered through
    // deliver); false: the server handles it as usual, right away
    bool Submit(gPlatformChatPlayer const& player, std::string const& line, Deliver deliver, Clock::time_point now);

    // the held lines not handed to the platform yet (in submission order)
    std::vector<gPlatformOutboundLine> TakeOutbound();

    void ApplyVerdicts(std::vector<gPlatformChatVerdict> const& verdicts, Clock::time_point now);

    void SetMode(Mode mode, Clock::time_point now, std::chrono::seconds offFor = std::chrono::seconds(600));

    // the settings may change while the server runs (PLATFORM_CHAT_FILTER, PLATFORM_CHAT_TIMEOUT)
    void SetChatFilter(bool on, Clock::time_point now);
    void SetTimeout(std::chrono::milliseconds timeout);
    Mode CurrentMode() const noexcept { return mode_; }

    // timeouts, the end of an Off spell, address ban graces, expired sanctions, stats
    void Tick(Clock::time_point now);

    std::size_t Held() const noexcept { return pending_.size(); }
    Stats const& CurrentStats() const noexcept { return stats_; }

    /* Sanctions. commandId is the command's id, 0 when it came with a verdict. */

    void ApplySanction(gPlatformSanction const& sanction, std::int64_t commandId);
    void RemoveSanction(std::string const& id, std::int64_t commandId);

    // GET .../moderation: the filter (if any) and the sanctions in force, which replace the
    // known ones; sanction commands above commandId are applied again on top
    void ApplyState(gPlatformChatFilter::Lists const* filter, std::vector<gPlatformSanction> const& sanctions, std::int64_t commandId);

    // the ban (or mute) in force for a player, null if none
    gPlatformSanction const* BanFor(gPlatformChatPlayer const& player) const;
    gPlatformSanction const* MuteFor(gPlatformChatPlayer const& player) const;

    std::size_t SanctionCount() const noexcept { return sanctions_.size(); }
    gPlatformChatFilter const& Filter() const noexcept { return filter_; }

    // a login (token) is about to complete: false and the reason if the account is banned
    bool AdmitLogin(int conn, std::string const& gid, std::string const& userId, std::string& reason) const;

    // the login token's sub of a connection's player (for account sanctions and chat lines)
    void SetUserId(int conn, std::string const& userId);
    std::string UserIdOf(int conn) const;

    // a player logged in (any method): a ban in force ends the connection
    void PlayerAuthenticated(gPlatformChatPlayer const& player);

    void ConnectionOpened(int conn, std::string const& ip, Clock::time_point now);
    // also ends the sanctions that name the connection (they never pass to its next player)
    void ConnectionClosed(int conn);

    /* Other commands. */

    bool Rename(std::string const& gid, std::string const& name, std::string const& reason);

    // ends a connection with a reason (through the effects)
    void EndConnection(int conn, std::string const& reason) { effects_.kick(conn, reason); }

    // the texts players see
    static std::string FormatTime(std::int64_t unixSeconds); // "2026-10-01 14:05 UTC"
    static std::string BanText(gPlatformSanction const& sanction);
    static std::string MuteText(gPlatformSanction const& sanction);

    // JSON (contract section 5)
    static std::string ChatRequestJson(std::string const& bootId, std::vector<gPlatformOutboundLine> const& lines);
    static bool TryParseVerdicts(std::string const& body, std::vector<gPlatformChatVerdict>& verdicts, std::vector<gPlatformSanction>& sanctions, std::string& error);
    static bool TryParseState(std::string const& body, bool& hasFilter, gPlatformChatFilter::Lists& filter, std::vector<gPlatformSanction>& sanctions, std::int64_t& commandId, std::string& error);

    // what a chat line is: its channel, the part to check and what goes before it;
    // false for lines that are not checked (commands other than the talking ones)
    struct ParsedLine
    {
        std::string command{}; // lowercase, empty for plain chat
        std::string channel{};
        std::string prefix{};
        std::string text{};
        std::string to{};
    };
    static bool ParseLine(std::string const& line, ParsedLine& parsed);

private:
    struct Line
    {
        std::uint64_t id{};
        int playerId{};
        int conn{};
        bool checked{};  // a talking line (recorded in the window)
        bool pending{};  // waiting for the verdict
        bool dropped{};
        std::string notice{};
        std::string prefix{};
        std::string text{};     // as written
        std::string deliver{};  // the text after the prefix as it goes out
        gPlatformChatOutcome outcome{gPlatformChatOutcome::AsWritten};
        std::string channel{};
        std::string to{};
        std::int64_t time{};
        Clock::time_point submitted{};
        Clock::time_point deadline{};
        bool wasHeld{};
        bool byVerdict{}; // decided by the platform's verdict (not the timeout or the local filter)
        gPlatformChatPlayer player{};
        Deliver deliverFn{};
    };

    struct WindowLine
    {
        std::uint64_t id{};
        std::int64_t time{};
        std::string channel{};
        std::string text{};
        std::string to{};
        gPlatformChatOutcome outcome{};
        std::string lateVerdict{}; // the platform's verdict when it came after the local filter decided
        gPlatformChatPlayer player{};
    };

    struct ReplayEntry
    {
        std::int64_t commandId{};
        bool remove{};
        gPlatformSanction sanction{};
        std::string id{};
    };

    bool HandleReport(gPlatformChatPlayer const& reporter, std::string const& args, Clock::time_point now);
    void Release(int playerId, Clock::time_point now);
    void Record(Line const& line);
    void Decide(Line& line, std::string const& deliverText, gPlatformChatOutcome outcome);
    void DecideLocally(Line& line);
    void LateVerdict(gPlatformChatVerdict const& verdict);
    void Store(gPlatformSanction const& sanction);
    void Enforce(gPlatformSanction const& sanction, bool tellMuted);
    void Remember(ReplayEntry entry);
    bool Applies(gPlatformSanction const& sanction, gPlatformChatPlayer const& player) const;
    bool InForce(gPlatformSanction const& sanction) const;
    gPlatformSanction const* Find(char const* kind, gPlatformChatPlayer const& player) const;
    void LogStats(Clock::time_point now, bool force);
    std::int64_t Now() const;
    void Log(std::string const& text) const;

    Config config_;
    gPlatformModerationEffects effects_;
    gPlatformChatFilter filter_{};
    Mode mode_{Mode::Hold};
    Clock::time_point offUntil_{};

    std::uint64_t nextId_{1};
    std::map<int, std::deque<Line>> queues_{}; // per player, in order
    std::map<std::uint64_t, int> pending_{};   // line id -> player id
    std::vector<gPlatformOutboundLine> outbound_{};
    std::deque<WindowLine> window_{};
    std::map<int, std::deque<Clock::time_point>> reportsBy_{};

    std::map<std::string, gPlatformSanction> sanctions_{};
    std::deque<ReplayEntry> replay_{};
    std::set<std::string> endedConnectionSanctions_{}; // ids of connection sanctions whose connection closed
    std::deque<std::string> endedConnectionOrder_{};   // the same, oldest first (at most MaxReplayLog kept)
    std::map<int, std::string> userIds_{};
    std::map<int, std::pair<Clock::time_point, std::string>> graces_{}; // connection -> end of its address ban grace, address

    Stats stats_{};
    std::vector<double> recentHoldMs_{};
    std::size_t recentTimeouts_{};
    Clock::time_point nextExpiry_{};
    Clock::time_point nextStats_{};
};

#endif // ArmageTron_GPLATFORMMODERATION_H
