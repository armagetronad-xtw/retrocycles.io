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

#include "ePlatform.h"

#include "ePlayer.h"
#include "nNetwork.h"
#include "tError.h"

#include <cstring>
#include <utility>

// *************************************************************
// ePlatformEventQueue
// *************************************************************

void ePlatformEventQueue::Push(ePlatformEvent event)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0)
        {
            ++dropped_;
            return;
        }
        while (events_.size() >= capacity_)
        {
            events_.pop_front();
            ++dropped_;
        }
        events_.push_back(std::move(event));
    }
    condition_.notify_one();
}

std::size_t ePlatformEventQueue::PopBatch(std::vector<ePlatformEvent>& out, std::size_t max)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    while (count < max && !events_.empty())
    {
        out.push_back(std::move(events_.front()));
        events_.pop_front();
        ++count;
    }
    return count;
}

bool ePlatformEventQueue::WaitUntil(std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock(mutex_);
    bool const ready = condition_.wait_until(lock, deadline, [this] { return woken_ || !events_.empty(); });
    woken_ = false;
    return ready;
}

void ePlatformEventQueue::Wake()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        woken_ = true;
    }
    condition_.notify_all();
}

std::size_t ePlatformEventQueue::Size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
}

std::size_t ePlatformEventQueue::TakeDropped()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t const dropped = dropped_;
    dropped_ = 0;
    return dropped;
}

// *************************************************************
// ePlatform: registry and game thread tasks
// *************************************************************

ePlatform* ePlatform::s_instance_ = nullptr;

ePlatform::ePlatform() {}

ePlatform::~ePlatform()
{
    if (this == s_instance_)
        s_instance_ = nullptr;
}

void ePlatform::Register(ePlatform* platform)
{
    tASSERT(platform);
    tASSERT(!s_instance_);
    s_instance_ = platform;
}

void ePlatform::Unregister(ePlatform* platform)
{
    tASSERT(platform == s_instance_);
    s_instance_ = nullptr;
}

void ePlatform::PostToGameThread(std::function<void()> task)
{
    std::lock_guard<std::mutex> lock(tasksMutex_);
    tasks_.push_back(std::move(task));
}

void ePlatform::RunGameThreadTasks()
{
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(tasksMutex_);
        tasks.swap(tasks_);
    }
    for (auto& task : tasks)
        task();
}

// *************************************************************
// hooks
// *************************************************************

static char const* se_platformLoginMethod = "krawall";

char const* ePlatform::CurrentLoginMethod() noexcept
{
    return se_platformLoginMethod;
}

bool ePlatform::LoginReplacesOld(int newOwner, int oldOwner, char const* method) noexcept
{
    return method && std::strcmp(method, "token") == 0 && oldOwner > 0 && oldOwner <= MAXCLIENTS && oldOwner != newOwner;
}

void ePlatform::ServerStarted()
{
    ePlatform* platform = s_instance_;
    if (!platform || platform->serverStartedReported_)
        return;

    platform->serverStartedReported_ = true;
    platform->OnServerStarted();
}

void ePlatform::PlayerAuthenticated(ePlayerNetID* player, tString const& gid)
{
    ePlatform* platform = s_instance_;
    if (!platform || !player)
        return;

#ifdef KRAWALL_SERVER
    // Authenticate() refuses banned names silently
    if (!player->IsAuthenticated())
        return;
#endif

    platform->OnPlayerAuthenticated(*player, gid, se_platformLoginMethod);
}

void ePlatform::RoundEnded(ePlatformRoundResult const& result)
{
    if (s_instance_)
        s_instance_->OnRoundEnded(result);
}

void ePlatform::MatchStarted(ePlatformMatchStart const& start)
{
    if (s_instance_)
        s_instance_->OnMatchStarted(start);
}

void ePlatform::Death(ePlatformDeath const& death)
{
    if (s_instance_)
        s_instance_->OnDeath(death);
}

void ePlatform::MatchEnded(ePlatformMatchResult const& result)
{
    if (s_instance_)
        s_instance_->OnMatchEnded(result);
}

// *************************************************************
// chat
// *************************************************************

static ePlatform::ChatDeliverCallback* se_platformChatDeliverCallback = nullptr;

void ePlatform::SetChatDeliverCallback(ChatDeliverCallback* callback)
{
    se_platformChatDeliverCallback = callback;
}

bool ePlatform::ChatSubmitted(ePlayerNetID* player, tColoredString const& line)
{
    ePlatform* platform = s_instance_;
    if (!platform || !player || sn_GetNetState() != nSERVER)
        return false;
    return platform->OnChatSubmitted(*player, line);
}

bool ePlatform::DeliverChat(ePlayerNetID* player, tColoredString const& line)
{
    if (!se_platformChatDeliverCallback || !player || player->Owner() <= 0)
        return false;

    // only players still on the server
    bool listed = false;
    for (int i = se_PlayerNetIDs.Len() - 1; i >= 0 && !listed; --i)
        listed = se_PlayerNetIDs(i) == player;
    if (!listed)
        return false;

    // replies to commands go to the player's connection, as while handling its message
    nCurrentSenderID currentSender(player->Owner());
    (*se_platformChatDeliverCallback)(player, line);
    return true;
}

// *************************************************************
// login tokens, client side
// *************************************************************

// a token queued by SendLoginToken() for the next connection
static tString se_platformPendingToken;
static std::chrono::steady_clock::time_point se_platformPendingTokenSince;

// tokens live 60 s; one that waited this long for a connection is useless
static std::chrono::seconds const se_platformPendingTokenLifetime(60);

static void se_PlatformLoginTokenHandler(nMessage& m);
static nDescriptor se_platformLoginTokenDescriptor(ePLATFORM_LOGIN_TOKEN_DESCRIPTOR, &se_PlatformLoginTokenHandler, "platform_login_token");

void ePlatform::SendLoginToken(tString const& token)
{
    se_platformPendingToken = token;
    se_platformPendingTokenSince = std::chrono::steady_clock::now();
}

// sends the pending token as soon as this client is connected to a server
static void se_SendPendingLoginToken()
{
    if (se_platformPendingToken.Len() <= 1)
        return;

    if (std::chrono::steady_clock::now() - se_platformPendingTokenSince > se_platformPendingTokenLifetime)
    {
        se_platformPendingToken = "";
        return;
    }

    if (sn_GetNetState() != nCLIENT || sn_myNetID <= 0)
        return;

    nMessage* m = new nMessage(se_platformLoginTokenDescriptor);
    *m << se_platformPendingToken;
    m->Send(0);

    se_platformPendingToken = "";
}

// *************************************************************
// moves to another room: capabilities and the move message
// *************************************************************

static tString se_platformClientCapabilities;
static bool se_platformCapabilitiesDue = false; // logged in to a server, not announced yet
static ePlatform::MoveCallback* se_platformMoveCallback = nullptr;

// server side: what each connection announced
static std::string se_platformPeerCapabilities[MAXCLIENTS + 2];

static void se_PlatformClientCapsHandler(nMessage& m);
static nDescriptor se_platformClientCapsDescriptor(ePLATFORM_CLIENT_CAPS_DESCRIPTOR, &se_PlatformClientCapsHandler, "platform_client_caps");

static void se_PlatformMoveHandler(nMessage& m);
static nDescriptor se_platformMoveDescriptor(ePLATFORM_MOVE_DESCRIPTOR, &se_PlatformMoveHandler, "platform_move");

void ePlatform::SetClientCapabilities(tString const& capabilities)
{
    se_platformClientCapabilities = capabilities;
}

void ePlatform::SetMoveCallback(MoveCallback* callback)
{
    se_platformMoveCallback = callback;
}

bool ePlatform::HasWord(std::string const& words, char const* word)
{
    std::string const wanted(word ? word : "");
    if (wanted.empty())
        return false;
    std::size_t pos = 0;
    while (pos < words.size())
    {
        while (pos < words.size() && words[pos] == ' ')
            ++pos;
        std::size_t end = words.find(' ', pos);
        if (end == std::string::npos)
            end = words.size();
        if (end > pos && words.compare(pos, end - pos, wanted) == 0)
            return true;
        pos = end;
    }
    return false;
}

bool ePlatform::ClientHasCapability(int userID, char const* capability)
{
    if (userID <= 0 || userID > MAXCLIENTS)
        return false;
    return HasWord(se_platformPeerCapabilities[userID], capability);
}

std::string ePlatform::ClientCapabilities(int userID)
{
    if (userID <= 0 || userID > MAXCLIENTS)
        return std::string();
    return se_platformPeerCapabilities[userID];
}

bool ePlatform::SendMove(int userID, ePlatformMoveNotice const& notice)
{
    if (sn_GetNetState() != nSERVER || !ClientHasCapability(userID, ePLATFORM_CAPABILITY_MOVE))
        return false;

    nMessage* m = new nMessage(se_platformMoveDescriptor);
    *m << tString(notice.moveId.c_str());
    *m << tString(notice.serverId.c_str());
    *m << tString(notice.name.c_str());
    *m << tString(notice.signalUrl.c_str());
    *m << tString(notice.reason.c_str());
    m->Send(userID);
    return true;
}

// keeps the announced words short and printable
static std::string se_CleanCapabilities(tString const& raw)
{
    std::string out;
    for (int i = 0; i < raw.Len() - 1 && out.size() < 200; ++i)
    {
        auto const c = static_cast<unsigned char>(raw[i]);
        out.push_back(c > 0x20 && c < 0x7f ? static_cast<char>(c) : ' ');
    }
    return out;
}

static void se_PlatformClientCapsHandler(nMessage& m)
{
    if (sn_GetNetState() != nSERVER)
        return;
    int const user = m.SenderID();
    if (user <= 0 || user > MAXCLIENTS)
        return;

    tString raw;
    m.ReadRaw(raw);
    se_platformPeerCapabilities[user] = se_CleanCapabilities(raw);
}

static void se_PlatformMoveHandler(nMessage& m)
{
    // only the server sends moves
    if (sn_GetNetState() != nCLIENT || m.SenderID() != 0)
        return;

    // raw: URLs and ids must not go through the colour code filters of operator >>
    tString moveId, serverId, name, signalUrl, reason;
    m.ReadRaw(moveId);
    m.ReadRaw(serverId);
    m.ReadRaw(name);
    m.ReadRaw(signalUrl);
    m.ReadRaw(reason);

    ePlatformMoveNotice notice;
    notice.moveId = static_cast<char const*>(moveId);
    notice.serverId = static_cast<char const*>(serverId);
    notice.name = static_cast<char const*>(name);
    notice.signalUrl = static_cast<char const*>(signalUrl);
    notice.reason = static_cast<char const*>(reason);

    if (se_platformMoveCallback)
        (*se_platformMoveCallback)(notice);
    else
        con << "Platform: the server moves this client to " << name << ", but nothing here follows moves.\n";
}

// a connection came or went: a server forgets what it announced; a client announces itself
// to the server it just logged in to
static void se_PlatformCapsLoginLogout()
{
    int const user = nCallbackLoginLogout::User();
    if (user > 0 && user <= MAXCLIENTS)
        se_platformPeerCapabilities[user].clear();
    if (user == 0)
        se_platformCapabilitiesDue = nCallbackLoginLogout::Login() && sn_GetNetState() == nCLIENT;
}

static nCallbackLoginLogout se_platformCapsLoginLogout(&se_PlatformCapsLoginLogout);

static void se_SendClientCapabilities()
{
    if (!se_platformCapabilitiesDue)
        return;
    if (sn_GetNetState() != nCLIENT)
    {
        se_platformCapabilitiesDue = false;
        return;
    }
    if (sn_myNetID <= 0)
        return;

    se_platformCapabilitiesDue = false;
    if (se_platformClientCapabilities.Len() <= 1)
        return;

    nMessage* m = new nMessage(se_platformClientCapsDescriptor);
    *m << se_platformClientCapabilities;
    m->Send(0);
}

void ePlatform::Drain()
{
    se_SendPendingLoginToken();
    se_SendClientCapabilities();

    ePlatform* platform = s_instance_;
    if (!platform)
        return;

    platform->RunGameThreadTasks();

    // a task may have unregistered the platform
    if (platform == s_instance_)
        platform->OnDrain();
}

ePlatformPlayerCounts ePlatform::CountPlayers()
{
    ePlatformPlayerCounts counts;
    for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
    {
        ePlayerNetID const* player = se_PlayerNetIDs(i);
        if (!player)
            continue;
        if (player->IsHuman())
            ++counts.humans;
        else
            ++counts.bots;
    }
    counts.capacity = sn_MaxUsers();
    counts.clients = sn_NumUsers(); // the server's own measure of full (GetFreeSlot())
    return counts;
}

std::vector<ePlatformRosterPlayer> ePlatform::RosterPlayers()
{
    std::vector<ePlatformRosterPlayer> roster;
    for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
    {
        ePlayerNetID const* player = se_PlayerNetIDs(i);
        if (!player || !player->IsHuman())
            continue;

        ePlatformRosterPlayer entry;
        entry.netId = static_cast<int>(player->ID());
#ifdef KRAWALL_SERVER
        if (player->IsAuthenticated())
            entry.gid = static_cast<char const*>(player->GetRawAuthenticatedName());
#endif
        // the live measurement of the player's connection (the synced ePlayerNetID::ping lags
        // behind it on purpose); a player of the server itself has none
        int const owner = player->Owner();
        if (owner > 0 && owner <= MAXCLIENTS)
        {
            REAL const seconds = sn_Connections[owner].ping.GetPing();
            int ms = static_cast<int>(seconds * 1000 + .5f);
            entry.ping = ms < 0 ? 0 : (ms > 60000 ? 60000 : ms);
            entry.browser = sn_IsVirtualPeer(owner);
        }
        entry.spectator = player->IsSpectating() || !player->CurrentTeam();
        roster.push_back(std::move(entry));
    }
    return roster;
}

// *************************************************************
// login tokens, server side
// *************************************************************

static void se_PlatformLoginTokenHandler(nMessage& m)
{
    if (sn_GetNetState() != nSERVER)
        return;

    // raw: the token must not go through the color code filters of operator >>
    tString token;
    m.ReadRaw(token);

    // servers without a platform ignore the message
    ePlatform* platform = ePlatform::Instance();
    if (platform)
        platform->HandleLoginToken(m.SenderID(), token);
}

static ePlatform::LoginResultCallback* se_platformLoginResultCallback = nullptr;

void ePlatform::SetLoginResultCallback(LoginResultCallback* callback)
{
    se_platformLoginResultCallback = callback;
}

ePlayerNetID* ePlatform::FindLoginCandidate(int userID)
{
    if (userID <= 0)
        return nullptr;

    for (int i = 0; i < se_PlayerNetIDs.Len(); ++i)
    {
        ePlayerNetID* player = se_PlayerNetIDs(i);
        if (!player || player->Owner() != userID || !player->IsHuman())
            continue;
#ifdef KRAWALL_SERVER
        if (player->IsAuthenticated())
            continue;
#endif
        return player;
    }

    return nullptr;
}

namespace
{
// marks the logins completed in its lifetime as platform token logins
class ePlatformTokenLoginScope final
{
public:
    ePlatformTokenLoginScope() : previous_{se_platformLoginMethod} { se_platformLoginMethod = "token"; }
    ~ePlatformTokenLoginScope() { se_platformLoginMethod = previous_; }

    ePlatformTokenLoginScope(ePlatformTokenLoginScope const&) = delete;
    ePlatformTokenLoginScope& operator=(ePlatformTokenLoginScope const&) = delete;

private:
    char const* previous_;
};
} // namespace

bool ePlatform::TryCompleteLogin(ePlayerNetID* player, tString const& gid, int accessLevel)
{
#ifdef KRAWALL_SERVER
    if (!player || !se_platformLoginResultCallback)
        return false;

    // gid is <handle>@<authority>; the authority is everything after the last @
    std::string const full(static_cast<char const*>(gid));
    std::size_t const at = full.rfind('@');
    if (at == std::string::npos || at == 0 || at + 1 >= full.size())
        return false;

    nKrawall::nCheckResult result;
    result.username = tString(full.substr(0, at).c_str());
    result.authority = tString(full.substr(at + 1).c_str());
    result.success = true;
    // platform roles (gPlatformHttp maps them); the owner level is never handed out by a token
    result.accessLevel = accessLevel < 0 ? tAccessLevel_Authenticated : accessLevel < tAccessLevel_Admin ? tAccessLevel_Admin : static_cast<tAccessLevel>(accessLevel);
    result.user = player;

    // console output of the login goes to the player, as in nLoginProcess::Finish()
    nCurrentSenderID currentSender(player->Owner());

    ePlatformTokenLoginScope scope;
    (*se_platformLoginResultCallback)(result);

    return player->IsAuthenticated();
#else
    return false;
#endif
}
