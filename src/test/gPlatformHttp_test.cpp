#include <nlohmann/json.hpp>

#include "gPlatformHttp.h"
#include "gPlatformToken.h"
#include "tConfiguration.h"
#include "monocypher-ed25519.h"
#include "ePlayer.h"
#include "nNetwork.h"

#include "doctest.h"

#include <condition_variable>
#include <algorithm>
#include <deque>
#include <map>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

// Tests for the HTTP platform implementation against a fake platform: the sender is
// replaced, so nothing touches the network. Only built with --enable-platform.

namespace
{
std::int64_t const s_now = 1790000030;

ePlatformPlayerCounts Counts(int humans, int bots, int capacity, int clients)
{
    ePlatformPlayerCounts counts;
    counts.humans = humans;
    counts.bots = bots;
    counts.capacity = capacity;
    counts.clients = clients;
    return counts;
}

// what the fake platform saw and how it answers; shared with the sender the platform owns.
// Events POSTs and key GETs land in requests; heartbeats and command polls separately.
struct cFakePlatformState
{
    std::mutex mutex{};
    std::condition_variable condition{};
    std::vector<gPlatformHttpRequest> requests{};
    std::deque<int> postStatuses{}; // scripted answers, then defaultPostStatus
    std::deque<int> getStatuses{};
    int defaultPostStatus{200};
    std::string keysBody{};

    std::vector<gPlatformHttpRequest> heartbeats{};
    std::deque<int> heartbeatStatuses{}; // then 204

    // command polls: a scripted answer if there is one, else an empty list after holdFor
    std::vector<gPlatformHttpRequest> polls{};
    std::deque<std::pair<int, std::string>> commandAnswers{};
    std::chrono::milliseconds holdFor{20};

    std::size_t Count(char const* method)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t count = 0;
        for (auto const& request : requests)
            if (request.method == method)
                ++count;
        return count;
    }

    std::vector<gPlatformHttpRequest> Requests(char const* method)
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<gPlatformHttpRequest> out;
        for (auto const& request : requests)
            if (request.method == method)
                out.push_back(request);
        return out;
    }

    // blocks until at least count requests of that method arrived
    bool WaitFor(char const* method, std::size_t count)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return condition.wait_for(lock, std::chrono::seconds(5), [&] {
            std::size_t seen = 0;
            for (auto const& request : requests)
                if (request.method == method)
                    ++seen;
            return seen >= count;
        });
    }

    std::vector<gPlatformHttpRequest> Heartbeats()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return heartbeats;
    }

    std::vector<gPlatformHttpRequest> Polls()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return polls;
    }

    void AnswerCommands(std::string body, int status = 200)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            commandAnswers.emplace_back(status, std::move(body));
        }
        condition.notify_all();
    }
};

// the after= of a command poll URL, -1 if there is none
std::int64_t AfterOf(std::string const& url)
{
    std::size_t const at = url.find("after=");
    if (at == std::string::npos)
        return -1;
    return std::stoll(url.substr(at + 6));
}

class cFakeSender final : public gPlatformHttpSender
{
public:
    explicit cFakeSender(std::shared_ptr<cFakePlatformState> state) : state_(std::move(state)) {}

private:
    gPlatformHttpResponse DoSend(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) override
    {
        if (request.url.find("/commands?") != std::string::npos)
            return Poll(request, cancel);

        gPlatformHttpResponse response;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (request.url.find("/heartbeat") != std::string::npos)
            {
                state_->heartbeats.push_back(request);
                response.status = 204;
                if (!state_->heartbeatStatuses.empty())
                {
                    response.status = state_->heartbeatStatuses.front();
                    state_->heartbeatStatuses.pop_front();
                }
                if (response.status == 0)
                    response.error = "connection refused";
                state_->condition.notify_all();
                return response;
            }

            state_->requests.push_back(request);
            auto& script = request.method == "POST" ? state_->postStatuses : state_->getStatuses;
            if (!script.empty())
            {
                response.status = script.front();
                script.pop_front();
            }
            else
            {
                response.status = request.method == "POST" ? state_->defaultPostStatus : 200;
            }
            if (response.status == 0)
                response.error = "connection refused";
            else if (request.method == "GET")
                response.body = state_->keysBody;
            else if (response.status == 200)
                response.body = R"({"accepted":1,"duplicates":0})";
            else
                response.body = R"({"error":"nope"})";
        }
        state_->condition.notify_all();
        return response;
    }

    gPlatformHttpResponse Poll(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel)
    {
        gPlatformHttpResponse response;
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->polls.push_back(request);
        state_->condition.notify_all();

        auto const deadline = std::chrono::steady_clock::now() + state_->holdFor;
        while (state_->commandAnswers.empty() && !cancel && std::chrono::steady_clock::now() < deadline)
            state_->condition.wait_for(lock, std::chrono::milliseconds(2));

        if (!state_->commandAnswers.empty())
        {
            response.status = state_->commandAnswers.front().first;
            response.body = state_->commandAnswers.front().second;
            state_->commandAnswers.pop_front();
            if (response.status == 0)
                response.error = "connection refused";
            return response;
        }
        if (cancel)
        {
            response.error = "cancelled";
            return response;
        }
        response.status = 200;
        response.body = R"({"commands":[],"cursor":)" + std::to_string(std::max<std::int64_t>(0, AfterOf(request.url))) + "}";
        return response;
    }

    std::shared_ptr<cFakePlatformState> state_;
};

nlohmann::json LoadVectors()
{
    std::ifstream file(std::string(PLATFORM_CONTRACT_DIR) + "/test-vectors/login-token-v0.json");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return nlohmann::json::parse(text.str());
}

std::string VectorKeysBody(nlohmann::json const& vectors)
{
    nlohmann::json keys;
    keys["keys"] = nlohmann::json::array({vectors["key"]});
    return keys.dump();
}

std::string ValidToken(nlohmann::json const& vectors)
{
    return vectors["cases"][0]["token"].get<std::string>();
}

// a platform wired to a fake, with fast timing and a recorded log
class cHarness
{
public:
    std::shared_ptr<cFakePlatformState> state{std::make_shared<cFakePlatformState>()};
    std::vector<std::string> log{};
    std::vector<std::pair<int, std::string>> logins{};
    std::deque<gPlatformLoginOutcome> loginOutcomes{}; // then Authenticated
    gPlatformHttpConfig config{};

    // the server's side, all on the game thread
    ePlatformPlayerCounts counts{Counts(2, 1, 16, 2)};
    bool announce{true};
    std::vector<std::string> console{};
    std::vector<std::string> center{};
    std::vector<std::pair<int, std::string>> refused{};

    cHarness()
    {
        config.apiUrl = "http://platform.test/";
        config.serverId = "dev-local-1";
        config.serverKey = "secret-key";
        config.loginEnabled = true;
        config.version = "0.2.9-test";
        config.retryInitial = std::chrono::milliseconds(5);
        config.retryMax = std::chrono::milliseconds(20);
        config.flushTimeout = std::chrono::milliseconds(1000);
        config.keysRetryInitial = std::chrono::milliseconds(5);
        config.keysRetryMax = std::chrono::milliseconds(20);
        config.clock = [] { return s_now; };
        config.log = [this](std::string const& line) { log.push_back(line); };
        config.completeLogin = [this](int userID, std::string const& gid) {
            logins.emplace_back(userID, gid);
            if (loginOutcomes.empty())
                return gPlatformLoginOutcome::Authenticated;
            gPlatformLoginOutcome const outcome = loginOutcomes.front();
            loginOutcomes.pop_front();
            return outcome;
        };
        config.countPlayers = [this] { return counts; };
        config.room.masterAnnounce = [this] { return announce; };
        config.room.setMasterAnnounce = [this](bool on) { announce = on; };
        config.room.console = [this](std::string const& text) { console.push_back(text); };
        config.room.center = [this](std::string const& text) { center.push_back(text); };
        config.room.refuse = [this](int userID, std::string const& reason) { refused.emplace_back(userID, reason); };
        config.commandPollMinInterval = std::chrono::milliseconds(1);
        config.commandRetryInitial = std::chrono::milliseconds(5);
        config.commandRetryMax = std::chrono::milliseconds(20);
        config.heartbeatMinGap = std::chrono::milliseconds(20);
        config.heartbeatRetry = std::chrono::milliseconds(20);
        state->keysBody = VectorKeysBody(LoadVectors());
    }

    std::unique_ptr<gPlatformHttp> Start()
    {
        return std::unique_ptr<gPlatformHttp>(new gPlatformHttp(config, std::unique_ptr<gPlatformHttpSender>(new cFakeSender(state))));
    }

    bool Logged(std::string const& part) const
    {
        for (auto const& line : log)
            if (line.find(part) != std::string::npos)
                return true;
        return false;
    }
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

// runs the game thread side until pred holds (or 5 s pass)
template <class Pred>
bool Pump(Pred pred)
{
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        ePlatform::Drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// the events of all POSTs so far, in the order they were sent
std::vector<nlohmann::json> PostedEvents(cFakePlatformState& state)
{
    std::vector<nlohmann::json> events;
    for (auto const& request : state.Requests("POST"))
    {
        nlohmann::json const batch = nlohmann::json::parse(request.body);
        for (auto const& event : batch["events"])
            events.push_back(event);
    }
    return events;
}

// JSON text with sorted members, for comparisons that print well
std::string Canonical(std::string const& json)
{
    return nlohmann::json::parse(json).dump();
}

ePlatformRoundResult ContractRound()
{
    ePlatformRoundResult result;
    result.match = 1;
    result.round = 4;
    result.seconds = 83;
    ePlatformRoundPlayer tester;
    tester.name = "Tester";
    tester.gid = "g-7f3a9c01@retrocycles";
    tester.team = "Tester";
    tester.score = 12;
    tester.alive = true;
    tester.place = 1;
    ePlatformRoundPlayer bot;
    bot.name = "Bot 1";
    bot.team = "Bot 1";
    bot.bot = true;
    bot.score = 3;
    bot.alive = false;
    bot.place = 2;
    result.players.push_back(tester);
    result.players.push_back(bot);
    return result;
}

ePlatformPlayerRef Ref(char const* name, char const* gid, bool bot, char const* team)
{
    ePlatformPlayerRef ref;
    ref.name = name;
    ref.gid = gid;
    ref.bot = bot;
    ref.team = team;
    return ref;
}
} // namespace

TEST_SUITE("gPlatformHttp serialization")
{
    TEST_CASE("Events Have The Contract's Shape")
    {
        // platform-contract/README.md section 3, member for member
        CHECK(Canonical(gPlatformHttp::ServerStartedJson(1, 1790000000, "0.2.9.3")) ==
              Canonical(R"({ "seq": 1, "type": "server_started", "time": 1790000000,
                  "data": { "version": "0.2.9.3", "platformContract": "v0" } })"));

        CHECK(Canonical(gPlatformHttp::PlayerAuthenticatedJson(2, 1790000012, "g-7f3a9c01@retrocycles", "Tester", "token")) ==
              Canonical(R"({ "seq": 2, "type": "player_authenticated", "time": 1790000012,
                  "data": { "gid": "g-7f3a9c01@retrocycles", "name": "Tester", "method": "token" } })"));
        // the connection the login came in on, as client_connected names it; none (0) is left out
        CHECK(Canonical(gPlatformHttp::PlayerAuthenticatedJson(2, 1790000012, "g-7f3a9c01@retrocycles", "Tester", "token", 3)) ==
              Canonical(R"({ "seq": 2, "type": "player_authenticated", "time": 1790000012,
                  "data": { "gid": "g-7f3a9c01@retrocycles", "name": "Tester", "method": "token", "connection": 3 } })"));

        CHECK(Canonical(gPlatformHttp::ClientConnectedJson(2, 1790000011, 3, true, 17)) ==
              Canonical(R"({ "seq": 2, "type": "client_connected", "time": 1790000011,
                  "data": { "connection": 3, "client": "browser", "protocol": 17 } })"));
        CHECK("desktop" == nlohmann::json::parse(gPlatformHttp::ClientConnectedJson(1, 0, 1, false, 16))["data"]["client"].get<std::string>());
        CHECK(Canonical(gPlatformHttp::ClientConnectedJson(4, 1790000013, 2, false, 16, "203.0.113.7")) ==
              Canonical(R"({ "seq": 4, "type": "client_connected", "time": 1790000013,
                  "data": { "connection": 2, "client": "desktop", "protocol": 16, "ip": "203.0.113.7" } })"));

        CHECK(Canonical(gPlatformHttp::RoundEndedJson(3, 1790000140, ContractRound())) ==
              Canonical(R"({ "seq": 3, "type": "round_ended", "time": 1790000140,
                  "data": { "match": 1, "round": 4, "seconds": 83, "players": [
                    { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "bot": false, "team": "Tester",
                      "score": 12, "alive": true, "place": 1 },
                    { "name": "Bot 1", "gid": null, "bot": true, "team": "Bot 1", "score": 3, "alive": false,
                      "place": 2 } ] } })"));
    }

    TEST_CASE("Match Events Have The Contract's Shape")
    {
        ePlatformMatchStart start;
        start.match = 3;
        start.limitRounds = 10;
        start.limitMinutes = 10;
        start.limitScore = 100000;
        CHECK(Canonical(gPlatformHttp::MatchStartedJson(4, 1790000200, start)) ==
              Canonical(R"({ "seq": 4, "type": "match_started", "time": 1790000200,
                  "data": { "match": 3, "limits": { "rounds": 10, "minutes": 10, "score": 100000 } } })"));

        ePlatformDeath death;
        death.match = 3;
        death.round = 4;
        death.victim = Ref("Tester", "g-7f3a9c01@retrocycles", false, "Tester");
        death.hasKiller = true;
        death.killer = Ref("Bot 1", "", true, "Bot 1");
        death.cause = "frag";
        CHECK(Canonical(gPlatformHttp::DeathJson(5, 1790000210, death)) ==
              Canonical(R"({ "seq": 5, "type": "death", "time": 1790000210,
                  "data": { "match": 3, "round": 4,
                    "victim": { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "bot": false, "team": "Tester" },
                    "killer": { "name": "Bot 1", "gid": null, "bot": true, "team": "Bot 1" },
                    "cause": "frag" } })"));

        death.hasKiller = false;
        death.cause = "suicide";
        nlohmann::json const suicide = nlohmann::json::parse(gPlatformHttp::DeathJson(6, 0, death));
        CHECK(suicide["data"]["killer"].is_null());
        CHECK("suicide" == suicide["data"]["cause"].get<std::string>());

        ePlatformMatchResult result;
        result.match = 3;
        result.completed = true;
        result.rounds = 10;
        result.winner = "Tester";
        ePlatformMatchPlayer tester;
        tester.player = Ref("Tester", "g-7f3a9c01@retrocycles", false, "Tester");
        tester.score = 42;
        tester.place = 1;
        tester.rounds = 10;
        ePlatformMatchPlayer leaver;
        leaver.player = Ref("Leaver", "", false, "Leaver");
        leaver.score = 5;
        leaver.place = 0;
        leaver.rounds = 3;
        leaver.leftEarly = true;
        result.players.push_back(tester);
        result.players.push_back(leaver);
        CHECK(Canonical(gPlatformHttp::MatchEndedJson(7, 1790000900, result)) ==
              Canonical(R"({ "seq": 7, "type": "match_ended", "time": 1790000900,
                  "data": { "match": 3, "result": "completed", "rounds": 10, "winner": "Tester", "players": [
                    { "name": "Tester", "gid": "g-7f3a9c01@retrocycles", "bot": false, "team": "Tester",
                      "score": 42, "place": 1, "rounds": 10, "leftEarly": false },
                    { "name": "Leaver", "gid": null, "bot": false, "team": "Leaver",
                      "score": 5, "place": null, "rounds": 3, "leftEarly": true } ] } })"));

        result.completed = false;
        nlohmann::json const aborted = nlohmann::json::parse(gPlatformHttp::MatchEndedJson(8, 0, result));
        CHECK("aborted" == aborted["data"]["result"].get<std::string>());
        CHECK(aborted["data"]["winner"].is_null());
    }

    TEST_CASE("A Bot Never Carries A Global Id")
    {
        nlohmann::json const death = nlohmann::json::parse(gPlatformHttp::DeathJson(1, 0, [] {
            ePlatformDeath d;
            d.victim = Ref("Bot 2", "not-a-gid@x", true, "");
            d.cause = "other";
            return d;
        }()));
        CHECK(death["data"]["victim"]["gid"].is_null());
        CHECK(death["data"]["victim"]["team"].is_null());
    }

    TEST_CASE("Members Keep The Contract's Order")
    {
        CHECK(R"({"seq":1,"type":"server_started","time":1790000000,"data":{"version":"0.2.9.3","platformContract":"v0"}})" ==
              gPlatformHttp::ServerStartedJson(1, 1790000000, "0.2.9.3"));
    }

    TEST_CASE("A Player Without A Team Has A Null Team")
    {
        ePlatformRoundResult result;
        result.round = 1;
        result.players.push_back(ePlatformRoundPlayer{});
        nlohmann::json const event = nlohmann::json::parse(gPlatformHttp::RoundEndedJson(1, 0, result));
        CHECK(event["data"]["players"][0]["team"].is_null());
        CHECK(event["data"]["players"][0]["gid"].is_null());
    }

    TEST_CASE("Batches Carry The Boot Id")
    {
        std::vector<ePlatformEvent> events(2);
        events[0].body = gPlatformHttp::ServerStartedJson(1, 5, "v");
        events[1].body = gPlatformHttp::PlayerAuthenticatedJson(2, 6, "a@b", "A", "krawall");
        nlohmann::json const batch = nlohmann::json::parse(gPlatformHttp::BatchJson("boot-1", events));
        CHECK("boot-1" == batch["bootId"].get<std::string>());
        REQUIRE(2 == batch["events"].size());
        CHECK(1 == batch["events"][0]["seq"].get<int>());
        CHECK("krawall" == batch["events"][1]["data"]["method"].get<std::string>());
        CHECK(2 == batch.size());
    }

    TEST_CASE("Game Strings Become UTF-8")
    {
        CHECK("Zo\xC3\xAB" == gPlatformHttp::ToUtf8("Zo\xEB"));      // Latin-1
        CHECK("Zo\xC3\xAB" == gPlatformHttp::ToUtf8("Zo\xC3\xAB"));  // already UTF-8
        CHECK("plain" == gPlatformHttp::ToUtf8("plain"));
        CHECK("\xC3\x83\xC2\xA9" == gPlatformHttp::ToUtf8("\xC3\xA9\xFF").substr(0, 4)); // invalid UTF-8: all Latin-1
        // and whatever comes in, the JSON is valid
        CHECK("\xC3\xBF\xC3\xBE" == nlohmann::json::parse(gPlatformHttp::PlayerAuthenticatedJson(1, 0, "x@y", "\xFF\xFE", "token"))["data"]["name"].get<std::string>());
    }

    TEST_CASE("URLs")
    {
        CHECK("http://x/v1/servers/dev-local-1/events" == gPlatformHttp::EventsUrl("http://x/", "dev-local-1"));
        CHECK("http://x/v1/servers/a%20b%2Fc/events" == gPlatformHttp::EventsUrl("http://x", "a b/c"));
        CHECK("https://p.example/api/v1/auth/keys" == gPlatformHttp::KeysUrl("https://p.example/api//"));
    }
}

TEST_SUITE("gPlatformHttp")
{
    TEST_CASE("Startup Fetches The Login Keys")
    {
        cHarness harness;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        auto const gets = harness.state->Requests("GET");
        REQUIRE(1 == gets.size());
        CHECK("http://platform.test/v1/auth/keys" == gets[0].url);
        CHECK(gets[0].bearer.empty());
        CHECK(harness.Logged("1 login token key(s) loaded"));
    }

    TEST_CASE("Key Fetches Are Retried Until They Work")
    {
        cHarness harness;
        harness.state->getStatuses = {503, 0};
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));
        CHECK(3 == harness.state->Count("GET"));
        CHECK(harness.Logged("retrying with backoff"));
    }

    TEST_CASE("Events Are Posted In Order, With The Bearer Key")
    {
        cHarness harness;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        ePlatform::ServerStarted();
        ePlatform::RoundEnded(ContractRound());
        ePlatform::Death(ePlatformDeath{});
        ePlatform::MatchEnded(ePlatformMatchResult{});

        REQUIRE(Pump([&] { return PostedEvents(*harness.state).size() >= 4; }));

        auto const posts = harness.state->Requests("POST");
        for (auto const& post : posts)
        {
            CHECK("http://platform.test/v1/servers/dev-local-1/events" == post.url);
            CHECK("secret-key" == post.bearer);
            CHECK(platform->BootId() == nlohmann::json::parse(post.body)["bootId"].get<std::string>());
        }
        auto const events = PostedEvents(*harness.state);
        REQUIRE(4 == events.size());
        CHECK(1 == events[0]["seq"].get<int>());
        CHECK("server_started" == events[0]["type"].get<std::string>());
        CHECK("0.2.9-test" == events[0]["data"]["version"].get<std::string>());
        CHECK(s_now == events[0]["time"].get<std::int64_t>());
        CHECK(2 == events[1]["seq"].get<int>());
        CHECK("round_ended" == events[1]["type"].get<std::string>());
        CHECK("death" == events[2]["type"].get<std::string>());
        CHECK(4 == events[3]["seq"].get<int>());
        CHECK("match_ended" == events[3]["type"].get<std::string>());
        CHECK(22 == platform->BootId().size());
    }

    TEST_CASE("Boot Ids Differ Between Runs")
    {
        cHarness harness;
        auto first = harness.Start();
        auto second = harness.Start();
        CHECK(first->BootId() != second->BootId());
    }

    TEST_CASE("Server And Network Errors Are Retried With The Same Batch")
    {
        cHarness harness;
        harness.state->postStatuses = {503, 0, 200};
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        ePlatform::ServerStarted();
        REQUIRE(harness.state->WaitFor("POST", 3));

        auto posts = harness.state->Requests("POST");
        CHECK(posts[0].body == posts[1].body);
        CHECK(posts[1].body == posts[2].body);

        // later events follow with the next seq
        ePlatform::RoundEnded(ContractRound());
        REQUIRE(harness.state->WaitFor("POST", 4));
        posts = harness.state->Requests("POST");
        auto const last = nlohmann::json::parse(posts[3].body)["events"];
        REQUIRE(1 == last.size());
        CHECK(2 == last[0]["seq"].get<int>());

        REQUIRE(Pump([&] { return harness.Logged("delivered again after 2 failed attempt(s)"); }));
        CHECK(harness.Logged("retrying with backoff"));
    }

    TEST_CASE("Refused Batches Are Dropped And Logged")
    {
        cHarness harness;
        harness.state->postStatuses = {400};
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        ePlatform::ServerStarted();
        REQUIRE(harness.state->WaitFor("POST", 1));
        ePlatform::RoundEnded(ContractRound());
        REQUIRE(harness.state->WaitFor("POST", 2));

        auto const posts = harness.state->Requests("POST");
        auto const second = nlohmann::json::parse(posts[1].body)["events"];
        REQUIRE(1 == second.size());
        CHECK(2 == second[0]["seq"].get<int>());
        REQUIRE(Pump([&] { return harness.Logged("dropped 1 event(s), the platform refused them: HTTP 400"); }));
    }

    TEST_CASE("The Bearer Key Never Reaches The Log")
    {
        cHarness harness;
        harness.state->postStatuses = {401, 0};
        auto platform = harness.Start();
        {
            cPlatformGuard guard(*platform);
            ePlatform::ServerStarted();
            REQUIRE(harness.state->WaitFor("POST", 1));
            ePlatform::RoundEnded(ContractRound());
            REQUIRE(harness.state->WaitFor("POST", 2));
            Pump([&] { return harness.log.size() >= 3; });
        }
        platform->Shutdown();

        REQUIRE(!harness.log.empty());
        for (auto const& line : harness.log)
            CHECK(line.find("secret-key") == std::string::npos);
    }

    TEST_CASE("Shutdown Delivers What Is Queued")
    {
        cHarness harness;
        auto platform = harness.Start();
        {
            cPlatformGuard guard(*platform);
            ePlatform::ServerStarted();
            for (int i = 0; i < 5; ++i)
                ePlatform::RoundEnded(ContractRound());
        }
        platform->Shutdown();

        auto const events = PostedEvents(*harness.state);
        REQUIRE(6 == events.size());
        for (int i = 0; i < 6; ++i)
            CHECK(i + 1 == events[i]["seq"].get<int>());
    }

    TEST_CASE("Shutdown Gives Up On An Unreachable Platform")
    {
        cHarness harness;
        harness.state->defaultPostStatus = 0;
        harness.config.flushTimeout = std::chrono::milliseconds(100);
        auto platform = harness.Start();
        {
            cPlatformGuard guard(*platform);
            ePlatform::ServerStarted();
            REQUIRE(harness.state->WaitFor("POST", 1));
        }

        auto const before = std::chrono::steady_clock::now();
        platform->Shutdown();
        auto const took = std::chrono::steady_clock::now() - before;

        CHECK(took < std::chrono::seconds(2));
        // still undelivered: nothing claims otherwise
        CHECK(!harness.Logged("delivered again"));
    }

    TEST_CASE("Login Tokens Are Verified And Completed")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));

        REQUIRE(1 == harness.logins.size());
        CHECK(3 == harness.logins[0].first);
        CHECK("g-7f3a9c01@retrocycles" == harness.logins[0].second);
        CHECK(harness.Logged("user 3 logged in as g-7f3a9c01@retrocycles"));

        // the same token again is a replay
        platform->HandleLoginToken(5, tString(ValidToken(vectors).c_str()));
        CHECK(1 == harness.logins.size());
        CHECK(harness.Logged("login token from user 5 refused: jti already used"));
        for (auto const& line : harness.log)
            CHECK(line.find(ValidToken(vectors).substr(0, 40)) == std::string::npos);
    }

    TEST_CASE("Tokens Wait For Their Player")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.loginOutcomes = {gPlatformLoginOutcome::NoPlayerYet, gPlatformLoginOutcome::NoPlayerYet};
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
        CHECK(1 == harness.logins.size());
        CHECK(!harness.Logged("logged in"));

        // a second token for the same connection is not taken while one waits
        platform->HandleLoginToken(3, tString("x.y.z"));
        CHECK(harness.Logged("ignored another login token from user 3"));

        ePlatform::Drain();
        ePlatform::Drain();
        CHECK(3 == harness.logins.size());
        CHECK(harness.Logged("user 3 logged in as g-7f3a9c01@retrocycles"));

        ePlatform::Drain();
        CHECK(3 == harness.logins.size());
    }

    TEST_CASE("A Token Whose Player Never Comes Is Dropped")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.config.loginPendingTimeout = std::chrono::milliseconds(20);
        harness.loginOutcomes = std::deque<gPlatformLoginOutcome>(1000, gPlatformLoginOutcome::NoPlayerYet);
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
        REQUIRE(Pump([&] { return harness.Logged("no player appeared in time"); }));
    }

    TEST_CASE("Tokens Wait For The Keys")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.state->getStatuses = {503, 503};
        harness.config.keysRetryInitial = std::chrono::milliseconds(50);
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
        CHECK(harness.logins.empty());
        CHECK(!harness.Logged("refused"));

        REQUIRE(Pump([&] { return harness.logins.size() == 1; }));
        CHECK("g-7f3a9c01@retrocycles" == harness.logins[0].second);
    }

    TEST_CASE("Unknown Keys Are Refreshed, At Most Once Per Interval")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        auto key = vectors["key"];
        key["kid"] = "other";
        nlohmann::json keys;
        keys["keys"] = nlohmann::json::array({key});
        harness.state->keysBody = keys.dump();

        SUBCASE("within the interval of the startup fetch, the token is refused at once")
        {
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            CHECK(harness.Logged("login token from user 3 refused: unknown kid"));
            CHECK(1 == harness.state->Count("GET"));
        }
        SUBCASE("after the interval, one refresh is tried first")
        {
            harness.config.keyRefreshInterval = std::chrono::milliseconds(0);
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            REQUIRE(Pump([&] { return harness.Logged("refused: unknown kid"); }));
            CHECK(2 == harness.state->Count("GET"));
            CHECK(harness.logins.empty());
        }
    }

    TEST_CASE("A Login That Does Not Go Through Leaves No User Id Behind")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.config.loginPendingTimeout = std::chrono::milliseconds(20);

        SUBCASE("refused")
        {
            harness.loginOutcomes = {gPlatformLoginOutcome::Refused};
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            CHECK(harness.Logged("refused by the server"));
            CHECK(platform->Moderation().UserIdOf(3).empty());
        }
        SUBCASE("no player yet, then never")
        {
            harness.loginOutcomes = std::deque<gPlatformLoginOutcome>(1000, gPlatformLoginOutcome::NoPlayerYet);
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            CHECK(platform->Moderation().UserIdOf(3).empty()); // while it waits
            REQUIRE(Pump([&] { return harness.Logged("no player appeared in time"); }));
            CHECK(platform->Moderation().UserIdOf(3).empty());
        }
        SUBCASE("a refused second login keeps the first one's")
        {
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));
            platform->Moderation().SetUserId(3, "u_first");
            harness.loginOutcomes = {gPlatformLoginOutcome::Refused};

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            CHECK("u_first" == platform->Moderation().UserIdOf(3));
        }
        SUBCASE("authenticated")
        {
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
            CHECK("u_test_0001" == platform->Moderation().UserIdOf(3));
        }
    }

    TEST_CASE("A Connection Cannot Flood The Server With Tokens")
    {
        cHarness harness;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        for (int i = 0; i < 50; ++i)
            platform->HandleLoginToken(3, tString("not.a.token"));

        // five refusals and one notice, then silence
        CHECK(6 == harness.log.size() - 1); // minus "key(s) loaded"
        CHECK(harness.Logged("user 3 sent too many login tokens"));

        // other connections are not affected
        platform->HandleLoginToken(4, tString("not.a.token"));
        CHECK(harness.Logged("login token from user 4 refused: malformed token"));
    }

    TEST_CASE("Tokens Are Ignored Unless PLATFORM_LOGIN Is Set")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.config.loginEnabled = false;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        platform->HandleLoginToken(3, tString(ValidToken(vectors).c_str()));
        CHECK(harness.logins.empty());
        CHECK(harness.Logged("PLATFORM_LOGIN is 0"));
    }
}

namespace
{
gPlatformCommand Command(std::int64_t id, char const* type)
{
    gPlatformCommand command;
    command.id = id;
    command.type = type;
    return command;
}

// the room with recorded effects, for the tests without threads
class cRoomHarness
{
public:
    bool announce{true};
    std::vector<std::string> console{};
    std::vector<std::string> center{};
    std::vector<std::pair<int, std::string>> refused{};
    std::vector<std::string> log{};
    gPlatformRoom room{Effects()};
    gPlatformRoom::Clock::time_point const t0{gPlatformRoom::Clock::now()};

    gPlatformRoom::Clock::time_point At(int milliseconds) const { return t0 + std::chrono::milliseconds(milliseconds); }

private:
    gPlatformRoomEffects Effects()
    {
        gPlatformRoomEffects effects;
        effects.masterAnnounce = [this] { return announce; };
        effects.setMasterAnnounce = [this](bool on) { announce = on; };
        effects.console = [this](std::string const& text) { console.push_back(text); };
        effects.center = [this](std::string const& text) { center.push_back(text); };
        effects.refuse = [this](int userID, std::string const& reason) { refused.emplace_back(userID, reason); };
        effects.log = [this](std::string const& line) { log.push_back(line); };
        return effects;
    }
};

std::vector<nlohmann::json> HeartbeatBodies(cFakePlatformState& state)
{
    std::vector<nlohmann::json> bodies;
    for (auto const& request : state.Heartbeats())
        bodies.push_back(nlohmann::json::parse(request.body));
    return bodies;
}
} // namespace

TEST_SUITE("gPlatformHttp room serialization")
{
    TEST_CASE("Heartbeats Have The Contract's Shape")
    {
        gPlatformHeartbeat heartbeat;
        heartbeat.bootId = "boot-1";
        heartbeat.time = 1790000000;
        heartbeat.state = gPlatformRoomState::Open;
        heartbeat.counts = Counts(3, 2, 16, 4);
        heartbeat.mode = "qp-ffa";
        heartbeat.version = "0.2.9.3";
        heartbeat.uptime = 3600;
        heartbeat.lastCommandId = 17;
        heartbeat.hostId = "h-eu-01";
        heartbeat.region = "eu";
        heartbeat.protocolMin = 0;
        heartbeat.protocolMax = 17;
        heartbeat.platformLogin = true;
        ePlatformRosterPlayer tester;
        tester.netId = 12;
        tester.gid = "g-7f3a9c01@retrocycles";
        tester.ping = 42;
        tester.browser = true;
        ePlatformRosterPlayer desktop;
        desktop.netId = 15;
        desktop.ping = 118;
        desktop.spectator = true;
        heartbeat.players = {tester, desktop};
        heartbeat.moderation.chatFilter = true;
        heartbeat.moderation.sanctions = true;
        heartbeat.moderation.reports = false;

        // platform-contract/README.md section 3, member for member
        CHECK(Canonical(gPlatformHttp::HeartbeatJson(heartbeat)) ==
              Canonical(R"({ "bootId": "boot-1", "time": 1790000000, "state": "open", "humans": 3, "bots": 2,
                  "capacity": 16, "clients": 4, "mode": "qp-ffa", "version": "0.2.9.3", "platformContract": "v0",
                  "uptime": 3600, "lastCommandId": 17, "hostId": "h-eu-01", "region": "eu",
                  "protocolMin": 0, "protocolMax": 17, "platformLogin": true,
                  "moderation": { "chatFilter": true, "sanctions": true, "reports": false },
                  "players": [ { "netId": 12, "gid": "g-7f3a9c01@retrocycles", "ping": 42, "spectator": false, "client": "browser" },
                               { "netId": 15, "gid": null, "ping": 118, "spectator": true, "client": "desktop" } ] })"));

        // an empty room lists nobody; a huge one is cut at MaxRosterPlayers
        heartbeat.players.clear();
        nlohmann::json const empty = nlohmann::json::parse(gPlatformHttp::HeartbeatJson(heartbeat))["players"];
        CHECK(empty.is_array());
        CHECK(empty.empty());
        heartbeat.players.assign(gPlatformHttp::MaxRosterPlayers + 5, tester);
        CHECK(gPlatformHttp::MaxRosterPlayers == nlohmann::json::parse(gPlatformHttp::HeartbeatJson(heartbeat))["players"].size());
        heartbeat.players.clear();

        // unset optional settings: mode is null, host and region are left out
        heartbeat.mode.clear();
        heartbeat.hostId.clear();
        heartbeat.region.clear();
        heartbeat.state = gPlatformRoomState::Draining;
        nlohmann::json const json = nlohmann::json::parse(gPlatformHttp::HeartbeatJson(heartbeat));
        CHECK(json["mode"].is_null());
        CHECK(json.find("hostId") == json.end());
        CHECK(json.find("region") == json.end());
        CHECK("draining" == json["state"].get<std::string>());
        CHECK(std::string("closing") == gPlatformRoom::StateName(gPlatformRoomState::Closing));
    }

    TEST_CASE("Room Channel URLs")
    {
        CHECK("http://x/v1/servers/dev-local-1/heartbeat" == gPlatformHttp::HeartbeatUrl("http://x/", "dev-local-1"));
        CHECK("http://x/v1/servers/a%20b/commands?wait=25" == gPlatformHttp::CommandsUrl("http://x", "a b", -1, 25));
        CHECK("http://x/v1/servers/r1/commands?after=0&wait=0" == gPlatformHttp::CommandsUrl("http://x", "r1", 0, 0));
        CHECK("http://x/v1/servers/r1/commands?after=17&wait=25" == gPlatformHttp::CommandsUrl("http://x", "r1", 17, 25));
    }

    TEST_CASE("Command Answers Are Parsed")
    {
        std::vector<gPlatformCommand> commands;
        std::int64_t cursor = 3;
        std::string error;

        SUBCASE("the contract's example")
        {
            REQUIRE(gPlatformHttp::TryParseCommands(R"({ "commands": [
                { "id": 18, "type": "drain", "time": 1790000100, "data": { "message": "Bye" } } ], "cursor": 18 })",
                                                    commands, cursor, error));
            REQUIRE(1 == commands.size());
            CHECK(18 == commands[0].id);
            CHECK("drain" == commands[0].type);
            CHECK("Bye" == commands[0].message);
            CHECK(18 == cursor);
        }
        SUBCASE("sorted by id, repeats and unusable ids dropped, odd members tolerated")
        {
            REQUIRE(gPlatformHttp::TryParseCommands(R"({ "commands": [
                { "id": 9, "type": "message", "data": { "text": "hi", "center": true } },
                { "id": 4, "type": "closing", "data": { "seconds": 9999, "message": 5 } },
                { "id": 9, "type": "message" },
                { "id": 0, "type": "drain" }, { "id": "7", "type": "drain" }, { "type": "drain" }, 12,
                { "id": 6, "type": ["x"] },
                { "id": 5, "type": "closing", "data": { "seconds": -3 } } ], "cursor": 9 })",
                                                    commands, cursor, error));
            REQUIRE(4 == commands.size());
            CHECK(4 == commands[0].id);
            CHECK(gPlatformRoom::MaxClosingSeconds == commands[0].seconds);
            CHECK(commands[0].message.empty());
            CHECK(5 == commands[1].id);
            CHECK(0 == commands[1].seconds);
            CHECK(6 == commands[2].id);
            CHECK(commands[2].type.empty()); // unknown: ignored and acknowledged
            CHECK(9 == commands[3].id);
            CHECK("hi" == commands[3].text);
            CHECK(commands[3].center);
            CHECK(-1 == Command(1, "closing").seconds); // no seconds: the default
            CHECK(9 == cursor);
        }
        SUBCASE("an empty answer keeps the platform's cursor, a missing one ours")
        {
            REQUIRE(gPlatformHttp::TryParseCommands(R"({"commands":[],"cursor":42})", commands, cursor, error));
            CHECK(42 == cursor);
            REQUIRE(gPlatformHttp::TryParseCommands(R"({"commands":[]})", commands, cursor, error));
            CHECK(42 == cursor);
            REQUIRE(gPlatformHttp::TryParseCommands(R"({"commands":[{"id":50,"type":"undrain"}],"cursor":1})", commands, cursor, error));
            CHECK(50 == cursor);
        }
        SUBCASE("a long list is taken in parts")
        {
            nlohmann::json answer;
            answer["commands"] = nlohmann::json::array();
            for (int id = 1; id <= 150; ++id)
                answer["commands"].push_back({{"id", id}, {"type", "message"}});
            answer["cursor"] = 150;
            REQUIRE(gPlatformHttp::TryParseCommands(answer.dump(), commands, cursor, error));
            CHECK(gPlatformHttp::MaxCommandsPerPoll == commands.size());
            CHECK(100 == cursor);
        }
        SUBCASE("unusable answers")
        {
            CHECK(!gPlatformHttp::TryParseCommands("not json", commands, cursor, error));
            CHECK(!gPlatformHttp::TryParseCommands("[]", commands, cursor, error));
            CHECK(!gPlatformHttp::TryParseCommands(R"({"commands":{}})", commands, cursor, error));
            CHECK("no commands list" == error);
            CHECK(3 == cursor);
        }
    }
}

TEST_SUITE("gPlatformRoom")
{
    TEST_CASE("Drain Stops Announcing And Refuses New Connections")
    {
        cRoomHarness harness;

        // connections before the drain stay
        harness.room.UserLoggedIn(2);
        harness.room.Tick(harness.At(0));
        CHECK(harness.refused.empty());

        gPlatformCommand drain = Command(1, "drain");
        drain.message = "Try the other room.";
        CHECK(harness.room.Apply(drain, harness.At(0)));
        CHECK(gPlatformRoomState::Draining == harness.room.State());
        CHECK(!harness.announce);

        harness.room.UserLoggedIn(3);
        harness.room.UserLoggedIn(3);
        harness.room.UserLoggedIn(4);
        harness.room.UserLoggedOut(4); // gone before it was refused
        harness.room.UserLoggedIn(0);  // the server itself
        CHECK(harness.refused.empty()); // not from inside the login callback
        harness.room.Tick(harness.At(10));
        REQUIRE(1 == harness.refused.size());
        CHECK(3 == harness.refused[0].first);
        CHECK("Try the other room." == harness.refused[0].second);

        CHECK(harness.room.Apply(Command(2, "undrain"), harness.At(20)));
        CHECK(gPlatformRoomState::Open == harness.room.State());
        CHECK(harness.announce);
        harness.room.UserLoggedIn(5);
        harness.room.Tick(harness.At(30));
        CHECK(1 == harness.refused.size());
    }

    TEST_CASE("Undrain Restores The Announce Setting As It Was")
    {
        cRoomHarness harness;
        harness.announce = false;
        harness.room.Apply(Command(1, "drain"), harness.At(0));
        harness.room.Apply(Command(2, "closing"), harness.At(0)); // already not announcing
        harness.room.Apply(Command(3, "undrain"), harness.At(0));
        CHECK(!harness.announce);

        harness.announce = true;
        harness.room.Apply(Command(4, "drain"), harness.At(0));
        harness.room.Apply(Command(5, "drain"), harness.At(0)); // again: still restores true
        CHECK(!harness.announce);
        harness.room.Apply(Command(6, "undrain"), harness.At(0));
        CHECK(harness.announce);
    }

    TEST_CASE("A Drain Without A Message Refuses With The Default One")
    {
        cRoomHarness harness;
        harness.room.Apply(Command(1, "drain"), harness.At(0));
        harness.room.UserLoggedIn(7);
        harness.room.Tick(harness.At(0));
        REQUIRE(1 == harness.refused.size());
        CHECK(harness.refused[0].second.find("join another server") != std::string::npos);
    }

    TEST_CASE("Commands Apply Once, In Order")
    {
        cRoomHarness harness;
        gPlatformCommand hello = Command(5, "message");
        hello.text = "hello";

        CHECK(harness.room.Apply(hello, harness.At(0)));
        CHECK(!harness.room.Apply(hello, harness.At(0)));           // the same id again
        CHECK(!harness.room.Apply(Command(3, "drain"), harness.At(0))); // an older one
        CHECK(5 == harness.room.LastAppliedId());
        CHECK(gPlatformRoomState::Open == harness.room.State());
        CHECK(std::vector<std::string>{"hello"} == harness.console);
    }

    TEST_CASE("Unknown Commands Are Ignored And Acknowledged")
    {
        cRoomHarness harness;
        CHECK(harness.room.Apply(Command(8, "sanction"), harness.At(0)));
        CHECK(harness.room.Apply(Command(9, ""), harness.At(0)));
        CHECK(9 == harness.room.LastAppliedId());
        CHECK(gPlatformRoomState::Open == harness.room.State());
        CHECK(harness.announce);
        CHECK(harness.console.empty());
        REQUIRE(2 == harness.log.size());
        CHECK(harness.log[0].find("unknown type 'sanction' ignored") != std::string::npos);
    }

    TEST_CASE("Messages Go To The Console, And To The Centre On Request")
    {
        cRoomHarness harness;
        gPlatformCommand message = Command(1, "message");
        message.text = "Line\none\x01";
        harness.room.Apply(message, harness.At(0));
        CHECK(std::vector<std::string>{"Lineone"} == harness.console);
        CHECK(harness.center.empty());

        message = Command(2, "message");
        message.text = std::string(150, 'a') + std::string(50, 'b') + "\xC3\xA9" + std::string(10, 'c');
        message.center = true;
        harness.room.Apply(message, harness.At(0));
        REQUIRE(2 == harness.console.size());
        CHECK(200 == harness.console[1].size()); // cut, and not inside the two byte character
        CHECK(std::vector<std::string>{harness.console[1]} == harness.center);

        // an empty text shows nothing
        harness.room.Apply(Command(3, "message"), harness.At(0));
        CHECK(2 == harness.console.size());
    }

    TEST_CASE("Closing Counts Down")
    {
        cRoomHarness harness;
        gPlatformCommand closing = Command(1, "closing");
        closing.message = "Closing, press Quick Play.";
        closing.seconds = 12;
        harness.room.Apply(closing, harness.At(0));

        CHECK(gPlatformRoomState::Closing == harness.room.State());
        CHECK(!harness.announce);
        CHECK(std::vector<std::string>{"Closing, press Quick Play. (closing in 12 seconds)"} == harness.console);
        CHECK(std::vector<std::string>{"Closing, press Quick Play."} == harness.center);

        harness.room.Tick(harness.At(1500)); // 10.5 s left: nothing yet
        CHECK(1 == harness.center.size());
        harness.room.Tick(harness.At(2500)); // 9.5 s left: the 10 s step
        REQUIRE(2 == harness.center.size());
        CHECK("Server closing in 10 seconds." == harness.center[1]);
        CHECK("Server closing in 10 seconds." == harness.console.back());
        harness.room.Tick(harness.At(2600));
        CHECK(2 == harness.center.size());

        harness.room.Tick(harness.At(7200)); // 4.8 s left: the 5 s step, centre only
        CHECK("Server closing in 5 seconds." == harness.center.back());
        CHECK(2 == harness.console.size());

        harness.room.Tick(harness.At(10500)); // a stall: 1.5 s left, only the 2 s step is spoken
        CHECK("Server closing in 2 seconds." == harness.center.back());
        CHECK(4 == harness.center.size());

        harness.room.Tick(harness.At(12000));
        CHECK("Server closing now." == harness.center.back());
        CHECK(5 == harness.center.size());
        harness.room.Tick(harness.At(20000));
        CHECK(5 == harness.center.size());

        // new connections are refused with the closing message; the state stays
        harness.room.UserLoggedIn(3);
        harness.room.Tick(harness.At(20000));
        REQUIRE(1 == harness.refused.size());
        CHECK("Closing, press Quick Play." == harness.refused[0].second);
        CHECK(gPlatformRoomState::Closing == harness.room.State());
    }

    TEST_CASE("Closing Defaults To 30 Seconds And Undrain Stops It")
    {
        cRoomHarness harness;
        harness.room.Apply(Command(1, "closing"), harness.At(0));
        CHECK(harness.console[0].find("(closing in 30 seconds)") != std::string::npos);
        CHECK(harness.center[0].find("join another server") != std::string::npos);

        harness.room.Apply(Command(2, "undrain"), harness.At(1000));
        CHECK(gPlatformRoomState::Open == harness.room.State());
        CHECK(harness.announce);
        harness.room.Tick(harness.At(40000));
        CHECK(1 == harness.center.size());
    }
}

TEST_SUITE("gPlatformHeartbeatSchedule")
{
    TEST_CASE("Heartbeats Are Due At Start, On Change At Most Once Per Gap, And Every Interval")
    {
        using ms = std::chrono::milliseconds;
        gPlatformHeartbeatSchedule schedule(ms(10000), ms(1000));
        auto const t0 = gPlatformHeartbeatSchedule::Clock::now();

        CHECK(schedule.Due(t0));
        schedule.Sent(t0);
        CHECK(!schedule.Due(t0 + ms(9999)));
        CHECK(schedule.Due(t0 + ms(10000)));

        // a change right after a heartbeat waits for the gap
        schedule.MarkChanged();
        CHECK(!schedule.Due(t0 + ms(999)));
        CHECK(schedule.Due(t0 + ms(1000)));
        schedule.Sent(t0 + ms(1000));
        schedule.MarkChanged();
        schedule.MarkChanged();
        CHECK(!schedule.Due(t0 + ms(1500)));
        CHECK(schedule.Due(t0 + ms(2000)));
        schedule.Sent(t0 + ms(2000));
        CHECK(!schedule.Due(t0 + ms(3000))); // nothing changed since

        // a failure brings the next one forward
        schedule.Failed(t0 + ms(4000));
        CHECK(!schedule.Due(t0 + ms(3999)));
        CHECK(schedule.Due(t0 + ms(4000)));
        schedule.Sent(t0 + ms(4000));
        CHECK(!schedule.Due(t0 + ms(5000)));
    }
}

TEST_SUITE("gPlatformHttp room")
{
    TEST_CASE("Heartbeats Report The Room")
    {
        cHarness harness;
        harness.config.mode = "qp-ffa";
        harness.config.hostId = "h-1";
        harness.config.region = "eu";
        harness.config.protocolVersions = [] { return std::make_pair(3, 17); };
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return !harness.state->Heartbeats().empty(); }));
        auto const request = harness.state->Heartbeats()[0];
        CHECK("POST" == request.method);
        CHECK("http://platform.test/v1/servers/dev-local-1/heartbeat" == request.url);
        CHECK("secret-key" == request.bearer);

        nlohmann::json const json = nlohmann::json::parse(request.body);
        CHECK(platform->BootId() == json["bootId"].get<std::string>());
        CHECK(s_now == json["time"].get<std::int64_t>());
        CHECK("open" == json["state"].get<std::string>());
        CHECK(2 == json["humans"].get<int>());
        CHECK(1 == json["bots"].get<int>());
        CHECK(16 == json["capacity"].get<int>());
        CHECK(2 == json["clients"].get<int>());
        CHECK("qp-ffa" == json["mode"].get<std::string>());
        CHECK("0.2.9-test" == json["version"].get<std::string>());
        CHECK("v0" == json["platformContract"].get<std::string>());
        CHECK(0 <= json["uptime"].get<std::int64_t>());
        CHECK(0 == json["lastCommandId"].get<std::int64_t>());
        CHECK("h-1" == json["hostId"].get<std::string>());
        CHECK("eu" == json["region"].get<std::string>());
        CHECK(3 == json["protocolMin"].get<int>());
        CHECK(17 == json["protocolMax"].get<int>());
        CHECK(json["platformLogin"].get<bool>());
        CHECK(json["players"].is_array());
        // the harness turns no moderation on
        CHECK(!json["moderation"]["chatFilter"].get<bool>());
        CHECK(!json["moderation"]["sanctions"].get<bool>());
        CHECK(!json["moderation"]["reports"].get<bool>());

        // heartbeats are not events
        CHECK(0 == harness.state->Count("POST"));
    }

    TEST_CASE("Heartbeats Report The Moderation In Force")
    {
        cHarness harness;
        harness.config.chatFilter = false;
        harness.config.sanctions = true;
        harness.config.reports = true;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return !harness.state->Heartbeats().empty(); }));
        nlohmann::json const json = nlohmann::json::parse(harness.state->Heartbeats()[0].body);
        REQUIRE(json["moderation"].is_object());
        CHECK(!json["moderation"]["chatFilter"].get<bool>());
        CHECK(json["moderation"]["sanctions"].get<bool>());
        CHECK(json["moderation"]["reports"].get<bool>());
    }

    TEST_CASE("A Join Or Leave Is Reported Promptly, Rate Limited")
    {
        cHarness harness;
        harness.config.playerCountInterval = std::chrono::milliseconds(0);
        harness.config.heartbeatMinGap = std::chrono::milliseconds(200);
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 1; }));

        harness.counts.humans = 3;
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 2; }));
        CHECK(3 == HeartbeatBodies(*harness.state).back()["humans"].get<int>());

        // players come and go on every frame for 700 ms: a heartbeat every 200 ms at most
        auto const start = std::chrono::steady_clock::now();
        auto const end = start + std::chrono::milliseconds(700);
        while (std::chrono::steady_clock::now() < end)
        {
            ++harness.counts.humans;
            ePlatform::Drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto const extra = harness.state->Heartbeats().size() - 2;
        CHECK(extra >= 2);
        CHECK(extra <= 5);
    }

    TEST_CASE("Heartbeats Carry The Roster")
    {
        cHarness harness;
        ePlatformRosterPlayer player;
        player.netId = 7;
        player.gid = "g-1@io";
        player.ping = 35;
        std::vector<ePlatformRosterPlayer> roster{player};
        harness.config.rosterPlayers = [&roster] { return roster; };
        harness.config.heartbeatInterval = std::chrono::milliseconds(50);
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return !harness.state->Heartbeats().empty(); }));
        nlohmann::json json = HeartbeatBodies(*harness.state).back();
        REQUIRE(1 == json["players"].size());
        CHECK(7 == json["players"][0]["netId"].get<int>());
        CHECK("g-1@io" == json["players"][0]["gid"].get<std::string>());
        CHECK(35 == json["players"][0]["ping"].get<int>());
        CHECK(!json["players"][0]["spectator"].get<bool>());

        // the next heartbeat lists the roster as it is then (a ping change alone is not a prompt one)
        roster[0].ping = 80;
        roster[0].spectator = true;
        auto const before = harness.state->Heartbeats().size();
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() > before; }));
        json = HeartbeatBodies(*harness.state).back();
        CHECK(80 == json["players"][0]["ping"].get<int>());
        CHECK(json["players"][0]["spectator"].get<bool>());
    }

    TEST_CASE("A Login Is Reported Promptly In The Roster")
    {
        cHarness harness;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 1; }));

        // nothing changed: the next one would be 10 s away; a login brings it forward, and
        // the roster (ePlatform::RosterPlayers(), not injected here) names the player's gid
        auto player = tRefPtr<ePlayerNetID>::Make();
#ifdef KRAWALL_SERVER
        player->Authenticate(tString("g-5@io"));
        REQUIRE(player->IsAuthenticated());
#endif
        ePlatform::PlayerAuthenticated(player, tString("g-5@io"));
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 2; }));
#ifdef KRAWALL_SERVER
        bool listed = false;
        nlohmann::json const roster = HeartbeatBodies(*harness.state).back()["players"];
        for (auto const& entry : roster)
            listed = listed || (entry["netId"].get<int>() == static_cast<int>(player->ID()) && entry["gid"].is_string()
                                && entry["gid"].get<std::string>() == "g-5@io");
        INFO(roster.dump());
        CHECK(listed);
#endif
    }

    TEST_CASE("A Connection Still Logging In Is Reported Promptly")
    {
        cHarness harness;
        harness.config.playerCountInterval = std::chrono::milliseconds(0);
        harness.config.loginEnabled = false;
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 1; }));

        // no player yet, but the connection holds a slot (occupancy for Quick Play)
        ++harness.counts.clients;
        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() == 2; }));
        nlohmann::json const json = HeartbeatBodies(*harness.state).back();
        CHECK(3 == json["clients"].get<int>());
        CHECK(2 == json["humans"].get<int>());
        CHECK(!json["platformLogin"].get<bool>());
        // not injected: the protocol versions this server accepts right now
        CHECK(sn_CurrentVersion().Min() == json["protocolMin"].get<int>());
        CHECK(sn_CurrentVersion().Max() == json["protocolMax"].get<int>());
    }

    TEST_CASE("A Failed Heartbeat Is Followed By A Fresh One Soon")
    {
        cHarness harness;
        harness.state->heartbeatStatuses = {503};
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return harness.state->Heartbeats().size() >= 2; }));
        REQUIRE(Pump([&] { return harness.Logged("heartbeats delivered again"); }));
        CHECK(harness.Logged("sending a heartbeat failed (HTTP 503)"));
    }

    TEST_CASE("Commands Are Polled, Applied In Order And Acknowledged")
    {
        cHarness harness;
        harness.state->AnswerCommands(R"({ "commands": [
            { "id": 7, "type": "message", "time": 1790000100, "data": { "text": "second" } },
            { "id": 5, "type": "drain", "time": 1790000100, "data": { "message": "Closed for maintenance." } } ],
            "cursor": 7 })");
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->LastCommandId() == 7; }));
        CHECK(gPlatformRoomState::Draining == platform->RoomState());
        CHECK(!harness.announce);
        CHECK(std::vector<std::string>{"second"} == harness.console);
        CHECK(harness.Logged("command 5: draining"));

        // the acknowledgement and the new state go out promptly
        REQUIRE(Pump([&] {
            auto const bodies = HeartbeatBodies(*harness.state);
            return !bodies.empty() && bodies.back()["lastCommandId"].get<std::int64_t>() == 7;
        }));
        CHECK("draining" == HeartbeatBodies(*harness.state).back()["state"].get<std::string>());

        // the first poll had no cursor, the following ones continue after 7
        REQUIRE(harness.state->WaitFor("GET", 1));
        REQUIRE(Pump([&] { return harness.state->Polls().size() >= 2; }));
        auto const polls = harness.state->Polls();
        CHECK(-1 == AfterOf(polls[0].url));
        CHECK(7 == AfterOf(polls[1].url));
        CHECK(polls[0].url.find("wait=25") != std::string::npos);
        CHECK("secret-key" == polls[0].bearer);
        CHECK(std::chrono::seconds(40) == polls[0].timeout);

        // a new connection is refused with the drain message
        platform->UserLoggedIn(4);
        REQUIRE(Pump([&] { return !harness.refused.empty(); }));
        CHECK(4 == harness.refused[0].first);
        CHECK("Closed for maintenance." == harness.refused[0].second);
    }

    TEST_CASE("Commands Handed Out Again Are Applied Once")
    {
        cHarness harness;
        std::string const answer = R"({ "commands": [ { "id": 3, "type": "message", "data": { "text": "once", "center": true } } ], "cursor": 3 })";
        harness.state->AnswerCommands(answer);
        harness.state->AnswerCommands(answer);
        harness.state->AnswerCommands(R"({ "commands": [ { "id": 3, "type": "message", "data": { "text": "once" } },
            { "id": 4, "type": "closing", "data": { "seconds": 0, "message": "Bye." } } ], "cursor": 4 })");
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->LastCommandId() == 4; }));
        // a zero second countdown is over on the same pass
        CHECK(std::vector<std::string>{"once", "Bye.", "Server closing now."} == harness.console);
        CHECK(std::vector<std::string>{"once", "Bye.", "Server closing now."} == harness.center);
        CHECK(gPlatformRoomState::Closing == platform->RoomState());
    }

    TEST_CASE("Unknown Command Types Are Acknowledged")
    {
        cHarness harness;
        harness.state->AnswerCommands(R"({ "commands": [ { "id": 11, "type": "room_template", "data": { "gid": "x@y" } } ], "cursor": 11 })");
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] {
            auto const bodies = HeartbeatBodies(*harness.state);
            return !bodies.empty() && bodies.back()["lastCommandId"].get<std::int64_t>() == 11;
        }));
        CHECK("open" == HeartbeatBodies(*harness.state).back()["state"].get<std::string>());
        CHECK(harness.Logged("command 11 of unknown type 'room_template' ignored"));
    }

    TEST_CASE("A Move Command Is Acknowledged, Runs And Is Reported")
    {
        cHarness harness;
        std::vector<std::pair<int, ePlatformMoveNotice>> sent;
        harness.config.moves.players = [] {
            gPlatformMovePlayer player;
            player.conn = 2;
            player.gid = "g-7f3a9c01@retrocycles";
            player.inTeam = true;
            return std::vector<gPlatformMovePlayer>{player};
        };
        harness.config.moves.followsMoves = [](int conn) { return conn == 2; };
        harness.config.moves.sendMove = [&](int conn, ePlatformMoveNotice const& notice) {
            sent.emplace_back(conn, notice);
            return true;
        };
        harness.config.moves.matchRunning = [] { return true; };
        harness.state->AnswerCommands(R"({ "commands": [ { "id": 21, "type": "move_players", "data": {
            "moveId": "mv_test", "when": "now", "reason": "admin", "gids": null,
            "target": { "serverId": "qp-b", "name": "Room B", "signalUrl": "ws://127.0.0.1:4621/" } } },
            { "id": 22, "type": "move_players", "data": { "moveId": "mv_wait", "when": "match_end", "gids": null,
            "target": { "serverId": "qp-c", "name": "Room C", "signalUrl": "/signal/qp-c" } } },
            { "id": 23, "type": "move_players", "data": { "when": "now" } } ], "cursor": 23 })");
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->LastCommandId() == 23; }));
        REQUIRE(Pump([&] { return !sent.empty(); }));
        CHECK(2 == sent[0].first);
        CHECK("mv_test" == sent[0].second.moveId);
        CHECK("Room B" == sent[0].second.name);
        CHECK(1 == platform->Moves().Waiting()); // mv_wait waits for the match end
        CHECK(harness.Logged("command 23: unusable move (no usable moveId)"));

        REQUIRE(Pump([&] {
            for (auto const& event : PostedEvents(*harness.state))
                if (event["type"] == "players_moved")
                    return true;
            return false;
        }));
        nlohmann::json moved;
        for (auto const& event : PostedEvents(*harness.state))
            if (event["type"] == "players_moved")
                moved = event["data"];
        CHECK("mv_test" == moved["moveId"].get<std::string>());
        CHECK("done" == moved["outcome"].get<std::string>());
        CHECK((nlohmann::json::parse(R"(["g-7f3a9c01@retrocycles"])") == moved["sent"]));

        // the match ends: the waiting move runs on the next pass
        ePlatformMatchResult result;
        result.match = 1;
        ePlatform::MatchEnded(result);
        REQUIRE(Pump([&] { return platform->Moves().Waiting() == 0; }));
        // the player is on the way already: nobody is told twice
        CHECK(1 == sent.size());
    }

    TEST_CASE("Command Poll Errors Back Off And Recover")
    {
        cHarness harness;
        harness.state->AnswerCommands("", 503);
        harness.state->AnswerCommands("", 0);
        harness.state->AnswerCommands("<html>", 200);
        harness.state->AnswerCommands(R"({ "commands": [ { "id": 2, "type": "drain" } ], "cursor": 2 })");
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        REQUIRE(Pump([&] { return platform->LastCommandId() == 2; }));
        REQUIRE(Pump([&] { return harness.Logged("command polling works again"); }));
        CHECK(harness.Logged("polling commands failed (HTTP 503); retrying with backoff"));
        std::size_t failures = 0;
        for (auto const& line : harness.log)
            if (line.find("polling commands failed") != std::string::npos)
                ++failures;
        CHECK(1 == failures);
        // the failed polls had no cursor yet: nothing was skipped
        for (auto const& poll : harness.state->Polls())
            if (AfterOf(poll.url) >= 0)
                CHECK(2 == AfterOf(poll.url));
    }

    TEST_CASE("Shutdown Does Not Wait For The Long Poll")
    {
        cHarness harness;
        harness.state->holdFor = std::chrono::seconds(30);
        harness.config.flushTimeout = std::chrono::milliseconds(100);
        auto platform = harness.Start();
        REQUIRE(Pump([&] { return !harness.state->Polls().empty(); }));

        auto const before = std::chrono::steady_clock::now();
        platform->Shutdown();
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(2));
    }
}

namespace
{
// a room ticket signed with the test vectors' private key
std::string RoomTicket(nlohmann::json const& vectors, std::string const& serverId, std::string const& jti)
{
    std::vector<std::uint8_t> seed;
    REQUIRE(gPlatformBase64Url::TryDecode(vectors["key"]["d_TEST_ONLY"].get<std::string>(), seed));
    std::uint8_t secretKey[64];
    std::uint8_t publicKey[32];
    crypto_ed25519_key_pair(secretKey, publicKey, seed.data());

    auto const encode = [](std::string const& text) {
        return gPlatformBase64Url::Encode(reinterpret_cast<std::uint8_t const*>(text.data()), text.size());
    };
    nlohmann::json const header{{"alg", "EdDSA"}, {"typ", "rc-room+jws"}, {"kid", vectors["key"]["kid"]}};
    nlohmann::json const payload{{"iss", "retrocycles"}, {"aud", serverId}, {"claim", "pc_test"},
                                 {"iat", s_now - 5}, {"exp", s_now + 55}, {"jti", jti}};
    std::string const input = encode(header.dump()) + "." + encode(payload.dump());
    std::uint8_t signature[64];
    crypto_ed25519_sign(signature, secretKey, reinterpret_cast<std::uint8_t const*>(input.data()), input.size());
    return input + "." + gPlatformBase64Url::Encode(signature, sizeof(signature));
}
} // namespace

TEST_SUITE("gPlatformHttp private room")
{
    TEST_CASE("A Private Room Keeps Only Connections With A Ticket")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.config.privateRoom = true;
        std::map<int, std::string> tickets;
        tickets[2] = RoomTicket(vectors, "dev-local-1", "r1");
        tickets[3] = RoomTicket(vectors, "other-room", "r2");
        tickets[5] = tickets[2]; // the same ticket again
        harness.config.roomTicketOf = [&tickets](int userID) { return tickets.count(userID) ? tickets[userID] : std::string(); };
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);
        REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

        platform->UserLoggedIn(2); // good ticket
        platform->UserLoggedIn(3); // another room's
        platform->UserLoggedIn(4); // none (a desktop client, or no link)
        platform->UserLoggedIn(5); // a replay
        REQUIRE(Pump([&] { return harness.refused.size() == 3; }));
        CHECK(3 == harness.refused.size());
        std::vector<int> users;
        for (auto const& refusal : harness.refused)
        {
            users.push_back(refusal.first);
            CHECK(refusal.second.find("private room") != std::string::npos);
        }
        std::sort(users.begin(), users.end());
        CHECK((std::vector<int>{3, 4, 5}) == users);
        CHECK(harness.Logged("connection 2 brought a ticket for claim pc_test"));
        CHECK(harness.Logged("refused connection 3 to the private room (aud != serverId)"));
        CHECK(harness.Logged("refused connection 4 to the private room (no ticket)"));
        CHECK(harness.Logged("refused connection 5 to the private room (jti already used)"));
        for (auto const& line : harness.log)
            CHECK(line.find(tickets[2].substr(0, 40)) == std::string::npos);
    }

    TEST_CASE("A Connection That Leaves Before Its Refusal Is Not Refused")
    {
        cHarness harness;
        harness.config.privateRoom = true;
        harness.config.roomTicketOf = [](int) { return std::string(); };
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        platform->UserLoggedIn(6);
        platform->UserLoggedOut(6);
        platform->UserLoggedIn(7);
        REQUIRE(Pump([&] { return !harness.refused.empty(); }));
        CHECK(1 == harness.refused.size());
        CHECK(7 == harness.refused[0].first);
    }

    TEST_CASE("An Ordinary Room Ignores Tickets")
    {
        cHarness harness;
        bool asked = false;
        harness.config.roomTicketOf = [&asked](int) { asked = true; return std::string(); };
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        platform->UserLoggedIn(2);
        CHECK(!asked);
        CHECK(harness.refused.empty());
    }

    TEST_CASE("A Ticket Waits For The Keys")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        harness.state->getStatuses = {503, 503};
        harness.config.keysRetryInitial = std::chrono::milliseconds(50);
        harness.config.privateRoom = true;
        std::string const ticket = RoomTicket(vectors, "dev-local-1", "r1");
        harness.config.roomTicketOf = [&ticket](int) { return ticket; };
        auto platform = harness.Start();
        cPlatformGuard guard(*platform);

        platform->UserLoggedIn(2);
        ePlatform::Drain();
        CHECK(harness.refused.empty());
        REQUIRE(Pump([&] { return harness.Logged("connection 2 brought a ticket"); }));
        ePlatform::Drain();
        CHECK(harness.refused.empty());
    }

    TEST_CASE("A Ticket With An Unknown Kid Asks For New Keys, At Most Once Per Interval")
    {
        nlohmann::json const vectors = LoadVectors();
        cHarness harness;
        auto key = vectors["key"];
        key["kid"] = "other";
        nlohmann::json keys;
        keys["keys"] = nlohmann::json::array({key});
        harness.state->keysBody = keys.dump();
        harness.config.privateRoom = true;
        std::string const ticket = RoomTicket(vectors, "dev-local-1", "r1");
        harness.config.roomTicketOf = [&ticket](int) { return ticket; };

        SUBCASE("after the interval, the keys are fetched again and the ticket is good")
        {
            harness.config.keyRefreshInterval = std::chrono::milliseconds(0);
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));
            {
                // the platform rotated its keys
                std::lock_guard<std::mutex> lock(harness.state->mutex);
                harness.state->keysBody = VectorKeysBody(vectors);
            }

            platform->UserLoggedIn(2);
            REQUIRE(Pump([&] { return harness.Logged("connection 2 brought a ticket"); }));
            CHECK(2 == harness.state->Count("GET"));
            ePlatform::Drain();
            CHECK(harness.refused.empty());
        }
        SUBCASE("within the interval of the startup fetch, the ticket is refused at once")
        {
            auto platform = harness.Start();
            cPlatformGuard guard(*platform);
            REQUIRE(Pump([&] { return platform->KeyCount() == 1; }));

            platform->UserLoggedIn(2);
            CHECK(harness.Logged("refused connection 2 to the private room (unknown kid)"));
            REQUIRE(Pump([&] { return harness.refused.size() == 1; }));
            CHECK(1 == harness.state->Count("GET"));
        }
    }
}

TEST_SUITE("gPlatformClosedRoom")
{
    TEST_CASE("A Private Room Without A Usable Platform Refuses Everyone")
    {
        std::vector<std::pair<int, std::string>> refused;
        gPlatformClosedRoom room([&refused](int userID, std::string const& reason) { refused.emplace_back(userID, reason); });
        cPlatformGuard guard(room);

        room.UserLoggedIn(2);
        room.UserLoggedIn(3);
        room.UserLoggedOut(3); // gone before its refusal
        ePlatform::Drain();
        REQUIRE(1 == refused.size());
        CHECK(2 == refused[0].first);
        CHECK(refused[0].second.find("private room") != std::string::npos);

        ePlatform::Drain();
        CHECK(1 == refused.size());
    }

    TEST_CASE("PLATFORM_PRIVATE Without PLATFORM_API_URL Closes The Room")
    {
        auto const& items = tConfItemBase::GetConfItemMap();
        auto const privateItem = items.find(tString("PLATFORM_PRIVATE"));
        auto const urlItem = items.find(tString("PLATFORM_API_URL"));
        REQUIRE(privateItem != items.end());
        REQUIRE(urlItem != items.end());
        auto const set = [](tConfItemBase* item, char const* value) {
            std::istringstream stream(value);
            item->ReadVal(stream);
        };
        set(urlItem->second, "");
        REQUIRE(ePlatform::Instance() == nullptr);

        SUBCASE("a public room stays stock")
        {
            set(privateItem->second, "0");
            gPlatformHttp::StartIfConfigured();
            CHECK(ePlatform::Instance() == nullptr);
        }
        SUBCASE("a private one refuses every connection")
        {
            set(privateItem->second, "1");
            gPlatformHttp::StartIfConfigured();
            CHECK(dynamic_cast<gPlatformClosedRoom*>(ePlatform::Instance()) != nullptr);
            gPlatformHttp::Stop();
            CHECK(ePlatform::Instance() == nullptr);
            set(privateItem->second, "0");
        }
    }
}
