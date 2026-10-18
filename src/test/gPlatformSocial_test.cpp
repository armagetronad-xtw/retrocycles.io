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

#include "gPlatformSocial.h"
#include "gPlatformHttp.h"

#include "doctest.h"

#include <cctype>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

// Tests for desktop players' friends and messages (platform-contract/README.md
// section 3, "Friends in the rooms"): which lines are taken (a /msg to someone in the room is
// not), one request per player, the answers on the console, delivery of player_message to desktop
// connections only, the JSON, and the round trip through gPlatformHttp's social thread against a
// fake platform. Only built with --enable-platform.

namespace
{
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

gPlatformChatPlayer Player(int playerId, int conn, std::string name, std::string gid = std::string())
{
    gPlatformChatPlayer player;
    player.playerId = playerId;
    player.conn = conn;
    player.name = std::move(name);
    player.gid = std::move(gid);
    return player;
}

// a room with recorded effects: who is in it (name -> player id), who is logged in as what
struct cSocialRoom
{
    std::vector<std::pair<int, std::string>> told{};
    std::vector<gPlatformSocialRequest> sent{};
    std::vector<std::string> log{};
    std::map<int, int> conns{};                    // player id -> connection; missing: gone
    std::map<std::string, int> names{};            // lower case name -> player id
    std::map<std::string, std::vector<int>> gids{}; // gid -> connections
    std::set<int> browsers{};

    gPlatformSocialEffects Effects()
    {
        gPlatformSocialEffects effects;
        effects.tell = [this](int conn, std::string const& text) { told.emplace_back(conn, text); };
        effects.connOf = [this](int playerId) {
            auto const found = conns.find(playerId);
            return found == conns.end() ? 0 : found->second;
        };
        effects.inRoom = [this](int, std::string const& name) {
            std::string lower = name;
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            auto const found = names.find(lower);
            return found == names.end() ? -1 : found->second;
        };
        effects.connsOfGid = [this](std::string const& gid) {
            auto const found = gids.find(gid);
            return found == gids.end() ? std::vector<int>() : found->second;
        };
        effects.isBrowser = [this](int conn) { return browsers.count(conn) > 0; };
        effects.send = [this](gPlatformSocialRequest const& request) { sent.push_back(request); };
        effects.log = [this](std::string const& line) { log.push_back(line); };
        return effects;
    }

    std::string Last() const { return told.empty() ? std::string() : told.back().second; }
};

gPlatformSocialAnswer Answer(std::uint64_t id, std::string result, std::vector<std::string> lines)
{
    gPlatformSocialAnswer answer;
    answer.id = id;
    answer.status = 200;
    answer.result = std::move(result);
    answer.lines = std::move(lines);
    return answer;
}
} // namespace

TEST_SUITE("gPlatformSocial")
{
    TEST_CASE("Our Commands Are Taken, A Whisper To Someone Here Is Not")
    {
        cSocialRoom room;
        room.names["kronk"] = 7;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const kim = Player(1, 3, "Kim", "kim@io");
        auto const now = Clock::now();

        CHECK_FALSE(social.Submit(kim, "hello everyone", now));
        CHECK_FALSE(social.Submit(kim, "/team gg", now));
        CHECK_FALSE(social.Submit(kim, "/msg kronk nice cut", now)); // in the room: the room's own whisper
        CHECK(room.sent.empty());

        REQUIRE(social.Submit(kim, "/msg Lee are you coming?", now));
        REQUIRE(1 == room.sent.size());
        CHECK("message" == room.sent[0].command);
        CHECK("Lee" == room.sent[0].target);
        CHECK("are you coming?" == room.sent[0].text);
        CHECK("kim@io" == room.sent[0].gid);
    }

    TEST_CASE("Friend, Friends, Reply And Join")
    {
        cSocialRoom room;
        room.names["lee"] = 9;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const kim = Player(1, 3, "Kim", "kim@io");
        auto now = Clock::now();
        auto next = [&] {
            social.Answer(Answer(room.sent.back().id, "ok", { "ok" }));
            now += seconds(1);
        };

        REQUIRE(social.Submit(kim, "/f Lee", now));
        CHECK("friend" == room.sent.back().command);
        CHECK(9 == room.sent.back().netId); // a player of this room: by network id
        next();
        REQUIRE(social.Submit(kim, "/friend somebody", now));
        CHECK(-1 == room.sent.back().netId);
        next();
        REQUIRE(social.Submit(kim, "/F", now));
        CHECK("friends" == room.sent.back().command);
        next();
        REQUIRE(social.Submit(kim, "/r on my way", now));
        CHECK("reply" == room.sent.back().command);
        CHECK("on my way" == room.sent.back().text);
        next();
        REQUIRE(social.Submit(kim, "/join Lee", now));
        CHECK("join" == room.sent.back().command);
        CHECK("Lee" == room.sent.back().target);
        next();

        std::size_t const sent = room.sent.size();
        REQUIRE(social.Submit(kim, "/r", now));
        CHECK(room.Last().find("Usage: /r") == 0);
        REQUIRE(social.Submit(kim, "/join", now));
        CHECK(room.Last().find("Usage: /join") == 0);
        CHECK(sent == room.sent.size());
    }

    TEST_CASE("Message Length Counts Characters, Not Bytes")
    {
        cSocialRoom room;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const kim = Player(1, 3, "Kim", "kim@io");
        auto now = Clock::now();

        std::string accents;
        for (std::size_t i = 0; i < gPlatformSocial::MaxTextLength; ++i)
            accents += "\xc3\xa9"; // é in UTF-8: 240 characters, 480 bytes
        REQUIRE(social.Submit(kim, "/r " + accents, now));
        REQUIRE(1 == room.sent.size());
        social.Answer(Answer(room.sent.back().id, "sent", { "ok" }));

        REQUIRE(social.Submit(kim, "/r " + std::string(gPlatformSocial::MaxTextLength, '\xe9'), now)); // Latin-1 é
        REQUIRE(2 == room.sent.size());
        social.Answer(Answer(room.sent.back().id, "sent", { "ok" }));

        REQUIRE(social.Submit(kim, "/r " + std::string(gPlatformSocial::MaxTextLength + 1, 'a'), now));
        CHECK(2 == room.sent.size());
        CHECK(room.Last() == "Not sent: at most 240 characters.");
    }

    TEST_CASE("Without A Login Only The Room's Own Commands Go On")
    {
        cSocialRoom room;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const guest = Player(2, 4, "Guest");
        auto const now = Clock::now();

        CHECK_FALSE(social.Submit(guest, "/msg Lee hi", now)); // the room says "no such player" as always
        REQUIRE(social.Submit(guest, "/f Lee", now));
        CHECK(room.Last().find("Friends and messages need a Retrocycles account") == 0);
        CHECK(room.sent.empty());
    }

    TEST_CASE("One Request Per Player, A Dozen A Minute")
    {
        cSocialRoom room;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const kim = Player(1, 3, "Kim", "kim@io");
        auto now = Clock::now();

        REQUIRE(social.Submit(kim, "/f", now));
        REQUIRE(social.Submit(kim, "/f", now));
        CHECK(1 == room.sent.size());
        CHECK(room.Last().find("One moment") == 0);

        for (int i = 0; i < 20; ++i)
        {
            if (social.Pending() > 0)
                social.Answer(Answer(room.sent.back().id, "friends", { "No friend is online." }));
            social.Submit(kim, "/f", now);
        }
        CHECK(gPlatformSocial::CommandsPerMinute == static_cast<int>(room.sent.size()));
        CHECK(room.Last() == "Too many commands. Wait a minute.");
        social.Answer(Answer(room.sent.back().id, "friends", { "No friend is online." }));
        now += seconds(61);
        CHECK(social.Submit(kim, "/f", now));
        CHECK(gPlatformSocial::CommandsPerMinute + 1 == static_cast<int>(room.sent.size()));
    }

    TEST_CASE("Answers Are Told To The Player Who Asked, Sent Messages In The Whisper's Colour")
    {
        cSocialRoom room;
        room.conns[1] = 3;
        gPlatformSocial social(room.Effects());
        gPlatformChatPlayer const kim = Player(1, 3, "Kim", "kim@io");
        auto now = Clock::now();

        REQUIRE(social.Submit(kim, "/f", now));
        social.Answer(Answer(room.sent.back().id, "friends", { "Friends online: Lee (playing in Quick Play EU 2).", "Friend requests from Neo: /f name accepts." }));
        REQUIRE(room.told.size() == 2);
        CHECK(std::make_pair(3, std::string("Friends online: Lee (playing in Quick Play EU 2).")) == room.told[0]);
        CHECK("Friend requests from Neo: /f name accepts." == room.told[1].second);

        now += seconds(1);
        REQUIRE(social.Submit(kim, "/msg Lee hi", now));
        social.Answer(Answer(room.sent.back().id, "sent", { "[to Lee] hi" }));
        CHECK("0xff8cff[to Lee] hi" == room.Last());

        now += seconds(1);
        REQUIRE(social.Submit(kim, "/r hi", now));
        gPlatformSocialAnswer down;
        down.id = room.sent.back().id;
        down.status = 503;
        social.Answer(down);
        CHECK("Friends and messages are not available right now. Try again in a minute." == room.Last());

        // a player who left is not told
        now += seconds(1);
        REQUIRE(social.Submit(kim, "/f", now));
        room.conns.erase(1);
        std::size_t const told = room.told.size();
        social.Answer(Answer(room.sent.back().id, "friends", { "No friend is online." }));
        CHECK(told == room.told.size());
    }

    TEST_CASE("Messages Reach Desktop Connections Only")
    {
        cSocialRoom room;
        room.gids["lee@io"] = { 5, 6 };
        room.browsers.insert(6);
        gPlatformSocial social(room.Effects());

        social.Deliver("lee@io", "message", "[from Kim @ Quick Play EU 1] gg");
        REQUIRE(1 == room.told.size());
        CHECK(std::make_pair(5, std::string("0xff8cff[from Kim @ Quick Play EU 1] gg")) == room.told[0]);
        social.Deliver("lee@io", "notice", "You have 2 unread messages; /r answers the last (Kim).");
        CHECK("You have 2 unread messages; /r answers the last (Kim)." == room.Last());
        social.Deliver("nobody@io", "message", "lost");
        social.Deliver("", "message", "lost");
        CHECK(2 == room.told.size());
    }

    TEST_CASE("UTF-8 From The Platform Becomes The Game's Latin-1")
    {
        CHECK("caf\xe9" == gPlatformSocial::ToLatin1("caf\xc3\xa9"));
        CHECK("[from Kim] ?? ok" == gPlatformSocial::ToLatin1("[from Kim] \xec\x95\x88\xeb\x85\x95 ok"));
        CHECK("ab" == gPlatformSocial::ToLatin1("a\x01" "b"));
    }

    TEST_CASE("Request And Answer JSON")
    {
        gPlatformSocialRequest request;
        request.gid = "Kim@io";
        request.command = "friend";
        request.target = "Lee";
        request.netId = 9;
        nlohmann::json const body = nlohmann::json::parse(gPlatformSocial::RequestJson("boot-1", request));
        CHECK("boot-1" == body["bootId"].get<std::string>());
        CHECK("Kim@io" == body["gid"].get<std::string>());
        CHECK("friend" == body["command"].get<std::string>());
        CHECK("Lee" == body["target"].get<std::string>());
        CHECK(9 == body["netId"].get<int>());
        CHECK(body.find("text") == body.end());

        gPlatformSocialAnswer answer;
        std::string error;
        REQUIRE(gPlatformSocial::TryParseAnswer(R"({"result":"sent","lines":["[to Lee] hi"]})", answer, error));
        CHECK("sent" == answer.result);
        REQUIRE(1 == answer.lines.size());
        CHECK_FALSE(gPlatformSocial::TryParseAnswer(R"({"result":"sent"})", answer, error));
        CHECK_FALSE(gPlatformSocial::TryParseAnswer("nope", answer, error));
    }
}

// *************************************************************
// the social thread against a fake platform
// *************************************************************

namespace
{
struct cSocialPlatform
{
    std::mutex mutex{};
    std::condition_variable condition{};
    std::vector<gPlatformHttpRequest> social{};
};

class cSocialSender final : public gPlatformHttpSender
{
public:
    explicit cSocialSender(std::shared_ptr<cSocialPlatform> platform) : platform_(std::move(platform)) {}

private:
    gPlatformHttpResponse DoSend(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) override
    {
        gPlatformHttpResponse response;
        std::unique_lock<std::mutex> lock(platform_->mutex);
        if (request.url.find("/commands?") != std::string::npos)
        {
            platform_->condition.wait_for(lock, milliseconds(20), [&] { return cancel.load(); });
            response.status = 200;
            response.body = R"({"commands":[],"cursor":0})";
        }
        else if (request.url.find("/social") != std::string::npos)
        {
            platform_->social.push_back(request);
            response.status = 200;
            response.body = R"({"result":"requested","lines":["Friend request sent to Lee."]})";
        }
        else if (request.url.find("/heartbeat") != std::string::npos)
            response.status = 204;
        else if (request.url.find("/events") != std::string::npos)
        {
            response.status = 200;
            response.body = R"({"accepted":1,"duplicates":0})";
        }
        else
        {
            response.status = 200;
            response.body = R"({"keys":[]})";
        }
        platform_->condition.notify_all();
        return response;
    }

    std::shared_ptr<cSocialPlatform> platform_;
};

class cSocialRegistered final
{
public:
    explicit cSocialRegistered(ePlatform& platform) : platform_(platform) { ePlatform::Register(&platform_); }
    ~cSocialRegistered() { ePlatform::Unregister(&platform_); }

private:
    ePlatform& platform_;
};

template <class Pred>
bool PumpSocialUntil(Pred pred, milliseconds limit = milliseconds(5000))
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

TEST_SUITE("gPlatformSocial Http")
{
    TEST_CASE("The Social Thread Asks The Platform And The Player Is Told")
    {
        auto platform = std::make_shared<cSocialPlatform>();
        cSocialRoom room;
        room.conns[1] = 3;
        room.names["lee"] = 9;

        gPlatformHttpConfig config;
        config.apiUrl = "http://platform.test/";
        config.serverId = "dev-local-1";
        config.serverKey = "secret-key";
        config.social = true;
        config.flushTimeout = milliseconds(500);
        config.commandPollMinInterval = milliseconds(1);
        config.log = [&room](std::string const& line) { room.log.push_back(line); };
        config.countPlayers = [] { return ePlatformPlayerCounts(); };
        config.rosterPlayers = [] { return std::vector<ePlatformRosterPlayer>(); };
        config.room.log = config.log;
        config.room.refuse = [](int, std::string const&) {};
        config.socialEffects = room.Effects();
        config.socialEffects.send = nullptr; // through the social thread

        std::unique_ptr<gPlatformHttp> http(new gPlatformHttp(config, std::unique_ptr<gPlatformHttpSender>(new cSocialSender(platform))));
        cSocialRegistered registered(*http);

        REQUIRE(http->Social().Submit(Player(1, 3, "Kim", "Kim@io"), "/f lee", Clock::now()));
        REQUIRE(PumpSocialUntil([&] { return !room.told.empty(); }));
        CHECK(std::make_pair(3, std::string("Friend request sent to Lee.")) == room.told.back());
        {
            std::lock_guard<std::mutex> lock(platform->mutex);
            REQUIRE(1 == platform->social.size());
            gPlatformHttpRequest const& request = platform->social[0];
            CHECK("POST" == request.method);
            CHECK("http://platform.test/v1/servers/dev-local-1/social" == request.url);
            CHECK("secret-key" == request.bearer);
            nlohmann::json const body = nlohmann::json::parse(request.body);
            CHECK("friend" == body["command"].get<std::string>());
            CHECK(9 == body["netId"].get<int>());
        }
        http->Shutdown();
    }
}
