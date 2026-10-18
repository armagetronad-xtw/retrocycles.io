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
#include <curl/curl.h>

#include "gPlatformHttp.h"

#include "ePlayer.h"
#include "eTeam.h"
#include "gGame.h"
#include "gPlatformEvents.h"
#include "nNetwork.h"
#include "nServerInfo.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tRecorder.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <deque>
#include <random>

// TODO: spool undelivered events to disk and replay them after a restart

// *************************************************************
// settings (platform-contract/README.md section 4)
// *************************************************************

namespace
{
// a PLATFORM_* setting: owner level only (it decides where server data goes), never
// written to user.cfg
template <class T>
class gPlatformSetting final : public tConfItem<T>
{
public:
    gPlatformSetting(char const* title, char const* help, T& target)
        : tConfItemBase(title, tOutput(tString(help))), tConfItem<T>(title, tOutput(tString(help)), target)
    {
        this->requiredLevel = tAccessLevel_Owner;
    }

    bool Save() override { return false; }
};

// the bearer secret: never printed, never saved (and vetoed from recordings in tConfiguration.cpp)
class gPlatformSecretSetting final : public tConfItemBase
{
public:
    gPlatformSecretSetting(char const* title, char const* help, tString& target)
        : tConfItemBase(title, tOutput(tString(help))), target_(target)
    {
        requiredLevel = tAccessLevel_Owner;
    }

    void ReadVal(std::istream& s) override
    {
        tString line;
        line.ReadLine(s);
        std::string value(static_cast<char const*>(line));
        std::size_t const begin = value.find_first_not_of(" \t\r\n");
        std::size_t const end = value.find_last_not_of(" \t\r\n");
        value = begin == std::string::npos ? std::string() : value.substr(begin, end - begin + 1);

        if (value != static_cast<char const*>(target_))
        {
            target_ = value.c_str();
            changed = true;
            if (printChange)
                con << title << " changed (value hidden).\n";
        }
    }

    // reveals nothing, not even whether it is set
    void WriteVal(std::ostream&) override {}

    bool Save() override { return false; }
    bool CanSave() override { return false; }

private:
    tString& target_;
};

tString sg_platformApiUrl("");
gPlatformSetting<tString> sg_platformApiUrlConf("PLATFORM_API_URL", "Base URL of the platform service; empty: no platform", sg_platformApiUrl);

tString sg_platformServerId("");
gPlatformSetting<tString> sg_platformServerIdConf("PLATFORM_SERVER_ID", "This server's id at the platform", sg_platformServerId);

tString sg_platformServerKey("");
gPlatformSecretSetting sg_platformServerKeyConf("PLATFORM_SERVER_KEY", "Bearer secret for the platform; never logged or recorded", sg_platformServerKey);

bool sg_platformLogin = false;
gPlatformSetting<bool> sg_platformLoginConf("PLATFORM_LOGIN", "Accept platform login tokens (0/1)", sg_platformLogin);

bool sg_platformPrivate = false;
gPlatformSetting<bool> sg_platformPrivateConf("PLATFORM_PRIVATE", "Private room: refuse connections without a room ticket from the platform (0/1)", sg_platformPrivate);

int sg_platformRematchSeconds = 30;
gPlatformSetting<int> sg_platformRematchSecondsConf("PLATFORM_PRIVATE_REMATCH_SECONDS", "Private room: seconds after a match for every player to say /rematch before everyone is disconnected (0: matches roll on)", sg_platformRematchSeconds);

tString sg_platformAuthority("retrocycles");
gPlatformSetting<tString> sg_platformAuthorityConf("PLATFORM_AUTHORITY", "Authority part required in platform login global ids", sg_platformAuthority);

tString sg_platformMode("");
gPlatformSetting<tString> sg_platformModeConf("PLATFORM_MODE", "Mode id this server reports in platform heartbeats", sg_platformMode);

tString sg_platformHostId("");
gPlatformSetting<tString> sg_platformHostIdConf("PLATFORM_HOST_ID", "Host id this server reports in platform heartbeats", sg_platformHostId);

tString sg_platformRegion("");
gPlatformSetting<tString> sg_platformRegionConf("PLATFORM_REGION", "Region this server reports in platform heartbeats", sg_platformRegion);

// moderation (contract section 5)
bool sg_platformChatFilter = true;
gPlatformSetting<bool> sg_platformChatFilterConf("PLATFORM_CHAT_FILTER", "Check chat lines with the platform before delivering them (0/1)", sg_platformChatFilter);

int sg_platformChatTimeout = 150;
gPlatformSetting<int> sg_platformChatTimeoutConf("PLATFORM_CHAT_TIMEOUT", "Milliseconds a chat line waits for the platform's verdict before the local word filter decides", sg_platformChatTimeout);

bool sg_platformSanctions = true;
gPlatformSetting<bool> sg_platformSanctionsConf("PLATFORM_SANCTIONS", "Fetch and enforce the platform's sanctions (0/1)", sg_platformSanctions);

bool sg_platformReports = true;
gPlatformSetting<bool> sg_platformReportsConf("PLATFORM_REPORTS", "Handle the /report chat command (0/1)", sg_platformReports);

// linking old global ids (contract section 3)
bool sg_platformLink = true;
gPlatformSetting<bool> sg_platformLinkConf("PLATFORM_LINK", "Handle the /link chat command: link an old global id to a platform account (0/1)", sg_platformLink);
bool sg_platformSocial = true;
gPlatformSetting<bool> sg_platformSocialConf("PLATFORM_SOCIAL", "Friends and messages for desktop players: /f, /msg to a friend elsewhere, /r, /join (0/1)", sg_platformSocial);

int sg_platformIpBanGrace = 15;
gPlatformSetting<int> sg_platformIpBanGraceConf("PLATFORM_IP_BAN_GRACE", "Seconds a connection from a banned address has to log in", sg_platformIpBanGrace);

int sg_platformAccessAdmin = tAccessLevel_Admin;
gPlatformSetting<int> sg_platformAccessAdminConf("PLATFORM_ACCESS_ADMIN", "Access level of players whose platform login token has the admin role", sg_platformAccessAdmin);

int sg_platformAccessModerator = tAccessLevel_Moderator;
gPlatformSetting<int> sg_platformAccessModeratorConf("PLATFORM_ACCESS_MODERATOR", "Access level of players whose platform login token has the moderator role", sg_platformAccessModerator);

int sg_platformAccessStaff = tAccessLevel_Authenticated;
gPlatformSetting<int> sg_platformAccessStaffConf("PLATFORM_ACCESS_STAFF", "Access level of players whose platform login token has the staff role", sg_platformAccessStaff);

std::string sg_ToStd(tString const& s)
{
    return std::string(static_cast<char const*>(s));
}

// ASCII lower case (global ids are matched case-insensitively)
std::string sg_ToLower(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// suppresses console output once static destruction has begun
bool sg_platformQuiet = false;

// a random id for this server run, 128 bits, base64url
std::string sg_MakeBootId(void const* salt)
{
    std::random_device device;
    auto const ticks = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    auto const wall = static_cast<std::uint64_t>(std::time(nullptr));
    auto const address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(salt));
    // random_device alone is deterministic on some old MinGW runtimes, so mix in more
    std::seed_seq seed{device(), device(), device(), device(),
                       static_cast<unsigned>(ticks), static_cast<unsigned>(ticks >> 32),
                       static_cast<unsigned>(wall), static_cast<unsigned>(address), static_cast<unsigned>(address >> 32)};
    std::mt19937 generator(seed);

    std::uint8_t bytes[16];
    for (auto& byte : bytes)
        byte = static_cast<std::uint8_t>(generator() & 0xff);

    return gPlatformBase64Url::Encode(bytes, sizeof(bytes));
}

// percent-encodes everything but RFC 3986 unreserved characters
std::string sg_EncodePathSegment(std::string const& in)
{
    static char const hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : in)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0f]);
        }
    }
    return out;
}

std::string sg_TrimTrailingSlashes(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

// a response body shortened and made printable for the console
std::string sg_Snippet(std::string const& body)
{
    std::string out;
    for (char c : body.substr(0, 200))
        out.push_back((c >= 32 && c < 127) ? c : ' ');
    if (body.size() > 200)
        out += "...";
    return out;
}

std::string sg_DescribeResponse(gPlatformHttpResponse const& response)
{
    if (response.status == 0)
        return response.error.empty() ? std::string("no response") : response.error;
    return "HTTP " + std::to_string(response.status);
}
} // namespace

// *************************************************************
// libcurl sender
// *************************************************************

namespace
{
class gPlatformCurlSender final : public gPlatformHttpSender
{
public:
    gPlatformCurlSender() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~gPlatformCurlSender() override { curl_global_cleanup(); }

    gPlatformCurlSender(gPlatformCurlSender const&) = delete;
    gPlatformCurlSender& operator=(gPlatformCurlSender const&) = delete;

private:
    // answers larger than this are cut off (and fail)
    static std::size_t const MaxResponse = 1 << 20;

    static std::size_t Write(char* data, std::size_t size, std::size_t count, void* user)
    {
        auto& body = *static_cast<std::string*>(user);
        std::size_t const len = size * count;
        if (body.size() + len > MaxResponse)
            return 0;
        body.append(data, len);
        return len;
    }

    static int Progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
    {
        return static_cast<std::atomic<bool> const*>(user)->load() ? 1 : 0;
    }

    gPlatformHttpResponse DoSend(gPlatformHttpRequest const& request, std::atomic<bool> const& cancel) override
    {
        gPlatformHttpResponse response;

        // one handle per thread, kept: its connection to the platform is reused (no new TCP and
        // TLS handshake per request, which matters for the chat check's latency)
        thread_local Handle kept;
        if (kept.handle)
            curl_easy_reset(kept.handle);
        else
            kept.handle = curl_easy_init();
        CURL* handle = kept.handle;
        if (!handle)
        {
            response.error = "curl_easy_init failed";
            return response;
        }

        curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");
        std::string authorization;
        if (!request.bearer.empty())
        {
            authorization = "Authorization: Bearer " + request.bearer;
            headers = curl_slist_append(headers, authorization.c_str());
        }
        if (request.method == "POST")
        {
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(handle, CURLOPT_POST, 1L);
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        }

        char errorBuffer[CURL_ERROR_SIZE] = {};
        curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &gPlatformCurlSender::Write);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
        curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, errorBuffer);
        curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L); // never carry the bearer elsewhere
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, static_cast<long>(request.timeout.count()));
        curl_easy_setopt(handle, CURLOPT_USERAGENT, "Armagetron Advanced platform");
        curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &gPlatformCurlSender::Progress);
        curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &cancel);

        CURLcode const result = curl_easy_perform(handle);
        if (result == CURLE_OK)
        {
            long status = 0;
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
            response.status = static_cast<int>(status);
        }
        else
        {
            response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
        }

        curl_slist_free_all(headers);
        return response;
    }

    struct Handle
    {
        CURL* handle{};
        Handle() = default;
        Handle(Handle const&) = delete;
        Handle& operator=(Handle const&) = delete;
        ~Handle()
        {
            if (handle)
                curl_easy_cleanup(handle);
        }
    };
};

std::size_t const gPlatformCurlSender::MaxResponse;
} // namespace

std::unique_ptr<gPlatformHttpSender> gPlatformHttpSender::CreateCurl()
{
    return std::unique_ptr<gPlatformHttpSender>(new gPlatformCurlSender);
}

// *************************************************************
// serialization
// *************************************************************

std::string gPlatformHttp::ToUtf8(std::string const& text)
{
    // valid UTF-8 (which includes plain ASCII) stays as it is
    std::size_t i = 0;
    bool valid = true;
    while (i < text.size() && valid)
    {
        auto const c = static_cast<unsigned char>(text[i]);
        int follow = 0;
        std::uint32_t codePoint = 0;
        if (c < 0x80)
            follow = 0, codePoint = c;
        else if (c >= 0xc2 && c <= 0xdf)
            follow = 1, codePoint = c & 0x1f;
        else if (c >= 0xe0 && c <= 0xef)
            follow = 2, codePoint = c & 0x0f;
        else if (c >= 0xf0 && c <= 0xf4)
            follow = 3, codePoint = c & 0x07;
        else
            valid = false;

        for (int k = 1; valid && k <= follow; ++k)
        {
            if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80)
                valid = false;
            else
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        }
        if (valid && ((follow == 2 && (codePoint < 0x800 || (codePoint >= 0xd800 && codePoint <= 0xdfff))) ||
                      (follow == 3 && (codePoint < 0x10000 || codePoint > 0x10ffff))))
            valid = false;

        i += follow + 1;
    }
    if (valid)
        return text;

    // otherwise it is Latin-1, the game's traditional encoding
    std::string out;
    for (char ch : text)
    {
        auto const c = static_cast<unsigned char>(ch);
        if (c < 0x80)
        {
            out.push_back(ch);
        }
        else
        {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return out;
}

namespace
{
nlohmann::ordered_json sg_Event(std::int64_t seq, char const* type, std::int64_t time, nlohmann::ordered_json data)
{
    nlohmann::ordered_json event;
    event["seq"] = seq;
    event["type"] = type;
    event["time"] = time;
    event["data"] = std::move(data);
    return event;
}

std::string sg_Dump(nlohmann::ordered_json const& json)
{
    return json.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}
} // namespace

std::string gPlatformHttp::ServerStartedJson(std::int64_t seq, std::int64_t time, std::string const& version)
{
    nlohmann::ordered_json data;
    data["version"] = ToUtf8(version);
    data["platformContract"] = "v0";
    return sg_Dump(sg_Event(seq, "server_started", time, std::move(data)));
}

std::string gPlatformHttp::PlayerAuthenticatedJson(std::int64_t seq, std::int64_t time, std::string const& gid, std::string const& name, std::string const& method, int connection)
{
    nlohmann::ordered_json data;
    data["gid"] = ToUtf8(gid);
    data["name"] = ToUtf8(name);
    data["method"] = method;
    // the connection the player came in on, as client_connected names it (0: none, left out)
    if (connection > 0)
        data["connection"] = connection;
    return sg_Dump(sg_Event(seq, "player_authenticated", time, std::move(data)));
}

std::string gPlatformHttp::ClientConnectedJson(std::int64_t seq, std::int64_t time, int connection, bool browser, int protocol, std::string const& ip)
{
    nlohmann::ordered_json data;
    data["connection"] = connection;
    data["client"] = browser ? "browser" : "desktop";
    data["protocol"] = protocol;
    if (!ip.empty())
        data["ip"] = ip;
    return sg_Dump(sg_Event(seq, "client_connected", time, std::move(data)));
}

namespace
{
nlohmann::ordered_json sg_NullOr(std::string const& text)
{
    return text.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(gPlatformHttp::ToUtf8(text));
}

// a player as every event names it: name, gid, bot, team
void sg_PlayerMembers(nlohmann::ordered_json& json, std::string const& name, std::string const& gid, bool bot, std::string const& team)
{
    json["name"] = gPlatformHttp::ToUtf8(name);
    json["gid"] = bot ? nlohmann::ordered_json(nullptr) : sg_NullOr(gid);
    json["bot"] = bot;
    json["team"] = sg_NullOr(team);
}

nlohmann::ordered_json sg_Player(ePlatformPlayerRef const& player)
{
    nlohmann::ordered_json json;
    sg_PlayerMembers(json, player.name, player.gid, player.bot, player.team);
    return json;
}
} // namespace

std::string gPlatformHttp::RoundEndedJson(std::int64_t seq, std::int64_t time, ePlatformRoundResult const& result)
{
    nlohmann::ordered_json players = nlohmann::ordered_json::array();
    for (auto const& player : result.players)
    {
        nlohmann::ordered_json line;
        sg_PlayerMembers(line, player.name, player.gid, player.bot, player.team);
        line["score"] = player.score;
        line["alive"] = player.alive;
        line["place"] = player.place;
        players.push_back(std::move(line));
    }

    nlohmann::ordered_json data;
    data["match"] = result.match;
    data["round"] = result.round;
    data["seconds"] = result.seconds;
    data["players"] = std::move(players);
    return sg_Dump(sg_Event(seq, "round_ended", time, std::move(data)));
}

std::string gPlatformHttp::MatchStartedJson(std::int64_t seq, std::int64_t time, ePlatformMatchStart const& start)
{
    nlohmann::ordered_json limits;
    limits["rounds"] = start.limitRounds;
    limits["minutes"] = start.limitMinutes;
    limits["score"] = start.limitScore;

    nlohmann::ordered_json data;
    data["match"] = start.match;
    data["limits"] = std::move(limits);
    return sg_Dump(sg_Event(seq, "match_started", time, std::move(data)));
}

std::string gPlatformHttp::DeathJson(std::int64_t seq, std::int64_t time, ePlatformDeath const& death)
{
    nlohmann::ordered_json data;
    data["match"] = death.match;
    data["round"] = death.round;
    data["victim"] = sg_Player(death.victim);
    data["killer"] = death.hasKiller ? sg_Player(death.killer) : nlohmann::ordered_json(nullptr);
    data["cause"] = death.cause;
    return sg_Dump(sg_Event(seq, "death", time, std::move(data)));
}

std::string gPlatformHttp::MatchEndedJson(std::int64_t seq, std::int64_t time, ePlatformMatchResult const& result)
{
    nlohmann::ordered_json players = nlohmann::ordered_json::array();
    for (auto const& player : result.players)
    {
        nlohmann::ordered_json line = sg_Player(player.player);
        line["score"] = player.score;
        line["place"] = player.leftEarly || player.place <= 0 ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(player.place);
        line["rounds"] = player.rounds;
        line["leftEarly"] = player.leftEarly;
        players.push_back(std::move(line));
    }

    nlohmann::ordered_json data;
    data["match"] = result.match;
    data["result"] = result.completed ? "completed" : "aborted";
    data["rounds"] = result.rounds;
    data["winner"] = result.completed ? sg_NullOr(result.winner) : nlohmann::ordered_json(nullptr);
    data["players"] = std::move(players);
    return sg_Dump(sg_Event(seq, "match_ended", time, std::move(data)));
}

std::string gPlatformHttp::BatchJson(std::string const& bootId, std::vector<ePlatformEvent> const& events)
{
    std::string body = "{\"bootId\":" + sg_Dump(nlohmann::ordered_json(bootId)) + ",\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (i > 0)
            body += ',';
        body += events[i].body;
    }
    body += "]}";
    return body;
}

std::string gPlatformHttp::EventsUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/events";
}

std::string gPlatformHttp::KeysUrl(std::string const& apiUrl)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/auth/keys";
}

std::string gPlatformHttp::HeartbeatUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/heartbeat";
}

std::string gPlatformHttp::CommandsUrl(std::string const& apiUrl, std::string const& serverId, std::int64_t cursor, int waitSeconds)
{
    std::string url = sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/commands?";
    if (cursor >= 0)
        url += "after=" + std::to_string(cursor) + "&";
    return url + "wait=" + std::to_string(waitSeconds);
}

std::string gPlatformHttp::ModerationUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/moderation";
}

std::string gPlatformHttp::ChatUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/moderation/chat";
}

std::string gPlatformHttp::LinksUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/links";
}

std::string gPlatformHttp::SocialUrl(std::string const& apiUrl, std::string const& serverId)
{
    return sg_TrimTrailingSlashes(apiUrl) + "/v1/servers/" + sg_EncodePathSegment(serverId) + "/social";
}

std::string gPlatformHttp::ReportJson(std::int64_t seq, std::int64_t time, std::string const& data)
{
    nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(data, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        parsed = nlohmann::ordered_json::object();
    return sg_Dump(sg_Event(seq, "report", time, std::move(parsed)));
}

std::string gPlatformHttp::PlayersMovedJson(std::int64_t seq, std::int64_t time, std::string const& data)
{
    nlohmann::ordered_json parsed = nlohmann::ordered_json::parse(data, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object())
        parsed = nlohmann::ordered_json::object();
    return sg_Dump(sg_Event(seq, "players_moved", time, std::move(parsed)));
}

std::string gPlatformHttp::HeartbeatJson(gPlatformHeartbeat const& heartbeat)
{
    nlohmann::ordered_json json;
    json["bootId"] = heartbeat.bootId;
    json["time"] = heartbeat.time;
    json["state"] = gPlatformRoom::StateName(heartbeat.state);
    json["humans"] = heartbeat.counts.humans;
    json["bots"] = heartbeat.counts.bots;
    json["capacity"] = heartbeat.counts.capacity;
    json["clients"] = heartbeat.counts.clients;
    json["mode"] = heartbeat.mode.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(ToUtf8(heartbeat.mode));
    json["version"] = ToUtf8(heartbeat.version);
    json["platformContract"] = "v0";
    json["uptime"] = heartbeat.uptime;
    json["lastCommandId"] = heartbeat.lastCommandId;
    if (!heartbeat.hostId.empty())
        json["hostId"] = ToUtf8(heartbeat.hostId);
    if (!heartbeat.region.empty())
        json["region"] = ToUtf8(heartbeat.region);
    json["protocolMin"] = heartbeat.protocolMin;
    json["protocolMax"] = heartbeat.protocolMax;
    json["platformLogin"] = heartbeat.platformLogin;
    nlohmann::ordered_json moderation;
    moderation["chatFilter"] = heartbeat.moderation.chatFilter;
    moderation["sanctions"] = heartbeat.moderation.sanctions;
    moderation["reports"] = heartbeat.moderation.reports;
    json["moderation"] = std::move(moderation);
    nlohmann::ordered_json players = nlohmann::ordered_json::array();
    for (auto const& player : heartbeat.players)
    {
        if (players.size() >= MaxRosterPlayers)
            break;
        nlohmann::ordered_json entry;
        entry["netId"] = player.netId;
        entry["gid"] = sg_NullOr(player.gid);
        entry["ping"] = player.ping;
        entry["spectator"] = player.spectator;
        entry["client"] = player.browser ? "browser" : "desktop";
        players.push_back(std::move(entry));
    }
    json["players"] = std::move(players);
    return sg_Dump(json);
}

std::size_t const gPlatformHttp::MaxCommandsPerPoll;
std::size_t const gPlatformHttp::MaxRosterPlayers;

bool gPlatformHttp::TryParseCommands(std::string const& body, std::vector<gPlatformCommand>& commands, std::int64_t& cursor, std::string& error)
{
    commands.clear();

    nlohmann::json const json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "not a JSON object";
        return false;
    }
    auto const list = json.find("commands");
    if (list == json.end() || !list->is_array())
    {
        error = "no commands list";
        return false;
    }

    for (auto const& item : *list)
    {
        // a command without a usable id can be neither applied nor acknowledged
        if (!item.is_object())
            continue;
        auto const id = item.find("id");
        if (id == item.end() || !id->is_number_integer() || id->get<std::int64_t>() <= 0)
            continue;

        gPlatformCommand command;
        command.id = id->get<std::int64_t>();

        // anything but a short string is an unknown type: ignored and acknowledged
        auto const type = item.find("type");
        if (type != item.end() && type->is_string() && type->get_ref<std::string const&>().size() <= 64)
            command.type = type->get<std::string>();

        auto const data = item.find("data");
        if (data != item.end() && data->is_object())
        {
            command.data = data->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            auto const message = data->find("message");
            if (message != data->end() && message->is_string())
                command.message = message->get<std::string>();
            auto const text = data->find("text");
            if (text != data->end() && text->is_string())
                command.text = text->get<std::string>();
            auto const center = data->find("center");
            if (center != data->end() && center->is_boolean())
                command.center = center->get<bool>();
            auto const seconds = data->find("seconds");
            if (seconds != data->end() && seconds->is_number_integer())
            {
                std::int64_t const value = seconds->get<std::int64_t>();
                command.seconds = static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(value, gPlatformRoom::MaxClosingSeconds)));
            }
        }

        commands.push_back(std::move(command));
    }

    std::sort(commands.begin(), commands.end(), [](gPlatformCommand const& a, gPlatformCommand const& b) { return a.id < b.id; });
    commands.erase(std::unique(commands.begin(), commands.end(), [](gPlatformCommand const& a, gPlatformCommand const& b) { return a.id == b.id; }),
                   commands.end());

    bool const truncated = commands.size() > MaxCommandsPerPoll;
    if (truncated)
        commands.resize(MaxCommandsPerPoll);
    std::int64_t const lastId = commands.empty() ? 0 : commands.back().id;

    // the rest of a long list comes with the next poll
    if (truncated)
    {
        cursor = lastId;
        return true;
    }

    auto const next = json.find("cursor");
    if (next != json.end() && next->is_number_integer() && next->get<std::int64_t>() >= 0)
        cursor = std::max(next->get<std::int64_t>(), lastId);
    else
        cursor = std::max(cursor, lastId);
    return true;
}

// *************************************************************
// the room: state, commands, heartbeat schedule (game thread)
// *************************************************************

namespace
{
char const* const sg_defaultDrainMessage = "This server is not taking new players right now. Please join another server.";
char const* const sg_defaultClosingMessage = "This server is closing. Please join another server.";
char const* const sg_privateRoomRefusal = "This is a private room. Open its invite link at retrocycles.io to join.";

// seconds left at which the closing countdown speaks
int const sg_countdownSteps[] = {60, 30, 20, 10, 5, 4, 3, 2, 1};
} // namespace

std::size_t const gPlatformRoom::MaxTextLength;
int const gPlatformRoom::DefaultClosingSeconds;
int const gPlatformRoom::MaxClosingSeconds;

gPlatformRoom::gPlatformRoom(gPlatformRoomEffects effects) : effects_(std::move(effects))
{
    if (!effects_.masterAnnounce)
        effects_.masterAnnounce = [] { return false; };
    if (!effects_.setMasterAnnounce)
        effects_.setMasterAnnounce = [](bool) {};
    if (!effects_.console)
        effects_.console = [](std::string const&) {};
    if (!effects_.center)
        effects_.center = [](std::string const&) {};
    if (!effects_.refuse)
        effects_.refuse = [](int, std::string const&) {};
    if (!effects_.log)
        effects_.log = [](std::string const&) {};
}

char const* gPlatformRoom::StateName(gPlatformRoomState state) noexcept
{
    switch (state)
    {
    case gPlatformRoomState::Open:
        return "open";
    case gPlatformRoomState::Draining:
        return "draining";
    case gPlatformRoomState::Closing:
        return "closing";
    }
    return "open";
}

std::string gPlatformRoom::CleanText(std::string const& text)
{
    std::string out;
    for (char c : text)
    {
        auto const byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7f)
            out.push_back(c);
    }
    if (out.size() > MaxTextLength)
    {
        std::size_t cut = MaxTextLength;
        // back off to the start of a UTF-8 sequence
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80)
            --cut;
        out.resize(cut);
    }
    return out;
}

void gPlatformRoom::StopAnnouncing()
{
    savedAnnounce_ = effects_.masterAnnounce();
    effects_.setMasterAnnounce(false);
}

void gPlatformRoom::RestoreAnnouncing()
{
    effects_.setMasterAnnounce(savedAnnounce_);
}

bool gPlatformRoom::Apply(gPlatformCommand const& command, Clock::time_point now)
{
    if (command.id <= lastAppliedId_)
        return false;
    lastAppliedId_ = command.id;

    std::string const id = std::to_string(command.id);

    if (command.type == "drain")
    {
        if (state_ == gPlatformRoomState::Open)
            StopAnnouncing();
        state_ = gPlatformRoomState::Draining;
        counting_ = false;
        std::string const message = CleanText(command.message);
        refusal_ = message.empty() ? sg_defaultDrainMessage : message;
        effects_.log("Platform: command " + id + ": draining (no master announce, new connections refused).");
    }
    else if (command.type == "undrain")
    {
        if (state_ != gPlatformRoomState::Open)
            RestoreAnnouncing();
        state_ = gPlatformRoomState::Open;
        counting_ = false;
        toRefuse_.clear();
        effects_.log("Platform: command " + id + ": open again.");
    }
    else if (command.type == "closing")
    {
        if (state_ == gPlatformRoomState::Open)
            StopAnnouncing();
        state_ = gPlatformRoomState::Closing;

        std::string const message = CleanText(command.message);
        refusal_ = message.empty() ? sg_defaultClosingMessage : message;
        int const seconds = command.seconds < 0 ? DefaultClosingSeconds : std::min(command.seconds, MaxClosingSeconds);

        effects_.log("Platform: command " + id + ": closing in " + std::to_string(seconds) + " s.");
        effects_.console(refusal_ + (seconds > 0 ? " (closing in " + std::to_string(seconds) + " seconds)" : ""));
        effects_.center(refusal_);

        counting_ = true;
        closingAt_ = now + std::chrono::seconds(seconds);
        steps_.clear();
        for (int step : sg_countdownSteps)
            if (step < seconds)
                steps_.push_back(step);
        steps_.push_back(0);
    }
    else if (command.type == "message")
    {
        std::string const text = CleanText(command.text);
        if (!text.empty())
        {
            effects_.console(text);
            if (command.center)
                effects_.center(text);
        }
    }
    else
    {
        effects_.log("Platform: command " + id + " of unknown type '" + CleanText(command.type) + "' ignored.");
    }

    return true;
}

bool gPlatformRoom::Acknowledge(std::int64_t id)
{
    if (id <= lastAppliedId_)
        return false;
    lastAppliedId_ = id;
    return true;
}

void gPlatformRoom::UserLoggedIn(int userID)
{
    if (userID <= 0 || state_ == gPlatformRoomState::Open)
        return;
    if (std::find(toRefuse_.begin(), toRefuse_.end(), userID) == toRefuse_.end())
        toRefuse_.push_back(userID);
}

void gPlatformRoom::UserLoggedOut(int userID)
{
    toRefuse_.erase(std::remove(toRefuse_.begin(), toRefuse_.end(), userID), toRefuse_.end());
}

void gPlatformRoom::Tick(Clock::time_point now)
{
    if (!toRefuse_.empty())
    {
        // a refusal ends the connection, which calls UserLoggedOut(): work on a copy
        std::vector<int> users;
        users.swap(toRefuse_);
        for (int user : users)
        {
            effects_.log("Platform: refused new connection " + std::to_string(user) + " (room " + StateName(state_) + ").");
            effects_.refuse(user, refusal_);
        }
    }

    if (!counting_)
        return;

    // whole seconds left, rounded up
    auto const left = std::chrono::duration_cast<std::chrono::milliseconds>(closingAt_ - now).count();
    std::int64_t const secondsLeft = left <= 0 ? 0 : (left + 999) / 1000;

    // after a stall, only the latest step due is spoken
    int due = -1;
    while (!steps_.empty() && steps_.front() >= secondsLeft)
    {
        due = steps_.front();
        steps_.erase(steps_.begin());
    }
    if (due < 0)
        return;

    if (due == 0)
    {
        effects_.center("Server closing now.");
        effects_.console("Server closing now.");
        counting_ = false;
        return;
    }

    std::string const text = "Server closing in " + std::to_string(due) + (due == 1 ? " second." : " seconds.");
    effects_.center(text);
    if (due >= 10)
        effects_.console(text);
}

bool gPlatformHeartbeatSchedule::Due(Clock::time_point now) const noexcept
{
    if (!sent_)
        return true;
    auto const since = now - last_;
    if (retry_ && now >= retryAt_)
        return true;
    if (changed_ && since >= minGap_)
        return true;
    return since >= interval_;
}

void gPlatformHeartbeatSchedule::Sent(Clock::time_point now) noexcept
{
    sent_ = true;
    changed_ = false;
    retry_ = false;
    last_ = now;
}

void gPlatformHeartbeatSchedule::Failed(Clock::time_point retryAt) noexcept
{
    retry_ = true;
    retryAt_ = retryAt;
}

// *************************************************************
// lifetime
// *************************************************************

namespace
{
// the running server's side of the room commands
gPlatformRoomEffects sg_ServerRoomEffects(std::function<void(std::string const&)> log)
{
    gPlatformRoomEffects effects;
    effects.masterAnnounce = [] { return sg_TalkToMaster; };
    effects.setMasterAnnounce = [](bool announce) { sg_TalkToMaster = announce; };
    effects.console = [](std::string const& text) { sn_ConsoleOut(tOutput(tString((text + "\n").c_str()))); };
    effects.center = [](std::string const& text) { sn_CenterMessage(tOutput(tString(text.c_str()))); };
    effects.refuse = [](int userID, std::string const& reason) { sn_DisconnectUser(userID, tOutput(tString(reason.c_str()))); };
    effects.log = std::move(log);
    return effects;
}

std::string sg_LowerAscii(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// a player as moderation sees it
gPlatformChatPlayer sg_ChatPlayerOf(ePlayerNetID const& player, std::string const& userId)
{
    gPlatformChatPlayer out;
    out.playerId = static_cast<int>(player.ID());
    out.conn = player.Owner();
    out.name = sg_ToStd(tColoredString::RemoveColors(player.GetName()));
#ifdef KRAWALL_SERVER
    if (player.IsAuthenticated())
        out.gid = sg_ToStd(player.GetRawAuthenticatedName());
#endif
    out.userId = out.gid.empty() ? std::string() : userId;
    if (out.conn > 0)
        out.ip = sg_ToStd(nMachine::GetMachine(out.conn).GetIP());
    if (player.CurrentTeam())
        out.team = sg_ToStd(tColoredString::RemoveColors(player.CurrentTeam()->Name()));
    return out;
}

ePlayerNetID* sg_PlayerById(int playerId)
{
    for (int i = se_PlayerNetIDs.Len() - 1; i >= 0; --i)
    {
        ePlayerNetID* player = se_PlayerNetIDs(i);
        if (player && static_cast<int>(player->ID()) == playerId)
            return player;
    }
    return nullptr;
}

// the running server's side of moderation (kick and report are wired by gPlatformHttp)
gPlatformModerationEffects sg_ServerModerationEffects(gPlatformModerationEffects effects, std::function<std::string(int conn)> userIdOf)
{
    if (!effects.players)
        effects.players = [userIdOf] {
            std::vector<gPlatformChatPlayer> players;
            for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
            {
                ePlayerNetID const* player = se_PlayerNetIDs(i);
                if (player && player->IsHuman() && player->Owner() > 0)
                    players.push_back(sg_ChatPlayerOf(*player, userIdOf(player->Owner())));
            }
            return players;
        };
    if (!effects.tell)
        effects.tell = [](int conn, std::string const& text) { sn_ConsoleOut(tOutput(tString((text + "\n").c_str())), conn); };
    if (!effects.rename)
        effects.rename = [](std::string const& gid, std::string const& name) {
#ifdef KRAWALL_SERVER
            for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
            {
                ePlayerNetID* player = se_PlayerNetIDs(i);
                if (player && player->IsAuthenticated() && sg_LowerAscii(gid) == sg_LowerAscii(sg_ToStd(player->GetRawAuthenticatedName())))
                {
                    player->ForceName(tString(name.c_str()));
                    return true;
                }
            }
#endif
            return false;
        };
    if (!effects.find)
        effects.find = [userIdOf](std::string const& name, int requesterPlayerId, gPlatformChatPlayer& found) {
            ePlayerNetID* requester = sg_PlayerById(requesterPlayerId);
            ePlayerNetID* player = ePlayerNetID::FindPlayerByName(tString(name.c_str()), requester);
            if (!player)
                return false;
            found = sg_ChatPlayerOf(*player, userIdOf(player->Owner()));
            return true;
        };
    return effects;
}

gPlatformHttpConfig sg_WithDefaults(gPlatformHttpConfig config)
{
    if (!config.clock)
        config.clock = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    if (!config.log)
        config.log = [](std::string const& message) {
            if (!sg_platformQuiet)
                con << message.c_str() << "\n";
        };
    if (!config.completeLoginAt && config.completeLogin)
    {
        auto const completeLogin = config.completeLogin;
        config.completeLoginAt = [completeLogin](int userID, std::string const& gid, int) { return completeLogin(userID, gid); };
    }
    if (!config.completeLoginAt)
        config.completeLoginAt = [](int userID, std::string const& gid, int accessLevel) {
            ePlayerNetID* player = ePlatform::FindLoginCandidate(userID);
            if (!player)
                return gPlatformLoginOutcome::NoPlayerYet;
            return ePlatform::TryCompleteLogin(player, tString(gid.c_str()), accessLevel) ? gPlatformLoginOutcome::Authenticated
                                                                                                                      : gPlatformLoginOutcome::Refused;
        };
    if (!config.countPlayers)
        config.countPlayers = [] { return ePlatform::CountPlayers(); };
    if (!config.rosterPlayers)
        config.rosterPlayers = [] { return ePlatform::RosterPlayers(); };
    if (!config.protocolVersions)
        config.protocolVersions = [] { return std::make_pair(sn_CurrentVersion().Min(), sn_CurrentVersion().Max()); };
    if (!config.roomTicketOf)
        config.roomTicketOf = [](int userID) { return sn_PeerTicket(userID); };
    if (!config.room.masterAnnounce && !config.room.setMasterAnnounce && !config.room.console && !config.room.center && !config.room.refuse)
        config.room = sg_ServerRoomEffects(config.room.log ? config.room.log : config.log);
    else if (!config.room.log)
        config.room.log = config.log;
    config.commandWaitSeconds = std::max(0, std::min(config.commandWaitSeconds, 25));
    if (!config.moderation.log)
        config.moderation.log = config.log;
    if (!config.moderation.clock)
        config.moderation.clock = config.clock;
    return config;
}

gPlatformModeration::Config sg_ModerationConfig(gPlatformHttpConfig const& config, std::string const& bootId)
{
    gPlatformModeration::Config out;
    out.chatFilter = config.chatFilter;
    out.sanctions = config.sanctions;
    out.reports = config.reports;
    out.timeout = config.chatTimeout;
    out.ipBanGrace = config.ipBanGrace;
    out.statsInterval = config.chatStatsInterval;
    out.serverId = config.serverId;
    out.bootId = bootId;
    return out;
}
} // namespace

namespace
{
// the moderation effects with the server's defaults; kicks wait for a safe moment, reports go
// out as events (both need the platform)
gPlatformModerationEffects sg_ModerationEffects(gPlatformHttpConfig const& config, gPlatformHttp* platform,
                                                std::function<void(int, std::string const&)> kick, std::function<void(std::string const&)> report)
{
    gPlatformModerationEffects effects = config.moderation;
    if (!effects.kick)
        effects.kick = std::move(kick);
    if (!effects.report)
        effects.report = std::move(report);
    return sg_ServerModerationEffects(std::move(effects), [platform](int conn) { return platform->Moderation().UserIdOf(conn); });
}

// the /link effects with the running server's defaults; requests go to the link thread
gPlatformLinkEffects sg_LinkEffects(gPlatformHttpConfig const& config, std::function<void(gPlatformLinkRequest const&)> send)
{
    gPlatformLinkEffects effects = config.link;
    if (!effects.tell)
        effects.tell = [](int conn, std::string const& text) { sn_ConsoleOut(tOutput(tString((text + "\n").c_str())), conn); };
    if (!effects.connOf)
        effects.connOf = [](int playerId) {
            ePlayerNetID const* player = sg_PlayerById(playerId);
            return player ? player->Owner() : 0;
        };
    if (!effects.send)
        effects.send = std::move(send);
    if (!effects.log)
        effects.log = config.log;
    return effects;
}

// the social effects with the running server's defaults; requests go to the social thread
gPlatformSocialEffects sg_SocialEffects(gPlatformHttpConfig const& config, std::function<void(gPlatformSocialRequest const&)> send)
{
    gPlatformSocialEffects effects = config.socialEffects;
    if (!effects.tell)
        effects.tell = [](int conn, std::string const& text) { sn_ConsoleOut(tOutput(tString((text + "\n").c_str())), conn); };
    if (!effects.connOf)
        effects.connOf = [](int playerId) {
            ePlayerNetID const* player = sg_PlayerById(playerId);
            return player ? player->Owner() : 0;
        };
    if (!effects.inRoom)
        // the same lax match /msg uses, without its error lines
        effects.inRoom = [](int requesterId, std::string const& name) {
            ePlayerNetID* found = ePlayerNetID::FindPlayerByName(tString(name.c_str()), sg_PlayerById(requesterId), false);
            return found ? static_cast<int>(found->ID()) : -1;
        };
    if (!effects.connsOfGid)
        effects.connsOfGid = [](std::string const& gid) {
            std::vector<int> conns;
#ifdef KRAWALL_SERVER
            std::string const want = sg_ToLower(gid);
            for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
            {
                ePlayerNetID const* player = se_PlayerNetIDs(i);
                if (player && player->Owner() > 0 && player->IsAuthenticated() && sg_ToLower(sg_ToStd(player->GetRawAuthenticatedName())) == want)
                    conns.push_back(player->Owner());
            }
#else
            (void)gid;
#endif
            return conns;
        };
    if (!effects.isBrowser)
        effects.isBrowser = [](int conn) { return sn_IsVirtualPeer(conn); };
    if (!effects.send)
        effects.send = std::move(send);
    if (!effects.log)
        effects.log = config.log;
    return effects;
}

// the rematch effects with the running server's defaults (a private room's end of match)
gPlatformRematchEffects sg_RematchEffects(gPlatformHttpConfig const& config)
{
    gPlatformRematchEffects effects = config.rematch;
    if (!effects.players)
        effects.players = [] {
            std::vector<gPlatformRematchPlayer> players;
            for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
            {
                ePlayerNetID const* player = se_PlayerNetIDs(i);
                if (!player || !player->IsHuman() || player->Owner() <= 0)
                    continue;
                gPlatformRematchPlayer out;
                out.conn = player->Owner();
                out.name = sg_ToStd(tColoredString::RemoveColors(player->GetName()));
                out.inTeam = player->CurrentTeam() != nullptr;
                players.push_back(out);
            }
            return players;
        };
    if (!effects.announce)
        effects.announce = [](std::string const& line) { sn_ConsoleOut(tOutput(tString((line + "\n").c_str()))); };
    if (!effects.tell)
        effects.tell = [](int conn, std::string const& line) { sn_ConsoleOut(tOutput(tString((line + "\n").c_str())), conn); };
    if (!effects.restart)
        effects.restart = [] { gPlatformEvents::RequestMatchRestart(); };
    if (!effects.disconnect)
        effects.disconnect = [](int conn, std::string const& reason) {
            if (sn_GetNetState() == nSERVER && conn > 0)
                sn_DisconnectUser(conn, tOutput(tString(reason.c_str())));
        };
    if (!effects.log)
        effects.log = config.log;
    return effects;
}

// the move effects with the running server's defaults; the report goes out as an event
gPlatformMoveEffects sg_MoveEffects(gPlatformHttpConfig const& config, std::function<void(std::string const&)> report)
{
    gPlatformMoveEffects effects = config.moves;
    if (!effects.players)
        effects.players = [] {
            std::vector<gPlatformMovePlayer> players;
            for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
            {
                ePlayerNetID const* player = se_PlayerNetIDs(i);
                if (!player || !player->IsHuman() || player->Owner() <= 0)
                    continue;
                gPlatformMovePlayer out;
                out.conn = player->Owner();
#ifdef KRAWALL_SERVER
                if (player->IsAuthenticated())
                    out.gid = sg_ToStd(player->GetRawAuthenticatedName());
#endif
                out.inTeam = player->CurrentTeam() != nullptr;
                players.push_back(out);
            }
            return players;
        };
    if (!effects.followsMoves)
        effects.followsMoves = [](int conn) { return ePlatform::ClientHasCapability(conn, ePLATFORM_CAPABILITY_MOVE); };
    if (!effects.browser)
        effects.browser = [](int conn) { return sn_IsVirtualPeer(conn); };
    if (!effects.sendMove)
        effects.sendMove = [](int conn, ePlatformMoveNotice const& notice) { return ePlatform::SendMove(conn, notice); };
    if (!effects.redirect)
        effects.redirect = [](int conn, std::string const& host, int port, std::string const& reason) {
            if (sn_GetNetState() != nSERVER || conn <= 0)
                return;
            nServerInfoRedirect redirect(tString(host.c_str()), static_cast<unsigned int>(port));
            sn_DisconnectUser(conn, tOutput(tString(reason.c_str())), &redirect);
        };
    if (!effects.disconnect)
        effects.disconnect = [](int conn, std::string const& reason) {
            if (sn_GetNetState() == nSERVER && conn > 0)
                sn_DisconnectUser(conn, tOutput(tString(reason.c_str())));
        };
    if (!effects.matchRunning)
        effects.matchRunning = [] { return gPlatformEvents::MatchRunning(); };
    if (!effects.report)
        effects.report = std::move(report);
    if (!effects.log)
        effects.log = config.log;
    return effects;
}
} // namespace

gPlatformHttp::gPlatformHttp(gPlatformHttpConfig config, std::unique_ptr<gPlatformHttpSender> sender)
    : config_(sg_WithDefaults(std::move(config))), sender_(std::move(sender)), bootId_(sg_MakeBootId(this)),
      started_(std::chrono::steady_clock::now()), room_(config_.room),
      moderation_(sg_ModerationConfig(config_, bootId_),
                  sg_ModerationEffects(
                      config_, this, [this](int conn, std::string const& reason) { kicks_.emplace_back(conn, reason); },
                      [this](std::string const& data) { Emit("report", [&](std::int64_t seq, std::int64_t time) { return ReportJson(seq, time, data); }); })),
      moves_(sg_MoveEffects(config_, [this](std::string const& data) {
                 Emit("players_moved", [&](std::int64_t seq, std::int64_t time) { return PlayersMovedJson(seq, time, data); });
                 heartbeatSchedule_.MarkChanged();
             }),
             config_.moveGrace),
      link_(config_.authority, sg_LinkEffects(config_,
                                              [this](gPlatformLinkRequest const& request) {
                                                  {
                                                      std::lock_guard<std::mutex> lock(linkMutex_);
                                                      linkQueue_.push_back(request);
                                                  }
                                                  linkCondition_.notify_all();
                                              })),
      social_(sg_SocialEffects(config_,
                               [this](gPlatformSocialRequest const& request) {
                                   {
                                       std::lock_guard<std::mutex> lock(socialMutex_);
                                       socialQueue_.push_back(request);
                                   }
                                   socialCondition_.notify_all();
                               })),
      rematch_(sg_RematchEffects(config_), config_.rematchWindow),
      heartbeatSchedule_(config_.heartbeatInterval, config_.heartbeatMinGap), outbound_(config_.queueCapacity)
{
    tASSERT(sender_);

    eventsUrl_ = EventsUrl(config_.apiUrl, config_.serverId);
    keysUrl_ = KeysUrl(config_.apiUrl);
    heartbeatUrl_ = HeartbeatUrl(config_.apiUrl, config_.serverId);
    moderationUrl_ = ModerationUrl(config_.apiUrl, config_.serverId);
    chatUrl_ = ChatUrl(config_.apiUrl, config_.serverId);
    linksUrl_ = LinksUrl(config_.apiUrl, config_.serverId);
    socialUrl_ = SocialUrl(config_.apiUrl, config_.serverId);

    // the startup key fetch counts as the first refresh
    lastKeyRequest_ = std::chrono::steady_clock::now();

    // word lists and sanctions at once
    moderationWanted_ = config_.chatFilter || config_.sanctions;

    worker_ = std::thread([this] { Run(); });
    commandWorker_ = std::thread([this] { RunCommands(); });
    if (config_.chatFilter)
        chatWorker_ = std::thread([this] { RunChat(); });
    if (config_.linking)
        linkWorker_ = std::thread([this] { RunLinks(); });
    if (config_.social)
        socialWorker_ = std::thread([this] { RunSocial(); });
}

gPlatformHttp::~gPlatformHttp()
{
    Shutdown();
}

void gPlatformHttp::Shutdown()
{
    if (shutDown_)
        return;
    shutDown_ = true;

    auto const now = std::chrono::steady_clock::now();
    flushDeadline_ = (now + config_.flushTimeout).time_since_epoch().count();
    stopping_ = true;
    outbound_.Wake();

    // the command poll and the chat check have nothing to flush: abandon them at once
    {
        std::lock_guard<std::mutex> lock(pollMutex_);
        pollCancel_ = true;
    }
    pollCondition_.notify_all();
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        chatStop_ = true;
        chatCancel_ = true;
    }
    chatCondition_.notify_all();
    {
        std::lock_guard<std::mutex> lock(linkMutex_);
        linkStop_ = true;
        linkCancel_ = true;
    }
    linkCondition_.notify_all();
    {
        std::lock_guard<std::mutex> lock(socialMutex_);
        socialStop_ = true;
        socialCancel_ = true;
    }
    socialCondition_.notify_all();

    // give the worker the flush time, then abandon whatever request is still running
    {
        std::unique_lock<std::mutex> lock(doneMutex_);
        doneCondition_.wait_until(lock, now + config_.flushTimeout + std::chrono::milliseconds(200), [this] { return done_; });
    }
    cancel_ = true;
    outbound_.Wake();
    if (worker_.joinable())
        worker_.join();
    if (commandWorker_.joinable())
        commandWorker_.join();
    if (chatWorker_.joinable())
        chatWorker_.join();
    if (linkWorker_.joinable())
        linkWorker_.join();
    if (socialWorker_.joinable())
        socialWorker_.join();

    // the workers' last words (commands that arrive now are not applied any more)
    RunGameThreadTasks();
}

namespace
{
gPlatformHttp* sg_platformHttp = nullptr;
gPlatformClosedRoom* sg_platformClosedRoom = nullptr;

// PLATFORM_PRIVATE without a usable platform: refuse everyone rather than be a public room
void sg_CloseIfPrivate(char const* why)
{
    if (!sg_platformPrivate || sg_platformClosedRoom || ePlatform::Instance())
        return;
    con << "Platform: PLATFORM_PRIVATE is 1, but " << why << "; nothing can check room tickets, so every connection is refused.\n";
    sg_platformClosedRoom = new gPlatformClosedRoom();
    ePlatform::Register(sg_platformClosedRoom);
}

// stops a platform that main() did not stop, without talking to a console that may be gone
class gPlatformHttpExitGuard final
{
public:
    ~gPlatformHttpExitGuard()
    {
        sg_platformQuiet = true;
        gPlatformHttp::Stop();
    }
};
gPlatformHttpExitGuard sg_platformHttpExitGuard;
} // namespace

gPlatformClosedRoom::gPlatformClosedRoom(std::function<void(int userID, std::string const& reason)> refuse)
    : refuse_(std::move(refuse))
{
    if (!refuse_)
        refuse_ = [](int userID, std::string const& reason) { sn_DisconnectUser(userID, tOutput(tString(reason.c_str()))); };
}

void gPlatformClosedRoom::UserLoggedIn(int userID)
{
    if (userID > 0 && std::find(toRefuse_.begin(), toRefuse_.end(), userID) == toRefuse_.end())
        toRefuse_.push_back(userID);
}

void gPlatformClosedRoom::UserLoggedOut(int userID)
{
    toRefuse_.erase(std::remove(toRefuse_.begin(), toRefuse_.end(), userID), toRefuse_.end());
}

void gPlatformClosedRoom::OnDrain()
{
    // a refusal ends the connection, which calls UserLoggedOut(): work on a copy
    std::vector<int> users;
    users.swap(toRefuse_);
    for (int user : users)
    {
        if (!sg_platformQuiet)
            con << "Platform: refused connection " << user << " to the private room (PLATFORM_PRIVATE is 1 without a usable platform).\n";
        refuse_(user, sg_privateRoomRefusal);
    }
}

void gPlatformHttp::StartIfConfigured()
{
    if (sg_platformHttp || ePlatform::Instance())
        return;

    // recordings never talk to the platform
    if (tRecorder::IsPlayingBack())
        return;

    std::string const url = sg_ToStd(sg_platformApiUrl);
    if (url.empty())
    {
        sg_CloseIfPrivate("PLATFORM_API_URL is empty");
        return;
    }

    gPlatformHttpConfig config;
    config.apiUrl = url;
    config.serverId = sg_ToStd(sg_platformServerId);
    config.serverKey = sg_ToStd(sg_platformServerKey);
    config.authority = sg_ToStd(sg_platformAuthority);
    config.loginEnabled = sg_platformLogin;
    config.privateRoom = sg_platformPrivate;
    config.rematchWindow = std::chrono::seconds(std::max(0, sg_platformRematchSeconds));
    config.version = sg_ToStd(sn_programVersion);
    config.mode = sg_ToStd(sg_platformMode);
    config.hostId = sg_ToStd(sg_platformHostId);
    config.region = sg_ToStd(sg_platformRegion);
    config.chatFilter = sg_platformChatFilter;
    config.chatTimeout = std::chrono::milliseconds(sg_platformChatTimeout);
    config.sanctions = sg_platformSanctions;
    config.reports = sg_platformReports;
    config.linking = sg_platformLink;
    config.social = sg_platformSocial;
    config.ipBanGrace = std::chrono::seconds(std::max(0, sg_platformIpBanGrace));
    config.accessAdmin = sg_platformAccessAdmin;
    config.accessModerator = sg_platformAccessModerator;
    config.accessStaff = sg_platformAccessStaff;

    if (config.serverId.empty())
    {
        con << "Platform: PLATFORM_API_URL is set, but PLATFORM_SERVER_ID is empty; the platform stays off.\n";
        sg_CloseIfPrivate("PLATFORM_SERVER_ID is empty");
        return;
    }
    if (config.serverKey.empty())
        con << "Platform: PLATFORM_SERVER_KEY is empty; the platform will refuse our events.\n";

    sg_platformHttp = new gPlatformHttp(std::move(config), gPlatformHttpSender::CreateCurl());
    ePlatform::Register(sg_platformHttp);

    con << "Platform: reporting to " << url.c_str() << " as server " << sg_platformServerId
        << " (boot " << sg_platformHttp->BootId().c_str() << "), login tokens "
        << (sg_platformLogin ? "accepted" : "ignored") << ", chat " << (sg_platformChatFilter ? "checked" : "not checked")
        << ", sanctions " << (sg_platformSanctions ? "on" : "off") << ", reports " << (sg_platformReports ? "on" : "off")
        << ", /link " << (sg_platformLink ? "on" : "off") << ", friends " << (sg_platformSocial ? "on" : "off") << ".\n";
}

void gPlatformHttp::Stop()
{
    if (gPlatformClosedRoom* closed = sg_platformClosedRoom)
    {
        if (ePlatform::Instance() == closed)
            ePlatform::Unregister(closed);
        sg_platformClosedRoom = nullptr;
        delete closed;
    }

    gPlatformHttp* platform = sg_platformHttp;
    if (!platform)
        return;

    platform->Shutdown();
    if (ePlatform::Instance() == platform)
        ePlatform::Unregister(platform);
    sg_platformHttp = nullptr;
    delete platform;
}

// *************************************************************
// events (game thread)
// *************************************************************

void gPlatformHttp::Log(std::string const& message) const
{
    config_.log(message);
}

void gPlatformHttp::Emit(char const* type, std::function<std::string(std::int64_t seq, std::int64_t time)> const& serialize)
{
    if (shutDown_)
        return;

    ++seq_;
    ePlatformEvent event;
    event.type = type;
    event.time = config_.clock();
    event.body = serialize(seq_, event.time);
    outbound_.Push(std::move(event));
}

void gPlatformHttp::OnServerStarted()
{
    std::string const version = config_.version;
    Emit("server_started", [&](std::int64_t seq, std::int64_t time) { return ServerStartedJson(seq, time, version); });
}

void gPlatformHttp::OnPlayerAuthenticated(ePlayerNetID& player, tString const& gid, char const* method)
{
    std::string const gidText = sg_ToStd(gid);
    std::string const name = sg_ToStd(player.GetName());
    std::string const methodText = method;
    int const connection = player.Owner();
    Emit("player_authenticated", [&](std::int64_t seq, std::int64_t time) { return PlayerAuthenticatedJson(seq, time, gidText, name, methodText, connection); });

    // a ban in force ends the connection (krawall logins; token logins are checked before)
    if (config_.sanctions)
    {
        gPlatformChatPlayer who = sg_ChatPlayerOf(player, moderation_.UserIdOf(player.Owner()));
        if (who.gid.empty())
            who.gid = gidText;
        moderation_.PlayerAuthenticated(who);
    }

    // the heartbeat's roster names the player's gid promptly (a login changes no count)
    heartbeatSchedule_.MarkChanged();
}

void gPlatformHttp::OnRoundEnded(ePlatformRoundResult const& result)
{
    Emit("round_ended", [&](std::int64_t seq, std::int64_t time) { return RoundEndedJson(seq, time, result); });
}

void gPlatformHttp::OnMatchStarted(ePlatformMatchStart const& start)
{
    Emit("match_started", [&](std::int64_t seq, std::int64_t time) { return MatchStartedJson(seq, time, start); });
}

void gPlatformHttp::OnDeath(ePlatformDeath const& death)
{
    Emit("death", [&](std::int64_t seq, std::int64_t time) { return DeathJson(seq, time, death); });
}

void gPlatformHttp::OnMatchEnded(ePlatformMatchResult const& result)
{
    Emit("match_ended", [&](std::int64_t seq, std::int64_t time) { return MatchEndedJson(seq, time, result); });

    // the moves waiting for the match end run in the pause before the next one (OnDrain)
    moves_.MatchEnded();

    // a private room asks its players for a rematch once a match was won (not one restarted because a
    // player came or went); without the answers it empties
    if (config_.privateRoom && result.completed)
        rematch_.MatchEnded(std::chrono::steady_clock::now());
}

void gPlatformHttp::OnDrain()
{
    RunKicks();

    if (!pendingLogins_.empty())
        ProcessPendingLogins();
    if (!pendingTickets_.empty())
        ProcessPendingTickets();

    if (shutDown_)
        return;

    auto const now = std::chrono::steady_clock::now();

    // the server's instance follows its settings while it runs
    if (this == sg_platformHttp && config_.chatFilter)
    {
        moderation_.SetChatFilter(sg_platformChatFilter, now);
        moderation_.SetTimeout(std::chrono::milliseconds(sg_platformChatTimeout));
    }

    moderation_.Tick(now);
    HandOverChat();
    RunKicks();

    if (!privateRefusals_.empty())
    {
        // a refusal ends the connection, which calls UserLoggedOut(): work on a copy
        std::vector<std::pair<int, std::string>> refusals;
        refusals.swap(privateRefusals_);
        for (auto const& refusal : refusals)
            config_.room.refuse(refusal.first, refusal.second);
    }

    room_.Tick(now);
    moves_.Tick(now, config_.clock());
    if (config_.privateRoom)
    {
        if (this == sg_platformHttp)
            rematch_.SetWindow(std::chrono::seconds(std::max(0, sg_platformRematchSeconds)));
        rematch_.Tick(now);
    }
    UpdateHeartbeat(now);
}

void gPlatformHttp::RunKicks()
{
    if (kicks_.empty())
        return;
    // a kick ends a connection, which may queue more: work on a copy
    std::vector<std::pair<int, std::string>> kicks;
    kicks.swap(kicks_);
    std::vector<int> done;
    for (auto const& kick : kicks)
    {
        if (std::find(done.begin(), done.end(), kick.first) != done.end())
            continue;
        done.push_back(kick.first);
        if (sn_GetNetState() == nSERVER && kick.first > 0)
            sn_KickUser(kick.first, tOutput(tString(kick.second.c_str())), 0); // severity 0: never an automatic address ban
    }
}

// *************************************************************
// moderation (game thread)
// *************************************************************

int gPlatformHttp::AccessLevelForRoles(std::string const& roles, int admin, int moderator, int staff)
{
    int level = tAccessLevel_Authenticated;
    std::string role;
    for (std::size_t i = 0; i <= roles.size(); ++i)
    {
        char const c = i < roles.size() ? roles[i] : ',';
        if (c != ',')
        {
            if (c != ' ')
                role.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
            continue;
        }
        if (role == "admin")
            level = std::min(level, admin);
        else if (role == "moderator")
            level = std::min(level, moderator);
        else if (role == "staff")
            level = std::min(level, staff);
        role.clear();
    }
    // a token never makes anyone the owner
    return std::max(level, static_cast<int>(tAccessLevel_Admin));
}

bool gPlatformHttp::OnChatSubmitted(ePlayerNetID& player, tColoredString const& line)
{
    if (shutDown_)
        return false;
    // /rematch in a private room: the room's, never said
    if (config_.privateRoom && rematch_.Submit(player.Owner(), sg_ToStd(line), std::chrono::steady_clock::now()))
        return true;
    if (!config_.chatFilter && !config_.sanctions && !config_.reports && !config_.linking && !config_.social)
        return false;

    gPlatformChatPlayer const who = sg_ChatPlayerOf(player, moderation_.UserIdOf(player.Owner()));

    // /link carries a one-time code: never delivered, never held, never in a report's chat window
    if (config_.linking && link_.Submit(who, sg_ToStd(line), std::chrono::steady_clock::now()))
        return true;
    // /f, /msg to a friend elsewhere, /r, /join: the platform's, never delivered (the message is checked there)
    if (config_.social && social_.Submit(who, sg_ToStd(line), std::chrono::steady_clock::now()))
        return true;
    if (!config_.chatFilter && !config_.sanctions && !config_.reports)
        return false;

    tJUST_CONTROLLED_PTR<ePlayerNetID> keep(&player);
    auto deliver = [keep](std::string const& text) { ePlatform::DeliverChat(keep, tColoredString(text.c_str())); };
    bool const taken = moderation_.Submit(who, sg_ToStd(line), deliver, std::chrono::steady_clock::now());
    RunKicks();
    return taken;
}

void gPlatformHttp::HandOverChat()
{
    std::vector<gPlatformOutboundLine> lines = moderation_.TakeOutbound();
    if (lines.empty() || !chatWorker_.joinable())
        return;
    {
        std::lock_guard<std::mutex> lock(chatMutex_);
        for (auto& line : lines)
            chatQueue_.push_back(std::move(line));
        if (chatQueue_.size() > gPlatformModeration::MaxOutbound)
            chatQueue_.erase(chatQueue_.begin(), chatQueue_.begin() + (chatQueue_.size() - gPlatformModeration::MaxOutbound));
    }
    chatCondition_.notify_all();
}

void gPlatformHttp::ApplyModerationCommand(gPlatformCommand const& command)
{
    std::string const id = std::to_string(command.id);
    nlohmann::json const data = nlohmann::json::parse(command.data.empty() ? std::string("{}") : command.data, nullptr, false);
    auto text = [&](char const* member) {
        if (!data.is_object())
            return std::string();
        auto const value = data.find(member);
        return value != data.end() && value->is_string() ? value->get<std::string>() : std::string();
    };

    if (command.type == "sanction")
    {
        gPlatformSanction sanction;
        std::string error;
        if (gPlatformSanction::TryParse(command.data, sanction, error))
            moderation_.ApplySanction(sanction, command.id);
        else
            Log("Platform: command " + id + ": unusable sanction (" + error + ").");
    }
    else if (command.type == "unsanction")
    {
        std::string const sanction = gPlatformRoom::CleanText(text("id"));
        if (!sanction.empty())
            moderation_.RemoveSanction(sanction, command.id);
    }
    else if (command.type == "moderation_config")
    {
        moderationWanted_ = true;
        outbound_.Wake();
    }
    else if (command.type == "rename")
    {
        if (!moderation_.Rename(text("gid"), text("name"), text("reason")))
            Log("Platform: command " + id + ": nobody to rename.");
    }
}

// *************************************************************
// room heartbeats and commands (game thread)
// *************************************************************

void gPlatformHttp::UpdateHeartbeat(std::chrono::steady_clock::time_point now)
{
    // joins and leaves show in the counts; a change is reported promptly
    if (now >= nextCount_)
    {
        nextCount_ = now + config_.playerCountInterval;
        ePlatformPlayerCounts const counts = config_.countPlayers();
        if (counts != lastCounts_)
        {
            lastCounts_ = counts;
            heartbeatSchedule_.MarkChanged();
        }
    }

    if (!heartbeatSchedule_.Due(now))
        return;
    heartbeatSchedule_.Sent(now);

    gPlatformHeartbeat heartbeat;
    heartbeat.bootId = bootId_;
    heartbeat.time = config_.clock();
    heartbeat.state = room_.State();
    heartbeat.counts = lastCounts_;
    heartbeat.mode = config_.mode;
    heartbeat.version = config_.version;
    heartbeat.uptime = std::chrono::duration_cast<std::chrono::seconds>(now - started_).count();
    heartbeat.lastCommandId = room_.LastAppliedId();
    heartbeat.hostId = config_.hostId;
    heartbeat.region = config_.region;
    std::pair<int, int> const protocol = config_.protocolVersions();
    heartbeat.protocolMin = protocol.first;
    heartbeat.protocolMax = protocol.second;
    heartbeat.platformLogin = config_.loginEnabled;
    // what is in force: sanctions and reports are fixed at start; the chat filter can be
    // turned off while the server runs (PLATFORM_CHAT_FILTER, followed in Update())
    heartbeat.moderation.chatFilter = config_.chatFilter && (this != sg_platformHttp || sg_platformChatFilter);
    heartbeat.moderation.sanctions = config_.sanctions;
    heartbeat.moderation.reports = config_.reports;
    heartbeat.players = config_.rosterPlayers();
    std::string body = HeartbeatJson(heartbeat);

    {
        std::lock_guard<std::mutex> lock(heartbeatMutex_);
        heartbeatBody_ = std::move(body);
        heartbeatQueued_ = true;
    }
    outbound_.Wake();
}

void gPlatformHttp::ApplyCommands(std::vector<gPlatformCommand> const& commands)
{
    if (shutDown_)
        return;

    auto const now = std::chrono::steady_clock::now();
    bool applied = false;
    for (auto const& command : commands)
    {
        if (command.type == "move_players")
        {
            if (!room_.Acknowledge(command.id))
                continue;
            applied = true;
            ApplyMoveCommand(command);
            continue;
        }
        if (command.type == "player_message")
        {
            if (!room_.Acknowledge(command.id))
                continue;
            applied = true;
            nlohmann::json const data = nlohmann::json::parse(command.data.empty() ? std::string("{}") : command.data, nullptr, false);
            auto text = [&](char const* member) {
                if (!data.is_object())
                    return std::string();
                auto const value = data.find(member);
                return value != data.end() && value->is_string() ? value->get<std::string>() : std::string();
            };
            if (config_.social)
                social_.Deliver(text("gid"), text("kind"), text("text"));
            continue;
        }
        bool const moderation = command.type == "sanction" || command.type == "unsanction" || command.type == "moderation_config" || command.type == "rename";
        if (!moderation)
        {
            applied = room_.Apply(command, now) || applied;
            continue;
        }
        if (!room_.Acknowledge(command.id))
            continue;
        applied = true;
        if (config_.sanctions)
            ApplyModerationCommand(command);
        else
            Log("Platform: command " + std::to_string(command.id) + " (" + command.type + ") ignored, PLATFORM_SANCTIONS is 0.");
    }
    RunKicks();

    // the acknowledgement (and a new state) goes out promptly
    if (applied)
        heartbeatSchedule_.MarkChanged();
}

void gPlatformHttp::ApplyMoveCommand(gPlatformCommand const& command)
{
    gPlatformMove move;
    std::string error;
    if (gPlatformMove::TryParse(command.data, move, error))
        moves_.Add(std::move(move));
    else
        Log("Platform: command " + std::to_string(command.id) + ": unusable move (" + error + ").");
}

void gPlatformHttp::UserLoggedIn(int userID, std::string const& ip)
{
    room_.UserLoggedIn(userID);
    moderation_.ConnectionOpened(userID, ip, std::chrono::steady_clock::now());

    // a private room: the connection must have brought a ticket for this room in
    // its signalling URL; desktop clients (UDP) have no way to, so they never get in
    if (config_.privateRoom && userID > 0)
    {
        PendingTicket pending;
        pending.ticket = config_.roomTicketOf(userID);
        pending.since = std::chrono::steady_clock::now();
        pendingTickets_[userID] = std::move(pending);
        ProcessPendingTickets();
    }
}

void gPlatformHttp::ProcessPendingTickets()
{
    auto const now = std::chrono::steady_clock::now();

    for (auto i = pendingTickets_.begin(); i != pendingTickets_.end();)
    {
        int const userID = i->first;
        PendingTicket& pending = i->second;
        bool const timedOut = now - pending.since > config_.loginPendingTimeout;

        gPlatformRoomTicketClaims claims;
        gPlatformTokenError const error = pending.ticket.empty()
            ? gPlatformTokenError::Malformed
            : gPlatformRoomTicket::Verify(pending.ticket, keys_, config_.serverId, config_.clock(), ticketJtis_, claims);

        // an unknown kid waits for the key fetch under way, or asks for one refresh (rate
        // limited), exactly as a login token does; after that, it is unknown for good
        if (error == gPlatformTokenError::UnknownKey && !timedOut)
        {
            if (keysPending_)
            {
                ++i;
                continue;
            }
            if (!pending.refreshRequested && TryRequestKeyRefresh())
            {
                pending.refreshRequested = true;
                ++i;
                continue;
            }
        }

        if (error == gPlatformTokenError::None)
            Log("Platform: connection " + std::to_string(userID) + " brought a ticket for claim " + claims.claim + ".");
        else
        {
            Log("Platform: refused connection " + std::to_string(userID) + " to the private room (" +
                (pending.ticket.empty() ? std::string("no ticket") : std::string(gPlatformLoginToken::Describe(error))) + ").");
            privateRefusals_.emplace_back(userID, sg_privateRoomRefusal);
        }
        i = pendingTickets_.erase(i);
    }
}

void gPlatformHttp::ClientConnected(int userID, bool browser, int protocol, std::string const& ip)
{
    Emit("client_connected", [&](std::int64_t seq, std::int64_t time) { return ClientConnectedJson(seq, time, userID, browser, protocol, ip); });
}

void gPlatformHttp::UserLoggedOut(int userID)
{
    room_.UserLoggedOut(userID);
    privateRefusals_.erase(std::remove_if(privateRefusals_.begin(), privateRefusals_.end(),
                                          [userID](std::pair<int, std::string> const& refusal) { return refusal.first == userID; }),
                           privateRefusals_.end());
    pendingTickets_.erase(userID);
    moderation_.ConnectionClosed(userID);
    moves_.UserLoggedOut(userID);
    rematch_.UserLoggedOut(userID);
}

// *************************************************************
// login tokens (game thread)
// *************************************************************

void gPlatformHttp::OnLoginToken(int userID, tString const& token)
{
    // a client gets a few tokens per connection, then it is ignored without further logging:
    // it must not be able to flood the console or keep the server busy verifying
    int const tokensSeen = ++tokensPerUser_[userID];
    if (tokensSeen > MaxTokensPerConnection)
    {
        if (tokensSeen == MaxTokensPerConnection + 1)
            Log("Platform: user " + std::to_string(userID) + " sent too many login tokens; ignoring further ones.");
        return;
    }

    if (!config_.loginEnabled)
    {
        Log("Platform: ignored a login token from user " + std::to_string(userID) + " (PLATFORM_LOGIN is 0).");
        return;
    }

    auto const now = std::chrono::steady_clock::now();

    // one token per connection at a time
    auto const existing = pendingLogins_.find(userID);
    if (existing != pendingLogins_.end())
    {
        Log("Platform: ignored another login token from user " + std::to_string(userID) + " while one is pending.");
        return;
    }

    PendingLogin pending;
    pending.token = sg_ToStd(token);
    pending.since = now;
    pendingLogins_[userID] = pending;

    ProcessPendingLogins();
}

bool gPlatformHttp::TryRequestKeyRefresh()
{
    if (keysPending_)
        return true;

    auto const now = std::chrono::steady_clock::now();
    if (now - lastKeyRequest_ < config_.keyRefreshInterval)
        return false;

    lastKeyRequest_ = now;
    keysPending_ = true;
    keysWanted_ = true;
    outbound_.Wake();
    return true;
}

void gPlatformHttp::ProcessPendingLogins()
{
    auto const now = std::chrono::steady_clock::now();

    for (auto i = pendingLogins_.begin(); i != pendingLogins_.end();)
    {
        int const userID = i->first;
        PendingLogin& pending = i->second;
        bool const timedOut = now - pending.since > config_.loginPendingTimeout;
        std::string const who = "user " + std::to_string(userID);

        if (!pending.verified)
        {
            gPlatformLoginClaims claims;
            gPlatformTokenError const error = gPlatformLoginToken::Verify(pending.token, keys_, config_.serverId, config_.authority,
                                                                          config_.clock(), jtis_, claims);

            // an unknown kid waits for the key fetch under way, or asks for one refresh
            // (rate limited); after that, it is unknown for good
            if (error == gPlatformTokenError::UnknownKey && !timedOut)
            {
                if (keysPending_)
                {
                    ++i;
                    continue;
                }
                if (!pending.refreshRequested && TryRequestKeyRefresh())
                {
                    pending.refreshRequested = true;
                    ++i;
                    continue;
                }
            }

            if (error != gPlatformTokenError::None)
            {
                Log("Platform: login token from " + who + " refused: " + gPlatformLoginToken::Describe(error) + ".");
                i = pendingLogins_.erase(i);
                continue;
            }

            pending.verified = true;
            pending.gid = claims.gid;
            pending.sub = claims.sub;
            pending.roles = claims.roles;
            pending.token.clear();
        }

        // a banned account is refused before it is logged in
        std::string banned;
        if (!moderation_.AdmitLogin(userID, pending.gid, pending.sub, banned))
        {
            Log("Platform: login of " + who + " as " + ToUtf8(pending.gid) + " refused: banned.");
            moderation_.EndConnection(userID, banned);
            i = pendingLogins_.erase(i);
            continue;
        }

        // the sub is the connection's while the login completes (account sanctions name it);
        // a login that does not go through, now or later, leaves the connection as it was
        std::string const previousUserId = moderation_.UserIdOf(userID);
        moderation_.SetUserId(userID, pending.sub);
        int const accessLevel = AccessLevelForRoles(pending.roles, config_.accessAdmin, config_.accessModerator, config_.accessStaff);
        gPlatformLoginOutcome const outcome = config_.completeLoginAt(userID, pending.gid, accessLevel);
        if (outcome != gPlatformLoginOutcome::Authenticated)
            moderation_.SetUserId(userID, previousUserId);
        if (outcome == gPlatformLoginOutcome::NoPlayerYet && !timedOut)
        {
            ++i;
            continue;
        }

        switch (outcome)
        {
        case gPlatformLoginOutcome::Authenticated:
            Log("Platform: " + who + " logged in as " + ToUtf8(pending.gid) + (pending.roles.empty() ? std::string() : " (roles " + ToUtf8(pending.roles) + ")") + ".");
            break;
        case gPlatformLoginOutcome::Refused:
            Log("Platform: login of " + who + " as " + ToUtf8(pending.gid) + " refused by the server (already logged in or banned).");
            break;
        case gPlatformLoginOutcome::NoPlayerYet:
            Log("Platform: login of " + who + " as " + ToUtf8(pending.gid) + " dropped, no player appeared in time.");
            break;
        }
        i = pendingLogins_.erase(i);
    }
}

void gPlatformHttp::OnKeysLoaded(gPlatformKeySet const& keys)
{
    keys_ = keys;
    keysPending_ = false;
    Log("Platform: " + std::to_string(keys_.Size()) + " login token key(s) loaded.");
    ProcessPendingLogins();
    ProcessPendingTickets();
}

void gPlatformHttp::OnKeysFailed(std::string const& reason)
{
    keysPending_ = false;
    Log("Platform: refreshing the login token keys failed: " + reason + ".");
    ProcessPendingLogins();
    ProcessPendingTickets();
}

void gPlatformHttp::ForgetUser(int userID)
{
    pendingLogins_.erase(userID);
    tokensPerUser_.erase(userID);
}

void gPlatformHttp::OnUserLoginLogout()
{
    if (sg_platformClosedRoom && sn_GetNetState() == nSERVER && nCallbackLoginLogout::User() > 0)
    {
        if (nCallbackLoginLogout::Login())
            sg_platformClosedRoom->UserLoggedIn(nCallbackLoginLogout::User());
        else
            sg_platformClosedRoom->UserLoggedOut(nCallbackLoginLogout::User());
    }

    if (!sg_platformHttp)
        return;

    int const user = nCallbackLoginLogout::User();
    sg_platformHttp->ForgetUser(user);

    if (sn_GetNetState() != nSERVER || user <= 0)
        return;
    if (nCallbackLoginLogout::Login())
    {
        std::string const ip = sg_ToStd(nMachine::GetMachine(user).GetIP());
        sg_platformHttp->UserLoggedIn(user, ip);
        sg_platformHttp->ClientConnected(user, sn_IsVirtualPeer(user), sn_Connections[user].version.Max(), ip);
    }
    else
        sg_platformHttp->UserLoggedOut(user);
}

static nCallbackLoginLogout sg_platformLoginLogout(&gPlatformHttp::OnUserLoginLogout);

// *************************************************************
// worker thread
// *************************************************************

bool gPlatformHttp::TryFetchKeys(gPlatformKeySet& keys, std::string& reason)
{
    gPlatformHttpRequest request;
    request.method = "GET";
    request.url = keysUrl_;

    gPlatformHttpResponse const response = sender_->Send(request, cancel_);

    std::string error;
    if (response.status == 200 && gPlatformKeySet::TryParse(response.body, keys, error))
        return true;

    reason = response.status == 200 ? "unusable key document (" + error + ")" : sg_DescribeResponse(response);
    return false;
}

void gPlatformHttp::Deliver(std::vector<ePlatformEvent>& batch, std::string const& body)
{
    gPlatformHttpRequest request;
    request.method = "POST";
    request.url = eventsUrl_;
    request.bearer = config_.serverKey;
    request.body = body;

    gPlatformHttpResponse const response = sender_->Send(request, cancel_);
    std::size_t const count = batch.size();

    if (response.status >= 200 && response.status < 300)
    {
        if (deliveryFailing_)
        {
            std::size_t const failures = deliveryFailures_;
            PostToGameThread([this, failures] { Log("Platform: events delivered again after " + std::to_string(failures) + " failed attempt(s)."); });
        }
        deliveryFailing_ = false;
        deliveryFailures_ = 0;
        batch.clear();
        return;
    }

    if (response.status == 0 || response.status >= 500)
    {
        // keep the batch (same body, same seqs) and try again later
        if (!deliveryFailing_)
        {
            std::string const reason = sg_DescribeResponse(response);
            PostToGameThread([this, reason] { Log("Platform: delivering events failed (" + reason + "); retrying with backoff."); });
        }
        deliveryFailing_ = true;
        ++deliveryFailures_;
        return;
    }

    // 4xx (and anything else unexpected): the platform will never take this batch
    std::string const reason = sg_DescribeResponse(response) + " " + sg_Snippet(response.body);
    PostToGameThread([this, reason, count] { Log("Platform: dropped " + std::to_string(count) + " event(s), the platform refused them: " + reason); });
    deliveryFailing_ = false;
    deliveryFailures_ = 0;
    batch.clear();
}

bool gPlatformHttp::TrySendHeartbeat(std::string const& body)
{
    gPlatformHttpRequest request;
    request.method = "POST";
    request.url = heartbeatUrl_;
    request.bearer = config_.serverKey;
    request.body = body;

    gPlatformHttpResponse const response = sender_->Send(request, cancel_);
    bool const delivered = response.status >= 200 && response.status < 300;
    // a platform that refuses heartbeats (4xx) gets the next one on the regular schedule
    bool const retry = response.status == 0 || response.status >= 500;

    if (delivered && heartbeatFailing_)
        PostToGameThread([this] { Log("Platform: heartbeats delivered again."); });
    else if (!delivered && !heartbeatFailing_)
    {
        std::string const reason = sg_DescribeResponse(response) + (response.status >= 400 && response.status < 500 ? " " + sg_Snippet(response.body) : "");
        PostToGameThread([this, reason] { Log("Platform: sending a heartbeat failed (" + reason + ")."); });
    }
    heartbeatFailing_ = !delivered;

    if (retry)
        PostToGameThread([this] { heartbeatSchedule_.Failed(std::chrono::steady_clock::now() + config_.heartbeatRetry); });
    return delivered;
}

void gPlatformHttp::Run()
{
    using Clock = std::chrono::steady_clock;

    std::deque<ePlatformEvent> backlog;
    std::vector<ePlatformEvent> batch;
    std::string batchBody;

    auto retryDelay = config_.retryInitial;
    auto nextSend = Clock::now();
    auto keysDelay = config_.keysRetryInitial;
    auto nextKeys = Clock::now();
    bool keysLoaded = false;
    std::size_t keysFailures = 0;

    bool const moderation = config_.chatFilter || config_.sanctions;
    auto nextModeration = Clock::now();
    auto moderationDelay = config_.moderationRetryInitial;
    bool moderationFailing = false;

    for (;;)
    {
        // take over what the game thread queued; the backlog has the same bound
        std::vector<ePlatformEvent> incoming;
        outbound_.PopBatch(incoming, config_.queueCapacity);
        for (auto& event : incoming)
            backlog.push_back(std::move(event));
        std::size_t dropped = outbound_.TakeDropped();
        while (backlog.size() > config_.queueCapacity)
        {
            backlog.pop_front();
            ++dropped;
        }
        if (dropped > 0)
            PostToGameThread([this, dropped] { Log("Platform: event queue full, dropped " + std::to_string(dropped) + " event(s)."); });

        // a batch, once formed, is sent unchanged until it is delivered or dropped
        if (batch.empty() && !backlog.empty())
        {
            std::size_t const count = std::min(config_.batchSize, backlog.size());
            for (std::size_t k = 0; k < count; ++k)
            {
                batch.push_back(std::move(backlog.front()));
                backlog.pop_front();
            }
            batchBody = BatchJson(bootId_, batch);
        }

        auto const now = Clock::now();
        bool const stopping = stopping_;
        auto const deadline = Clock::time_point(Clock::duration(flushDeadline_.load()));
        if (stopping && (batch.empty() || now >= deadline || nextSend >= deadline))
            break;

        // the latest heartbeat goes first and never waits behind events
        if (!stopping)
        {
            std::string heartbeat;
            {
                std::lock_guard<std::mutex> lock(heartbeatMutex_);
                if (heartbeatQueued_)
                {
                    heartbeat.swap(heartbeatBody_);
                    heartbeatQueued_ = false;
                }
            }
            if (!heartbeat.empty())
            {
                TrySendHeartbeat(heartbeat);
                continue;
            }
        }

        if (!stopping && keysWanted_ && now >= nextKeys)
        {
            gPlatformKeySet keys;
            std::string reason;
            if (TryFetchKeys(keys, reason))
            {
                keysWanted_ = false;
                keysLoaded = true;
                keysFailures = 0;
                keysDelay = config_.keysRetryInitial;
                PostToGameThread([this, keys] { OnKeysLoaded(keys); });
            }
            else if (!keysLoaded)
            {
                // no keys at all yet: keep trying, but only say so once
                if (keysFailures++ == 0)
                    PostToGameThread([this, reason] { Log("Platform: fetching " + keysUrl_ + " failed (" + reason + "); retrying with backoff."); });
                nextKeys = Clock::now() + keysDelay;
                keysDelay = std::min(keysDelay * 2, config_.keysRetryMax);
            }
            else
            {
                // a refresh failed; the old keys stay
                keysWanted_ = false;
                PostToGameThread([this, reason] { OnKeysFailed(reason); });
            }
            continue;
        }

        if (!stopping && moderation && (moderationWanted_ || now >= nextModeration))
        {
            moderationWanted_ = false;
            FetchModeration(nextModeration, moderationDelay, moderationFailing);
            continue;
        }

        if (!batch.empty() && now >= nextSend)
        {
            Deliver(batch, batchBody);
            if (batch.empty())
            {
                retryDelay = config_.retryInitial;
            }
            else
            {
                nextSend = Clock::now() + retryDelay;
                retryDelay = std::min(retryDelay * 2, config_.retryMax);
            }
            continue;
        }

        // sleep until there is something to do
        auto wake = now + std::chrono::hours(1);
        if (!batch.empty())
            wake = std::min(wake, nextSend);
        if (!stopping && keysWanted_)
            wake = std::min(wake, nextKeys);
        if (!stopping && moderation)
            wake = std::min(wake, nextModeration);
        if (stopping)
            wake = std::min(wake, deadline);
        outbound_.WaitUntil(wake);
    }

    {
        std::lock_guard<std::mutex> lock(doneMutex_);
        done_ = true;
    }
    doneCondition_.notify_all();
}

void gPlatformHttp::FetchModeration(std::chrono::steady_clock::time_point& next, std::chrono::milliseconds& retryDelay, bool& failing)
{
    using Clock = std::chrono::steady_clock;

    gPlatformHttpRequest request;
    request.method = "GET";
    request.url = moderationUrl_;
    request.bearer = config_.serverKey;
    gPlatformHttpResponse const response = sender_->Send(request, cancel_);

    bool hasFilter = false;
    auto filter = std::make_shared<gPlatformChatFilter::Lists>();
    std::vector<gPlatformSanction> sanctions;
    std::int64_t commandId = 0;
    std::string error;
    if (response.status == 200 && gPlatformModeration::TryParseState(response.body, hasFilter, *filter, sanctions, commandId, error))
    {
        if (failing)
            PostToGameThread([this] { Log("Platform: moderation state fetched again."); });
        failing = false;
        retryDelay = config_.moderationRetryInitial;
        next = Clock::now() + config_.moderationRefresh;
        PostToGameThread([this, hasFilter, filter, sanctions, commandId] {
            if (!shutDown_)
                moderation_.ApplyState(hasFilter ? filter.get() : nullptr, sanctions, commandId);
        });
        return;
    }

    if (response.status == 404)
    {
        // a platform without moderation: the built-in word list, no holding, ask again later
        next = Clock::now() + config_.chatOffAfterNotFound;
        std::chrono::seconds const offFor = config_.chatOffAfterNotFound;
        PostToGameThread([this, offFor] {
            if (!shutDown_)
                moderation_.SetMode(gPlatformModeration::Mode::Off, Clock::now(), offFor);
        });
        if (!failing)
            PostToGameThread([this] { Log("Platform: the platform does no moderation (404); chat is filtered locally."); });
        failing = true;
        return;
    }

    if (!failing)
    {
        std::string const reason = response.status == 200 ? "unusable answer (" + error + ")" : sg_DescribeResponse(response);
        PostToGameThread([this, reason] { Log("Platform: fetching the moderation state failed (" + reason + "); retrying with backoff."); });
    }
    failing = true;
    next = Clock::now() + retryDelay;
    retryDelay = std::min(retryDelay * 2, config_.moderationRetryMax);
}

// *************************************************************
// chat thread: held lines to the platform, verdicts back
// *************************************************************

void gPlatformHttp::RunChat()
{
    using Clock = std::chrono::steady_clock;

    int failures = 0;
    bool asyncPosted = false;

    // an empty batch first and after a quiet spell keeps the connection (and the platform's
    // side) warm, so a line never pays for a new connection or a cold start
    bool warm = true;

    for (;;)
    {
        std::vector<gPlatformOutboundLine> lines;
        {
            std::unique_lock<std::mutex> lock(chatMutex_);
            if (!warm)
                warm = !chatCondition_.wait_for(lock, config_.chatKeepWarm, [this] { return chatStop_ || !chatQueue_.empty(); });
            if (chatStop_)
                return;
            std::size_t const count = std::min(config_.chatBatchSize, chatQueue_.size());
            lines.assign(std::make_move_iterator(chatQueue_.begin()), std::make_move_iterator(chatQueue_.begin() + count));
            chatQueue_.erase(chatQueue_.begin(), chatQueue_.begin() + count);
        }
        if (!lines.empty())
            warm = false;
        if (warm && failures > 0)
        {
            // no keep-warm requests at a platform that is failing
            warm = false;
            continue;
        }

        gPlatformHttpRequest request;
        request.method = "POST";
        request.url = chatUrl_;
        request.bearer = config_.serverKey;
        request.body = gPlatformModeration::ChatRequestJson(bootId_, lines);
        request.timeout = config_.chatRequestTimeout;
        gPlatformHttpResponse const response = sender_->Send(request, chatCancel_);
        if (chatCancel_)
            return;

        std::vector<gPlatformChatVerdict> verdicts;
        std::vector<gPlatformSanction> sanctions;
        std::string error;
        if (warm)
        {
            // only the connection mattered
            warm = false;
            if (response.status == 200)
                continue;
        }

        if (response.status == 200 && gPlatformModeration::TryParseVerdicts(response.body, verdicts, sanctions, error))
        {
            bool const recovered = failures >= config_.chatFailuresBeforeAsync;
            failures = 0;
            asyncPosted = false;
            PostToGameThread([this, verdicts, sanctions, recovered] {
                if (shutDown_)
                    return;
                auto const now = Clock::now();
                if (recovered && moderation_.CurrentMode() == gPlatformModeration::Mode::Async)
                    moderation_.SetMode(gPlatformModeration::Mode::Hold, now);
                moderation_.ApplyVerdicts(verdicts, now);
                for (auto const& sanction : sanctions)
                    moderation_.ApplySanction(sanction, 0);
            });
            continue;
        }

        if (response.status == 404)
        {
            std::chrono::seconds const offFor = config_.chatOffAfterNotFound;
            PostToGameThread([this, offFor] {
                if (!shutDown_)
                    moderation_.SetMode(gPlatformModeration::Mode::Off, Clock::now(), offFor);
            });
            std::lock_guard<std::mutex> lock(chatMutex_);
            chatQueue_.clear();
            continue;
        }

        // anything else counts: no answer, 5xx, an unusable 200, and 4xx too (a platform that
        // refuses our requests gives no verdicts either; holding would only delay every line)
        ++failures;
        if (failures == 1)
        {
            std::string const reason = response.status == 200 ? "unusable answer (" + error + ")" : sg_DescribeResponse(response) + " " + sg_Snippet(response.body);
            PostToGameThread([this, reason] { Log("Platform: checking chat lines failed (" + reason + ")."); });
        }
        if (failures >= config_.chatFailuresBeforeAsync && !asyncPosted)
        {
            asyncPosted = true;
            PostToGameThread([this] {
                if (!shutDown_ && moderation_.CurrentMode() == gPlatformModeration::Mode::Hold)
                    moderation_.SetMode(gPlatformModeration::Mode::Async, Clock::now());
            });
        }
    }
}

// *************************************************************
// link thread: /link to the platform, the answers back
// *************************************************************

void gPlatformHttp::RunLinks()
{
    for (;;)
    {
        gPlatformLinkRequest next;
        {
            std::unique_lock<std::mutex> lock(linkMutex_);
            linkCondition_.wait(lock, [this] { return linkStop_ || !linkQueue_.empty(); });
            if (linkStop_)
                return;
            next = linkQueue_.front();
            linkQueue_.erase(linkQueue_.begin());
        }

        gPlatformHttpRequest request;
        request.method = "POST";
        request.url = linksUrl_;
        request.bearer = config_.serverKey;
        request.body = gPlatformLink::RequestJson(bootId_, next);
        request.timeout = config_.linkRequestTimeout;
        gPlatformHttpResponse const response = sender_->Send(request, linkCancel_);
        if (linkCancel_)
            return;

        gPlatformLinkAnswer answer;
        answer.id = next.id;
        answer.status = response.status;
        std::string error;
        if (response.status == 200 && !gPlatformLink::TryParseAnswer(response.body, answer, error))
            answer.status = 0;
        if (answer.status != 200 && answer.status != 404)
        {
            std::string const reason = response.status == 200 ? "unusable answer (" + error + ")" : sg_DescribeResponse(response) + " " + sg_Snippet(response.body);
            PostToGameThread([this, reason] { Log("Platform: /link failed (" + reason + ")."); });
        }
        PostToGameThread([this, answer] {
            if (!shutDown_)
                link_.Answer(answer);
        });
    }
}

// *************************************************************
// social thread: desktop players' social commands, the answers back
// *************************************************************

void gPlatformHttp::RunSocial()
{
    for (;;)
    {
        gPlatformSocialRequest next;
        {
            std::unique_lock<std::mutex> lock(socialMutex_);
            socialCondition_.wait(lock, [this] { return socialStop_ || !socialQueue_.empty(); });
            if (socialStop_)
                return;
            next = socialQueue_.front();
            socialQueue_.erase(socialQueue_.begin());
        }

        gPlatformHttpRequest request;
        request.method = "POST";
        request.url = socialUrl_;
        request.bearer = config_.serverKey;
        request.body = gPlatformSocial::RequestJson(bootId_, next);
        request.timeout = config_.socialRequestTimeout;
        gPlatformHttpResponse const response = sender_->Send(request, socialCancel_);
        if (socialCancel_)
            return;

        gPlatformSocialAnswer answer;
        answer.id = next.id;
        answer.status = response.status;
        std::string error;
        if (response.status == 200 && !gPlatformSocial::TryParseAnswer(response.body, answer, error))
            answer.status = 0;
        if (answer.status != 200 && answer.status != 404)
        {
            std::string const reason = response.status == 200 ? "unusable answer (" + error + ")" : sg_DescribeResponse(response) + " " + sg_Snippet(response.body);
            PostToGameThread([this, reason] { Log("Platform: a friends command failed (" + reason + ")."); });
        }
        PostToGameThread([this, answer] {
            if (!shutDown_)
                social_.Answer(answer);
        });
    }
}

// *************************************************************
// command thread: the long poll
// *************************************************************

bool gPlatformHttp::WaitForCommandPoll(std::chrono::steady_clock::time_point until)
{
    std::unique_lock<std::mutex> lock(pollMutex_);
    return !pollCondition_.wait_until(lock, until, [this] { return pollCancel_.load(); });
}

void gPlatformHttp::RunCommands()
{
    using Clock = std::chrono::steady_clock;

    std::int64_t cursor = -1; // none until the platform names one
    auto retryDelay = config_.commandRetryInitial;
    bool failing = false;

    while (!pollCancel_)
    {
        auto const started = Clock::now();

        gPlatformHttpRequest request;
        request.method = "GET";
        request.url = CommandsUrl(config_.apiUrl, config_.serverId, cursor, config_.commandWaitSeconds);
        request.bearer = config_.serverKey;
        request.timeout = std::chrono::seconds(config_.commandWaitSeconds + 15);

        gPlatformHttpResponse const response = sender_->Send(request, pollCancel_);
        if (pollCancel_)
            break;

        std::vector<gPlatformCommand> commands;
        std::int64_t next = cursor;
        std::string error;
        if (response.status == 200 && TryParseCommands(response.body, commands, next, error))
        {
            if (failing)
                PostToGameThread([this] { Log("Platform: command polling works again."); });
            failing = false;
            retryDelay = config_.commandRetryInitial;
            cursor = next;
            if (!commands.empty())
                PostToGameThread([this, commands] { ApplyCommands(commands); });

            // a platform that answers at once must not make this a busy loop
            WaitForCommandPoll(started + config_.commandPollMinInterval);
            continue;
        }

        if (!failing)
        {
            std::string const reason = response.status == 200 ? "unusable answer (" + error + ")" : sg_DescribeResponse(response);
            PostToGameThread([this, reason] { Log("Platform: polling commands failed (" + reason + "); retrying with backoff."); });
        }
        failing = true;
        WaitForCommandPoll(Clock::now() + retryDelay);
        retryDelay = std::min(retryDelay * 2, config_.commandRetryMax);
    }
}
