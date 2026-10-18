#include <nlohmann/json.hpp>

#include "gPlatformMoves.h"

#include "doctest.h"

// Tests for moves (platform-contract/README.md section 3, "Moving players"): the command's
// data, who moves where and when, the grace before leftovers are disconnected, and the
// players_moved report. Only built with --enable-platform.

namespace
{
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

std::string const s_target = R"("target": { "serverId": "qp-b", "name": "Room é B ☃", "signalUrl": "ws://127.0.0.1:4621/", "udp": "203.0.113.7:4620" })";

gPlatformMove Parse(std::string const& data)
{
    gPlatformMove move;
    std::string error;
    REQUIRE_MESSAGE(gPlatformMove::TryParse(data, move, error), error);
    return move;
}

// a room with scripted players and recorded effects
struct cRoom
{
    std::vector<gPlatformMovePlayer> players{};
    std::vector<int> capable{}; // connections that announced "move"
    std::vector<int> browsers{};
    bool matchRunning{true};

    std::vector<std::pair<int, ePlatformMoveNotice>> sent{};
    std::vector<std::string> redirects{}; // "conn host:port reason"
    std::vector<std::pair<int, std::string>> disconnects{};
    std::vector<nlohmann::json> reports{};
    std::vector<std::string> log{};

    gPlatformMoveEffects Effects()
    {
        gPlatformMoveEffects effects;
        effects.players = [this] { return players; };
        effects.followsMoves = [this](int conn) { return std::find(capable.begin(), capable.end(), conn) != capable.end(); };
        effects.browser = [this](int conn) { return std::find(browsers.begin(), browsers.end(), conn) != browsers.end(); };
        effects.sendMove = [this](int conn, ePlatformMoveNotice const& notice) {
            sent.emplace_back(conn, notice);
            return true;
        };
        effects.redirect = [this](int conn, std::string const& host, int port, std::string const& reason) {
            redirects.push_back(std::to_string(conn) + " " + host + ":" + std::to_string(port) + " " + reason);
        };
        effects.disconnect = [this](int conn, std::string const& reason) { disconnects.emplace_back(conn, reason); };
        effects.matchRunning = [this] { return matchRunning; };
        effects.report = [this](std::string const& data) { reports.push_back(nlohmann::json::parse(data)); };
        effects.log = [this](std::string const& line) { log.push_back(line); };
        return effects;
    }

    void Add(int conn, std::string gid, bool inTeam = true)
    {
        gPlatformMovePlayer player;
        player.conn = conn;
        player.gid = std::move(gid);
        player.inTeam = inTeam;
        players.push_back(player);
    }
};
} // namespace

TEST_SUITE("gPlatformMoves")
{
    TEST_CASE("The Command's Data Is Parsed")
    {
        gPlatformMove const move = Parse(R"({ "moveId": "mv_1", "when": "now", "reason": "closer", "expiresAt": 1790000900, )" + s_target +
                                         R"(, "gids": ["g-a@io", 7, "g-b@io"] })");
        CHECK("mv_1" == move.moveId);
        CHECK(!move.atMatchEnd);
        CHECK("closer" == move.reason);
        CHECK(1790000900 == move.expiresAt);
        CHECK("qp-b" == move.targetServerId);
        CHECK("ws://127.0.0.1:4621/" == move.targetSignalUrl);
        // UTF-8 to the game's Latin-1, '?' for the rest
        CHECK("Room \xe9 B ?" == move.targetName);
        CHECK("203.0.113.7" == move.udpHost);
        CHECK(4620 == move.udpPort);
        CHECK(!move.everyone);
        CHECK(std::vector<std::string>{"g-a@io", "g-b@io"} == move.gids);

        // defaults: at the match end, everyone, reason admin, no udp, the id as the name
        gPlatformMove const plain = Parse(R"({ "moveId": "mv_2", "target": { "serverId": "qp-c", "signalUrl": "/signal/qp-c", "udp": "nohost" }, "gids": null, "reason": "party" })");
        CHECK(plain.atMatchEnd);
        CHECK(plain.everyone);
        CHECK("admin" == plain.reason);
        CHECK(plain.udpHost.empty());
        CHECK("qp-c" == plain.targetName);
        CHECK(0 == plain.expiresAt);
    }

    TEST_CASE("Unusable Commands Are Refused")
    {
        gPlatformMove move;
        std::string error;
        CHECK(!gPlatformMove::TryParse("[]", move, error));
        CHECK(!gPlatformMove::TryParse(R"({ "target": { "serverId": "x", "signalUrl": "/s" } })", move, error));
        CHECK("no usable moveId" == error);
        CHECK(!gPlatformMove::TryParse(R"({ "moveId": "mv 1", "target": { "serverId": "x", "signalUrl": "/s" } })", move, error));
        CHECK(!gPlatformMove::TryParse(R"({ "moveId": "mv_1" })", move, error));
        CHECK("no target" == error);
        CHECK(!gPlatformMove::TryParse(R"({ "moveId": "mv_1", "target": { "serverId": "x" } })", move, error));
        CHECK(!gPlatformMove::TryParse(R"({ "moveId": "mv_1", "when": "later", "target": { "serverId": "x", "signalUrl": "/s" } })", move, error));
        CHECK(!gPlatformMove::TryParse(R"({ "moveId": "mv_1", "gids": "g@io", "target": { "serverId": "x", "signalUrl": "/s" } })", move, error));
    }

    TEST_CASE("Everyone Moves: Browser Clients Are Told, Desktop Clients Redirected, Old Pages Stay")
    {
        cRoom room;
        room.Add(1, "g-a@io");          // a browser client with the capability
        room.Add(2, "", false);         // an unauthenticated spectator, same kind of client
        room.Add(3, "g-c@io");          // a desktop client with two local players
        room.Add(3, "");
        room.Add(4, "g-d@io");          // an older page: a browser client without the capability
        room.capable = {1, 2};
        room.browsers = {1, 2, 4};
        gPlatformMoves moves(room.Effects(), milliseconds(10000));

        moves.Add(Parse(R"({ "moveId": "mv_all", "when": "now", "reason": "merge", )" + s_target + R"(, "gids": null })"));
        auto const t0 = Clock::now();
        moves.Tick(t0, 1790000000);

        REQUIRE(2 == room.sent.size());
        CHECK(1 == room.sent[0].first);
        CHECK(2 == room.sent[1].first);
        CHECK("mv_all" == room.sent[0].second.moveId);
        CHECK("qp-b" == room.sent[0].second.serverId);
        CHECK("ws://127.0.0.1:4621/" == room.sent[0].second.signalUrl);
        CHECK("merge" == room.sent[0].second.reason);
        CHECK(std::vector<std::string>{"3 203.0.113.7:4620 Rooms merged: moving everyone to Room \xe9 B ?."} == room.redirects);

        REQUIRE(1 == room.reports.size());
        nlohmann::json const& report = room.reports[0];
        CHECK("mv_all" == report["moveId"].get<std::string>());
        CHECK("done" == report["outcome"].get<std::string>());
        CHECK("qp-b" == report["target"].get<std::string>());
        CHECK((nlohmann::json::parse(R"(["g-a@io", null])") == report["sent"]));
        CHECK((nlohmann::json::parse(R"(["g-c@io", null])") == report["redirected"]));
        CHECK((nlohmann::json::parse(R"([{"gid": "g-d@io", "why": "no_support"}])") == report["skipped"]));
        CHECK(0 == moves.Waiting());
        CHECK(2 == moves.Leaving());

        // the browser clients leave by themselves; one that does not is ended after the grace
        moves.UserLoggedOut(1);
        moves.Tick(t0 + milliseconds(9999), 1790000009);
        CHECK(room.disconnects.empty());
        moves.Tick(t0 + milliseconds(10000), 1790000010);
        REQUIRE(1 == room.disconnects.size());
        CHECK(2 == room.disconnects[0].first);
        CHECK("Moved to Room \xe9 B ?." == room.disconnects[0].second);
        CHECK(0 == moves.Leaving());
    }

    TEST_CASE("Named Players Move At Once, The Rest Stay")
    {
        cRoom room;
        room.Add(1, "g-a@io");
        room.Add(2, "G-B@io");
        room.Add(3, "g-c@io");
        room.capable = {1, 2, 3};
        room.browsers = {1, 2, 3};
        gPlatformMoves moves(room.Effects());

        // case does not matter; a gid that is not here is reported
        moves.Add(Parse(R"({ "moveId": "mv_some", "when": "now", "reason": "closer", )" + s_target + R"(, "gids": ["g-b@io", "g-a@IO", "g-gone@io"] })"));
        moves.Tick(Clock::now(), 1790000000);

        REQUIRE(2 == room.sent.size());
        CHECK(1 == room.sent[0].first);
        CHECK(2 == room.sent[1].first);
        REQUIRE(1 == room.reports.size());
        CHECK((nlohmann::json::parse(R"(["g-a@io", "G-B@io"])") == room.reports[0]["sent"]));
        CHECK((nlohmann::json::parse(R"([{"gid": "g-gone@io", "why": "not_here"}])") == room.reports[0]["skipped"]));
    }

    TEST_CASE("Desktop Players Stay Without A UDP Target")
    {
        cRoom room;
        room.Add(5, "g-e@io");
        gPlatformMoves moves(room.Effects());
        moves.Add(Parse(R"({ "moveId": "mv_x", "when": "now", "target": { "serverId": "qp-b", "signalUrl": "/signal/qp-b" }, "gids": null })"));
        moves.Tick(Clock::now(), 1790000000);
        CHECK(room.redirects.empty());
        REQUIRE(1 == room.reports.size());
        CHECK((nlohmann::json::parse(R"([{"gid": "g-e@io", "why": "no_udp"}])") == room.reports[0]["skipped"]));
    }

    TEST_CASE("A Match End Move Waits For The Match End")
    {
        cRoom room;
        room.Add(1, "g-a@io");
        room.capable = {1};
        room.browsers = {1};
        gPlatformMoves moves(room.Effects());

        moves.Add(Parse(R"({ "moveId": "mv_end", "when": "match_end", "reason": "merge", )" + s_target + R"(, "gids": null })"));
        auto const t0 = Clock::now();
        moves.Tick(t0, 1790000000);
        moves.Tick(t0 + milliseconds(100), 1790000001);
        CHECK(room.sent.empty());
        CHECK(1 == moves.Waiting());

        moves.MatchEnded();
        moves.Tick(t0 + milliseconds(200), 1790000002);
        CHECK(1 == room.sent.size());
        CHECK(0 == moves.Waiting());
        CHECK(1 == room.reports.size());
    }

    TEST_CASE("A Room Where Nobody Plays Runs A Match End Move At Once")
    {
        cRoom room;
        room.Add(1, "g-a@io", false);
        room.capable = {1};
        room.matchRunning = false;
        gPlatformMoves moves(room.Effects());
        moves.Add(Parse(R"({ "moveId": "mv_idle", )" + s_target + R"(, "gids": null })"));
        moves.Tick(Clock::now(), 1790000000);
        CHECK(1 == room.sent.size());
    }

    TEST_CASE("A Move Still Waiting At Its Expiry Is Reported Expired")
    {
        cRoom room;
        room.Add(1, "g-a@io");
        room.capable = {1};
        gPlatformMoves moves(room.Effects());
        moves.Add(Parse(R"({ "moveId": "mv_late", "expiresAt": 1790000100, )" + s_target + R"(, "gids": ["g-a@io", "g-b@io"] })"));
        moves.Tick(Clock::now(), 1790000099);
        CHECK(room.reports.empty());
        moves.Tick(Clock::now(), 1790000100);
        CHECK(room.sent.empty());
        REQUIRE(1 == room.reports.size());
        CHECK("expired" == room.reports[0]["outcome"].get<std::string>());
        CHECK((nlohmann::json::array() == room.reports[0]["sent"]));
        CHECK((nlohmann::json::parse(R"([{"gid": "g-a@io", "why": "expired"}, {"gid": "g-b@io", "why": "expired"}])") == room.reports[0]["skipped"]));
        CHECK(0 == moves.Waiting());
    }

    TEST_CASE("A Player On The Way Is Not Moved Twice")
    {
        cRoom room;
        room.Add(1, "g-a@io");
        room.Add(2, "g-b@io");
        room.capable = {1, 2};
        gPlatformMoves moves(room.Effects());
        moves.Add(Parse(R"({ "moveId": "mv_1", "when": "now", )" + s_target + R"(, "gids": ["g-a@io"] })"));
        moves.Add(Parse(R"({ "moveId": "mv_2", "when": "now", )" + s_target + R"(, "gids": null })"));
        moves.Tick(Clock::now(), 1790000000);
        REQUIRE(2 == room.sent.size());
        CHECK("mv_1" == room.sent[0].second.moveId);
        CHECK(1 == room.sent[0].first);
        CHECK("mv_2" == room.sent[1].second.moveId);
        CHECK(2 == room.sent[1].first);
    }

    TEST_CASE("Capability Words")
    {
        CHECK(ePlatform::HasWord("move", "move"));
        CHECK(ePlatform::HasWord("  clips move ", "move"));
        CHECK(!ePlatform::HasWord("moves", "move"));
        CHECK(!ePlatform::HasWord("", "move"));
        CHECK(!ePlatform::HasWord("move", ""));
        // nobody announced anything here
        CHECK(!ePlatform::ClientHasCapability(1, ePLATFORM_CAPABILITY_MOVE));
        CHECK(!ePlatform::ClientHasCapability(0, ePLATFORM_CAPABILITY_MOVE));
        CHECK(!ePlatform::ClientHasCapability(100000, ePLATFORM_CAPABILITY_MOVE));
    }
}
