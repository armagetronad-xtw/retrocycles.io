#include <nlohmann/json.hpp>

#include "gPlatformModeration.h"
#include "gPlatformHttp.h"

#include "doctest.h"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

// Tests for moderation (platform-contract/README.md section 5): the word filter against the
// contract's vectors, the chat hold, sanctions, reports, and the HTTP side against a fake
// platform. Only built with --enable-platform.

namespace
{
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

nlohmann::json LoadFilterVectors()
{
    std::ifstream file(std::string(PLATFORM_CONTRACT_DIR) + "/test-vectors/chat-filter-v0.json");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return nlohmann::json::parse(text.str());
}

gPlatformChatFilter::Lists VectorLists()
{
    nlohmann::json const vectors = LoadFilterVectors();
    gPlatformChatFilter::Lists lists;
    lists.version = vectors["filter"]["version"].get<std::string>();
    lists.mask = vectors["filter"]["mask"].get<std::vector<std::string>>();
    lists.block = vectors["filter"]["block"].get<std::vector<std::string>>();
    lists.allow = vectors["filter"]["allow"].get<std::vector<std::string>>();
    return lists;
}

gPlatformChatPlayer Player(int playerId, int conn, std::string name, std::string gid = std::string(), std::string ip = "203.0.113.7")
{
    gPlatformChatPlayer player;
    player.playerId = playerId;
    player.conn = conn;
    player.name = std::move(name);
    player.gid = std::move(gid);
    player.ip = std::move(ip);
    return player;
}

std::int64_t const s_unixNow = 1790000000;

// a JSON string member as a std::string (doctest cannot print JSON next to a string literal)
std::string S(nlohmann::json const& json)
{
    return json.is_string() ? json.get<std::string>() : json.dump();
}

// a server as moderation sees it, recording what moderation does
class cWorld
{
public:
    std::vector<gPlatformChatPlayer> players{};
    std::vector<std::pair<int, std::string>> told{};
    std::vector<std::pair<int, std::string>> kicked{};
    std::vector<std::pair<std::string, std::string>> renamed{};
    std::vector<std::string> reports{};
    std::vector<std::string> log{};
    std::vector<std::string> delivered{};
    std::int64_t now{s_unixNow};

    gPlatformModerationEffects Effects()
    {
        gPlatformModerationEffects effects;
        effects.players = [this] { return players; };
        effects.tell = [this](int conn, std::string const& text) { told.emplace_back(conn, text); };
        effects.kick = [this](int conn, std::string const& reason) { kicked.emplace_back(conn, reason); };
        effects.rename = [this](std::string const& gid, std::string const& name) {
            renamed.emplace_back(gid, name);
            return true;
        };
        effects.find = [this](std::string const& name, int, gPlatformChatPlayer& found) {
            for (auto const& player : players)
                if (player.name == name)
                {
                    found = player;
                    return true;
                }
            return false;
        };
        effects.report = [this](std::string const& data) { reports.push_back(data); };
        effects.log = [this](std::string const& line) { log.push_back(line); };
        effects.clock = [this] { return now; };
        return effects;
    }

    gPlatformModeration::Deliver Deliver(std::string const& who)
    {
        return [this, who](std::string const& line) { delivered.push_back(who + ": " + line); };
    }

    bool Told(int conn, std::string const& part) const
    {
        for (auto const& line : told)
            if (line.first == conn && line.second.find(part) != std::string::npos)
                return true;
        return false;
    }
};

gPlatformModeration::Config ModerationConfig(bool chatFilter = true)
{
    gPlatformModeration::Config config;
    config.chatFilter = chatFilter;
    config.sanctions = true;
    config.reports = true;
    config.timeout = milliseconds(150);
    config.ipBanGrace = seconds(15);
    config.serverId = "dev-local-1";
    config.bootId = "boot-1";
    return config;
}

gPlatformSanction Sanction(std::string const& json)
{
    gPlatformSanction sanction;
    std::string error;
    REQUIRE_MESSAGE(gPlatformSanction::TryParse(json, sanction, error), error);
    return sanction;
}

gPlatformChatVerdict Verdict(std::uint64_t id, std::string action, std::string text = std::string(), std::string notice = std::string())
{
    gPlatformChatVerdict verdict;
    verdict.id = id;
    verdict.action = std::move(action);
    verdict.text = std::move(text);
    verdict.notice = std::move(notice);
    return verdict;
}
} // namespace

TEST_SUITE("Platform Moderation")
{
    TEST_CASE("Chat Filter Contract Vectors")
    {
        nlohmann::json const vectors = LoadFilterVectors();
        gPlatformChatFilter const filter(VectorLists());
        CHECK(filter.Version() == "test-vectors-v0");

        for (auto const& item : vectors["cases"])
        {
            std::string const text = item["text"].get<std::string>();
            bool masked = false;
            bool blocked = false;
            std::string const out = filter.Apply(text, &masked, &blocked);
            CAPTURE(item["name"].get<std::string>());
            CHECK(out == item["expected"].get<std::string>());
            CHECK(masked == item["masked"].get<bool>());
            CHECK(blocked == item["blocked"].get<bool>());
        }

        // what a server does with each case when the local filter decides: drop the blocked ones
        // (the sender is told), deliver the rest as expected
        for (auto const& item : vectors["cases"])
        {
            cWorld world;
            gPlatformModeration moderation(ModerationConfig(), world.Effects());
            gPlatformChatFilter::Lists const lists = VectorLists();
            moderation.ApplyState(&lists, {}, 0);
            moderation.SetMode(gPlatformModeration::Mode::Async, Clock::now());
            CAPTURE(item["name"].get<std::string>());
            REQUIRE(moderation.Submit(Player(1, 1, "alice"), item["text"].get<std::string>(), world.Deliver("alice"), Clock::now()));
            if (item["blocked"].get<bool>())
            {
                CHECK(world.delivered.empty());
                CHECK(world.Told(1, gPlatformChatFilter::DefaultNotice));
            }
            else
            {
                REQUIRE(1 == world.delivered.size());
                CHECK(world.delivered[0] == "alice: " + item["expected"].get<std::string>());
                CHECK(world.told.empty());
            }
        }
    }

    TEST_CASE("Chat Filter Built In")
    {
        gPlatformChatFilter const filter;
        CHECK(filter.Apply("what the fuck") == "what the ****");
        CHECK(filter.Apply("f u c k off") == "* * * * off");
        CHECK(filter.Apply("sh1t happens") == "**** happens");
        CHECK(filter.Apply("kill him, frag them, he died") == "kill him, frag them, he died");
        bool masked = false;
        bool blocked = false;
        CHECK(filter.Apply("kys", &masked, &blocked) == "***");
        CHECK(blocked);
        CHECK(filter.Apply("go kill yourself") == "go **** ********");
        CHECK(filter.Apply("what the fuck", &masked, &blocked) == "what the ****");
        CHECK(masked);
        CHECK(!blocked);
        CHECK(filter.Notice() == gPlatformChatFilter::DefaultNotice);
        CHECK(filter.Apply("") == "");

        // Latin-1 text (the desktop client's encoding) keeps its bytes
        std::string const latin1 = "caf\xe9 fuck";
        CHECK(filter.Apply(latin1) == "caf\xe9 ****");
    }

    TEST_CASE("Chat Filter Names")
    {
        // the contract's lists: mask darn, heck*, *bloop*, dump; block zorp, frak off
        gPlatformChatFilter const filter(VectorLists());

        // a listed word anywhere, in any spelling the filter reads
        CHECK(!filter.NameAllowed("HECK YEAH"));
        CHECK(!filter.NameAllowed("H3CK"));
        CHECK(!filter.NameAllowed("0xff0000Darn"));
        CHECK(!filter.NameAllowed("Zorp"));
        CHECK(!filter.NameAllowed("frak off"));

        // the letters alone, as the platform reads names: separators do not hide a word
        CHECK(!filter.NameAllowed("z_o_r_p"));
        CHECK(!filter.NameAllowed("Da.Rn"));
        CHECK(!filter.NameAllowed("h.e.c.k"));

        // ordinary names and game talk
        CHECK(filter.NameAllowed("WildCobra"));
        CHECK(filter.NameAllowed("Player 7a44"));
        CHECK(filter.NameAllowed("Checkers"));
        CHECK(filter.NameAllowed("Killer"));
        CHECK(filter.NameAllowed(""));

        gPlatformChatFilter const builtIn;
        CHECK(!builtIn.NameAllowed("SEXY BITCH"));
        CHECK(builtIn.NameAllowed("WildCobra"));
    }

    TEST_CASE("Parsing Chat Lines")
    {
        gPlatformModeration::ParsedLine parsed;

        REQUIRE(gPlatformModeration::ParseLine("hello all", parsed));
        CHECK(parsed.channel == "public");
        CHECK(parsed.prefix.empty());
        CHECK(parsed.text == "hello all");

        REQUIRE(gPlatformModeration::ParseLine("/me waves", parsed));
        CHECK(parsed.channel == "me");
        CHECK(parsed.prefix == "/me ");
        CHECK(parsed.text == "waves");

        REQUIRE(gPlatformModeration::ParseLine("/MSG  Bob   hi there", parsed));
        CHECK(parsed.channel == "private");
        CHECK(parsed.to == "Bob");
        CHECK(parsed.prefix == "/MSG  Bob   ");
        CHECK(parsed.text == "hi there");

        REQUIRE(gPlatformModeration::ParseLine("/team go left", parsed));
        CHECK(parsed.channel == "team");

        CHECK(!gPlatformModeration::ParseLine("/vote kick bob", parsed));
        CHECK(parsed.command == "/vote");
        CHECK(!gPlatformModeration::ParseLine("/msg bob", parsed));
        CHECK(!gPlatformModeration::ParseLine("/me", parsed));
        CHECK(!gPlatformModeration::ParseLine("   ", parsed));
        CHECK(!gPlatformModeration::ParseLine("/report bob spam", parsed));
        CHECK(parsed.command == "/report");
        CHECK(parsed.text == "bob spam");
    }

    TEST_CASE("Held Lines Keep Their Order")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 1, "alice", "alice@retrocycles");
        gPlatformChatPlayer const bob = Player(2, 2, "bob", "bob@retrocycles");

        CHECK(moderation.Submit(alice, "first", world.Deliver("alice"), t0));
        CHECK(moderation.Submit(alice, "/team second", world.Deliver("alice"), t0));
        CHECK(moderation.Submit(alice, "/players", world.Deliver("alice"), t0)); // waits behind them
        CHECK(moderation.Submit(bob, "hi", world.Deliver("bob"), t0));
        CHECK(moderation.Submit(bob, "/players", world.Deliver("bob"), t0)); // behind bob's line
        CHECK(!moderation.Submit(Player(3, 3, "carol"), "/players", world.Deliver("carol"), t0)); // nothing of carol's waits: as usual
        CHECK(world.delivered.empty());
        CHECK(3 == moderation.Held()); // the three checked lines

        std::vector<gPlatformOutboundLine> const out = moderation.TakeOutbound();
        REQUIRE(3 == out.size());
        CHECK(out[0].text == "first");
        CHECK(out[1].channel == "team");
        CHECK(out[1].text == "second");
        CHECK(out[2].player.name == "bob");
        CHECK(moderation.TakeOutbound().empty());

        // the second line's verdict first: it still waits for the first
        moderation.ApplyVerdicts({Verdict(out[1].id, "allow"), Verdict(out[2].id, "allow")}, t0 + milliseconds(10));
        REQUIRE(2 == world.delivered.size());
        CHECK(world.delivered[0] == "bob: hi");
        CHECK(world.delivered[1] == "bob: /players");

        moderation.ApplyVerdicts({Verdict(out[0].id, "allow")}, t0 + milliseconds(20));
        REQUIRE(5 == world.delivered.size());
        CHECK(world.delivered[2] == "alice: first");
        CHECK(world.delivered[3] == "alice: /team second");
        CHECK(world.delivered[4] == "alice: /players");
        CHECK(0 == moderation.Held());

        CHECK(3 == moderation.CurrentStats().checked);
        CHECK(0 == moderation.CurrentStats().timeouts);
        CHECK(moderation.CurrentStats().holdMs.back() == doctest::Approx(20.0));
    }

    TEST_CASE("Verdicts Replace And Drop")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 3, "alice", "alice@retrocycles");

        moderation.Submit(alice, "/msg bob you idiot", world.Deliver("alice"), t0);
        moderation.Submit(alice, "something awful", world.Deliver("alice"), t0);
        std::vector<gPlatformOutboundLine> const out = moderation.TakeOutbound();
        REQUIRE(2 == out.size());
        CHECK(out[0].text == "you idiot");
        CHECK(out[0].to == "bob");

        moderation.ApplyVerdicts({Verdict(out[0].id, "replace", "you *****"), Verdict(out[1].id, "drop", std::string(), "That message was not sent.")}, t0);
        REQUIRE(1 == world.delivered.size());
        CHECK(world.delivered[0] == "alice: /msg bob you *****");
        CHECK(world.Told(3, "That message was not sent."));
    }

    TEST_CASE("Timeout Falls Back To The Local Filter")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        moderation.ApplyState(nullptr, {}, 0);
        gPlatformChatFilter::Lists const lists = VectorLists();
        moderation.ApplyState(&lists, {}, 0);
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 1, "alice");

        moderation.Submit(alice, "darn it", world.Deliver("alice"), t0);
        std::vector<gPlatformOutboundLine> const out = moderation.TakeOutbound();
        REQUIRE(1 == out.size());

        moderation.Tick(t0 + milliseconds(149));
        CHECK(world.delivered.empty());
        moderation.Tick(t0 + milliseconds(150));
        REQUIRE(1 == world.delivered.size());
        CHECK(world.delivered[0] == "alice: **** it"); // delivered, masked
        CHECK(1 == moderation.CurrentStats().timeouts);

        // the verdict that comes later changes nothing players see; it is logged and kept with
        // the line for reports (the platform counts the violation and its mute comes with it)
        moderation.ApplyVerdicts({Verdict(out[0].id, "drop")}, t0 + milliseconds(300));
        CHECK(1 == world.delivered.size());
        CHECK(1 == moderation.CurrentStats().late);
        CHECK(1 == moderation.CurrentStats().lateDrops);
        CHECK(world.log.back().find("delivered it masked") != std::string::npos);

        // a block term: dropped by the local filter too, never delivered masked
        moderation.Submit(alice, "/msg bob zorp you", world.Deliver("alice"), t0 + milliseconds(400));
        std::vector<gPlatformOutboundLine> const blocked = moderation.TakeOutbound();
        REQUIRE(1 == blocked.size());
        moderation.Tick(t0 + milliseconds(550));
        CHECK(1 == world.delivered.size());
        CHECK(world.Told(1, gPlatformChatFilter::DefaultNotice));
        CHECK(1 == moderation.CurrentStats().localDrops);
        CHECK(2 == moderation.CurrentStats().timeouts);

        // its late drop verdict is no news
        moderation.ApplyVerdicts({Verdict(blocked[0].id, "drop")}, t0 + milliseconds(600));
        CHECK(2 == moderation.CurrentStats().late);
        CHECK(1 == moderation.CurrentStats().lateDrops);

        // the report window: what players saw, and the platform's late verdict
        world.players = {alice, Player(2, 2, "bob")};
        REQUIRE(moderation.Submit(Player(2, 2, "bob"), "/report alice insults", world.Deliver("bob"), t0 + milliseconds(700)));
        REQUIRE(1 == world.reports.size());
        nlohmann::json const report = nlohmann::json::parse(world.reports[0]);
        REQUIRE(2 == report["chat"].size());
        CHECK(S(report["chat"][0]["delivered"]) == "masked");
        CHECK(S(report["chat"][0]["verdict"]) == "drop");
        CHECK(S(report["chat"][1]["delivered"]) == "dropped");
        CHECK(!report["chat"][1].contains("verdict"));
    }

    TEST_CASE("The Platform's Drop Notice For Local Drops")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        std::vector<gPlatformSanction> sanctions;
        gPlatformChatFilter::Lists lists;
        bool hasFilter = false;
        std::int64_t commandId = 0;
        std::string error;
        REQUIRE(gPlatformModeration::TryParseState(R"({"commandId":0,"chatFilter":{"version":"v","mask":[],"block":["zorp"],"allow":[],"notice":"Not sent: house rules."},"sanctions":[]})",
                                                   hasFilter, lists, sanctions, commandId, error));
        CHECK(lists.notice == "Not sent: house rules.");
        moderation.ApplyState(&lists, sanctions, commandId);
        moderation.SetMode(gPlatformModeration::Mode::Async, Clock::now());
        CHECK(moderation.Submit(Player(1, 1, "alice"), "zorp", world.Deliver("alice"), Clock::now()));
        CHECK(world.delivered.empty());
        CHECK(world.Told(1, "Not sent: house rules."));
    }

    TEST_CASE("Failing Or Missing Platform")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 1, "alice");

        moderation.Submit(alice, "held", world.Deliver("alice"), t0);
        CHECK(world.delivered.empty());

        WHEN("the platform keeps failing")
        {
            moderation.SetMode(gPlatformModeration::Mode::Async, t0);
            THEN("what was held goes out, and new lines go out at once but are still sent")
            {
                REQUIRE(1 == world.delivered.size());
                CHECK(moderation.Submit(alice, "fuck this", world.Deliver("alice"), t0));
                REQUIRE(2 == world.delivered.size());
                CHECK(world.delivered[1] == "alice: **** this");
                CHECK(2 == moderation.TakeOutbound().size());
            }
        }
        WHEN("the platform does no moderation")
        {
            moderation.SetMode(gPlatformModeration::Mode::Off, t0, seconds(600));
            moderation.TakeOutbound();
            THEN("lines go out at once and are not sent, until the spell ends")
            {
                moderation.Submit(alice, "again", world.Deliver("alice"), t0);
                CHECK(2 == world.delivered.size());
                CHECK(moderation.TakeOutbound().empty());
                moderation.Tick(t0 + seconds(601));
                CHECK(moderation.CurrentMode() == gPlatformModeration::Mode::Hold);
            }
        }
        WHEN("the server turns checking off")
        {
            moderation.SetChatFilter(false, t0);
            THEN("the held line goes out as written and the next is the server's again")
            {
                REQUIRE(1 == world.delivered.size());
                CHECK(world.delivered[0] == "alice: held");
                CHECK(!moderation.Submit(alice, "plain", world.Deliver("alice"), t0));
            }
        }
    }

    TEST_CASE("Mutes")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 4, "alice", "Alice@retrocycles");
        gPlatformChatPlayer const bob = Player(2, 5, "bob", "bob@retrocycles");
        world.players = {alice, bob};

        moderation.ApplySanction(Sanction(R"({"id":"s_1","kind":"mute","scope":"all","target":{"type":"account","userId":"u_9","gids":["alice@retrocycles"]},"until":1790000600,"reason":"Abusive chat"})"), 7);
        CHECK(world.Told(4, "You are muted until 2026-09-21 14:23 UTC: Abusive chat"));
        CHECK(!world.Told(5, "muted"));

        world.told.clear();
        CHECK(moderation.Submit(alice, "hello?", world.Deliver("alice"), t0));
        CHECK(world.delivered.empty());
        CHECK(world.Told(4, "You are muted until"));
        CHECK(moderation.TakeOutbound().empty()); // answered locally, no request
        CHECK(!moderation.Submit(alice, "/players", world.Deliver("alice"), t0)); // commands still work
        CHECK(moderation.Submit(bob, "hi", world.Deliver("bob"), t0));          // others chat as usual

        SUBCASE("lifted")
        {
            moderation.RemoveSanction("s_1", 8);
            CHECK(world.Told(4, "You can chat again."));
            moderation.Submit(alice, "back", world.Deliver("alice"), t0);
            CHECK(2 == moderation.TakeOutbound().size());
        }
        SUBCASE("over")
        {
            world.now = 1790000601;
            moderation.Tick(t0 + seconds(1));
            CHECK(0 == moderation.SanctionCount());
            CHECK(moderation.MuteFor(alice) == nullptr);
        }
        SUBCASE("by the token's user id")
        {
            gPlatformChatPlayer other = Player(3, 6, "renamed", "new-handle@retrocycles");
            other.userId = "u_9";
            CHECK(moderation.MuteFor(other) != nullptr);
        }
        SUBCASE("muted lines are evidence")
        {
            world.players.push_back(Player(9, 9, "carol"));
            CHECK(moderation.Submit(world.players[2], "/report alice rude", world.Deliver("carol"), t0));
            REQUIRE(1 == world.reports.size());
            nlohmann::json const report = nlohmann::json::parse(world.reports[0]);
            REQUIRE(1 <= report["chat"].size());
            CHECK(report["chat"][0]["delivered"].get<std::string>() == "muted");
        }
    }

    TEST_CASE("Mutes Without Chat Checks")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(false), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const alice = Player(1, 4, "alice", "alice@retrocycles");

        CHECK(!moderation.Submit(alice, "fuck", world.Deliver("alice"), t0)); // stock chat
        moderation.ApplySanction(Sanction(R"({"id":"s_1","kind":"mute","target":{"type":"account","gids":["alice@retrocycles"]},"until":1790000600})"), 0);
        CHECK(moderation.Submit(alice, "hello", world.Deliver("alice"), t0));
        CHECK(world.delivered.empty());
    }

    TEST_CASE("Bans")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        gPlatformChatPlayer const alice = Player(1, 4, "alice", "alice@retrocycles");
        gPlatformChatPlayer const bob = Player(2, 5, "bob", "bob@retrocycles");
        world.players = {alice, bob};
        std::string const ban = R"({"id":"s_2","kind":"ban","scope":"all","target":{"type":"account","userId":"u_1","gids":["alice@retrocycles","g-01020304@retrocycles"]},"until":1790086400,"reason":"Hate speech"})";

        WHEN("a ban arrives for a connected player")
        {
            moderation.ApplySanction(Sanction(ban), 3);
            THEN("the player is removed at once, nobody else")
            {
                REQUIRE(1 == world.kicked.size());
                CHECK(4 == world.kicked[0].first);
                CHECK(world.kicked[0].second == "You are banned from this server until 2026-09-22 14:13 UTC: Hate speech");
            }
            AND_THEN("logins are refused, by gid, former gid or user id")
            {
                std::string reason;
                CHECK(!moderation.AdmitLogin(7, "alice@retrocycles", std::string(), reason));
                CHECK(reason.find("banned") != std::string::npos);
                CHECK(!moderation.AdmitLogin(7, "g-01020304@retrocycles", std::string(), reason));
                CHECK(!moderation.AdmitLogin(7, "other@retrocycles", "u_1", reason));
                CHECK(moderation.AdmitLogin(7, "bob@retrocycles", "u_2", reason));
            }
            AND_THEN("a login by another method is ended")
            {
                world.kicked.clear();
                moderation.PlayerAuthenticated(Player(8, 8, "alice2", "alice@retrocycles"));
                REQUIRE(1 == world.kicked.size());
                CHECK(8 == world.kicked[0].first);
            }
        }
        WHEN("the list from the platform has it")
        {
            moderation.ApplyState(nullptr, {Sanction(ban)}, 0);
            THEN("it is enforced too")
            {
                CHECK(1 == world.kicked.size());
                CHECK(1 == moderation.SanctionCount());
            }
        }
        WHEN("a ban for another room arrives")
        {
            moderation.ApplySanction(Sanction(R"({"id":"s_3","kind":"ban","scope":"room","serverId":"elsewhere","target":{"type":"account","gids":["alice@retrocycles"]},"until":1790086400})"), 4);
            THEN("it does not apply here")
            {
                CHECK(world.kicked.empty());
                std::string reason;
                CHECK(moderation.AdmitLogin(7, "alice@retrocycles", std::string(), reason));
            }
        }
        WHEN("a kick arrives")
        {
            moderation.ApplySanction(Sanction(R"({"id":"s_4","kind":"kick","target":{"type":"account","gids":["bob@retrocycles"]},"until":1790000000,"reason":"Spam"})"), 5);
            THEN("the player is removed and nothing is kept")
            {
                REQUIRE(1 == world.kicked.size());
                CHECK(5 == world.kicked[0].first);
                CHECK(world.kicked[0].second == "You were removed from this server: Spam");
                CHECK(0 == moderation.SanctionCount());
            }
        }
        WHEN("a permanent ban arrives")
        {
            moderation.ApplySanction(Sanction(R"({"id":"s_5","kind":"ban","target":{"type":"account","gids":["bob@retrocycles"]},"until":null})"), 6);
            THEN("it says so")
            {
                REQUIRE(1 == world.kicked.size());
                CHECK(world.kicked[0].second == "You are banned from this server permanently.");
            }
        }
    }

    TEST_CASE("The List Never Undoes A Later Command")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        gPlatformSanction const s1 = Sanction(R"({"id":"s_1","kind":"mute","target":{"type":"account","gids":["a@retrocycles"]},"until":1790000600})");
        gPlatformSanction const s2 = Sanction(R"({"id":"s_2","kind":"mute","target":{"type":"account","gids":["b@retrocycles"]},"until":1790000600})");

        moderation.ApplySanction(s1, 10);
        moderation.RemoveSanction("s_1", 11);
        moderation.ApplySanction(s2, 12);

        // a list made before command 11 (it still has s_1, not yet s_2)
        moderation.ApplyState(nullptr, {s1}, 10);
        CHECK(1 == moderation.SanctionCount());
        CHECK(moderation.MuteFor(Player(1, 1, "a", "a@retrocycles")) == nullptr);
        CHECK(moderation.MuteFor(Player(2, 2, "b", "b@retrocycles")) != nullptr);

        // a newer list replaces everything
        moderation.ApplyState(nullptr, {}, 12);
        CHECK(0 == moderation.SanctionCount());
    }

    TEST_CASE("Address Bans Spare Logged In Players")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const student = Player(1, 2, "student", "g-0a0b0c0d@retrocycles", "198.51.100.9");
        gPlatformChatPlayer const desktop = Player(2, 3, "desk", std::string(), "198.51.100.9");
        world.players = {student, desktop};

        moderation.ApplySanction(Sanction(R"({"id":"s_9","kind":"ban","target":{"type":"address","ip":"198.51.100.9"},"until":1790003600,"reason":"Spam"})"), 1);
        REQUIRE(1 == world.kicked.size());
        CHECK(3 == world.kicked[0].first); // only the player who is not logged in

        world.kicked.clear();
        world.players = {student};
        moderation.ConnectionOpened(4, "198.51.100.9", t0); // a classmate's browser, logging in
        moderation.ConnectionOpened(5, "198.51.100.9", t0); // a desktop player who does not
        moderation.ConnectionOpened(6, "192.0.2.1", t0);    // another address
        moderation.Tick(t0 + seconds(5));
        CHECK(world.kicked.empty());

        world.players.push_back(Player(3, 4, "classmate", "g-01010101@retrocycles", "198.51.100.9"));
        world.players.push_back(Player(4, 5, "desk2", std::string(), "198.51.100.9"));
        moderation.Tick(t0 + seconds(16));
        REQUIRE(1 == world.kicked.size());
        CHECK(5 == world.kicked[0].first);

        std::string reason;
        CHECK(moderation.AdmitLogin(7, "g-01010101@retrocycles", std::string(), reason));
    }

    TEST_CASE("Connection Sanctions")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const desk = Player(1, 3, "desk");

        moderation.ApplySanction(Sanction(R"({"id":"s_c","kind":"mute","scope":"room","serverId":"dev-local-1","target":{"type":"connection","serverId":"dev-local-1","bootId":"boot-1","conn":3,"name":"desk"},"until":1790000600})"), 2);
        CHECK(moderation.MuteFor(desk) != nullptr);
        CHECK(moderation.MuteFor(Player(2, 4, "other")) == nullptr);
        CHECK(moderation.Submit(desk, "hi", world.Deliver("desk"), t0));
        CHECK(world.delivered.empty());

        // another server run's: someone else on connection 3; the list is older than command 2
        moderation.ApplyState(nullptr, {Sanction(R"({"id":"s_d","kind":"mute","target":{"type":"connection","serverId":"dev-local-1","bootId":"boot-0","conn":3},"until":1790000600})")}, 1);
        CHECK(moderation.MuteFor(desk) != nullptr); // s_c came with command 2 and stays
        moderation.ApplyState(nullptr, {Sanction(R"({"id":"s_d","kind":"mute","target":{"type":"connection","serverId":"dev-local-1","bootId":"boot-0","conn":3},"until":1790000600})")}, 3);
        CHECK(moderation.MuteFor(desk) == nullptr);
    }

    TEST_CASE("Connection Sanctions End With Their Connection")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        std::string const mute = R"({"id":"s_c","kind":"mute","target":{"type":"connection","serverId":"dev-local-1","bootId":"boot-1","conn":3,"name":"desk"},"until":1790000600})";
        std::string const account = R"({"id":"s_a","kind":"mute","target":{"type":"account","gids":["a@retrocycles"]},"until":1790000600})";
        gPlatformChatPlayer const desk = Player(1, 3, "desk");
        gPlatformChatPlayer const next = Player(2, 3, "next"); // the next player on connection 3
        world.players = {desk};

        moderation.ConnectionOpened(3, "203.0.113.7", t0);
        moderation.ApplySanction(Sanction(mute), 2);
        moderation.ApplySanction(Sanction(account), 3);
        CHECK(moderation.MuteFor(desk) != nullptr);

        moderation.ConnectionClosed(3);
        world.players = {next};
        moderation.ConnectionOpened(3, "198.51.100.20", t0);
        CHECK(moderation.MuteFor(next) == nullptr);
        CHECK(1 == moderation.SanctionCount()); // the account's stays

        // a list that still has it, or the command again, does not bring it back
        world.told.clear();
        moderation.ApplyState(nullptr, {Sanction(mute), Sanction(account)}, 3);
        CHECK(moderation.MuteFor(next) == nullptr);
        moderation.ApplySanction(Sanction(mute), 4);
        CHECK(moderation.MuteFor(next) == nullptr);
        CHECK(world.told.empty());
        CHECK(1 == moderation.SanctionCount());

        // a new sanction for the new player does
        moderation.ApplySanction(Sanction(R"({"id":"s_n","kind":"mute","target":{"type":"connection","serverId":"dev-local-1","bootId":"boot-1","conn":3},"until":1790000600})"), 5);
        CHECK(moderation.MuteFor(next) != nullptr);
    }

    TEST_CASE("Address Bans Keep The Grace Of A Connection Logging In")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        std::string const ban = R"({"id":"s_9","kind":"ban","target":{"type":"address","ip":"198.51.100.9"},"until":1790003600,"reason":"Spam"})";

        moderation.ApplySanction(Sanction(ban), 1);
        moderation.ConnectionOpened(4, "198.51.100.9", t0); // a browser, its login on the way
        world.players = {Player(1, 4, "newcomer", std::string(), "198.51.100.9")};

        // the list refreshed, the ban sent again: the grace still runs
        moderation.ApplyState(nullptr, {Sanction(ban)}, 1);
        moderation.ApplySanction(Sanction(ban), 2);
        moderation.Tick(t0 + seconds(5));
        CHECK(world.kicked.empty());

        // it logged in: never touched
        world.players = {Player(1, 4, "newcomer", "g-0f0f0f0f@retrocycles", "198.51.100.9")};
        moderation.Tick(t0 + seconds(16));
        moderation.ApplyState(nullptr, {Sanction(ban)}, 2);
        CHECK(world.kicked.empty());

        // one that does not log in is ended when its grace is over
        moderation.ConnectionOpened(5, "198.51.100.9", t0 + seconds(16));
        world.players.push_back(Player(2, 5, "desk", std::string(), "198.51.100.9"));
        moderation.ApplyState(nullptr, {Sanction(ban)}, 2);
        CHECK(world.kicked.empty());
        moderation.Tick(t0 + seconds(32));
        REQUIRE(1 == world.kicked.size());
        CHECK(5 == world.kicked[0].first);
    }

    TEST_CASE("Sanction Objects")
    {
        gPlatformSanction sanction;
        std::string error;
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"ban","target":{"type":"connection","bootId":"b","conn":1},"until":5})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"mute","target":{"type":"address","ip":"1.2.3.4"},"until":5})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"ban","target":{"type":"address","ip":"1.2.3.4"},"until":null})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"mute","target":{"type":"account","gids":["a@b"]},"until":null})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"mute","target":{"type":"account","gids":[]},"until":5})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"shame","target":{"type":"account","gids":["a@b"]},"until":5})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"kind":"ban","target":{"type":"account","gids":["a@b"]},"until":5})", sanction, error));
        CHECK(!gPlatformSanction::TryParse(R"({"id":"s","kind":"ban","scope":"room","target":{"type":"account","gids":["a@b"]},"until":5})", sanction, error));
        REQUIRE(gPlatformSanction::TryParse(R"({"id":"s","kind":"ban","target":{"type":"account","gids":["A@B"]},"until":5,"reason":"x\u0001y","extra":1})", sanction, error));
        CHECK(sanction.scope == "all");
        CHECK(sanction.gids[0] == "a@b");
        CHECK(sanction.reason == "xy");
    }

    TEST_CASE("In Game Reports")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer reporter = Player(1, 1, "rep", "rep@retrocycles");
        reporter.userId = "u_rep";
        gPlatformChatPlayer const target = Player(2, 2, "troll", std::string(), "192.0.2.50");
        gPlatformChatPlayer const bystander = Player(3, 3, "carol", "carol@retrocycles");
        world.players = {reporter, target, bystander};

        // the window: what each said, verdicts all in
        auto say = [&](gPlatformChatPlayer const& who, std::string const& line) {
            moderation.Submit(who, line, world.Deliver(who.name), t0);
            for (auto const& out : moderation.TakeOutbound())
                moderation.ApplyVerdicts({Verdict(out.id, "allow")}, t0);
        };
        say(target, "everyone here is bad");
        say(target, "/team secret plan");
        say(bystander, "/msg dave private stuff");
        say(bystander, "/msg rep hey");
        say(bystander, "gg");

        CHECK(moderation.Submit(reporter, "/report troll keeps insulting people", world.Deliver("rep"), t0));
        REQUIRE(1 == world.reports.size());
        CHECK(world.Told(1, "Thank you. Your report about troll was sent to the moderators."));
        CHECK(world.delivered.size() == 5); // the report itself is not chat

        nlohmann::json const report = nlohmann::json::parse(world.reports[0]);
        CHECK(S(report["reporter"]["gid"]) == "rep@retrocycles");
        CHECK(S(report["reporter"]["userId"]) == "u_rep");
        CHECK(report["reporter"].count("ip") == 0); // logged in: no address
        CHECK(report["target"]["gid"].is_null());
        CHECK(report["target"]["conn"].get<long long>() == 2);
        CHECK(S(report["target"]["ip"]) == "192.0.2.50");
        CHECK(S(report["reason"]) == "keeps insulting people");
        std::vector<std::string> texts;
        for (auto const& line : report["chat"])
            texts.push_back(line["text"].get<std::string>());
        CHECK(texts == std::vector<std::string>{"everyone here is bad", "secret plan", "hey", "gg"}); // not the /msg to dave
        CHECK(S(report["chat"][2]["to"]) == "rep");
        CHECK(S(report["chat"][0]["delivered"]) == "as_written");

        SUBCASE("limits")
        {
            CHECK(moderation.Submit(reporter, "/report troll again", world.Deliver("rep"), t0));
            CHECK(moderation.Submit(reporter, "/report troll again", world.Deliver("rep"), t0));
            CHECK(3 == world.reports.size());
            CHECK(moderation.Submit(reporter, "/report troll again", world.Deliver("rep"), t0));
            CHECK(3 == world.reports.size());
            CHECK(world.Told(1, "Please wait a minute"));
            CHECK(moderation.Submit(reporter, "/report troll later", world.Deliver("rep"), t0 + seconds(61)));
            CHECK(4 == world.reports.size());
        }
        SUBCASE("mistakes")
        {
            CHECK(moderation.Submit(reporter, "/report", world.Deliver("rep"), t0));
            CHECK(world.Told(1, "Usage: /report <name> <reason>"));
            CHECK(moderation.Submit(reporter, "/report rep me", world.Deliver("rep"), t0));
            CHECK(world.Told(1, "You cannot report yourself."));
            CHECK(moderation.Submit(reporter, "/report nobody hm", world.Deliver("rep"), t0));
            CHECK(1 == world.reports.size());
        }
        SUBCASE("desktop players report too")
        {
            CHECK(moderation.Submit(target, "/report carol said gg", world.Deliver("troll"), t0));
            REQUIRE(2 == world.reports.size());
            nlohmann::json const second = nlohmann::json::parse(world.reports[1]);
            CHECK(second["reporter"]["gid"].is_null());
            CHECK(S(second["reporter"]["ip"]) == "192.0.2.50");
        }
    }

    TEST_CASE("Reports Off")
    {
        cWorld world;
        gPlatformModeration::Config config = ModerationConfig();
        config.reports = false;
        gPlatformModeration moderation(config, world.Effects());
        CHECK(!moderation.Submit(Player(1, 1, "a"), "/report b x", world.Deliver("a"), Clock::now()));
    }

    TEST_CASE("Rename")
    {
        cWorld world;
        gPlatformModeration moderation(ModerationConfig(), world.Effects());
        world.players = {Player(1, 4, "badname", "x@retrocycles")};
        CHECK(moderation.Rename("X@retrocycles", "Player 7f3a", "That name is not allowed."));
        REQUIRE(1 == world.renamed.size());
        CHECK(world.renamed[0].second == "Player 7f3a");
        CHECK(world.Told(4, "Your name was changed to Player 7f3a: That name is not allowed."));
        CHECK(!moderation.Rename("nobody@retrocycles", "Player 1", ""));
    }

    TEST_CASE("Chat JSON")
    {
        gPlatformOutboundLine line;
        line.id = 41;
        line.time = 1790000000;
        line.channel = "private";
        line.text = "caf\xe9";
        line.to = "Bob";
        line.player = Player(1, 5, "desk", std::string(), "203.0.113.7");
        nlohmann::json const body = nlohmann::json::parse(gPlatformModeration::ChatRequestJson("b1", {line}));
        CHECK(S(body["bootId"]) == "b1");
        CHECK(body["lines"][0]["id"].get<long long>() == 41);
        CHECK(S(body["lines"][0]["text"]) == "caf\xc3\xa9");
        CHECK(S(body["lines"][0]["to"]) == "Bob");
        CHECK(body["lines"][0]["player"]["gid"].is_null());
        CHECK(S(body["lines"][0]["player"]["ip"]) == "203.0.113.7");

        std::vector<gPlatformChatVerdict> verdicts;
        std::vector<gPlatformSanction> sanctions;
        std::string error;
        REQUIRE(gPlatformModeration::TryParseVerdicts(
            R"({"verdicts":[{"id":41,"action":"replace","text":"x"},{"id":"nope"},{"id":42,"action":"drop","notice":"n"}],"sanctions":[{"id":"s_1","kind":"mute","target":{"type":"account","gids":["a@b"]},"until":1790000600},{"id":"bad"}]})",
            verdicts, sanctions, error));
        REQUIRE(2 == verdicts.size());
        CHECK(verdicts[0].text == "x");
        CHECK(verdicts[1].notice == "n");
        CHECK(1 == sanctions.size());
        CHECK(!gPlatformModeration::TryParseVerdicts("[]", verdicts, sanctions, error));

        bool hasFilter = false;
        gPlatformChatFilter::Lists lists;
        std::int64_t commandId = -1;
        REQUIRE(gPlatformModeration::TryParseState(R"({"commandId":17,"chatFilter":{"version":"w1","mask":["darn"],"block":[],"allow":["kill"]},"sanctions":[]})",
                                                  hasFilter, lists, sanctions, commandId, error));
        CHECK(hasFilter);
        CHECK(17 == commandId);
        CHECK(lists.mask == std::vector<std::string>{"darn"});
    }

    TEST_CASE("Platform Roles")
    {
        CHECK(19 == gPlatformHttp::AccessLevelForRoles("", 1, 2, 19));
        CHECK(2 == gPlatformHttp::AccessLevelForRoles("moderator", 1, 2, 19));
        CHECK(1 == gPlatformHttp::AccessLevelForRoles("moderator, Admin", 1, 2, 19));
        CHECK(19 == gPlatformHttp::AccessLevelForRoles("staff", 1, 2, 19));
        CHECK(5 == gPlatformHttp::AccessLevelForRoles("staff,unknown", 1, 2, 5));
        CHECK(1 == gPlatformHttp::AccessLevelForRoles("admin", 0, 2, 19)); // never the owner
    }
}

// *************************************************************
// the HTTP side
// *************************************************************

namespace
{
// a fake platform for moderation: the state document, the chat endpoint (scripted), commands
struct cModPlatform
{
    std::mutex mutex{};
    std::condition_variable condition{};
    std::string state{R"({"commandId":0,"chatFilter":{"version":"w-test","mask":["darn"],"block":["zorp"],"allow":[]},"sanctions":[]})"};
    int stateStatus{200};
    std::vector<std::string> chatBodies{};
    std::vector<std::string> events{};
    std::size_t stateFetches{};
    std::deque<int> chatStatuses{}; // then 200
    milliseconds chatDelay{0};
    std::string extraSanctions{"[]"};
    std::deque<std::string> commands{};
    std::string keysBody{R"({"keys":[]})"};

    // allow everything, but drop lines with "zorp" in them
    std::string Answer(std::string const& body)
    {
        nlohmann::json const request = nlohmann::json::parse(body);
        nlohmann::json verdicts = nlohmann::json::array();
        for (auto const& line : request["lines"])
        {
            nlohmann::json verdict;
            verdict["id"] = line["id"];
            bool const bad = line["text"].get<std::string>().find("zorp") != std::string::npos;
            verdict["action"] = bad ? "drop" : "allow";
            if (bad)
                verdict["notice"] = "That message was not sent.";
            verdicts.push_back(verdict);
        }
        return R"({"verdicts":)" + verdicts.dump() + R"(,"sanctions":)" + extraSanctions + "}";
    }
};

class cModSender final : public gPlatformHttpSender
{
public:
    explicit cModSender(std::shared_ptr<cModPlatform> platform) : platform_(std::move(platform)) {}

private:
    gPlatformHttpResponse DoSend(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) override
    {
        gPlatformHttpResponse response;
        if (request.url.find("/commands?") != std::string::npos)
        {
            std::unique_lock<std::mutex> lock(platform_->mutex);
            platform_->condition.wait_for(lock, milliseconds(20), [&] { return !platform_->commands.empty() || cancel.load(); });
            response.status = 200;
            if (!platform_->commands.empty())
            {
                response.body = platform_->commands.front();
                platform_->commands.pop_front();
            }
            else
                response.body = R"({"commands":[],"cursor":0})";
            return response;
        }
        if (request.url.find("/moderation/chat") != std::string::npos)
        {
            milliseconds delay;
            {
                std::lock_guard<std::mutex> lock(platform_->mutex);
                delay = platform_->chatDelay;
            }
            std::this_thread::sleep_for(delay);
            std::lock_guard<std::mutex> lock(platform_->mutex);
            platform_->chatBodies.push_back(request.body);
            response.status = 200;
            if (!platform_->chatStatuses.empty())
            {
                response.status = platform_->chatStatuses.front();
                platform_->chatStatuses.pop_front();
            }
            if (response.status == 200)
                response.body = platform_->Answer(request.body);
            platform_->condition.notify_all();
            return response;
        }
        std::lock_guard<std::mutex> lock(platform_->mutex);
        if (request.url.find("/moderation") != std::string::npos)
        {
            ++platform_->stateFetches;
            response.status = platform_->stateStatus;
            response.body = platform_->state;
        }
        else if (request.url.find("/events") != std::string::npos)
        {
            platform_->events.push_back(request.body);
            response.status = 200;
            response.body = R"({"accepted":1,"duplicates":0})";
        }
        else if (request.url.find("/heartbeat") != std::string::npos)
            response.status = 204;
        else
        {
            response.status = 200;
            response.body = platform_->keysBody;
        }
        platform_->condition.notify_all();
        return response;
    }

    std::shared_ptr<cModPlatform> platform_;
};

class cModHarness
{
public:
    std::shared_ptr<cModPlatform> platform{std::make_shared<cModPlatform>()};
    cWorld world{};
    gPlatformHttpConfig config{};

    cModHarness()
    {
        config.apiUrl = "http://platform.test";
        config.serverId = "dev-local-1";
        config.serverKey = "secret-key";
        config.version = "0.2.9-test";
        config.chatFilter = true;
        config.sanctions = true;
        config.reports = true;
        config.flushTimeout = milliseconds(500);
        config.retryInitial = milliseconds(5);
        config.commandPollMinInterval = milliseconds(1);
        config.chatOffAfterNotFound = seconds(600);
        config.log = [this](std::string const& line) { world.log.push_back(line); };
        config.countPlayers = [] { return ePlatformPlayerCounts(); };
        config.room.log = [this](std::string const& line) { world.log.push_back(line); };
        config.room.refuse = [](int, std::string const&) {};
        config.completeLoginAt = [](int, std::string const&, int) { return gPlatformLoginOutcome::Authenticated; };
        config.moderation = world.Effects();
        config.moderation.report = nullptr; // through the event stream
    }

    std::unique_ptr<gPlatformHttp> Start()
    {
        return std::unique_ptr<gPlatformHttp>(new gPlatformHttp(config, std::unique_ptr<gPlatformHttpSender>(new cModSender(platform))));
    }
};

class cRegistered final
{
public:
    explicit cRegistered(ePlatform& platform) : platform_(platform) { ePlatform::Register(&platform_); }
    ~cRegistered() { ePlatform::Unregister(&platform_); }

private:
    ePlatform& platform_;
};

template <class Pred>
bool PumpUntil(Pred pred, milliseconds limit = milliseconds(5000))
{
    auto const deadline = Clock::now() + limit;
    while (!pred())
    {
        if (Clock::now() > deadline)
            return false;
        ePlatform::Drain();
        std::this_thread::sleep_for(milliseconds(1));
    }
    return true;
}
} // namespace

TEST_SUITE("Platform Moderation Http")
{
    TEST_CASE("Chat Round Trip")
    {
        cModHarness harness;
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();

        // the word lists arrive first
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        gPlatformChatPlayer const alice = Player(1, 1, "alice", "alice@retrocycles");
        auto const t0 = Clock::now();
        CHECK(moderation.Submit(alice, "hello", harness.world.Deliver("alice"), t0));
        CHECK(moderation.Submit(alice, "zorp you", harness.world.Deliver("alice"), t0));
        CHECK(moderation.Submit(alice, "bye", harness.world.Deliver("alice"), t0));
        REQUIRE(PumpUntil([&] { return harness.world.delivered.size() == 2; }));
        double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        MESSAGE("verdicts through the fake platform after " << ms << " ms");
        CHECK(ms < 150);
        CHECK(harness.world.delivered[0] == "alice: hello");
        CHECK(harness.world.delivered[1] == "alice: bye");
        CHECK(harness.world.Told(1, "That message was not sent."));
        CHECK(0 == moderation.CurrentStats().timeouts);

        std::lock_guard<std::mutex> lock(harness.platform->mutex);
        REQUIRE(2 <= harness.platform->chatBodies.size());
        // an empty batch first: it opens the connection before anyone chats
        CHECK(0 == nlohmann::json::parse(harness.platform->chatBodies[0])["lines"].size());
        nlohmann::json const body = nlohmann::json::parse(harness.platform->chatBodies[1]);
        CHECK(S(body["bootId"]) == platform->BootId());
        CHECK(3 == body["lines"].size()); // one batch per tick
        CHECK(S(body["lines"][1]["text"]) == "zorp you");
    }

    TEST_CASE("Slow Platform")
    {
        cModHarness harness;
        harness.platform->chatDelay = milliseconds(400);
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        auto const t0 = Clock::now();
        moderation.Submit(Player(1, 1, "alice"), "darn it", harness.world.Deliver("alice"), t0);
        moderation.Submit(Player(1, 1, "alice"), "darn zorp", harness.world.Deliver("alice"), t0);
        REQUIRE(PumpUntil([&] { return !harness.world.delivered.empty() && !harness.world.told.empty(); }));
        double const ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        CHECK(ms >= 150);
        CHECK(ms < 350);
        REQUIRE(1 == harness.world.delivered.size());
        CHECK(harness.world.delivered[0] == "alice: **** it"); // the local filter: masked, delivered
        CHECK(harness.world.Told(1, gPlatformChatFilter::DefaultNotice)); // a block term: dropped, never masked
        REQUIRE(PumpUntil([&] { return moderation.CurrentStats().late == 2; }));
        CHECK(1 == harness.world.delivered.size());
    }

    TEST_CASE("Platform Down")
    {
        cModHarness harness;
        harness.platform->chatStatuses = {0, 503, 0};
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        gPlatformChatPlayer const alice = Player(1, 1, "alice");
        for (int i = 0; i < 3; ++i)
        {
            moderation.Submit(alice, "line " + std::to_string(i), harness.world.Deliver("alice"), Clock::now());
            REQUIRE(PumpUntil([&] { return harness.world.delivered.size() == static_cast<std::size_t>(i + 1); }));
        }
        REQUIRE(PumpUntil([&] { return moderation.CurrentMode() == gPlatformModeration::Mode::Async; }));

        // failing: lines go out at once
        auto const t0 = Clock::now();
        moderation.Submit(alice, "darn", harness.world.Deliver("alice"), t0);
        CHECK(harness.world.delivered.back() == "alice: ****");
        CHECK(std::chrono::duration<double, std::milli>(Clock::now() - t0).count() < 50);

        // the first answer turns holding back on
        REQUIRE(PumpUntil([&] { return moderation.CurrentMode() == gPlatformModeration::Mode::Hold; }));
    }

    TEST_CASE("No Moderation At The Platform")
    {
        cModHarness harness;
        harness.platform->stateStatus = 404;
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        REQUIRE(PumpUntil([&] { return moderation.CurrentMode() == gPlatformModeration::Mode::Off; }));

        moderation.Submit(Player(1, 1, "alice"), "fuck", harness.world.Deliver("alice"), Clock::now());
        REQUIRE(1 == harness.world.delivered.size());
        CHECK(harness.world.delivered[0] == "alice: ****"); // the built-in list
    }

    TEST_CASE("Sanction Commands")
    {
        cModHarness harness;
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        harness.world.players = {Player(1, 4, "alice", "alice@retrocycles")};
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        {
            std::lock_guard<std::mutex> lock(harness.platform->mutex);
            harness.platform->commands.push_back(
                R"({"commands":[{"id":5,"type":"sanction","time":1,"data":{"id":"s_1","kind":"ban","scope":"all","target":{"type":"account","userId":"u_1","gids":["alice@retrocycles"]},"until":4102444800,"reason":"Hate speech"}},)"
                R"({"id":6,"type":"moderation_config","time":1,"data":{}}],"cursor":6})");
        }
        harness.platform->condition.notify_all();

        REQUIRE(PumpUntil([&] { return !harness.world.kicked.empty(); }));
        CHECK(4 == harness.world.kicked[0].first);
        CHECK(harness.world.kicked[0].second.find("You are banned from this server until 2100-01-01") == 0);
        CHECK(6 == platform->LastCommandId());
        REQUIRE(PumpUntil([&] {
            std::lock_guard<std::mutex> lock(harness.platform->mutex);
            return harness.platform->stateFetches >= 2;
        }));
        // the refreshed list does not have the ban yet (commandId 0): the command stands
        REQUIRE(PumpUntil([&] { return platform->Moderation().SanctionCount() == 1; }));
        std::string reason;
        CHECK(!moderation.AdmitLogin(9, "alice@retrocycles", std::string(), reason));

        {
            std::lock_guard<std::mutex> lock(harness.platform->mutex);
            harness.platform->commands.push_back(R"({"commands":[{"id":7,"type":"unsanction","time":2,"data":{"id":"s_1"}}],"cursor":7})");
        }
        harness.platform->condition.notify_all();
        REQUIRE(PumpUntil([&] { return platform->Moderation().SanctionCount() == 0; }));
    }

    TEST_CASE("Sanctions Off")
    {
        cModHarness harness;
        harness.config.sanctions = false;
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        harness.world.players = {Player(1, 4, "alice", "alice@retrocycles")};
        {
            std::lock_guard<std::mutex> lock(harness.platform->mutex);
            harness.platform->commands.push_back(
                R"({"commands":[{"id":5,"type":"sanction","time":1,"data":{"id":"s_1","kind":"ban","target":{"type":"account","gids":["alice@retrocycles"]},"until":4102444800}}],"cursor":5})");
        }
        harness.platform->condition.notify_all();
        REQUIRE(PumpUntil([&] { return platform->LastCommandId() == 5; }));
        CHECK(harness.world.kicked.empty());
        CHECK(0 == platform->Moderation().SanctionCount());
    }

    TEST_CASE("Verdict Sanctions Apply At Once")
    {
        cModHarness harness;
        harness.platform->extraSanctions = R"([{"id":"s_m","kind":"mute","target":{"type":"account","gids":["alice@retrocycles"]},"until":4102444800,"reason":"Strike 1"}])";
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        gPlatformChatPlayer const alice = Player(1, 4, "alice", "alice@retrocycles");
        harness.world.players = {alice};
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        moderation.Submit(alice, "zorp", harness.world.Deliver("alice"), Clock::now());
        REQUIRE(PumpUntil([&] { return moderation.MuteFor(alice) != nullptr; }));
        CHECK(harness.world.Told(4, "You are muted until 2100-01-01 00:00 UTC: Strike 1"));
        CHECK(harness.world.delivered.empty());

        harness.world.told.clear();
        CHECK(moderation.Submit(alice, "sorry", harness.world.Deliver("alice"), Clock::now()));
        CHECK(harness.world.Told(4, "You are muted until"));
    }

    TEST_CASE("Reports Become Events")
    {
        cModHarness harness;
        std::unique_ptr<gPlatformHttp> platform = harness.Start();
        cRegistered registered(*platform);
        gPlatformModeration& moderation = platform->Moderation();
        harness.world.players = {Player(1, 1, "rep", "rep@retrocycles"), Player(2, 2, "troll")};
        REQUIRE(PumpUntil([&] { return moderation.Filter().Version() == "w-test"; }));

        CHECK(moderation.Submit(harness.world.players[0], "/report troll spam", harness.world.Deliver("rep"), Clock::now()));
        REQUIRE(PumpUntil([&] {
            std::lock_guard<std::mutex> lock(harness.platform->mutex);
            return !harness.platform->events.empty();
        }));
        std::lock_guard<std::mutex> lock(harness.platform->mutex);
        nlohmann::json const batch = nlohmann::json::parse(harness.platform->events[0]);
        REQUIRE(1 == batch["events"].size());
        CHECK(S(batch["events"][0]["type"]) == "report");
        CHECK(S(batch["events"][0]["data"]["target"]["name"]) == "troll");
        CHECK(S(batch["events"][0]["data"]["reason"]) == "spam");
    }

    TEST_CASE("Banned Accounts Cannot Log In With A Token")
    {
        std::ifstream file(std::string(PLATFORM_CONTRACT_DIR) + "/test-vectors/login-token-v0.json");
        REQUIRE(file.good());
        std::stringstream text;
        text << file.rdbuf();
        nlohmann::json const vectors = nlohmann::json::parse(text.str());
        std::string const token = vectors["cases"][0]["token"].get<std::string>();

        cModHarness harness;
        harness.config.loginEnabled = true;
        harness.config.clock = [] { return static_cast<std::int64_t>(1790000030); };
        harness.world.now = 1790000030;
        harness.config.moderation.clock = [] { return static_cast<std::int64_t>(1790000030); };
        std::vector<std::pair<std::string, int>> logins;
        harness.config.completeLoginAt = [&logins](int, std::string const& gid, int accessLevel) {
            logins.emplace_back(gid, accessLevel);
            return gPlatformLoginOutcome::Authenticated;
        };
        nlohmann::json keys;
        keys["keys"] = nlohmann::json::array({vectors["key"]});
        harness.platform->keysBody = keys.dump();

        SUBCASE("not banned: logged in, at the level of its roles")
        {
            harness.platform->state = R"({"commandId":0,"sanctions":[]})";
            std::unique_ptr<gPlatformHttp> platform = harness.Start();
            cRegistered registered(*platform);
            REQUIRE(PumpUntil([&] {
                std::lock_guard<std::mutex> lock(harness.platform->mutex);
                return harness.platform->stateFetches > 0;
            }));
            platform->HandleLoginToken(3, tString(token.c_str()));
            REQUIRE(PumpUntil([&] { return !logins.empty(); }));
            CHECK(logins[0].first == "g-7f3a9c01@retrocycles");
            CHECK(logins[0].second == 19); // roles "": a plain authenticated player
            CHECK(platform->Moderation().UserIdOf(3) == "u_test_0001");
        }
        SUBCASE("banned by user id: the connection ends, no login")
        {
            harness.platform->state = R"({"commandId":0,"sanctions":[{"id":"s_b","kind":"ban","target":{"type":"account","userId":"u_test_0001","gids":[]},"until":1790086400,"reason":"Cheating"}]})";
            std::unique_ptr<gPlatformHttp> platform = harness.Start();
            cRegistered registered(*platform);
            REQUIRE(PumpUntil([&] { return platform->Moderation().SanctionCount() == 1; }));
            platform->HandleLoginToken(3, tString(token.c_str()));
            REQUIRE(PumpUntil([&] { return !harness.world.kicked.empty(); }));
            CHECK(3 == harness.world.kicked[0].first);
            CHECK(harness.world.kicked[0].second == "You are banned from this server until 2026-09-22 14:13 UTC: Cheating");
            CHECK(logins.empty());
        }
    }

    TEST_CASE("Moderation Off By Default In The Config")
    {
        gPlatformHttpConfig config;
        CHECK(!config.chatFilter);
        CHECK(!config.sanctions);
        CHECK(!config.reports);
        CHECK(gPlatformHttp::ModerationUrl("http://p/", "a b") == "http://p/v1/servers/a%20b/moderation");
        CHECK(gPlatformHttp::ChatUrl("http://p", "s1") == "http://p/v1/servers/s1/moderation/chat");
    }
}
