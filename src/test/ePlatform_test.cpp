#include "ePlatform.h"
#include "gPlatformEvents.h"

#include "doctest.h"
#include "ePlayer.h"
#include "nNetwork.h"

#include <atomic>
#include <thread>

// Tests for the platform seam: registry, null default, queues, hooks, login completion.

namespace
{
// an in-process platform that records what the hooks told it (the loopback of ePlatform)
class cRecordingPlatform final : public ePlatform
{
public:
    int serverStarted_{};
    std::vector<std::string> authenticated_{}; // "gid method"
    std::vector<ePlatformRoundResult> rounds_{};
    std::vector<ePlatformMatchStart> matchStarts_{};
    std::vector<ePlatformDeath> deaths_{};
    std::vector<ePlatformMatchResult> matchEnds_{};
    std::vector<std::string> order_{}; // event types in the order they came
    std::vector<std::pair<int, std::string>> tokens_{};
    int drains_{};

    void Post(std::function<void()> task) { PostToGameThread(std::move(task)); }

private:
    void OnServerStarted() override { ++serverStarted_; }
    void OnPlayerAuthenticated(ePlayerNetID&, tString const& gid, char const* method) override
    {
        authenticated_.push_back(std::string(static_cast<char const*>(gid)) + " " + method);
    }
    void OnRoundEnded(ePlatformRoundResult const& result) override
    {
        rounds_.push_back(result);
        order_.push_back("round_ended");
    }
    void OnMatchStarted(ePlatformMatchStart const& start) override
    {
        matchStarts_.push_back(start);
        order_.push_back("match_started");
    }
    void OnDeath(ePlatformDeath const& death) override
    {
        deaths_.push_back(death);
        order_.push_back("death");
    }
    void OnMatchEnded(ePlatformMatchResult const& result) override
    {
        matchEnds_.push_back(result);
        order_.push_back("match_ended");
    }
    void OnLoginToken(int userID, tString const& token) override { tokens_.emplace_back(userID, std::string(static_cast<char const*>(token))); }
    void OnDrain() override { ++drains_; }
};

// registers a platform for the duration of a scope
class cPlatformGuard final
{
public:
    explicit cPlatformGuard(ePlatform& platform) : platform_(platform) { ePlatform::Register(&platform_); }
    ~cPlatformGuard() { ePlatform::Unregister(&platform_); }

    cPlatformGuard(cPlatformGuard const&) = delete;
    cPlatformGuard& operator=(cPlatformGuard const&) = delete;

private:
    ePlatform& platform_;
};

gPlatformEvents::MatchTracker::Line HumanLine(char const* name, char const* gid, int team, bool alive, int score = 0)
{
    gPlatformEvents::MatchTracker::Line line;
    line.key = gid;
    line.player.name = name;
    line.player.gid = gid;
    line.player.team = name;
    line.alive = alive;
    line.score = score;
    line.teamKey = std::to_string(team);
    line.teamScore = score;
    return line;
}

// a bot in a team of its own
gPlatformEvents::MatchTracker::Line BotLine(char const* name, int team, bool alive, int score = 0)
{
    gPlatformEvents::MatchTracker::Line line;
    line.key = "#" + std::to_string(team) + ":" + name;
    line.player.name = name;
    line.player.team = name;
    line.player.bot = true;
    line.alive = alive;
    line.score = score;
    line.teamKey = std::to_string(team);
    line.teamScore = score;
    return line;
}

ePlatformEvent MakeEvent(std::string const& type)
{
    ePlatformEvent event;
    event.type = type;
    event.time = 1790000000;
    event.body = "{}";
    return event;
}

#ifdef KRAWALL_SERVER
// what the fake login result handler saw
struct cLoginSeen
{
    int calls{};
    std::string username{};
    std::string authority{};
    bool success{};
    tAccessLevel accessLevel{tAccessLevel_Default};
    nNetObject* user{};
    std::string method{};
};
cLoginSeen s_loginSeen;

void RecordLoginResult(nKrawall::nCheckResult const& result)
{
    ++s_loginSeen.calls;
    s_loginSeen.username = static_cast<char const*>(result.username);
    s_loginSeen.authority = static_cast<char const*>(result.authority);
    s_loginSeen.success = result.success;
    s_loginSeen.accessLevel = result.accessLevel;
    s_loginSeen.user = result.user;
    s_loginSeen.method = ePlatform::CurrentLoginMethod();
}
#endif
} // namespace

TEST_SUITE("ePlatformEventQueue")
{
    TEST_CASE("Events Come Out In Order, In Batches")
    {
        ePlatformEventQueue queue(10);
        queue.Push(MakeEvent("a"));
        queue.Push(MakeEvent("b"));
        queue.Push(MakeEvent("c"));

        std::vector<ePlatformEvent> out;
        CHECK(2 == queue.PopBatch(out, 2));
        CHECK(1 == queue.Size());
        CHECK(1 == queue.PopBatch(out, 2));
        CHECK(0 == queue.PopBatch(out, 2));
        REQUIRE(3 == out.size());
        CHECK("a" == out[0].type);
        CHECK("b" == out[1].type);
        CHECK("c" == out[2].type);
    }

    TEST_CASE("A Full Queue Drops The Oldest And Counts It")
    {
        ePlatformEventQueue queue(2);
        queue.Push(MakeEvent("a"));
        queue.Push(MakeEvent("b"));
        queue.Push(MakeEvent("c"));

        CHECK(1 == queue.TakeDropped());
        CHECK(0 == queue.TakeDropped());

        std::vector<ePlatformEvent> out;
        queue.PopBatch(out, 10);
        REQUIRE(2 == out.size());
        CHECK("b" == out[0].type);
        CHECK("c" == out[1].type);
    }

    TEST_CASE("Waiting Ends On Timeout, On Events And On Wake")
    {
        ePlatformEventQueue queue;
        auto const soon = [] { return std::chrono::steady_clock::now() + std::chrono::milliseconds(20); };

        CHECK(!queue.WaitUntil(soon()));

        queue.Push(MakeEvent("a"));
        CHECK(queue.WaitUntil(soon()));

        std::vector<ePlatformEvent> out;
        queue.PopBatch(out, 10);
        queue.Wake();
        CHECK(queue.WaitUntil(soon()));
        // the wake is consumed
        CHECK(!queue.WaitUntil(soon()));
    }

    TEST_CASE("A Consumer Thread Receives Everything A Producer Pushed")
    {
        ePlatformEventQueue queue(100000);
        int const count = 2000;
        std::vector<ePlatformEvent> received;

        std::thread consumer([&] {
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (static_cast<int>(received.size()) < count && std::chrono::steady_clock::now() < deadline)
            {
                queue.WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
                queue.PopBatch(received, 64);
            }
        });

        for (int i = 0; i < count; ++i)
            queue.Push(MakeEvent(std::to_string(i)));
        consumer.join();

        REQUIRE(count == static_cast<int>(received.size()));
        bool ordered = true;
        for (int i = 0; i < count; ++i)
            ordered = ordered && received[i].type == std::to_string(i);
        CHECK(ordered);
        CHECK(0 == queue.TakeDropped());
    }
}

TEST_SUITE("ePlatform")
{
    TEST_CASE("Without A Platform The Hooks Change Nothing")
    {
        REQUIRE(nullptr == ePlatform::Instance());

        // none of these may touch anything; the stock game runs them every frame or round
        ePlatform::ServerStarted();
        ePlatform::RoundEnded(ePlatformRoundResult{});
        ePlatform::PlayerAuthenticated(nullptr, tString("x@y"));
        ePlatform::Drain();
        gPlatformEvents::RoundEnded(0);

        CHECK(nullptr == ePlatform::Instance());
        CHECK(std::string("krawall") == ePlatform::CurrentLoginMethod());
    }

    TEST_CASE("Registered Platforms Receive The Hooks")
    {
        cRecordingPlatform platform;
        {
            cPlatformGuard guard(platform);
            CHECK(&platform == ePlatform::Instance());

            ePlatform::ServerStarted();
            ePlatform::ServerStarted(); // hosting again does not start the server again

            ePlatformRoundResult result;
            result.round = 3;
            ePlatformRoundPlayer line;
            line.name = "Tester";
            result.players.push_back(line);
            ePlatform::RoundEnded(result);

            platform.HandleLoginToken(4, tString("a.b.c"));

            CHECK(1 == platform.serverStarted_);
            REQUIRE(1 == platform.rounds_.size());
            CHECK(3 == platform.rounds_[0].round);
            REQUIRE(1 == platform.rounds_[0].players.size());
            CHECK("Tester" == platform.rounds_[0].players[0].name);
            REQUIRE(1 == platform.tokens_.size());
            CHECK(4 == platform.tokens_[0].first);
            CHECK("a.b.c" == platform.tokens_[0].second);
        }
        CHECK(nullptr == ePlatform::Instance());
    }

    TEST_CASE("Unauthenticated Players Are Not Reported As Authenticated")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);

        auto player = tRefPtr<ePlayerNetID>::Make();
        ePlatform::PlayerAuthenticated(player, tString("g-7f3a9c01@retrocycles"));

#ifdef KRAWALL_SERVER
        // Authenticate() refuses (bans) silently; the hook checks the outcome
        CHECK(platform.authenticated_.empty());
#else
        CHECK(1 == platform.authenticated_.size());
#endif
    }

    TEST_CASE("Tasks From Other Threads Run On Drain, In Order")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);

        std::vector<int> ran;
        std::thread worker([&] {
            for (int i = 0; i < 3; ++i)
                platform.Post([&ran, i] { ran.push_back(i); });
        });
        worker.join();

        CHECK(ran.empty());
        ePlatform::Drain();
        REQUIRE(3 == ran.size());
        CHECK(0 == ran[0]);
        CHECK(2 == ran[2]);
        CHECK(1 == platform.drains_);

        ePlatform::Drain();
        CHECK(3 == ran.size());
    }

    TEST_CASE("Player Counts Tell Humans From Bots")
    {
        // a stand-in for an AI player (gAIPlayer answers IsHuman() the same way)
        class cBot final : public ePlayerNetID
        {
        public:
            bool IsHuman() const override { return false; }
        };

        ePlatformPlayerCounts const before = ePlatform::CountPlayers();
        {
            auto human = tRefPtr<ePlayerNetID>::Make();
            tRefPtr<ePlayerNetID> bot1(new cBot);
            tRefPtr<ePlayerNetID> bot2(new cBot);

            ePlatformPlayerCounts const counts = ePlatform::CountPlayers();
            CHECK(before.humans + 1 == counts.humans);
            CHECK(before.bots + 2 == counts.bots);
            CHECK(sn_MaxUsers() == counts.capacity);
            CHECK(sn_NumUsers() == counts.clients); // players are not connections
        }
        CHECK(before == ePlatform::CountPlayers());
    }

    TEST_CASE("The Roster Lists Humans Only")
    {
        class cBot final : public ePlayerNetID
        {
        public:
            bool IsHuman() const override { return false; }
        };

        std::size_t const before = ePlatform::RosterPlayers().size();
        {
            auto human = tRefPtr<ePlayerNetID>::Make();
            tRefPtr<ePlayerNetID> bot(new cBot);

            std::vector<ePlatformRosterPlayer> const roster = ePlatform::RosterPlayers();
            REQUIRE(before + 1 == roster.size());
            ePlatformRosterPlayer const& entry = roster.back();
            CHECK(static_cast<int>(human->ID()) == entry.netId);
            CHECK(entry.gid.empty()); // not authenticated
            CHECK(0 == entry.ping);   // the server's own player: no connection to measure
            CHECK(entry.spectator);   // in no team
        }
        CHECK(before == ePlatform::RosterPlayers().size());
    }

    TEST_CASE("No Login Candidates Without A Remote Connection")
    {
        auto player = tRefPtr<ePlayerNetID>::Make();
        CHECK(nullptr == ePlatform::FindLoginCandidate(0));
        CHECK(nullptr == ePlatform::FindLoginCandidate(-1));
        CHECK(nullptr == ePlatform::FindLoginCandidate(MAXCLIENTS));
    }

    TEST_CASE("A Token Login Replaces The Same Account On Another Connection")
    {
        CHECK(ePlatform::LoginReplacesOld(2, 1, "token"));
        CHECK(ePlatform::LoginReplacesOld(1, MAXCLIENTS, "token"));

        // the same connection, the server, nobody, password logins: refused as before
        CHECK(!ePlatform::LoginReplacesOld(1, 1, "token"));
        CHECK(!ePlatform::LoginReplacesOld(2, 0, "token"));
        CHECK(!ePlatform::LoginReplacesOld(2, -1, "token"));
        CHECK(!ePlatform::LoginReplacesOld(2, MAXCLIENTS + 1, "token"));
        CHECK(!ePlatform::LoginReplacesOld(2, 1, "krawall"));
        CHECK(!ePlatform::LoginReplacesOld(2, 1, nullptr));
    }

#ifdef KRAWALL_SERVER
    TEST_CASE("Token Logins Complete Through The Regular Login Result Path")
    {
        s_loginSeen = cLoginSeen{};
        ePlatform::SetLoginResultCallback(&RecordLoginResult);
        auto player = tRefPtr<ePlayerNetID>::Make();

        GIVEN("a verified global id")
        {
            bool const authenticated = ePlatform::TryCompleteLogin(player, tString("g-7f3a9c01@retrocycles"));

            THEN("the handler gets a successful result split into user and authority")
            {
                CHECK(1 == s_loginSeen.calls);
                CHECK("g-7f3a9c01" == s_loginSeen.username);
                CHECK("retrocycles" == s_loginSeen.authority);
                CHECK(s_loginSeen.success);
                CHECK(tAccessLevel_Authenticated == s_loginSeen.accessLevel);
                CHECK(static_cast<nNetObject*>(player) == s_loginSeen.user);
            }
            AND_THEN("the login is marked as a token login only while it completes")
            {
                CHECK("token" == s_loginSeen.method);
                CHECK(std::string("krawall") == ePlatform::CurrentLoginMethod());
            }
            AND_THEN("the result reflects the player's state, which this fake handler did not change")
            {
                CHECK(!authenticated);
            }
        }

        GIVEN("a global id whose authority is split at the last @")
        {
            ePlatform::TryCompleteLogin(player, tString("a@b@retrocycles"));
            CHECK("a@b" == s_loginSeen.username);
            CHECK("retrocycles" == s_loginSeen.authority);
        }

        GIVEN("malformed global ids")
        {
            CHECK(!ePlatform::TryCompleteLogin(player, tString("nohandle")));
            CHECK(!ePlatform::TryCompleteLogin(player, tString("@retrocycles")));
            CHECK(!ePlatform::TryCompleteLogin(player, tString("handle@")));
            CHECK(!ePlatform::TryCompleteLogin(nullptr, tString("a@b")));
            CHECK(0 == s_loginSeen.calls);
        }

        ePlatform::SetLoginResultCallback(nullptr);
    }
#endif
}

TEST_SUITE("gPlatformEvents")
{
    TEST_CASE("A Match Is Reported Once, In Order, With Numbered Rounds")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);
        gPlatformEvents::MatchTracker tracker;

        tracker.MatchStarted(10, 30, 100000);
        // gGame::StartNewMatchNow() runs again when the first round starts: the same match,
        // and the limits it really is played to (the game switched its settings meanwhile)
        tracker.MatchStarted(3, 10, 100000);
        CHECK(platform.order_.empty());

        auto const lines = std::vector<gPlatformEvents::MatchTracker::Line>{
            HumanLine("Tester", "g-1@rc", 0, true), BotLine("Bot 1", 1, false), BotLine("Bot 2", 2, false)};

        tracker.Death("#1:Bot 1", lines[1].player, 10.0, &lines[0].player, "frag");
        tracker.RoundEnded(0, 42, lines);
        tracker.RoundEnded(1, 30, lines);
        tracker.MatchDecided("Tester");
        tracker.RoundEnded(-100, 25, lines); // gGame::rounds was reset when the winner was declared

        // the next match: reported with its first round
        tracker.MatchStarted(3, 10, 100000);
        tracker.RoundEnded(0, 20, lines);

        std::vector<std::string> const expected{"match_started", "death", "round_ended", "round_ended", "round_ended",
                                                "match_ended", "match_started", "round_ended"};
        CHECK(expected == platform.order_);

        REQUIRE(2 == platform.matchStarts_.size());
        CHECK(1 == platform.matchStarts_[0].match);
        CHECK(3 == platform.matchStarts_[0].limitRounds);
        CHECK(10 == platform.matchStarts_[0].limitMinutes);
        CHECK(2 == platform.matchStarts_[1].match);

        REQUIRE(4 == platform.rounds_.size());
        CHECK(1 == platform.rounds_[0].match);
        CHECK(1 == platform.rounds_[0].round);
        CHECK(42 == platform.rounds_[0].seconds);
        CHECK(3 == platform.rounds_[2].round);
        CHECK(2 == platform.rounds_[3].match);
        CHECK(1 == platform.rounds_[3].round);

        REQUIRE(1 == platform.deaths_.size());
        CHECK(1 == platform.deaths_[0].round);
        CHECK("frag" == platform.deaths_[0].cause);
        CHECK(platform.deaths_[0].hasKiller);
        CHECK("Tester" == platform.deaths_[0].killer.name);

        REQUIRE(1 == platform.matchEnds_.size());
        auto const& end = platform.matchEnds_[0];
        CHECK(1 == end.match);
        CHECK(end.completed);
        CHECK("Tester" == end.winner);
        CHECK(3 == end.rounds);
        REQUIRE(3 == end.players.size());
        CHECK("g-1@rc" == end.players[0].player.gid);
        CHECK(3 == end.players[0].rounds);
        CHECK(!end.players[0].leftEarly);
    }

    TEST_CASE("In A Free For All Every Player Places On Their Own")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);
        gPlatformEvents::MatchTracker tracker;
        tracker.MatchStarted(10, 10, 100000);

        // scores: the human 12, the bots 5 and 5 (a tie), 1. The MIN_PLAYERS bots share one
        // AI team ("Team blue"); with TEAM_MAX_PLAYERS 1 the game side gives every player
        // its own team key and score for the placement
        auto human = HumanLine("Tester", "g-1@rc", 0, false, 12);
        auto bot1 = BotLine("Bot 1", 1, true, 5);
        auto bot2 = BotLine("Bot 2", 2, false, 5);
        auto bot3 = BotLine("Bot 3", 3, false, 1);
        for (auto* bot : {&bot1, &bot2, &bot3})
            bot->player.team = "Team blue";
        tracker.Death(human.key, human.player, 5.0, nullptr, "suicide");
        tracker.Death(bot3.key, bot3.player, 6.0, nullptr, "suicide");
        tracker.Death(bot2.key, bot2.player, 9.0, &bot1.player, "frag");
        tracker.MatchDecided("Tester");
        tracker.RoundEnded(-100, 20, {human, bot1, bot2, bot3});

        REQUIRE(1 == platform.rounds_.size());
        auto const& round = platform.rounds_[0];
        // the survivor first, then the later a death the better
        CHECK(1 == round.players[1].place); // Bot 1, alive
        CHECK(2 == round.players[2].place); // Bot 2, died last
        CHECK(3 == round.players[3].place); // Bot 3
        CHECK(4 == round.players[0].place); // the human, first to die
        CHECK(round.players[1].bot);
        CHECK("Team blue" == round.players[1].team);
        CHECK(!round.players[0].bot);

        REQUIRE(1 == platform.matchEnds_.size());
        auto const& end = platform.matchEnds_[0];
        CHECK(1 == end.players[0].place); // by team score: 12
        CHECK(2 == end.players[1].place); // 5 and 5 share
        CHECK(2 == end.players[2].place);
        CHECK(4 == end.players[3].place);
    }

    TEST_CASE("Round Places: Nobody Alive, Ties, Unknown Deaths")
    {
        auto a = BotLine("A", 1, false);
        auto b = BotLine("B", 2, false);
        auto c = BotLine("C", 3, false);
        auto d = BotLine("D", 4, false);
        std::map<std::string, double> const deaths{{a.key, 3.0}, {b.key, 7.5}, {c.key, 7.5}};
        std::vector<int> const places = gPlatformEvents::MatchTracker::RoundPlaces({a, b, c, d}, deaths);
        std::vector<int> const expected{3, 1, 1, 4}; // B and C died last together; D's death is unknown
        CHECK(expected == places);

        auto e = BotLine("E", 5, true);
        auto f = BotLine("F", 6, true);
        std::vector<int> const survivors = gPlatformEvents::MatchTracker::RoundPlaces({e, f, a}, deaths);
        std::vector<int> const both{1, 1, 3};
        CHECK(both == survivors);
    }

    TEST_CASE("A Match Restarted Before It Was Decided Ends As Aborted")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);
        gPlatformEvents::MatchTracker tracker;
        tracker.MatchStarted(10, 10, 100000);

        auto human = HumanLine("Tester", "g-1@rc", 0, true);
        auto leaver = HumanLine("Leaver", "g-2@rc", 9, true);
        tracker.RoundEnded(0, 20, {human, leaver});
        tracker.RoundEnded(1, 20, {human});

        SUBCASE("START_NEW_MATCH: gGame::rounds is reset, the round ends")
        {
            tracker.RoundEnded(-100, 20, {human});
            REQUIRE(1 == platform.matchEnds_.size());
            CHECK(!platform.matchEnds_[0].completed);
            CHECK(platform.matchEnds_[0].winner.empty());
            CHECK(3 == platform.matchEnds_[0].rounds);
        }
        SUBCASE("a new game (everybody left): the next match starts")
        {
            tracker.MatchStarted(10, 10, 100000);
            tracker.RoundEnded(0, 20, {human});
            REQUIRE(1 == platform.matchEnds_.size());
            CHECK(!platform.matchEnds_[0].completed);
            CHECK(2 == platform.matchEnds_[0].rounds);
            REQUIRE(2 == platform.matchStarts_.size());
            CHECK(2 == platform.matchStarts_[1].match);
        }

        auto const& end = platform.matchEnds_[0];
        REQUIRE(2 == end.players.size());
        CHECK("Leaver" == end.players[1].player.name);
        CHECK(end.players[1].leftEarly);
        CHECK(0 == end.players[1].place);
        CHECK(1 == end.players[1].rounds);
        CHECK(!end.players[0].leftEarly);
    }

    TEST_CASE("Deaths Outside A Match Are Not Reported")
    {
        cRecordingPlatform platform;
        cPlatformGuard guard(platform);
        gPlatformEvents::MatchTracker tracker;
        auto bot = BotLine("Bot 1", 1, false);

        tracker.Death(bot.key, bot.player, 1.0, nullptr, "other");
        tracker.RoundEnded(0, 10, {bot});
        CHECK(platform.order_.empty());

        tracker.MatchStarted(1, 10, 100000);
        tracker.MatchDecided("Bot 1");
        tracker.RoundEnded(-100, 10, {bot});
        // between the last round and the next match
        tracker.Death(bot.key, bot.player, 2.0, nullptr, "other");
        std::vector<std::string> const expected{"match_started", "round_ended", "match_ended"};
        CHECK(expected == platform.order_);
    }

    TEST_CASE("Without A Platform The Hooks Do Nothing")
    {
        CHECK(nullptr == ePlatform::Instance());
        gPlatformEvents::MatchStarted();
        gPlatformEvents::MatchDecided();
        gPlatformEvents::RoundEnded(0);
        gPlatformEvents::DeathCredited(nullptr, nullptr);
        gPlatformEvents::CycleDied(nullptr);
    }

    TEST_CASE("Round Results List The Players In Teams")
    {
        auto spectator = tRefPtr<ePlayerNetID>::Make();

        ePlatformRoundResult const result = gPlatformEvents::CollectRoundResult(7);

        CHECK(7 == result.round);
        // a fresh player has no team yet, so it did not play
        CHECK(result.players.empty());
    }
}
