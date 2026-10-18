#include <nlohmann/json.hpp>

#include "gPlatformLink.h"
#include "gPlatformHttp.h"

#include "doctest.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// Tests for /link (platform-contract/README.md section 3, "Linking old global ids"): what the
// room checks itself, one request per player, the words for each answer, the JSON, and the
// round trip through gPlatformHttp's link thread against a fake platform. Only built with
// --enable-platform.

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

// a room with recorded effects
struct cLinkRoom
{
    std::vector<std::pair<int, std::string>> told{};
    std::vector<gPlatformLinkRequest> sent{};
    std::vector<std::string> log{};
    std::map<int, int> conns{}; // player id -> connection; missing: gone

    gPlatformLinkEffects Effects()
    {
        gPlatformLinkEffects effects;
        effects.tell = [this](int conn, std::string const& text) { told.emplace_back(conn, text); };
        effects.connOf = [this](int playerId) {
            auto const found = conns.find(playerId);
            return found == conns.end() ? 0 : found->second;
        };
        effects.send = [this](gPlatformLinkRequest const& request) { sent.push_back(request); };
        effects.log = [this](std::string const& line) { log.push_back(line); };
        return effects;
    }

    std::string Last() const { return told.empty() ? std::string() : told.back().second; }
};

gPlatformLinkAnswer Answer(std::uint64_t id, std::string result, std::string gid = "Vet@forums", std::string handle = "nova")
{
    gPlatformLinkAnswer answer;
    answer.id = id;
    answer.status = 200;
    answer.result = std::move(result);
    answer.gid = std::move(gid);
    answer.handle = std::move(handle);
    return answer;
}

bool Contains(std::string const& text, std::string const& part) { return text.find(part) != std::string::npos; }
} // namespace

TEST_SUITE("gPlatformLink")
{
    TEST_CASE("Only /link Is Taken")
    {
        std::string args;
        CHECK(gPlatformLink::IsLinkCommand("/link K7Q-4M2", args));
        CHECK("K7Q-4M2" == args);
        CHECK(gPlatformLink::IsLinkCommand("/LINK   k7q 4m2  ", args));
        CHECK("k7q 4m2" == args);
        CHECK(gPlatformLink::IsLinkCommand("/link", args));
        CHECK(args.empty());
        CHECK(!gPlatformLink::IsLinkCommand("/linked", args));
        CHECK(!gPlatformLink::IsLinkCommand("link K7Q-4M2", args));
        CHECK(!gPlatformLink::IsLinkCommand("hello /link K7Q-4M2", args));

        cLinkRoom room;
        gPlatformLink link("retrocycles", room.Effects());
        CHECK(!link.Submit(Player(1, 1, "Vet", "Vet@forums"), "/linkx", Clock::now()));
        CHECK(!link.Submit(Player(1, 1, "Vet", "Vet@forums"), "gg", Clock::now()));
        CHECK(room.told.empty());
        CHECK(room.sent.empty());
    }

    TEST_CASE("The Room Answers What It Can Tell Itself")
    {
        cLinkRoom room;
        gPlatformLink link("retrocycles", room.Effects());
        auto const now = Clock::now();

        CHECK(link.Submit(Player(1, 3, "Vet", "Vet@forums"), "/link", now));
        CHECK(Contains(room.Last(), "Usage: /link CODE"));

        CHECK(link.Submit(Player(1, 3, "Vet"), "/link K7Q-4M2", now));
        CHECK(Contains(room.Last(), "Log in with the ID you want to link first"));
        CHECK(Contains(room.Last(), "/link K7Q-4M2 again"));

        CHECK(link.Submit(Player(1, 3, "Vet", "tester@"), "/link K7Q-4M2", now));
        CHECK(Contains(room.Last(), "this server's own account"));

        CHECK(link.Submit(Player(1, 3, "Vet", "nova@retrocycles"), "/link K7Q-4M2", now));
        CHECK(Contains(room.Last(), "is an ID of this platform already"));
        CHECK(link.Submit(Player(1, 3, "Vet", "g-0123abcd@Retrocycles.authentication.armagetronad.net"), "/link K7Q-4M2", now));
        CHECK(Contains(room.Last(), "is an ID of this platform already"));

        CHECK(5 == room.told.size());
        for (auto const& line : room.told)
            CHECK(3 == line.first);
        CHECK(room.sent.empty());
        CHECK(0 == link.Pending());
    }

    TEST_CASE("One Request Per Player, Three A Minute")
    {
        cLinkRoom room;
        room.conns[1] = 3;
        gPlatformLink link("retrocycles", room.Effects());
        auto const t0 = Clock::now();
        gPlatformChatPlayer const vet = Player(1, 3, "Vet", "Vet@forums");

        CHECK(link.Submit(vet, "/link k7q-4m2", t0));
        REQUIRE(1 == room.sent.size());
        CHECK("Vet@forums" == room.sent[0].gid);
        CHECK("Vet" == room.sent[0].name);
        CHECK("k7q-4m2" == room.sent[0].code);
        CHECK(1 == room.sent[0].playerId);
        CHECK(room.told.empty()); // nothing until the answer

        CHECK(link.Submit(vet, "/link K7Q-4M2", t0));
        CHECK("Still linking, one moment." == room.Last());
        CHECK(1 == room.sent.size());

        link.Answer(Answer(room.sent[0].id, "invalid_code"));
        CHECK(0 == link.Pending());
        CHECK(link.Submit(vet, "/link ABC-DEF", t0 + seconds(1)));
        link.Answer(Answer(room.sent[1].id, "invalid_code"));
        CHECK(link.Submit(vet, "/link ABC-DEG", t0 + seconds(2)));
        link.Answer(Answer(room.sent[2].id, "invalid_code"));
        CHECK(link.Submit(vet, "/link ABC-DEH", t0 + seconds(3)));
        CHECK("Please wait a minute before trying /link again." == room.Last());
        CHECK(3 == room.sent.size());

        // another player is not held up
        room.conns[2] = 4;
        CHECK(link.Submit(Player(2, 4, "Old", "Old@ct"), "/link ABC-DEF", t0 + seconds(3)));
        CHECK(4 == room.sent.size());

        // a minute later the first player may again
        CHECK(link.Submit(vet, "/link ABC-DEJ", t0 + seconds(62)));
        CHECK(5 == room.sent.size());
    }

    TEST_CASE("A Code Too Long Is Not Sent")
    {
        cLinkRoom room;
        gPlatformLink link("retrocycles", room.Effects());
        CHECK(link.Submit(Player(1, 3, "Vet", "Vet@forums"), "/link " + std::string(40, 'A'), Clock::now()));
        CHECK(room.sent.empty());
        CHECK(Contains(room.Last(), "That code is not valid"));
    }

    TEST_CASE("Each Answer In Plain Words, To The Player Who Asked")
    {
        cLinkRoom room;
        room.conns[1] = 3;
        gPlatformLink link("retrocycles", room.Effects());
        gPlatformChatPlayer const vet = Player(1, 3, "Vet", "Vet@forums");

        struct Case
        {
            char const* result;
            char const* says;
        };
        Case const cases[] = {
            {"linked", "Linked Vet@forums to the account nova. Your games as Vet@forums count for it now."},
            {"already_linked", "Vet@forums is linked to the account nova already."},
            {"invalid_code", "That code is not valid. Check it on the Linked IDs screen of your profile."},
            {"expired", "That code has expired. Get a new one on the Linked IDs screen of your profile."},
            {"used", "That code was used already. Get a new one on the Linked IDs screen of your profile."},
            {"linked_elsewhere", "Vet@forums is linked to another account. Unlink it there first."},
        };
        auto t = Clock::now();
        for (auto const& c : cases)
        {
            t += seconds(61);
            REQUIRE(link.Submit(vet, "/link K7Q-4M2", t));
            link.Answer(Answer(room.sent.back().id, c.result));
            CHECK(std::make_pair(3, std::string(c.says)) == room.told.back());
        }
        // the server log names the link
        REQUIRE(1 == room.log.size());
        CHECK("Platform: Vet linked Vet@forums to the account nova." == room.log[0]);

        // a result the room does not know: the platform's words, cleaned
        gPlatformLinkAnswer other = Answer(0, "too_many");
        other.message = "nova has 10 linked IDs already.\n";
        CHECK("nova has 10 linked IDs already." == gPlatformLink::TextFor(other));
        other.message.clear();
        CHECK("Linking did not work (too_many)." == gPlatformLink::TextFor(other));

        gPlatformLinkAnswer off;
        off.status = 404;
        CHECK("This server cannot link IDs." == gPlatformLink::TextFor(off));
        gPlatformLinkAnswer down;
        down.status = 0;
        CHECK("Linking is not available right now. Try again in a minute." == gPlatformLink::TextFor(down));
        down.status = 503;
        CHECK("Linking is not available right now. Try again in a minute." == gPlatformLink::TextFor(down));
    }

    TEST_CASE("A Player Who Left Is Not Told")
    {
        cLinkRoom room;
        room.conns[1] = 3;
        gPlatformLink link("retrocycles", room.Effects());
        REQUIRE(link.Submit(Player(1, 3, "Vet", "Vet@forums"), "/link K7Q-4M2", Clock::now()));
        room.conns.erase(1);
        link.Answer(Answer(room.sent[0].id, "linked"));
        CHECK(room.told.empty());
        CHECK(0 == link.Pending());
        // an answer nobody waits for is ignored
        link.Answer(Answer(99, "linked"));
        CHECK(room.told.empty());
    }

    TEST_CASE("Request And Answer JSON")
    {
        gPlatformLinkRequest request;
        request.gid = "Vet@forums";
        request.name = "V\xe9t"; // Latin-1
        request.code = "K7Q-4M2";
        nlohmann::json const body = nlohmann::json::parse(gPlatformLink::RequestJson("boot-1", request));
        CHECK("boot-1" == body["bootId"].get<std::string>());
        CHECK("Vet@forums" == body["gid"].get<std::string>());
        CHECK("V\xc3\xa9t" == body["name"].get<std::string>());
        CHECK("K7Q-4M2" == body["code"].get<std::string>());

        gPlatformLinkAnswer answer;
        std::string error;
        REQUIRE(gPlatformLink::TryParseAnswer(R"({"result":"linked","message":"Linked.","gid":"Vet@forums","handle":"nova","name":"Nova"})", answer, error));
        CHECK("linked" == answer.result);
        CHECK("Linked." == answer.message);
        CHECK("Vet@forums" == answer.gid);
        CHECK("nova" == answer.handle);
        REQUIRE(gPlatformLink::TryParseAnswer(R"({"result":"expired","message":"x","gid":null,"handle":null})", answer, error));
        CHECK(answer.handle.empty());
        CHECK(!gPlatformLink::TryParseAnswer(R"({"message":"x"})", answer, error));
        CHECK(!gPlatformLink::TryParseAnswer("[]", answer, error));
        CHECK(!gPlatformLink::TryParseAnswer("not json", answer, error));
    }
}

// *************************************************************
// the link thread against a fake platform
// *************************************************************

namespace
{
struct cLinkPlatform
{
    std::mutex mutex{};
    std::condition_variable condition{};
    std::vector<gPlatformHttpRequest> links{};
    std::deque<std::pair<int, std::string>> answers{}; // status, body
};

class cLinkSender final : public gPlatformHttpSender
{
public:
    explicit cLinkSender(std::shared_ptr<cLinkPlatform> platform) : platform_(std::move(platform)) {}

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
        else if (request.url.find("/links") != std::string::npos)
        {
            platform_->links.push_back(request);
            response.status = 200;
            response.body = R"({"result":"linked","message":"Linked.","gid":"Vet@forums","handle":"nova","name":"Nova"})";
            if (!platform_->answers.empty())
            {
                response.status = platform_->answers.front().first;
                response.body = platform_->answers.front().second;
                platform_->answers.pop_front();
            }
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

    std::shared_ptr<cLinkPlatform> platform_;
};

class cLinkRegistered final
{
public:
    explicit cLinkRegistered(ePlatform& platform) : platform_(platform) { ePlatform::Register(&platform_); }
    ~cLinkRegistered() { ePlatform::Unregister(&platform_); }

private:
    ePlatform& platform_;
};

template <class Pred>
bool PumpLinksUntil(Pred pred, milliseconds limit = milliseconds(5000))
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

TEST_SUITE("gPlatformLink Http")
{
    TEST_CASE("The Link Thread Asks The Platform And The Player Is Told")
    {
        auto platform = std::make_shared<cLinkPlatform>();
        cLinkRoom room;
        room.conns[1] = 3;

        gPlatformHttpConfig config;
        config.apiUrl = "http://platform.test/";
        config.serverId = "dev-local-1";
        config.serverKey = "secret-key";
        config.linking = true;
        config.flushTimeout = milliseconds(500);
        config.commandPollMinInterval = milliseconds(1);
        config.log = [&room](std::string const& line) { room.log.push_back(line); };
        config.countPlayers = [] { return ePlatformPlayerCounts(); };
        config.rosterPlayers = [] { return std::vector<ePlatformRosterPlayer>(); };
        config.room.log = config.log;
        config.room.refuse = [](int, std::string const&) {};
        config.link = room.Effects();
        config.link.send = nullptr; // through the link thread

        std::unique_ptr<gPlatformHttp> http(new gPlatformHttp(config, std::unique_ptr<gPlatformHttpSender>(new cLinkSender(platform))));
        cLinkRegistered registered(*http);
        gPlatformChatPlayer const vet = Player(1, 3, "Vet", "Vet@forums");

        REQUIRE(http->Links().Submit(vet, "/link k7q-4m2", Clock::now()));
        REQUIRE(PumpLinksUntil([&] { return !room.told.empty(); }));
        CHECK(std::make_pair(3, std::string("Linked Vet@forums to the account nova. Your games as Vet@forums count for it now.")) == room.told.back());
        {
            std::lock_guard<std::mutex> lock(platform->mutex);
            REQUIRE(1 == platform->links.size());
            gPlatformHttpRequest const& request = platform->links[0];
            CHECK("POST" == request.method);
            CHECK("http://platform.test/v1/servers/dev-local-1/links" == request.url);
            CHECK("secret-key" == request.bearer);
            nlohmann::json const body = nlohmann::json::parse(request.body);
            CHECK(http->BootId() == body["bootId"].get<std::string>());
            CHECK("Vet@forums" == body["gid"].get<std::string>());
            CHECK("k7q-4m2" == body["code"].get<std::string>());
        }

        // the platform failing: the player hears it, the server log says why
        {
            std::lock_guard<std::mutex> lock(platform->mutex);
            platform->answers.emplace_back(503, "busy");
            platform->answers.emplace_back(404, R"({"code":"linking_off"})");
        }
        REQUIRE(http->Links().Submit(vet, "/link K7Q-4M3", Clock::now()));
        REQUIRE(PumpLinksUntil([&] { return room.told.size() == 2; }));
        CHECK("Linking is not available right now. Try again in a minute." == room.told.back().second);
        bool logged = false;
        for (auto const& line : room.log)
            logged = logged || line.find("Platform: /link failed (HTTP 503") == 0;
        CHECK(logged);
        REQUIRE(http->Links().Submit(vet, "/link K7Q-4M4", Clock::now()));
        REQUIRE(PumpLinksUntil([&] { return room.told.size() == 3; }));
        CHECK("This server cannot link IDs." == room.told.back().second);

        http->Shutdown();
    }
}
