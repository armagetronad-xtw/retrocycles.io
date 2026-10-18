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

#include <utility>

namespace
{
std::string sg_SocialTrim(std::string const& text)
{
    std::size_t const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    std::size_t const end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string sg_SocialLower(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

// the first word and what follows it
void sg_SocialFirstWord(std::string const& text, std::string& word, std::string& rest)
{
    std::size_t const end = text.find_first_of(" \t");
    word = end == std::string::npos ? text : text.substr(0, end);
    rest = end == std::string::npos ? std::string() : sg_SocialTrim(text.substr(end));
}

// characters as the platform counts them: the code points of the UTF-8 the request carries
std::size_t sg_SocialCharacters(std::string const& text)
{
    std::size_t count = 0;
    for (char c : gPlatformHttp::ToUtf8(text))
        if ((static_cast<unsigned char>(c) & 0xc0) != 0x80)
            ++count;
    return count;
}

// the whisper's pink, as the play page draws direct messages
char const* const sg_socialMessageColor = "0xff8cff";

char const* const sg_socialLogin = "Friends and messages need a Retrocycles account: log in with your game password first.";
} // namespace

gPlatformSocial::gPlatformSocial(gPlatformSocialEffects effects) : effects_(std::move(effects))
{
}

void gPlatformSocial::SplitCommand(std::string const& line, std::string& word, std::string& rest)
{
    word.clear();
    rest.clear();
    std::string const trimmed = sg_SocialTrim(line);
    if (trimmed.empty() || trimmed[0] != '/')
        return;
    sg_SocialFirstWord(trimmed, word, rest);
    word = sg_SocialLower(word);
}

std::string gPlatformSocial::ToLatin1(std::string const& utf8)
{
    std::string out;
    for (std::size_t i = 0; i < utf8.size();)
    {
        unsigned char const c = static_cast<unsigned char>(utf8[i]);
        std::size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
        if (i + length > utf8.size())
            length = 1;
        if (length == 1)
        {
            if (c >= 0x20 && c != 0x7f && c < 0x80)
                out.push_back(static_cast<char>(c));
            else if (c >= 0x80)
                out.push_back('?');
        }
        else if (length == 2)
        {
            unsigned const code = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3fu);
            out.push_back(code >= 0xa0 && code <= 0xff ? static_cast<char>(code) : '?');
        }
        else
            out.push_back('?');
        i += length;
    }
    return out;
}

bool gPlatformSocial::Submit(gPlatformChatPlayer const& player, std::string const& line, Clock::time_point now)
{
    std::string word, rest;
    SplitCommand(line, word, rest);
    if (word.empty())
        return false;

    gPlatformSocialRequest request;
    request.playerId = player.playerId;
    request.gid = player.gid;

    if (word == "/msg")
    {
        // a name that matches someone here is the room's own whisper; only a name that matches
        // nobody falls through, and only for a player the platform can know
        std::string name, text;
        sg_SocialFirstWord(rest, name, text);
        if (name.empty() || text.empty() || !player.LoggedIn())
            return false;
        if (effects_.inRoom && effects_.inRoom(player.playerId, name) >= 0)
            return false;
        request.command = "message";
        request.target = name;
        request.text = text;
    }
    else if (word == "/f" || word == "/friend")
    {
        std::string name, ignored;
        sg_SocialFirstWord(rest, name, ignored);
        request.command = name.empty() ? "friends" : "friend";
        request.target = name;
        if (!name.empty() && effects_.inRoom)
            request.netId = effects_.inRoom(player.playerId, name);
    }
    else if (word == "/r")
    {
        if (rest.empty())
        {
            effects_.tell(player.conn, "Usage: /r message, an answer to the last friend who messaged you.");
            return true;
        }
        request.command = "reply";
        request.text = rest;
    }
    else if (word == "/join")
    {
        std::string name, ignored;
        sg_SocialFirstWord(rest, name, ignored);
        if (name.empty())
        {
            effects_.tell(player.conn, "Usage: /join name, a friend's name: you move to their room.");
            return true;
        }
        request.command = "join";
        request.target = name;
    }
    else
        return false;

    if (!player.LoggedIn())
    {
        effects_.tell(player.conn, sg_socialLogin);
        return true;
    }
    if (sg_SocialCharacters(request.text) > MaxTextLength)
    {
        effects_.tell(player.conn, "Not sent: at most " + std::to_string(MaxTextLength) + " characters.");
        return true;
    }
    return Send(player, std::move(request), now);
}

bool gPlatformSocial::Send(gPlatformChatPlayer const& player, gPlatformSocialRequest request, Clock::time_point now)
{
    for (auto const& pending : pending_)
        if (pending.second.playerId == player.playerId)
        {
            effects_.tell(player.conn, "One moment, the last command is still on its way.");
            return true;
        }

    auto& times = commands_[player.playerId];
    while (!times.empty() && now - times.front() > std::chrono::seconds(60))
        times.pop_front();
    if (static_cast<int>(times.size()) >= CommandsPerMinute)
    {
        effects_.tell(player.conn, "Too many commands. Wait a minute.");
        return true;
    }
    times.push_back(now);

    request.id = nextId_++;
    pending_[request.id] = request;
    effects_.send(request);
    return true;
}

void gPlatformSocial::Answer(gPlatformSocialAnswer const& answer)
{
    auto const found = pending_.find(answer.id);
    if (found == pending_.end())
        return;
    gPlatformSocialRequest const request = found->second;
    pending_.erase(found);

    int const conn = effects_.connOf(request.playerId);
    if (conn <= 0)
        return; // the player left meanwhile
    if (answer.status == 404)
    {
        effects_.tell(conn, "This server has no friends or messages.");
        return;
    }
    if (answer.status != 200 || answer.lines.empty())
    {
        effects_.tell(conn, "Friends and messages are not available right now. Try again in a minute.");
        return;
    }
    for (auto const& line : answer.lines)
    {
        std::string const text = ToLatin1(line);
        // a message on its way is echoed in the whisper's colour, as it arrives
        bool const echo = answer.result == "sent";
        effects_.tell(conn, echo ? std::string(sg_socialMessageColor) + text : text);
    }
}

void gPlatformSocial::Deliver(std::string const& gid, std::string const& kind, std::string const& text)
{
    if (gid.empty() || text.empty() || !effects_.connsOfGid)
        return;
    std::string line = ToLatin1(text);
    if (line.size() > 400)
        line.resize(400);
    if (kind == "message")
        line = sg_socialMessageColor + line;
    for (int const conn : effects_.connsOfGid(gid))
        if (conn > 0 && !(effects_.isBrowser && effects_.isBrowser(conn)))
            effects_.tell(conn, line);
}

std::string gPlatformSocial::RequestJson(std::string const& bootId, gPlatformSocialRequest const& request)
{
    nlohmann::ordered_json body;
    body["bootId"] = bootId;
    body["gid"] = gPlatformHttp::ToUtf8(request.gid);
    body["command"] = request.command;
    if (!request.target.empty())
        body["target"] = gPlatformHttp::ToUtf8(request.target);
    if (request.netId >= 0)
        body["netId"] = request.netId;
    if (!request.text.empty())
        body["text"] = gPlatformHttp::ToUtf8(request.text);
    return body.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

bool gPlatformSocial::TryParseAnswer(std::string const& body, gPlatformSocialAnswer& answer, std::string& error)
{
    nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
    {
        error = "not a JSON object";
        return false;
    }
    auto const result = json.find("result");
    auto const lines = json.find("lines");
    if (result == json.end() || !result->is_string() || lines == json.end() || !lines->is_array())
    {
        error = "no result or lines";
        return false;
    }
    answer.result = result->get<std::string>();
    answer.lines.clear();
    for (auto const& line : *lines)
        if (line.is_string() && answer.lines.size() < 8)
            answer.lines.push_back(line.get<std::string>());
    return true;
}
