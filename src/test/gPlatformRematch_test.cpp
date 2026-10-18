#include "gPlatformRematch.h"

#include "doctest.h"

#include <algorithm>
#include <utility>

namespace
{
using Clock = gPlatformRematch::Clock;

// a private room with scripted players and recorded effects
struct cDuelRoom
{
    std::vector<gPlatformRematchPlayer> players{};
    std::vector<std::string> said{};                  // announced to everyone
    std::vector<std::pair<int, std::string>> told{};  // to one connection
    std::vector<std::pair<int, std::string>> disconnects{};
    int restarts{};

    gPlatformRematchEffects Effects()
    {
        gPlatformRematchEffects effects;
        effects.players = [this] { return players; };
        effects.announce = [this](std::string const& line) { said.push_back(line); };
        effects.tell = [this](int conn, std::string const& line) { told.emplace_back(conn, line); };
        effects.restart = [this] { ++restarts; };
        effects.disconnect = [this](int conn, std::string const& reason) {
            disconnects.emplace_back(conn, reason);
            players.erase(std::remove_if(players.begin(), players.end(), [conn](gPlatformRematchPlayer const& p) { return p.conn == conn; }), players.end());
        };
        return effects;
    }

    void Add(int conn, std::string name, bool inTeam = true)
    {
        gPlatformRematchPlayer player;
        player.conn = conn;
        player.name = std::move(name);
        player.inTeam = inTeam;
        players.push_back(player);
    }
};
} // namespace

TEST_SUITE("gPlatformRematch")
{
    TEST_CASE("Both Players Asking Starts The Match Over")
    {
        cDuelRoom room;
        room.Add(1, "Ana");
        room.Add(2, "Ben");
        gPlatformRematch rematch(room.Effects(), std::chrono::seconds(30));
        auto const t0 = Clock::now();

        // outside the window: taken, told why, nothing happens
        CHECK(rematch.Submit(1, "/rematch", t0));
        REQUIRE(1 == room.told.size());
        CHECK(0 == room.restarts);

        rematch.MatchEnded(t0);
        CHECK(rematch.Open());
        REQUIRE(1 == room.said.size());
        CHECK(gPlatformRematch::AskText(30) == room.said.back());

        // other chat is not the rematch's
        CHECK(!rematch.Submit(1, "gg", t0));
        CHECK(rematch.Submit(1, "  /REMATCH ", t0 + std::chrono::seconds(3)));
        CHECK("Ana wants a rematch." == room.said.back());
        CHECK(1 == rematch.Answers());
        // asking twice changes nothing
        CHECK(rematch.Submit(1, "/rematch", t0 + std::chrono::seconds(4)));
        CHECK(1 == rematch.Answers());
        CHECK(0 == room.restarts);

        CHECK(rematch.Submit(2, "/rematch", t0 + std::chrono::seconds(5)));
        CHECK(1 == room.restarts);
        CHECK(gPlatformRematch::StartText == room.said.back());
        CHECK(!rematch.Open());

        // the window is closed: nobody is disconnected later
        rematch.Tick(t0 + std::chrono::seconds(31));
        CHECK(room.disconnects.empty());
    }

    TEST_CASE("Without Every Player The Room Empties When The Window Ends")
    {
        cDuelRoom room;
        room.Add(1, "Ana");
        room.Add(2, "Ben");
        room.Add(3, "Cyd", false); // watching
        gPlatformRematch rematch(room.Effects(), std::chrono::seconds(30));
        auto const t0 = Clock::now();
        rematch.MatchEnded(t0);

        // a spectator cannot ask
        CHECK(rematch.Submit(3, "/rematch", t0));
        CHECK(0 == rematch.Answers());
        CHECK(rematch.Submit(1, "/rematch", t0));

        rematch.Tick(t0 + std::chrono::seconds(29));
        CHECK(room.disconnects.empty());
        rematch.Tick(t0 + std::chrono::seconds(30));
        REQUIRE(3 == room.disconnects.size());
        CHECK(gPlatformRematch::ClosedText == room.disconnects[0].second);
        CHECK(0 == room.restarts);
        CHECK(!rematch.Open());
    }

    TEST_CASE("A Player Alone Cannot Rematch")
    {
        cDuelRoom room;
        room.Add(1, "Ana");
        room.Add(2, "Ben");
        gPlatformRematch rematch(room.Effects(), std::chrono::seconds(30));
        auto const t0 = Clock::now();
        rematch.MatchEnded(t0);
        CHECK(rematch.Submit(2, "/rematch", t0));

        // Ana leaves: Ben's answer alone is no rematch
        room.players.erase(room.players.begin());
        rematch.UserLoggedOut(1);
        CHECK(rematch.Submit(2, "/rematch", t0)); // already asked
        CHECK(0 == room.restarts);
        rematch.Tick(t0 + std::chrono::seconds(30));
        REQUIRE(1 == room.disconnects.size());
        CHECK(2 == room.disconnects[0].first);
    }

    TEST_CASE("A Window Of Zero Leaves The Room Alone")
    {
        cDuelRoom room;
        room.Add(1, "Ana");
        room.Add(2, "Ben");
        gPlatformRematch rematch(room.Effects(), std::chrono::seconds(0));
        auto const t0 = Clock::now();
        rematch.MatchEnded(t0);
        CHECK(!rematch.Open());
        CHECK(room.said.empty());
        rematch.Tick(t0 + std::chrono::hours(1));
        CHECK(room.disconnects.empty());
    }
}
