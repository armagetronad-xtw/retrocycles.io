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

// gPlatformHttp.h: the ePlatform implementation that talks to a platform service over
// HTTP(S), platform-contract/README.md sections 2 to 5: events, login tokens, the room
// channel (heartbeats up, commands down), moderation (chat checks, sanctions, reports;
// the game thread side is gPlatformModeration) and /link (gPlatformLink). Only built with
// --enable-platform.

#ifndef ArmageTron_GPLATFORMHTTP_H
#define ArmageTron_GPLATFORMHTTP_H

#include "ePlatform.h"
#include "gPlatformLink.h"
#include "gPlatformSocial.h"
#include "gPlatformModeration.h"
#include "gPlatformMoves.h"
#include "gPlatformRematch.h"
#include "gPlatformToken.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

struct gPlatformHttpRequest
{
    std::string method{}; // "GET" or "POST"
    std::string url{};
    std::string bearer{}; // Authorization: Bearer, empty for none
    std::string body{};   // JSON, POST only
    std::chrono::seconds timeout{15}; // whole request; long polls need more
};

struct gPlatformHttpResponse
{
    int status{};        // HTTP status; 0 if there was no HTTP answer at all
    std::string body{};
    std::string error{}; // transport error when status is 0
};

// performs one HTTP request, blocking; called on the worker threads (events and heartbeats
// on one, the command long poll, chat checks and /link on their own), possibly at the same time
class gPlatformHttpSender
{
public:
    virtual ~gPlatformHttpSender() = default;

    // cancel turns true when the request should be abandoned (shutdown)
    gPlatformHttpResponse Send(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) { return DoSend(request, cancel); }

    // libcurl with TLS verification, no redirects
    static std::unique_ptr<gPlatformHttpSender> CreateCurl();

private:
    virtual gPlatformHttpResponse DoSend(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) = 0;
};

// what completing a verified login did
enum class gPlatformLoginOutcome
{
    Authenticated,
    NoPlayerYet, // the connection has no unauthenticated player yet; try again later
    Refused      // e.g. that global id is already logged in, or banned
};

/* The room channel (contract section 3, "heartbeat" and "commands"). */

// a command from the platform; members a type does not use stay empty
struct gPlatformCommand
{
    std::int64_t id{};
    std::string type{};
    std::string message{}; // drain, closing
    std::string text{};    // message
    bool center{};         // message
    int seconds{-1};       // closing; -1: the default
    std::string data{};    // the whole data object as JSON text (moderation commands), empty if none
};

enum class gPlatformRoomState
{
    Open,     // takes new players
    Draining, // no master announce, new connections refused, players stay
    Closing   // like Draining, plus a countdown; the host stops the process
};

// what the room commands touch in the running server; injectable for tests
struct gPlatformRoomEffects
{
    std::function<bool()> masterAnnounce{};           // TALK_TO_MASTER
    std::function<void(bool)> setMasterAnnounce{};
    std::function<void(std::string const&)> console{}; // to every client and the console
    std::function<void(std::string const&)> center{};  // centre of every client's screen
    std::function<void(int userID, std::string const& reason)> refuse{}; // end a new connection
    std::function<void(std::string const&)> log{};     // server console only
};

/* The room's state and the effects of the commands, game thread only. Commands apply
   once each, in id order; refusals of new connections and the closing countdown run from
   Tick(). */
class gPlatformRoom
{
public:
    using Clock = std::chrono::steady_clock;

    static std::size_t const MaxTextLength = 200;
    static int const DefaultClosingSeconds = 30;
    static int const MaxClosingSeconds = 600;

    explicit gPlatformRoom(gPlatformRoomEffects effects);

    // false if the command was seen before (id at or below the last applied one)
    bool Apply(gPlatformCommand const& command, Clock::time_point now);

    // counts a command someone else applies (moderation) as applied; false if seen before
    bool Acknowledge(std::int64_t id);

    // a connection logged in: while not open, it is ended at the next Tick()
    void UserLoggedIn(int userID);
    void UserLoggedOut(int userID);

    // refusals and countdown steps that are due
    void Tick(Clock::time_point now);

    gPlatformRoomState State() const noexcept { return state_; }
    std::int64_t LastAppliedId() const noexcept { return lastAppliedId_; }

    static char const* StateName(gPlatformRoomState state) noexcept;

    // control characters removed, at most MaxTextLength bytes (not splitting UTF-8)
    static std::string CleanText(std::string const& text);

private:
    void StopAnnouncing();
    void RestoreAnnouncing();

    gPlatformRoomEffects effects_;
    gPlatformRoomState state_{gPlatformRoomState::Open};
    std::int64_t lastAppliedId_{};
    bool savedAnnounce_{};
    std::string refusal_{};
    std::vector<int> toRefuse_{};

    // closing countdown
    bool counting_{};
    Clock::time_point closingAt_{};
    std::vector<int> steps_{}; // seconds left at which to speak, descending
};

// when to send the next heartbeat: at once when something changed (at most once per
// minGap), after a failure at the retry time, otherwise every interval
class gPlatformHeartbeatSchedule
{
public:
    using Clock = std::chrono::steady_clock;

    gPlatformHeartbeatSchedule(std::chrono::milliseconds interval, std::chrono::milliseconds minGap)
        : interval_{interval}, minGap_{minGap} {}

    void MarkChanged() noexcept { changed_ = true; }
    bool Due(Clock::time_point now) const noexcept;
    void Sent(Clock::time_point now) noexcept;
    void Failed(Clock::time_point retryAt) noexcept; // a fresh heartbeat is due then

private:
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds minGap_;
    bool sent_{};
    bool changed_{};
    bool retry_{};
    Clock::time_point last_{};
    Clock::time_point retryAt_{};
};

// what a heartbeat reports
struct gPlatformHeartbeat
{
    std::string bootId{};
    std::int64_t time{};
    gPlatformRoomState state{gPlatformRoomState::Open};
    ePlatformPlayerCounts counts{};
    std::string mode{};
    std::string version{};
    std::int64_t uptime{};
    std::int64_t lastCommandId{};
    std::string hostId{};
    std::string region{};
    int protocolMin{};    // the network protocol versions a new client may speak
    int protocolMax{};    // (sn_CurrentVersion(): it overlaps the client's or the login is refused)
    bool platformLogin{}; // PLATFORM_LOGIN: login tokens are accepted
    struct Moderation     // the moderation in force (contract section 5)
    {
        bool chatFilter{}; // PLATFORM_CHAT_FILTER: chat is checked with the platform
        bool sanctions{};  // PLATFORM_SANCTIONS: sanctions are fetched and enforced
        bool reports{};    // PLATFORM_REPORTS: /report goes to the platform
    } moderation{};
    std::vector<ePlatformRosterPlayer> players{}; // the connected humans (at most MaxRosterPlayers are sent)
};

struct gPlatformHttpConfig
{
    std::string apiUrl{};    // PLATFORM_API_URL
    std::string serverId{};  // PLATFORM_SERVER_ID
    std::string serverKey{}; // PLATFORM_SERVER_KEY, never logged
    std::string authority{"retrocycles"};
    bool loginEnabled{};     // PLATFORM_LOGIN
    bool privateRoom{};      // PLATFORM_PRIVATE: only connections with a room ticket stay
    std::string version{};   // reported in server_started

    // event delivery
    std::size_t batchSize{100};
    std::size_t queueCapacity{10000};
    std::chrono::milliseconds retryInitial{1000};
    std::chrono::milliseconds retryMax{60000};
    std::chrono::milliseconds flushTimeout{2000};

    // login keys
    std::chrono::milliseconds keysRetryInitial{5000};
    std::chrono::milliseconds keysRetryMax{60000};
    std::chrono::milliseconds keyRefreshInterval{60000}; // on unknown kid, at most this often
    std::chrono::milliseconds loginPendingTimeout{30000}; // a token waits this long for its player or keys

    // room heartbeats
    std::string mode{};   // PLATFORM_MODE
    std::string hostId{}; // PLATFORM_HOST_ID
    std::string region{}; // PLATFORM_REGION
    std::chrono::milliseconds heartbeatInterval{10000};
    std::chrono::milliseconds heartbeatMinGap{1000};   // prompt heartbeats at most this often
    std::chrono::milliseconds heartbeatRetry{2000};    // after a failed one
    std::chrono::milliseconds playerCountInterval{250}; // how often the game thread counts players

    // moderation (contract section 5); all off unless set (StartIfConfigured sets them from
    // the PLATFORM_* settings, whose defaults are on)
    bool chatFilter{};  // PLATFORM_CHAT_FILTER
    bool sanctions{};   // PLATFORM_SANCTIONS
    bool reports{};     // PLATFORM_REPORTS
    std::chrono::milliseconds chatTimeout{150};  // PLATFORM_CHAT_TIMEOUT
    std::chrono::seconds ipBanGrace{15};         // PLATFORM_IP_BAN_GRACE
    int accessAdmin{1};      // PLATFORM_ACCESS_ADMIN
    int accessModerator{2};  // PLATFORM_ACCESS_MODERATOR
    int accessStaff{19};     // PLATFORM_ACCESS_STAFF
    std::chrono::seconds chatRequestTimeout{3};
    std::chrono::seconds chatKeepWarm{60}; // an empty batch after this long without lines
    std::size_t chatBatchSize{100};
    int chatFailuresBeforeAsync{3};
    std::chrono::seconds chatOffAfterNotFound{600};
    std::chrono::seconds chatStatsInterval{60};
    std::chrono::milliseconds moderationRefresh{300000}; // GET .../moderation
    std::chrono::milliseconds moderationRetryInitial{5000};
    std::chrono::milliseconds moderationRetryMax{60000};

    // room commands (long poll)
    int commandWaitSeconds{25};
    std::chrono::milliseconds commandPollMinInterval{1000}; // between the starts of two polls
    std::chrono::milliseconds commandRetryInitial{1000};
    std::chrono::milliseconds commandRetryMax{60000};

    // injectable for tests; defaults: time(), console, the ePlatform login path,
    // ePlatform::CountPlayers() and RosterPlayers(), the running server (TALK_TO_MASTER, console, centre
    // messages, disconnects)
    std::function<std::int64_t()> clock{};
    std::function<void(std::string const&)> log{};
    std::function<gPlatformLoginOutcome(int userID, std::string const& gid, int accessLevel)> completeLoginAt{}; // accessLevel: from the token's roles
    std::function<gPlatformLoginOutcome(int userID, std::string const& gid)> completeLogin{}; // used when completeLoginAt is not set
    std::function<ePlatformPlayerCounts()> countPlayers{};
    std::function<std::vector<ePlatformRosterPlayer>()> rosterPlayers{}; // default: ePlatform::RosterPlayers()
    std::function<std::pair<int, int>()> protocolVersions{}; // default: sn_CurrentVersion() min, max
    std::function<std::string(int userID)> roomTicketOf{}; // default: sn_PeerTicket(), the signalling URL's ?ticket=
    gPlatformRoomEffects room{};
    gPlatformModerationEffects moderation{}; // empty members: the running server (report: the event stream)

    // moves (move_players); empty members: the running server (report: the event stream)
    gPlatformMoveEffects moves{};

    // a private room's end of match: empty members are the running server's; the window
    // follows PLATFORM_PRIVATE_REMATCH_SECONDS on the server's own instance
    gPlatformRematchEffects rematch{};
    std::chrono::seconds rematchWindow{30};
    std::chrono::milliseconds moveGrace{10000}; // a browser client told to move has this long to leave

    // /link (contract section 3, "Linking old global ids"); off unless set (StartIfConfigured: PLATFORM_LINK)
    bool linking{};
    std::chrono::seconds linkRequestTimeout{5};
    gPlatformLinkEffects link{}; // empty members: the running server (send: the link thread)
    // desktop players' /f, /msg to a friend, /r, /join and the player_message command (contract
    // section 3, "Friends in the rooms"); off unless set (StartIfConfigured: PLATFORM_SOCIAL)
    bool social{};
    std::chrono::seconds socialRequestTimeout{5};
    gPlatformSocialEffects socialEffects{}; // empty members: the running server (send: the social thread)
};

/* A private room (PLATFORM_PRIVATE 1) without a usable platform (PLATFORM_API_URL or
   PLATFORM_SERVER_ID empty): nothing can check a room ticket, so every connection is refused
   rather than the room silently taking everyone. Registered by gPlatformHttp::StartIfConfigured(). */
class gPlatformClosedRoom final : public ePlatform
{
public:
    // refuse: ends a new connection; empty: the running server's (a disconnect with the reason)
    explicit gPlatformClosedRoom(std::function<void(int userID, std::string const& reason)> refuse = {});

    // game thread; called by gPlatformHttp::OnUserLoginLogout() on a server (exposed for tests)
    void UserLoggedIn(int userID);
    void UserLoggedOut(int userID);

private:
    void OnDrain() override;

    std::function<void(int userID, std::string const& reason)> refuse_;
    std::vector<int> toRefuse_{};
};

class gPlatformHttp final : public ePlatform
{
public:
    // starts the worker thread and the key fetch
    gPlatformHttp(gPlatformHttpConfig config, std::unique_ptr<gPlatformHttpSender> sender);
    ~gPlatformHttp() override;

    /* The server's instance, driven by the PLATFORM_* settings. */

    // creates and registers it if PLATFORM_API_URL is set (never during playback)
    static void StartIfConfigured();

    // flushes briefly, stops and unregisters it
    static void Stop();

    // nCallbackLoginLogout: a connection came or went, its pending token is void; while
    // the room is not open, a new connection is refused
    static void OnUserLoginLogout();

    // game thread; called by OnUserLoginLogout() on a server (exposed for tests)
    void UserLoggedIn(int userID, std::string const& ip = std::string());
    void UserLoggedOut(int userID);

    // game thread: a client logged in (client_connected); called by OnUserLoginLogout() on a
    // server (exposed for tests). browser: it came over the WebRTC transport (our page);
    // protocol: the newest network protocol it speaks (16: 0.2.8.3, 17: 0.2.9, ...); ip: its address
    // without the port (the platform turns it into a rough location and does not keep it)
    void ClientConnected(int userID, bool browser, int protocol, std::string const& ip = std::string());

    // game thread: moderation's state (exposed for tests)
    gPlatformModeration& Moderation() noexcept { return moderation_; }

    // game thread: the waiting moves (exposed for tests)
    gPlatformMoves const& Moves() const noexcept { return moves_; }

    // game thread: /link (exposed for tests)
    gPlatformLink& Links() noexcept { return link_; }
    // game thread: friends and messages of desktop players (exposed for tests)
    gPlatformSocial& Social() noexcept { return social_; }

    // the access level a token's roles give (contract section 2, "Roles")
    static int AccessLevelForRoles(std::string const& roles, int admin, int moderator, int staff);

    // delivers what is queued for up to flushTimeout, then stops the workers; idempotent.
    // Game thread.
    void Shutdown();

    std::string const& BootId() const noexcept { return bootId_; }
    std::size_t KeyCount() const noexcept { return keys_.Size(); } // game thread
    gPlatformRoomState RoomState() const noexcept { return room_.State(); } // game thread
    std::int64_t LastCommandId() const noexcept { return room_.LastAppliedId(); } // game thread

    /* Serialization, exposed for tests. */
    static std::string ServerStartedJson(std::int64_t seq, std::int64_t time, std::string const& version);
    static std::string PlayerAuthenticatedJson(std::int64_t seq, std::int64_t time, std::string const& gid, std::string const& name, std::string const& method, int connection = 0);
    static std::string ClientConnectedJson(std::int64_t seq, std::int64_t time, int connection, bool browser, int protocol, std::string const& ip = std::string());
    static std::string RoundEndedJson(std::int64_t seq, std::int64_t time, ePlatformRoundResult const& result);
    static std::string MatchStartedJson(std::int64_t seq, std::int64_t time, ePlatformMatchStart const& start);
    static std::string DeathJson(std::int64_t seq, std::int64_t time, ePlatformDeath const& death);
    static std::string MatchEndedJson(std::int64_t seq, std::int64_t time, ePlatformMatchResult const& result);
    static std::string BatchJson(std::string const& bootId, std::vector<ePlatformEvent> const& events);
    static std::size_t const MaxRosterPlayers = 256; // players listed in one heartbeat
    static std::string HeartbeatJson(gPlatformHeartbeat const& heartbeat);
    static std::string EventsUrl(std::string const& apiUrl, std::string const& serverId);
    static std::string KeysUrl(std::string const& apiUrl);
    static std::string HeartbeatUrl(std::string const& apiUrl, std::string const& serverId);
    // cursor < 0: none yet
    static std::string CommandsUrl(std::string const& apiUrl, std::string const& serverId, std::int64_t cursor, int waitSeconds);
    static std::string ModerationUrl(std::string const& apiUrl, std::string const& serverId);
    static std::string ChatUrl(std::string const& apiUrl, std::string const& serverId);
    static std::string LinksUrl(std::string const& apiUrl, std::string const& serverId);
    static std::string SocialUrl(std::string const& apiUrl, std::string const& serverId);
    static std::string ReportJson(std::int64_t seq, std::int64_t time, std::string const& data);
    static std::string PlayersMovedJson(std::int64_t seq, std::int64_t time, std::string const& data);

    // the answer of a command poll: commands sorted by id (at most MaxCommandsPerPoll) and
    // the cursor for the next poll; false and a reason if it is unusable
    static std::size_t const MaxCommandsPerPoll = 100;
    static bool TryParseCommands(std::string const& body, std::vector<gPlatformCommand>& commands, std::int64_t& cursor, std::string& error);

    // game strings are Latin-1 or UTF-8; JSON wants UTF-8
    static std::string ToUtf8(std::string const& text);

private:
    void OnServerStarted() override;
    void OnPlayerAuthenticated(ePlayerNetID& player, tString const& gid, char const* method) override;
    void OnRoundEnded(ePlatformRoundResult const& result) override;
    void OnMatchStarted(ePlatformMatchStart const& start) override;
    void OnDeath(ePlatformDeath const& death) override;
    void OnMatchEnded(ePlatformMatchResult const& result) override;
    void OnLoginToken(int userID, tString const& token) override;
    bool OnChatSubmitted(ePlayerNetID& player, tColoredString const& line) override;
    void OnDrain() override;

    // game thread
    void Emit(char const* type, std::function<std::string(std::int64_t seq, std::int64_t time)> const& serialize);
    void Log(std::string const& message) const;
    void ProcessPendingLogins();
    void ProcessPendingTickets();
    bool TryRequestKeyRefresh();
    void OnKeysLoaded(gPlatformKeySet const& keys);
    void OnKeysFailed(std::string const& reason);
    void ForgetUser(int userID);
    void UpdateHeartbeat(std::chrono::steady_clock::time_point now);
    void ApplyCommands(std::vector<gPlatformCommand> const& commands);
    void ApplyModerationCommand(gPlatformCommand const& command);
    void ApplyMoveCommand(gPlatformCommand const& command);
    void HandOverChat();
    void RunKicks();

    // worker thread
    void Run();
    bool TryFetchKeys(gPlatformKeySet& keys, std::string& reason);
    void FetchModeration(std::chrono::steady_clock::time_point& next, std::chrono::milliseconds& retryDelay, bool& failing);
    void Deliver(std::vector<ePlatformEvent>& batch, std::string const& body);
    bool TrySendHeartbeat(std::string const& body);

    // command thread
    void RunCommands();
    bool WaitForCommandPoll(std::chrono::steady_clock::time_point until);

    // chat thread: the held lines to the platform, the verdicts back
    void RunChat();

    // link thread: /link requests to the platform, the answers back
    void RunLinks();
    // social thread: desktop players' social commands to the platform, the answers back
    void RunSocial();

    gPlatformHttpConfig config_;
    std::unique_ptr<gPlatformHttpSender> sender_;
    std::string bootId_;
    std::string eventsUrl_;
    std::string keysUrl_;
    std::string heartbeatUrl_;
    std::string moderationUrl_;
    std::string chatUrl_;
    std::string linksUrl_;
    std::string socialUrl_;
    std::chrono::steady_clock::time_point started_{};

    // game thread state
    std::int64_t seq_{};
    gPlatformRoom room_;
    gPlatformModeration moderation_;
    gPlatformMoves moves_;
    gPlatformLink link_;
    gPlatformSocial social_;
    gPlatformRematch rematch_;
    std::vector<std::pair<int, std::string>> kicks_{}; // connections to end at the next safe moment
    gPlatformHeartbeatSchedule heartbeatSchedule_;
    ePlatformPlayerCounts lastCounts_{};
    std::chrono::steady_clock::time_point nextCount_{};
    gPlatformKeySet keys_{};
    gPlatformJtiCache jtis_{};
    gPlatformJtiCache ticketJtis_{};
    std::vector<std::pair<int, std::string>> privateRefusals_{}; // PLATFORM_PRIVATE: connections without a good ticket
    bool keysPending_{true};
    std::chrono::steady_clock::time_point lastKeyRequest_{};
    bool shutDown_{};

    // a token waiting for its verification keys or its player
    struct PendingLogin
    {
        std::string token{};
        std::chrono::steady_clock::time_point since{};
        bool refreshRequested{};
        bool verified{};
        std::string gid{};
        std::string sub{};
        std::string roles{};
    };
    std::map<int, PendingLogin> pendingLogins_{};

    // a private room's connection whose ticket waits for the verification keys (as a token does)
    struct PendingTicket
    {
        std::string ticket{};
        std::chrono::steady_clock::time_point since{};
        bool refreshRequested{};
    };
    std::map<int, PendingTicket> pendingTickets_{};

    // login tokens received per connection, capped
    static int const MaxTokensPerConnection = 5;
    std::map<int, int> tokensPerUser_{};

    // shared with the worker
    ePlatformEventQueue outbound_;
    std::atomic<bool> keysWanted_{true};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> cancel_{false};
    std::atomic<std::chrono::steady_clock::rep> flushDeadline_{0};
    std::mutex doneMutex_{};
    std::condition_variable doneCondition_{};
    bool done_{};

    // the latest heartbeat, handed from the game thread to the worker (newer replaces older)
    std::mutex heartbeatMutex_{};
    std::string heartbeatBody_{};
    bool heartbeatQueued_{};

    // the command thread's sleep, cut short at shutdown
    std::atomic<bool> pollCancel_{false};
    std::mutex pollMutex_{};
    std::condition_variable pollCondition_{};

    // GET .../moderation wanted at once (a moderation_config command)
    std::atomic<bool> moderationWanted_{false};

    // held chat lines on their way to the chat thread
    std::mutex chatMutex_{};
    std::condition_variable chatCondition_{};
    std::vector<gPlatformOutboundLine> chatQueue_{};
    bool chatStop_{};
    std::atomic<bool> chatCancel_{false};

    // /link requests on their way to the link thread
    std::mutex linkMutex_{};
    std::condition_variable linkCondition_{};
    std::vector<gPlatformLinkRequest> linkQueue_{};
    bool linkStop_{};
    std::atomic<bool> linkCancel_{false};
    // social commands on their way to the social thread
    std::mutex socialMutex_{};
    std::condition_variable socialCondition_{};
    std::vector<gPlatformSocialRequest> socialQueue_{};
    bool socialStop_{};
    std::atomic<bool> socialCancel_{false};

    // worker thread state
    bool deliveryFailing_{};
    std::size_t deliveryFailures_{};
    bool heartbeatFailing_{};

    std::thread worker_{};
    std::thread commandWorker_{};
    std::thread chatWorker_{};
    std::thread linkWorker_{};
    std::thread socialWorker_{};
};

#endif // ArmageTron_GPLATFORMHTTP_H
