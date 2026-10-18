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

#include "gPlatformModeration.h"
#include "gPlatformHttp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

// *************************************************************
// text helpers
// *************************************************************

namespace
{
// one character of a line: its code point and where its bytes are
struct sgCodePoint
{
    sgCodePoint(std::uint32_t c, std::size_t b, std::size_t e) : cp(c), begin(b), end(e) {}

    std::uint32_t cp;
    std::size_t begin;
    std::size_t end;
};

// decodes UTF-8 if the whole text is valid UTF-8, Latin-1 otherwise (the game's two encodings)
std::vector<sgCodePoint> sg_Decode(std::string const& text, bool* utf8 = nullptr)
{
    std::vector<sgCodePoint> out;
    bool valid = true;
    std::size_t i = 0;
    while (i < text.size() && valid)
    {
        auto const c = static_cast<unsigned char>(text[i]);
        int follow = 0;
        std::uint32_t cp = 0;
        if (c < 0x80)
            follow = 0, cp = c;
        else if (c >= 0xc2 && c <= 0xdf)
            follow = 1, cp = c & 0x1f;
        else if (c >= 0xe0 && c <= 0xef)
            follow = 2, cp = c & 0x0f;
        else if (c >= 0xf0 && c <= 0xf4)
            follow = 3, cp = c & 0x07;
        else
            valid = false;

        for (int k = 1; valid && k <= follow; ++k)
        {
            if (i + k >= text.size() || (static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80)
                valid = false;
            else
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        }
        if (valid && ((follow == 2 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) || (follow == 3 && (cp < 0x10000 || cp > 0x10ffff))))
            valid = false;
        if (valid)
            out.emplace_back(cp, i, i + follow + 1);
        i += follow + 1;
    }
    if (utf8)
        *utf8 = valid;
    if (valid)
        return out;

    out.clear();
    for (std::size_t k = 0; k < text.size(); ++k)
        out.emplace_back(static_cast<unsigned char>(text[k]), k, k + 1);
    return out;
}

// UTF-8 to Latin-1 (for a replacement of a line that was Latin-1); '?' for what does not fit
std::string sg_ToLatin1(std::string const& text)
{
    bool utf8 = false;
    std::vector<sgCodePoint> const cps = sg_Decode(text, &utf8);
    if (!utf8)
        return text;
    std::string out;
    for (auto const& c : cps)
        out.push_back(c.cp <= 0xff ? static_cast<char>(c.cp) : '?');
    return out;
}

bool sg_IsUtf8(std::string const& text)
{
    bool utf8 = false;
    sg_Decode(text, &utf8);
    return utf8;
}

// control characters removed, at most max bytes (not splitting UTF-8)
std::string sg_Clean(std::string const& text, std::size_t max)
{
    std::string out;
    for (char c : text)
    {
        auto const byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7f)
            out.push_back(c);
    }
    if (out.size() > max)
    {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xc0) == 0x80)
            --cut;
        out.resize(cut);
    }
    return out;
}

std::string sg_Lower(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string sg_Trim(std::string const& text)
{
    std::size_t const begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
        return std::string();
    std::size_t const end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// the letter a code point stands for (a-z, Latin-1 accents removed), 0 if none
char sg_Letter(std::uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z')
        return static_cast<char>(cp);
    if (cp >= 'A' && cp <= 'Z')
        return static_cast<char>(cp - 'A' + 'a');
    // 0xc0..0xdf and 0xe0..0xff; 0 for the multiplication and division signs
    static char const upper[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts";
    static char const lower[] = "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";
    if (cp >= 0xc0 && cp <= 0xdf)
        return upper[cp - 0xc0];
    if (cp >= 0xe0 && cp <= 0xff)
        return lower[cp - 0xe0];
    return 0;
}

// the letter a digit or sign may stand for, 0 if none
char sg_Leet(std::uint32_t cp)
{
    switch (cp)
    {
    case '0': return 'o';
    case '1': return 'i';
    case '3': return 'e';
    case '4': return 'a';
    case '5': return 's';
    case '7': return 't';
    case '8': return 'b';
    case '@': return 'a';
    case '$': return 's';
    case '!': return 'i';
    case '|': return 'i';
    case '+': return 't';
    default: return 0;
    }
}

bool sg_IsHex(std::uint32_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') || (cp >= 'A' && cp <= 'F');
}

// a word of the line, read two ways: core (signs at its ends left out) and full (all read as letters)
struct sgToken
{
    std::string core{};
    std::string full{};
    std::vector<std::size_t> coreCps{};
    std::vector<std::size_t> fullCps{};
};

std::vector<sgToken> sg_Tokenize(std::vector<sgCodePoint> const& cps)
{
    std::size_t const n = cps.size();

    // colour codes 0xRRGGBB and 0xRESETT are left out
    std::vector<bool> transparent(n, false);
    for (std::size_t i = 0; i + 7 < n; ++i)
    {
        if (cps[i].cp != '0' || cps[i + 1].cp != 'x')
            continue;
        bool hex = true;
        for (std::size_t k = 2; k < 8 && hex; ++k)
            hex = sg_IsHex(cps[i + k].cp);
        bool reset = true;
        static char const resett[] = "RESETT";
        for (std::size_t k = 0; k < 6 && reset; ++k)
            reset = cps[i + 2 + k].cp == static_cast<unsigned char>(resett[k]);
        if (hex || reset)
        {
            for (std::size_t k = 0; k < 8; ++k)
                transparent[i + k] = true;
            i += 7;
        }
    }

    std::vector<sgToken> tokens;
    std::vector<std::size_t> run; // indices of letters and signs
    auto flush = [&] {
        if (run.empty())
            return;
        std::size_t first = run.size(), last = 0;
        for (std::size_t k = 0; k < run.size(); ++k)
        {
            if (sg_Letter(cps[run[k]].cp))
            {
                if (first == run.size())
                    first = k;
                last = k;
            }
        }
        if (first < run.size())
        {
            sgToken token;
            for (std::size_t k = 0; k < run.size(); ++k)
            {
                std::uint32_t const cp = cps[run[k]].cp;
                char const letter = sg_Letter(cp) ? sg_Letter(cp) : sg_Leet(cp);
                token.full.push_back(letter);
                token.fullCps.push_back(run[k]);
                if (k >= first && k <= last)
                {
                    token.core.push_back(letter);
                    token.coreCps.push_back(run[k]);
                }
            }
            tokens.push_back(std::move(token));
        }
        run.clear();
    };

    for (std::size_t i = 0; i < n; ++i)
    {
        if (transparent[i])
            continue;
        std::uint32_t const cp = cps[i].cp;
        if (sg_Letter(cp) || sg_Leet(cp))
            run.push_back(i);
        else
            flush();
    }
    flush();

    // three or more one-letter words in a row are one word
    std::vector<sgToken> joined;
    for (std::size_t i = 0; i < tokens.size();)
    {
        std::size_t j = i;
        while (j < tokens.size() && tokens[j].core.size() == 1)
            ++j;
        if (j - i >= 3)
        {
            sgToken token;
            for (std::size_t k = i; k < j; ++k)
            {
                token.core += tokens[k].core;
                token.coreCps.insert(token.coreCps.end(), tokens[k].coreCps.begin(), tokens[k].coreCps.end());
            }
            token.full = token.core;
            token.fullCps = token.coreCps;
            joined.push_back(std::move(token));
            i = j;
        }
        else
        {
            joined.push_back(std::move(tokens[i]));
            ++i;
        }
    }
    return joined;
}

// runs of three or more equal letters cut to keep letters
std::string sg_Collapse(std::string const& text, std::size_t keep)
{
    std::string out;
    for (std::size_t i = 0; i < text.size();)
    {
        std::size_t j = i;
        while (j < text.size() && text[j] == text[i])
            ++j;
        out.append(j - i >= 3 ? keep : j - i, text[i]);
        i = j;
    }
    return out;
}
} // namespace

// *************************************************************
// gPlatformChatFilter
// *************************************************************

gPlatformChatFilter::Lists gPlatformChatFilter::BuiltIn()
{
    // a small fallback for a server that never reached its platform; the platform ships the real lists
    Lists lists;
    lists.version = "builtin-1";
    lists.mask = {"fuck*", "motherfuck*", "shit*", "bullshit*", "bitch*", "cunt*", "asshole*", "dickhead*",
                  "bastard*", "wank*", "twat*", "slut*", "whore*", "retard*", "stfu", "fck*", "fuk*"};
    lists.block = {"nigger*", "nigga*", "faggot*", "kys", "kill yourself", "kill urself", "go die"};
    lists.allow = {"kill", "kills", "killed", "frag", "frags", "core dump", "cut him off", "cut her off",
                   "cut them off", "die", "died", "dead"};
    return lists;
}

char const* const gPlatformChatFilter::DefaultNotice = "That message was not sent: it breaks the chat rules.";

gPlatformChatFilter::gPlatformChatFilter() : gPlatformChatFilter(BuiltIn()) {}

gPlatformChatFilter::gPlatformChatFilter(Lists const& lists)
    : version_(lists.version), notice_(lists.notice.empty() ? std::string(DefaultNotice) : lists.notice)
{
    Parse(lists.mask, terms_);
    blockFrom_ = terms_.size();
    Parse(lists.block, terms_);
    Parse(lists.allow, allow_);
}

void gPlatformChatFilter::Parse(std::vector<std::string> const& in, std::vector<Term>& out)
{
    for (auto const& entry : in)
    {
        Term term;
        std::string word;
        std::string const text = entry + " ";
        for (char c : text)
        {
            if (c != ' ' && c != '\t')
            {
                word.push_back(c);
                continue;
            }
            if (word.empty())
                continue;

            Word parsed;
            bool const leading = word.front() == '*';
            bool const trailing = word.size() > 1 && word.back() == '*';
            parsed.infix = leading;
            parsed.prefix = !leading && trailing;
            for (char w : word)
            {
                char const letter = sg_Letter(static_cast<unsigned char>(w));
                if (letter)
                    parsed.text.push_back(letter);
            }
            word.clear();
            if (!parsed.text.empty())
                term.push_back(parsed);
        }
        if (!term.empty())
            out.push_back(std::move(term));
    }
}

namespace
{
// which reading of a token a word of a term matched: 0 none, 1 core, 2 full
template <class Word>
int sg_MatchWord(Word const& word, sgToken const& token)
{
    auto matches = [&](std::string const& text) {
        for (std::string const& v : {text, sg_Collapse(text, 1), sg_Collapse(text, 2)})
        {
            if (word.infix ? v.find(word.text) != std::string::npos
                           : word.prefix ? v.compare(0, word.text.size(), word.text) == 0 && v.size() >= word.text.size()
                                         : v == word.text)
                return true;
        }
        return false;
    };
    if (matches(token.core))
        return 1;
    if (token.full != token.core && matches(token.full))
        return 2;
    return 0;
}

// the readings of tokens[at..] matched by term, empty if it does not match there
template <class Term>
std::vector<int> sg_MatchTerm(Term const& term, std::vector<sgToken> const& tokens, std::size_t at)
{
    std::vector<int> how;
    if (at + term.size() > tokens.size())
        return how;
    for (std::size_t k = 0; k < term.size(); ++k)
    {
        int const reading = sg_MatchWord(term[k], tokens[at + k]);
        if (!reading)
            return std::vector<int>();
        how.push_back(reading);
    }
    return how;
}
} // namespace

std::string gPlatformChatFilter::Apply(std::string const& text, bool* masked, bool* blocked) const
{
    if (masked)
        *masked = false;
    if (blocked)
        *blocked = false;

    std::vector<sgCodePoint> const cps = sg_Decode(text);
    std::vector<sgToken> const tokens = sg_Tokenize(cps);
    if (tokens.empty() || terms_.empty())
        return text;

    std::vector<bool> allowed(tokens.size(), false);
    for (auto const& term : allow_)
        for (std::size_t i = 0; i < tokens.size(); ++i)
            if (!sg_MatchTerm(term, tokens, i).empty())
                for (std::size_t k = 0; k < term.size(); ++k)
                    allowed[i + k] = true;

    std::vector<bool> hide(cps.size(), false);
    bool any = false;
    bool anyBlock = false;
    for (std::size_t t = 0; t < terms_.size(); ++t)
    {
        Term const& term = terms_[t];
        for (std::size_t i = 0; i < tokens.size(); ++i)
        {
            std::vector<int> const how = sg_MatchTerm(term, tokens, i);
            if (how.empty())
                continue;
            bool covered = true;
            for (std::size_t k = 0; k < term.size(); ++k)
                covered = covered && allowed[i + k];
            if (covered)
                continue;
            any = true;
            anyBlock = anyBlock || t >= blockFrom_;
            for (std::size_t k = 0; k < term.size(); ++k)
            {
                sgToken const& token = tokens[i + k];
                for (std::size_t cp : how[k] == 1 ? token.coreCps : token.fullCps)
                    hide[cp] = true;
            }
        }
    }
    if (!any)
        return text;

    if (masked)
        *masked = true;
    if (blocked)
        *blocked = anyBlock;
    std::string out;
    for (std::size_t i = 0; i < cps.size(); ++i)
    {
        if (hide[i])
            out.push_back('*');
        else
            out.append(text, cps[i].begin, cps[i].end - cps[i].begin);
    }
    return out;
}

bool gPlatformChatFilter::NameAllowed(std::string const& name) const
{
    bool masked = false;
    Apply(name, &masked);
    if (masked)
        return false;

    // the letters alone, as one word, in both readings
    std::vector<sgToken> const tokens = sg_Tokenize(sg_Decode(name));
    if (tokens.size() < 2)
        return true;
    std::vector<sgToken> squashed(1);
    for (auto const& token : tokens)
    {
        squashed[0].core += token.core;
        squashed[0].full += token.full;
    }
    for (auto const& term : terms_)
        if (!sg_MatchTerm(term, squashed, 0).empty())
            return false;
    return true;
}

// *************************************************************
// JSON
// *************************************************************

namespace
{
bool sg_ParseSanction(nlohmann::json const& json, gPlatformSanction& out, std::string& error)
{
    out = gPlatformSanction();
    if (!json.is_object())
        return error = "not an object", false;

    auto text = [&](nlohmann::json const& object, char const* member, std::size_t max) -> std::string {
        auto const value = object.find(member);
        if (value == object.end() || !value->is_string())
            return std::string();
        return sg_Clean(value->get<std::string>(), max);
    };

    out.id = text(json, "id", 100);
    out.kind = text(json, "kind", 16);
    out.scope = text(json, "scope", 16);
    out.serverId = text(json, "serverId", 200);
    out.reason = text(json, "reason", 200);
    if (out.id.empty())
        return error = "no id", false;
    if (out.kind != "mute" && out.kind != "kick" && out.kind != "ban")
        return error = "unknown kind", false;
    if (out.scope.empty())
        out.scope = "all";
    if (out.scope != "all" && out.scope != "room")
        return error = "unknown scope", false;

    auto const until = json.find("until");
    if (until == json.end() || until->is_null())
    {
        out.permanent = true;
        if (out.kind == "mute")
            return error = "a mute needs an end", false;
    }
    else if (until->is_number_integer())
        out.until = until->get<std::int64_t>();
    else
        return error = "until is not an integer", false;

    auto const target = json.find("target");
    if (target == json.end() || !target->is_object())
        return error = "no target", false;
    out.targetType = text(*target, "type", 16);
    if (out.targetType == "account")
    {
        out.userId = text(*target, "userId", 100);
        auto const gids = target->find("gids");
        if (gids != target->end() && gids->is_array())
            for (auto const& gid : *gids)
                if (gid.is_string() && !gid.get<std::string>().empty())
                    out.gids.push_back(sg_Lower(sg_Clean(gid.get<std::string>(), 200)));
        if (out.userId.empty() && out.gids.empty())
            return error = "an account target names nobody", false;
    }
    else if (out.targetType == "connection")
    {
        out.bootId = text(*target, "bootId", 100);
        if (out.serverId.empty())
            out.serverId = text(*target, "serverId", 200);
        else if (!text(*target, "serverId", 200).empty() && text(*target, "serverId", 200) != out.serverId)
            return error = "two server ids", false;
        out.name = text(*target, "name", 100);
        auto const conn = target->find("conn");
        if (conn != target->end() && conn->is_number_integer())
            out.conn = static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(conn->get<std::int64_t>(), 1 << 20)));
        if (out.bootId.empty() || out.conn <= 0)
            return error = "a connection target needs bootId and conn", false;
        if (out.kind == "ban")
            return error = "a connection cannot be banned", false;
        if (out.serverId.empty())
            out.scope = "all"; // the bootId names the server run anyway
        else
            out.scope = "room";
    }
    else if (out.targetType == "address")
    {
        out.ip = text(*target, "ip", 64);
        if (out.ip.empty())
            return error = "an address target needs ip", false;
        if (out.kind != "ban")
            return error = "an address can only be banned", false;
        if (out.permanent)
            return error = "an address ban needs an end", false;
    }
    else
        return error = "unknown target type", false;

    if (out.scope == "room" && out.serverId.empty())
        return error = "scope room needs serverId", false;
    return true;
}

nlohmann::ordered_json sg_NullOr(std::string const& text)
{
    return text.empty() ? nlohmann::ordered_json(nullptr) : nlohmann::ordered_json(gPlatformHttp::ToUtf8(text));
}

nlohmann::ordered_json sg_PlayerJson(gPlatformChatPlayer const& player)
{
    nlohmann::ordered_json json;
    json["name"] = gPlatformHttp::ToUtf8(player.name);
    json["gid"] = sg_NullOr(player.gid);
    json["userId"] = sg_NullOr(player.userId);
    json["conn"] = player.conn;
    if (!player.LoggedIn() && !player.ip.empty())
        json["ip"] = player.ip;
    return json;
}

std::string sg_Dump(nlohmann::ordered_json const& json)
{
    return json.dump(-1, ' ', false, nlohmann::ordered_json::error_handler_t::replace);
}

char const* sg_OutcomeName(gPlatformChatOutcome outcome)
{
    switch (outcome)
    {
    case gPlatformChatOutcome::AsWritten:
        return "as_written";
    case gPlatformChatOutcome::Masked:
        return "masked";
    case gPlatformChatOutcome::Dropped:
        return "dropped";
    case gPlatformChatOutcome::Muted:
        return "muted";
    }
    return "as_written";
}

std::vector<std::string> sg_StringList(nlohmann::json const& object, char const* member)
{
    std::vector<std::string> out;
    auto const list = object.find(member);
    if (list == object.end() || !list->is_array())
        return out;
    for (auto const& item : *list)
    {
        if (out.size() >= 20000)
            break;
        if (item.is_string() && item.get_ref<std::string const&>().size() <= 200)
            out.push_back(item.get<std::string>());
    }
    return out;
}
} // namespace

bool gPlatformSanction::TryParse(std::string const& json, gPlatformSanction& out, std::string& error)
{
    nlohmann::json const parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded())
        return error = "not JSON", false;
    return sg_ParseSanction(parsed, out, error);
}

std::string gPlatformModeration::ChatRequestJson(std::string const& bootId, std::vector<gPlatformOutboundLine> const& lines)
{
    nlohmann::ordered_json list = nlohmann::ordered_json::array();
    for (auto const& line : lines)
    {
        nlohmann::ordered_json json;
        json["id"] = line.id;
        json["time"] = line.time;
        json["channel"] = line.channel;
        json["text"] = gPlatformHttp::ToUtf8(line.text);
        if (!line.to.empty())
            json["to"] = gPlatformHttp::ToUtf8(line.to);
        json["player"] = sg_PlayerJson(line.player);
        list.push_back(std::move(json));
    }
    nlohmann::ordered_json body;
    body["bootId"] = bootId;
    body["lines"] = std::move(list);
    return sg_Dump(body);
}

bool gPlatformModeration::TryParseVerdicts(std::string const& body, std::vector<gPlatformChatVerdict>& verdicts, std::vector<gPlatformSanction>& sanctions, std::string& error)
{
    verdicts.clear();
    sanctions.clear();
    nlohmann::json const json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return error = "not a JSON object", false;
    auto const list = json.find("verdicts");
    if (list == json.end() || !list->is_array())
        return error = "no verdicts list", false;

    for (auto const& item : *list)
    {
        if (!item.is_object())
            continue;
        auto const id = item.find("id");
        auto const action = item.find("action");
        if (id == item.end() || !id->is_number_integer() || id->get<std::int64_t>() <= 0 || action == item.end() || !action->is_string())
            continue;
        gPlatformChatVerdict verdict;
        verdict.id = static_cast<std::uint64_t>(id->get<std::int64_t>());
        verdict.action = action->get<std::string>();
        auto const text = item.find("text");
        if (text != item.end() && text->is_string())
            verdict.text = text->get<std::string>();
        auto const notice = item.find("notice");
        if (notice != item.end() && notice->is_string())
            verdict.notice = notice->get<std::string>();
        verdicts.push_back(std::move(verdict));
    }

    auto const more = json.find("sanctions");
    if (more != json.end() && more->is_array())
    {
        for (auto const& item : *more)
        {
            gPlatformSanction sanction;
            std::string ignored;
            if (sg_ParseSanction(item, sanction, ignored))
                sanctions.push_back(std::move(sanction));
        }
    }
    return true;
}

bool gPlatformModeration::TryParseState(std::string const& body, bool& hasFilter, gPlatformChatFilter::Lists& filter, std::vector<gPlatformSanction>& sanctions, std::int64_t& commandId, std::string& error)
{
    hasFilter = false;
    sanctions.clear();
    commandId = 0;
    nlohmann::json const json = nlohmann::json::parse(body, nullptr, false);
    if (json.is_discarded() || !json.is_object())
        return error = "not a JSON object", false;

    auto const id = json.find("commandId");
    if (id != json.end() && id->is_number_integer())
        commandId = std::max<std::int64_t>(0, id->get<std::int64_t>());

    auto const chat = json.find("chatFilter");
    if (chat != json.end() && chat->is_object())
    {
        hasFilter = true;
        auto const version = chat->find("version");
        filter.version = version != chat->end() && version->is_string() ? sg_Clean(version->get<std::string>(), 64) : std::string();
        filter.mask = sg_StringList(*chat, "mask");
        filter.block = sg_StringList(*chat, "block");
        filter.allow = sg_StringList(*chat, "allow");
        auto const notice = chat->find("notice");
        filter.notice = notice != chat->end() && notice->is_string() ? sg_Clean(notice->get<std::string>(), 200) : std::string();
    }

    auto const list = json.find("sanctions");
    if (list != json.end() && list->is_array())
    {
        for (auto const& item : *list)
        {
            gPlatformSanction sanction;
            std::string ignored;
            if (sg_ParseSanction(item, sanction, ignored))
                sanctions.push_back(std::move(sanction));
        }
    }
    return true;
}

// *************************************************************
// gPlatformModeration: lines
// *************************************************************

std::size_t const gPlatformModeration::MaxWindow;
std::int64_t const gPlatformModeration::WindowSeconds;
std::size_t const gPlatformModeration::MaxOutbound;
int const gPlatformModeration::ReportsPerMinute;
std::size_t const gPlatformModeration::MaxReplayLog;

gPlatformModeration::gPlatformModeration(Config config, gPlatformModerationEffects effects)
    : config_(std::move(config)), effects_(std::move(effects))
{
    if (!effects_.players)
        effects_.players = [] { return std::vector<gPlatformChatPlayer>(); };
    if (!effects_.tell)
        effects_.tell = [](int, std::string const&) {};
    if (!effects_.kick)
        effects_.kick = [](int, std::string const&) {};
    if (!effects_.rename)
        effects_.rename = [](std::string const&, std::string const&) { return false; };
    if (!effects_.find)
        effects_.find = [](std::string const&, int, gPlatformChatPlayer&) { return false; };
    if (!effects_.report)
        effects_.report = [](std::string const&) {};
    if (!effects_.log)
        effects_.log = [](std::string const&) {};
    if (!effects_.clock)
        effects_.clock = [] { return static_cast<std::int64_t>(std::time(nullptr)); };
    config_.timeout = std::max(std::chrono::milliseconds(50), std::min(config_.timeout, std::chrono::milliseconds(2000)));
}

std::int64_t gPlatformModeration::Now() const
{
    return effects_.clock();
}

void gPlatformModeration::Log(std::string const& text) const
{
    effects_.log(text);
}

bool gPlatformModeration::ParseLine(std::string const& line, ParsedLine& parsed)
{
    parsed = ParsedLine();
    if (line.empty())
        return false;

    if (line[0] != '/')
    {
        parsed.channel = "public";
        parsed.text = line;
        return !sg_Trim(line).empty();
    }

    std::size_t const commandEnd = std::min(line.find_first_of(" \t"), line.size());
    parsed.command = sg_Lower(line.substr(0, commandEnd));
    std::size_t restStart = line.find_first_not_of(" \t", commandEnd);
    if (restStart == std::string::npos)
        restStart = line.size();
    parsed.text = line.substr(restStart);
    parsed.prefix = line.substr(0, restStart);

    std::string const& command = parsed.command;
    if (command == "/me")
        parsed.channel = "me";
    else if (command == "/team")
        parsed.channel = "team";
    else if (command == "/shout")
        parsed.channel = "public";
    else if (command == "/enemy" || command == "/enemies")
        parsed.channel = "enemy";
    else if (command == "/msg" || command == "/chat")
    {
        // the recipient is the next word, as the server reads it
        std::size_t const nameEnd = std::min(line.find_first_of(" \t", restStart), line.size());
        parsed.to = line.substr(restStart, nameEnd - restStart);
        std::size_t textStart = line.find_first_not_of(" \t", nameEnd);
        if (textStart == std::string::npos)
            textStart = line.size();
        parsed.prefix = line.substr(0, textStart);
        parsed.text = line.substr(textStart);
        parsed.channel = "private";
        if (parsed.to.empty())
            return false;
    }
    else
        return false;

    return !sg_Trim(parsed.text).empty();
}

bool gPlatformModeration::Submit(gPlatformChatPlayer const& player, std::string const& line, Deliver deliver, Clock::time_point now)
{
    ParsedLine parsed;
    bool const talking = ParseLine(line, parsed);
    int const key = player.playerId;
    auto const queue = queues_.find(key);
    bool const waiting = queue != queues_.end() && !queue->second.empty();

    if (parsed.command == "/report" && config_.reports)
        return HandleReport(player, parsed.text, now);

    if (!talking)
    {
        if (!waiting)
            return false;

        // behind a held line of the same player: keep the order
        Line next;
        next.id = nextId_++;
        next.playerId = key;
        next.conn = player.conn;
        next.deliver = line;
        next.deliverFn = std::move(deliver);
        next.player = player;
        queues_[key].push_back(std::move(next));
        Release(key, now);
        return true;
    }

    Line next;
    next.id = nextId_++;
    next.playerId = key;
    next.conn = player.conn;
    next.checked = true;
    next.prefix = parsed.prefix;
    next.text = parsed.text;
    next.deliver = parsed.text;
    next.channel = parsed.channel;
    next.to = parsed.to;
    next.time = Now();
    next.submitted = now;
    next.player = player;
    next.deliverFn = std::move(deliver);

    if (config_.sanctions)
    {
        gPlatformSanction const* mute = MuteFor(player);
        if (mute)
        {
            effects_.tell(player.conn, MuteText(*mute));
            next.outcome = gPlatformChatOutcome::Muted;
            Record(next);
            return true;
        }
    }

    if (!config_.chatFilter)
    {
        if (!waiting)
        {
            Record(next);
            return false;
        }
        queues_[key].push_back(std::move(next));
        Release(key, now);
        return true;
    }

    if (mode_ == Mode::Hold)
    {
        next.pending = true;
        next.wasHeld = true;
        next.deadline = now + config_.timeout;
        pending_[next.id] = key;
    }
    else
    {
        DecideLocally(next);
    }

    if (mode_ != Mode::Off)
    {
        gPlatformOutboundLine out;
        out.id = next.id;
        out.time = next.time;
        out.channel = next.channel;
        out.text = next.text;
        out.to = next.to;
        out.player = player;
        outbound_.push_back(std::move(out));
        if (outbound_.size() > MaxOutbound)
            outbound_.erase(outbound_.begin(), outbound_.begin() + (outbound_.size() - MaxOutbound));
    }

    queues_[key].push_back(std::move(next));
    Release(key, now);
    return true;
}

std::vector<gPlatformOutboundLine> gPlatformModeration::TakeOutbound()
{
    std::vector<gPlatformOutboundLine> out;
    out.swap(outbound_);
    return out;
}

void gPlatformModeration::Decide(Line& line, std::string const& deliverText, gPlatformChatOutcome outcome)
{
    line.pending = false;
    line.deliver = deliverText;
    line.outcome = outcome;
    pending_.erase(line.id);
}

void gPlatformModeration::DecideLocally(Line& line)
{
    bool masked = false;
    bool blocked = false;
    std::string const text = filter_.Apply(line.text, &masked, &blocked);
    if (blocked)
    {
        // a block term is never delivered, not even masked: dropped as the platform would
        Decide(line, std::string(), gPlatformChatOutcome::Dropped);
        line.dropped = true;
        line.notice = filter_.Notice();
        ++stats_.localDrops;
        return;
    }
    Decide(line, text, masked ? gPlatformChatOutcome::Masked : gPlatformChatOutcome::AsWritten);
}

void gPlatformModeration::ApplyVerdicts(std::vector<gPlatformChatVerdict> const& verdicts, Clock::time_point now)
{
    std::set<int> touched;
    for (auto const& verdict : verdicts)
    {
        auto const pending = pending_.find(verdict.id);
        if (pending == pending_.end())
        {
            if (verdict.id < nextId_)
            {
                ++stats_.late;
                LateVerdict(verdict);
            }
            continue;
        }
        int const key = pending->second;
        auto& queue = queues_[key];
        auto line = std::find_if(queue.begin(), queue.end(), [&](Line const& l) { return l.id == verdict.id; });
        if (line == queue.end())
        {
            pending_.erase(pending);
            continue;
        }

        if (verdict.action == "allow")
        {
            Decide(*line, line->text, gPlatformChatOutcome::AsWritten);
        }
        else if (verdict.action == "replace")
        {
            std::string text = sg_Clean(verdict.text, 1000);
            if (!sg_IsUtf8(line->text))
                text = sg_ToLatin1(text);
            Decide(*line, text, text == line->text ? gPlatformChatOutcome::AsWritten : gPlatformChatOutcome::Masked);
        }
        else if (verdict.action == "drop")
        {
            Decide(*line, std::string(), gPlatformChatOutcome::Dropped);
            line->dropped = true;
            line->notice = sg_Clean(verdict.notice, 200);
        }
        else
        {
            DecideLocally(*line);
        }
        line->byVerdict = true;
        touched.insert(key);
    }
    for (int key : touched)
        Release(key, now);
}

void gPlatformModeration::LateVerdict(gPlatformChatVerdict const& verdict)
{
    // the line went out as the local filter decided; what players saw stays as it was, and the
    // sanctions that came with the verdict (a strike's mute) are applied by the caller as usual
    if (verdict.action != "drop")
        return;
    auto const line = std::find_if(window_.rbegin(), window_.rend(), [&](WindowLine const& l) { return l.id == verdict.id; });
    if (line == window_.rend() || line->outcome == gPlatformChatOutcome::Dropped || line->outcome == gPlatformChatOutcome::Muted)
        return;
    line->lateVerdict = verdict.action;
    ++stats_.lateDrops;
    Log("Platform: the platform's verdict on chat line " + std::to_string(verdict.id) + " of " + line->player.name + " (drop) came after the timeout; the local filter had delivered it " + (line->outcome == gPlatformChatOutcome::Masked ? "masked" : "as written") + ". The platform counts it as a violation all the same.");
}

void gPlatformModeration::Release(int playerId, Clock::time_point now)
{
    for (;;)
    {
        auto queue = queues_.find(playerId);
        if (queue == queues_.end())
            return;
        if (queue->second.empty())
        {
            queues_.erase(queue);
            return;
        }
        if (queue->second.front().pending)
            return;

        Line line = std::move(queue->second.front());
        queue->second.pop_front();
        if (queue->second.empty())
            queues_.erase(queue);

        if (line.checked)
        {
            Record(line);
            if (line.wasHeld)
            {
                double const ms = std::chrono::duration<double, std::milli>(now - line.submitted).count();
                ++stats_.checked;
                if (stats_.holdMs.size() < 100000)
                    stats_.holdMs.push_back(ms);
                if (line.byVerdict)
                    recentHoldMs_.push_back(ms);
            }
        }

        // delivering runs the server's chat code, which may end connections: nothing held over
        if (line.dropped)
        {
            if (!line.notice.empty())
                effects_.tell(line.conn, line.notice);
        }
        else if (line.deliverFn)
        {
            line.deliverFn(line.prefix + line.deliver);
        }
    }
}

void gPlatformModeration::Record(Line const& line)
{
    WindowLine entry;
    entry.id = line.id;
    entry.time = line.time;
    entry.channel = line.channel;
    entry.text = line.text;
    entry.to = line.to;
    entry.outcome = line.outcome;
    entry.player = line.player;
    window_.push_back(std::move(entry));
    while (window_.size() > MaxWindow)
        window_.pop_front();
}

void gPlatformModeration::SetMode(Mode mode, Clock::time_point now, std::chrono::seconds offFor)
{
    if (mode == Mode::Off)
        offUntil_ = now + offFor;
    if (mode == mode_)
        return;

    static char const* const names[] = {"held for the platform's verdict", "filtered locally while the platform is failing",
                                        "filtered locally (the platform does no moderation)"};
    if (config_.chatFilter)
        Log(std::string("Platform: chat lines are now ") + names[static_cast<int>(mode)] + ".");
    mode_ = mode;

    if (mode_ == Mode::Hold)
        return;

    // nobody keeps waiting for a verdict that will not come
    std::set<int> touched;
    for (auto& queue : queues_)
        for (auto& line : queue.second)
            if (line.pending)
            {
                DecideLocally(line);
                touched.insert(queue.first);
            }
    pending_.clear();
    for (int key : touched)
        Release(key, now);
}

void gPlatformModeration::SetChatFilter(bool on, Clock::time_point now)
{
    if (on == config_.chatFilter)
        return;
    config_.chatFilter = on;
    Log(on ? "Platform: chat lines are checked again." : "Platform: chat lines are not checked any more (PLATFORM_CHAT_FILTER 0).");
    if (on)
        return;

    // what is held goes out as written, as stock chat would have it
    std::set<int> touched;
    for (auto& queue : queues_)
        for (auto& line : queue.second)
            if (line.pending)
            {
                Decide(line, line.text, gPlatformChatOutcome::AsWritten);
                touched.insert(queue.first);
            }
    pending_.clear();
    outbound_.clear();
    for (int key : touched)
        Release(key, now);
}

void gPlatformModeration::SetTimeout(std::chrono::milliseconds timeout)
{
    config_.timeout = std::max(std::chrono::milliseconds(50), std::min(timeout, std::chrono::milliseconds(2000)));
}

void gPlatformModeration::Tick(Clock::time_point now)
{
    if (mode_ == Mode::Off && now >= offUntil_)
        SetMode(Mode::Hold, now);

    // timeouts: the local filter decides
    if (!pending_.empty())
    {
        std::set<int> touched;
        for (auto& queue : queues_)
            for (auto& line : queue.second)
                if (line.pending && now >= line.deadline)
                {
                    DecideLocally(line);
                    ++stats_.timeouts;
                    ++recentTimeouts_;
                    touched.insert(queue.first);
                }
        for (int key : touched)
            Release(key, now);
    }

    // address bans: connections that did not log in in time
    if (!graces_.empty())
    {
        std::vector<gPlatformChatPlayer> const players = effects_.players();
        for (auto i = graces_.begin(); i != graces_.end();)
        {
            int const conn = i->first;
            bool loggedIn = false;
            for (auto const& player : players)
                loggedIn = loggedIn || (player.conn == conn && player.LoggedIn());
            if (loggedIn)
            {
                i = graces_.erase(i);
                continue;
            }
            if (now < i->second.first)
            {
                ++i;
                continue;
            }
            gPlatformChatPlayer guest;
            guest.conn = conn;
            guest.ip = i->second.second;
            gPlatformSanction const* ban = BanFor(guest);
            i = graces_.erase(i);
            if (ban)
            {
                Log("Platform: connection " + std::to_string(conn) + " from a banned address did not log in; ending it (sanction " + ban->id + ").");
                effects_.kick(conn, BanText(*ban));
            }
        }
    }

    // sanctions that ran out
    if (now >= nextExpiry_)
    {
        nextExpiry_ = now + std::chrono::seconds(1);
        for (auto i = sanctions_.begin(); i != sanctions_.end();)
        {
            if (InForce(i->second))
            {
                ++i;
                continue;
            }
            Log("Platform: sanction " + i->first + " (" + i->second.kind + ") ended.");
            i = sanctions_.erase(i);
        }
    }

    LogStats(now, false);
}

void gPlatformModeration::LogStats(Clock::time_point now, bool force)
{
    if (!force && now < nextStats_)
        return;
    nextStats_ = now + config_.statsInterval;
    if (recentHoldMs_.empty() && recentTimeouts_ == 0)
        return;

    std::vector<double> times = recentHoldMs_;
    std::sort(times.begin(), times.end());
    auto at = [&](double q) { return times.empty() ? 0.0 : times[std::min(times.size() - 1, static_cast<std::size_t>(q * (times.size() - 1) + 0.5))]; };
    char buffer[200];
    std::snprintf(buffer, sizeof(buffer), "Platform: chat check: %u line(s) released by the platform's verdict after median %.0f ms, 95th percentile %.0f ms, max %.0f ms; %u decided locally after the timeout.",
                  static_cast<unsigned>(times.size()), at(0.5), at(0.95), times.empty() ? 0.0 : times.back(), static_cast<unsigned>(recentTimeouts_));
    Log(buffer);
    recentHoldMs_.clear();
    recentTimeouts_ = 0;
}

// *************************************************************
// gPlatformModeration: sanctions
// *************************************************************

bool gPlatformModeration::InForce(gPlatformSanction const& sanction) const
{
    return sanction.permanent || sanction.until > Now();
}

bool gPlatformModeration::Applies(gPlatformSanction const& sanction, gPlatformChatPlayer const& player) const
{
    if (sanction.scope == "room" && sanction.serverId != config_.serverId)
        return false;

    if (sanction.targetType == "account")
    {
        if (!sanction.userId.empty() && sanction.userId == player.userId)
            return true;
        if (!player.LoggedIn())
            return false;
        std::string const gid = sg_Lower(player.gid);
        return std::find(sanction.gids.begin(), sanction.gids.end(), gid) != sanction.gids.end();
    }
    if (sanction.targetType == "connection")
        return sanction.bootId == config_.bootId && sanction.conn == player.conn;
    if (sanction.targetType == "address")
        return !player.LoggedIn() && !sanction.ip.empty() && sanction.ip == player.ip;
    return false;
}

gPlatformSanction const* gPlatformModeration::Find(char const* kind, gPlatformChatPlayer const& player) const
{
    gPlatformSanction const* best = nullptr;
    for (auto const& entry : sanctions_)
    {
        gPlatformSanction const& sanction = entry.second;
        if (sanction.kind != kind || !InForce(sanction) || !Applies(sanction, player))
            continue;
        if (!best || (sanction.permanent && !best->permanent) || (!best->permanent && sanction.until > best->until))
            best = &sanction;
    }
    return best;
}

gPlatformSanction const* gPlatformModeration::BanFor(gPlatformChatPlayer const& player) const
{
    return Find("ban", player);
}

gPlatformSanction const* gPlatformModeration::MuteFor(gPlatformChatPlayer const& player) const
{
    return Find("mute", player);
}

std::string gPlatformModeration::FormatTime(std::int64_t unixSeconds)
{
    std::time_t const t = static_cast<std::time_t>(unixSeconds);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buffer[40];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M UTC", &utc);
    return buffer;
}

namespace
{
std::string sg_Because(gPlatformSanction const& sanction)
{
    return sanction.reason.empty() ? std::string(".") : ": " + sanction.reason;
}
} // namespace

std::string gPlatformModeration::BanText(gPlatformSanction const& sanction)
{
    if (sanction.kind == "kick")
        return "You were removed from this server" + sg_Because(sanction);
    if (sanction.permanent)
        return "You are banned from this server permanently" + sg_Because(sanction);
    return "You are banned from this server until " + FormatTime(sanction.until) + sg_Because(sanction);
}

std::string gPlatformModeration::MuteText(gPlatformSanction const& sanction)
{
    return "You are muted until " + FormatTime(sanction.until) + (sanction.reason.empty() ? std::string() : ": " + sanction.reason) +
           ". Other players do not see your chat until then.";
}

void gPlatformModeration::Store(gPlatformSanction const& sanction)
{
    // a connection sanction ended with its connection; the platform may still list it
    if (sanction.targetType == "connection" && endedConnectionSanctions_.count(sanction.id))
        return;
    sanctions_[sanction.id] = sanction;
}

void gPlatformModeration::Remember(ReplayEntry entry)
{
    if (entry.commandId <= 0)
        return;
    replay_.push_back(std::move(entry));
    while (replay_.size() > MaxReplayLog)
        replay_.pop_front();
}

void gPlatformModeration::Enforce(gPlatformSanction const& sanction, bool tellMuted)
{
    std::set<int> done;
    for (auto const& player : effects_.players())
    {
        if (!Applies(sanction, player) || done.count(player.conn))
            continue;
        // a connection still within its address ban grace may log in until it ends (Tick)
        if (sanction.targetType == "address" && graces_.count(player.conn))
            continue;
        if (sanction.kind == "mute")
        {
            if (tellMuted)
                effects_.tell(player.conn, MuteText(sanction));
            continue;
        }
        done.insert(player.conn);
        Log("Platform: sanction " + sanction.id + " (" + sanction.kind + ") ends the connection of " + player.name + " (" + std::to_string(player.conn) + ").");
        effects_.kick(player.conn, BanText(sanction));
    }
}

void gPlatformModeration::ApplySanction(gPlatformSanction const& sanction, std::int64_t commandId)
{
    if (!config_.sanctions)
        return;

    ReplayEntry entry;
    entry.commandId = commandId;
    entry.sanction = sanction;
    Remember(std::move(entry));

    if (sanction.kind == "kick")
    {
        Enforce(sanction, false);
        return;
    }
    if (!InForce(sanction))
        return;
    if (sanction.targetType == "connection" && endedConnectionSanctions_.count(sanction.id))
        return;

    bool const known = sanctions_.count(sanction.id) > 0;
    Store(sanction);
    if (!known)
        Log("Platform: sanction " + sanction.id + ": " + sanction.kind + " of " + sanction.targetType + " " +
            (sanction.permanent ? std::string("permanently") : "until " + FormatTime(sanction.until)) + sg_Because(sanction));
    Enforce(sanction, !known);
}

void gPlatformModeration::RemoveSanction(std::string const& id, std::int64_t commandId)
{
    if (!config_.sanctions)
        return;

    ReplayEntry entry;
    entry.commandId = commandId;
    entry.remove = true;
    entry.id = id;
    Remember(std::move(entry));

    auto const found = sanctions_.find(id);
    if (found == sanctions_.end())
        return;
    gPlatformSanction const lifted = found->second;
    sanctions_.erase(found);
    Log("Platform: sanction " + id + " (" + lifted.kind + ") lifted.");

    if (lifted.kind != "mute")
        return;
    std::set<int> told;
    for (auto const& player : effects_.players())
        if (Applies(lifted, player) && !MuteFor(player) && told.insert(player.conn).second)
            effects_.tell(player.conn, "You can chat again.");
}

void gPlatformModeration::ApplyState(gPlatformChatFilter::Lists const* filter, std::vector<gPlatformSanction> const& sanctions, std::int64_t commandId)
{
    if (filter && config_.chatFilter)
    {
        gPlatformChatFilter loaded(*filter);
        if (loaded.Version() != filter_.Version())
            Log("Platform: chat word lists " + loaded.Version() + " loaded (" + std::to_string(loaded.TermCount()) + " terms).");
        filter_ = std::move(loaded);
    }
    if (!config_.sanctions)
        return;

    sanctions_.clear();
    for (auto const& sanction : sanctions)
        if (sanction.kind != "kick" && InForce(sanction))
            Store(sanction);

    // commands the list may have crossed on its way
    for (auto const& entry : replay_)
    {
        if (entry.commandId <= commandId)
            continue;
        if (entry.remove)
            sanctions_.erase(entry.id);
        else if (entry.sanction.kind != "kick" && InForce(entry.sanction))
            Store(entry.sanction);
    }
    while (!replay_.empty() && replay_.front().commandId <= commandId)
        replay_.pop_front();

    std::vector<gPlatformSanction> bans;
    for (auto const& entry : sanctions_)
        if (entry.second.kind == "ban")
            bans.push_back(entry.second);
    for (auto const& ban : bans)
        Enforce(ban, false);
}

bool gPlatformModeration::AdmitLogin(int conn, std::string const& gid, std::string const& userId, std::string& reason) const
{
    if (!config_.sanctions)
        return true;
    gPlatformChatPlayer player;
    player.conn = conn;
    player.gid = gid;
    player.userId = userId;
    gPlatformSanction const* ban = BanFor(player);
    if (!ban)
        return true;
    reason = BanText(*ban);
    return false;
}

void gPlatformModeration::SetUserId(int conn, std::string const& userId)
{
    if (userId.empty())
        userIds_.erase(conn);
    else
        userIds_[conn] = userId;
}

std::string gPlatformModeration::UserIdOf(int conn) const
{
    auto const found = userIds_.find(conn);
    return found == userIds_.end() ? std::string() : found->second;
}

void gPlatformModeration::PlayerAuthenticated(gPlatformChatPlayer const& player)
{
    if (!config_.sanctions)
        return;
    graces_.erase(player.conn);
    gPlatformSanction const* ban = BanFor(player);
    if (!ban)
        return;
    Log("Platform: " + player.name + " logged in as " + player.gid + ", banned (sanction " + ban->id + "); ending the connection.");
    effects_.kick(player.conn, BanText(*ban));
}

void gPlatformModeration::ConnectionOpened(int conn, std::string const& ip, Clock::time_point now)
{
    if (!config_.sanctions || conn <= 0)
        return;
    gPlatformChatPlayer guest;
    guest.conn = conn;
    guest.ip = ip;
    gPlatformSanction const* ban = BanFor(guest);
    if (!ban)
        return;
    graces_[conn] = std::make_pair(now + config_.ipBanGrace, ip);
    Log("Platform: connection " + std::to_string(conn) + " comes from an address under a ban (sanction " + ban->id + "); it must log in within " +
        std::to_string(config_.ipBanGrace.count()) + " s.");
}

void gPlatformModeration::ConnectionClosed(int conn)
{
    graces_.erase(conn);
    userIds_.erase(conn);

    // sanctions on this connection end with it: the next player on the same connection
    // number is someone else
    for (auto i = sanctions_.begin(); i != sanctions_.end();)
    {
        gPlatformSanction const& sanction = i->second;
        if (sanction.targetType != "connection" || sanction.bootId != config_.bootId || sanction.conn != conn)
        {
            ++i;
            continue;
        }
        Log("Platform: sanction " + i->first + " (" + sanction.kind + ") ended with connection " + std::to_string(conn) + ".");
        if (endedConnectionSanctions_.insert(i->first).second)
            endedConnectionOrder_.push_back(i->first);
        i = sanctions_.erase(i);
    }
    while (endedConnectionOrder_.size() > MaxReplayLog)
    {
        endedConnectionSanctions_.erase(endedConnectionOrder_.front());
        endedConnectionOrder_.pop_front();
    }

    for (auto i = queues_.begin(); i != queues_.end();)
    {
        auto& queue = i->second;
        for (auto line = queue.begin(); line != queue.end();)
        {
            if (line->conn == conn)
            {
                pending_.erase(line->id);
                line = queue.erase(line);
            }
            else
                ++line;
        }
        if (queue.empty())
            i = queues_.erase(i);
        else
            ++i;
    }
}

bool gPlatformModeration::Rename(std::string const& gid, std::string const& name, std::string const& reason)
{
    if (!config_.sanctions)
        return false;
    std::string const clean = sg_Clean(name, 30);
    if (gid.empty() || sg_Trim(clean).empty())
        return false;

    int conn = 0;
    for (auto const& player : effects_.players())
        if (player.LoggedIn() && sg_Lower(player.gid) == sg_Lower(gid))
            conn = player.conn;
    if (!conn || !effects_.rename(gid, clean))
        return false;
    Log("Platform: renamed " + gid + " to " + clean + (reason.empty() ? std::string(".") : ": " + sg_Clean(reason, 200)));
    effects_.tell(conn, "Your name was changed to " + clean + (reason.empty() ? std::string(".") : ": " + sg_Clean(reason, 200)));
    return true;
}

// *************************************************************
// gPlatformModeration: reports
// *************************************************************

bool gPlatformModeration::HandleReport(gPlatformChatPlayer const& reporter, std::string const& args, Clock::time_point now)
{
    auto& times = reportsBy_[reporter.playerId];
    while (!times.empty() && now - times.front() > std::chrono::seconds(60))
        times.pop_front();
    if (static_cast<int>(times.size()) >= ReportsPerMinute)
    {
        effects_.tell(reporter.conn, "Please wait a minute before sending another report.");
        return true;
    }

    std::string const rest = sg_Trim(args);
    std::size_t const nameEnd = std::min(rest.find_first_of(" \t"), rest.size());
    std::string const name = rest.substr(0, nameEnd);
    std::string const reason = sg_Clean(sg_Trim(rest.substr(nameEnd)), 200);
    if (name.empty())
    {
        effects_.tell(reporter.conn, "Usage: /report <name> <reason>");
        return true;
    }

    gPlatformChatPlayer target;
    if (!effects_.find(name, reporter.playerId, target))
        return true; // the lookup told the reporter
    if (target.playerId == reporter.playerId)
    {
        effects_.tell(reporter.conn, "You cannot report yourself.");
        return true;
    }
    times.push_back(now);

    std::int64_t const time = Now();
    nlohmann::ordered_json chat = nlohmann::ordered_json::array();
    for (auto const& line : window_)
    {
        if (time - line.time > WindowSeconds)
            continue;
        bool include = line.channel != "team" && line.channel != "private";
        include = include || line.player.playerId == reporter.playerId || line.player.playerId == target.playerId;
        include = include || (line.channel == "private" && (sg_Lower(line.to) == sg_Lower(reporter.name) || sg_Lower(line.to) == sg_Lower(target.name)));
        if (!include)
            continue;

        nlohmann::ordered_json entry;
        entry["id"] = line.id;
        entry["time"] = line.time;
        entry["channel"] = line.channel;
        entry["name"] = gPlatformHttp::ToUtf8(line.player.name);
        entry["gid"] = sg_NullOr(line.player.gid);
        entry["conn"] = line.player.conn;
        entry["text"] = gPlatformHttp::ToUtf8(line.text);
        if (!line.to.empty())
            entry["to"] = gPlatformHttp::ToUtf8(line.to);
        entry["delivered"] = sg_OutcomeName(line.outcome);
        if (!line.lateVerdict.empty())
            entry["verdict"] = line.lateVerdict;
        chat.push_back(std::move(entry));
    }

    nlohmann::ordered_json data;
    data["reporter"] = sg_PlayerJson(reporter);
    data["target"] = sg_PlayerJson(target);
    data["reason"] = gPlatformHttp::ToUtf8(reason);
    data["chat"] = std::move(chat);
    effects_.report(sg_Dump(data));

    Log("Platform: " + reporter.name + " reported " + target.name + (reason.empty() ? std::string(".") : ": " + reason));
    effects_.tell(reporter.conn, "Thank you. Your report about " + target.name + " was sent to the moderators.");
    return true;
}
