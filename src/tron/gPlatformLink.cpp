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

#include "gPlatformLink.h"

#include "gPlatformHttp.h"

#include <utility>

namespace
{
std::string sg_LinkTrim(std::string const& text)
{
    std::size_t const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    std::size_t const end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string sg_LinkLower(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// a platform text for the console: no control characters, at most 200 bytes
std::string sg_LinkClean(std::string const& text)
{
    std::string out;
    for (char const c : text)
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f)
            out.push_back(c);
    if (out.size() > 200)
        out.resize(200);
    return out;
}

char const* const sg_linkScreen = "the Linked IDs screen of your profile";
} // namespace

gPlatformLink::gPlatformLink(std::string authority, gPlatformLinkEffects effects)
    : authority_(std::move(authority)), effects_(std::move(effects))
{
}

bool gPlatformLink::IsLinkCommand(std::string const& line, std::string& args)
{
    if (line.size() < 5 || sg_LinkLower(line.substr(0, 5)) != "/link")
        return false;
    if (line.size() > 5 && line[5] != ' ' && line[5] != '\t')
        return false;
    args = sg_LinkTrim(line.substr(5));
    return true;
}

std::string gPlatformLink::AuthorityOf(std::string const& gid)
{
    std::size_t const at = gid.rfind('@');
    if (at == std::string::npos || at == 0 || at + 1 >= gid.size())
        return std::string();
    return gid.substr(at + 1);
}

bool gPlatformLink::IsOwnAuthority(std::string const& authority, std::string const& own)
{
    std::string const lower = sg_LinkLower(authority);
    std::string const ours = sg_LinkLower(own);
    return lower == ours || lower == ours + ".authentication.armagetronad.net";
}

bool gPlatformLink::Submit(gPlatformChatPlayer const& player, std::string const& line, Clock::time_point now)
{
    std::string code;
    if (!IsLinkCommand(line, code))
        return false;

    if (code.empty())
    {
        effects_.tell(player.conn, std::string("Usage: /link CODE, with the code from ") + sg_linkScreen + ".");
        return true;
    }
    if (!player.LoggedIn())
    {
        effects_.tell(player.conn, "Log in with the ID you want to link first (/login name@forums), then type /link " + code + " again.");
        return true;
    }
    std::string const authority = AuthorityOf(player.gid);
    if (authority.empty())
    {
        effects_.tell(player.conn, "You are logged in with this server's own account (" + player.gid + "). Only global IDs like name@forums can be linked.");
        return true;
    }
    if (IsOwnAuthority(authority, authority_))
    {
        effects_.tell(player.conn, player.gid + " is an ID of this platform already. Log in with your old ID (/login name@forums) to link it.");
        return true;
    }
    for (auto const& pending : pending_)
        if (pending.second.playerId == player.playerId)
        {
            effects_.tell(player.conn, "Still linking, one moment.");
            return true;
        }

    auto& times = attempts_[player.playerId];
    while (!times.empty() && now - times.front() > std::chrono::seconds(60))
        times.pop_front();
    if (static_cast<int>(times.size()) >= AttemptsPerMinute)
    {
        effects_.tell(player.conn, "Please wait a minute before trying /link again.");
        return true;
    }
    times.push_back(now);

    if (code.size() > MaxCodeLength)
    {
        gPlatformLinkAnswer answer;
        answer.status = 200;
        answer.result = "invalid_code";
        effects_.tell(player.conn, TextFor(answer));
        return true;
    }

    gPlatformLinkRequest request;
    request.id = nextId_++;
    request.playerId = player.playerId;
    request.gid = player.gid;
    request.name = player.name;
    request.code = code;
    pending_[request.id] = request;
    effects_.send(request);
    return true;
}

void gPlatformLink::Answer(gPlatformLinkAnswer const& answer)
{
    auto const found = pending_.find(answer.id);
    if (found == pending_.end())
        return;
    gPlatformLinkRequest const request = found->second;
    pending_.erase(found);

    if (answer.status == 200 && answer.result == "linked" && effects_.log)
        effects_.log("Platform: " + request.name + " linked " + request.gid + " to the account " + sg_LinkClean(answer.handle) + ".");

    int const conn = effects_.connOf(request.playerId);
    if (conn <= 0)
        return; // the player left meanwhile
    gPlatformLinkAnswer named = answer;
    if (named.gid.empty())
        named.gid = request.gid;
    effects_.tell(conn, TextFor(named));
}

std::string gPlatformLink::TextFor(gPlatformLinkAnswer const& answer)
{
    if (answer.status == 404)
        return "This server cannot link IDs.";
    if (answer.status != 200)
        return "Linking is not available right now. Try again in a minute.";

    std::string const gid = sg_LinkClean(answer.gid);
    std::string const handle = sg_LinkClean(answer.handle);
    std::string const& result = answer.result;
    if (result == "linked")
        return "Linked " + gid + " to the account " + handle + ". Your games as " + gid + " count for it now.";
    if (result == "already_linked")
        return gid + " is linked to the account " + handle + " already.";
    if (result == "invalid_code")
        return std::string("That code is not valid. Check it on ") + sg_linkScreen + ".";
    if (result == "expired")
        return std::string("That code has expired. Get a new one on ") + sg_linkScreen + ".";
    if (result == "used")
        return std::string("That code was used already. Get a new one on ") + sg_linkScreen + ".";
    if (result == "linked_elsewhere")
        return gid + " is linked to another account. Unlink it there first.";
    if (result == "rate_limited")
        return "Too many tries with " + gid + ". Wait an hour, then try again with a new code.";

    std::string const message = sg_LinkClean(answer.message);
    return message.empty() ? "Linking did not work (" + sg_LinkClean(result) + ")." : message;
}

std::string gPlatformLink::RequestJson(std::string const& bootId, gPlatformLinkRequest const& request)
{
    nlohmann::ordered_json body;
    body["bootId"] = bootId;
    body["gid"] = gPlatformHttp::ToUtf8(request.gid);
    body["name"] = gPlatformHttp::ToUtf8(request.name);
    body["code"] = gPlatformHttp::ToUtf8(request.code);
    return body.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

bool gPlatformLink::TryParseAnswer(std::string const& body, gPlatformLinkAnswer& answer, std::string& error)
{
    nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "not a JSON object";
        return false;
    }
    auto const text = [&](char const* key) {
        auto const found = json.find(key);
        return found != json.end() && found->is_string() ? found->get<std::string>() : std::string();
    };
    answer.result = text("result");
    if (answer.result.empty())
    {
        error = "no result";
        return false;
    }
    answer.message = text("message");
    answer.gid = text("gid");
    answer.handle = text("handle");
    return true;
}
