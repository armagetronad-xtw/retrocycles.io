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

// nTransportRtcSupport.h: the parts of the WebRTC transport that do not need
// libdatachannel: signalling message coding, host candidate rewriting,
// connection ID allocation and client address attribution. Kept separate so
// they are unit testable in builds without the library.

#ifndef ArmageTron_NTRANSPORTRTCSUPPORT_H
#define ArmageTron_NTRANSPORTRTCSUPPORT_H

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

//! one signalling message; only one of sdp and candidate is ever filled
struct nRtcSignal
{
    std::string type{};      //!< "offer", "answer", "candidate", "websocket" or "refused"
    std::string sdp{};       //!< session description, for "offer" and "answer"
    std::string candidate{}; //!< ICE candidate, for "candidate"
    std::string mid{};       //!< media ID the candidate belongs to
    std::string reason{};    //!< why the server said no, for "refused"
};

/* "websocket" without a peer connection: the client asks for the game packets to
   travel as binary frames on the signalling socket itself (for browsers that may
   not use WebRTC, such as a Discord Activity), and the server agrees with the same
   message or says "refused" with a reason. */

/* The signalling protocol is a handful of flat JSON objects with string values
   only, so a full parser would be dead weight. These two cover exactly that
   shape and reject everything else. */

//! appends text to out as a quoted JSON string
inline void nRtcAppendJsonString(std::string const& text, std::string& out)
{
    out += '"';
    for (char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                static char const* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 0xf];
                out += hex[c & 0xf];
            }
            else
            {
                out += c;
            }
        }
    }
    out += '"';
}

//! reads a quoted JSON string starting at text[pos] (which must be the opening quote)
inline bool nRtcTryReadJsonString(std::string const& text, size_t& pos, std::string& value)
{
    if (pos >= text.size() || text[pos] != '"')
        return false;

    ++pos;
    value.clear();

    while (pos < text.size())
    {
        char const c = text[pos++];

        if (c == '"')
            return true;

        if (c != '\\')
        {
            value += c;
            continue;
        }

        if (pos >= text.size())
            return false;

        char const escape = text[pos++];
        switch (escape)
        {
        case '"':
        case '\\':
        case '/':
            value += escape;
            break;
        case 'b':
            value += '\b';
            break;
        case 'f':
            value += '\f';
            break;
        case 'n':
            value += '\n';
            break;
        case 'r':
            value += '\r';
            break;
        case 't':
            value += '\t';
            break;
        case 'u':
        {
            if (pos + 4 > text.size())
                return false;

            unsigned int code = 0;
            for (int i = 0; i < 4; ++i)
            {
                char const digit = text[pos++];
                code <<= 4;
                if (digit >= '0' && digit <= '9')
                    code |= static_cast<unsigned int>(digit - '0');
                else if (digit >= 'a' && digit <= 'f')
                    code |= static_cast<unsigned int>(digit - 'a' + 10);
                else if (digit >= 'A' && digit <= 'F')
                    code |= static_cast<unsigned int>(digit - 'A' + 10);
                else
                    return false;
            }

            // UTF-8 encode the basic plane; surrogate pairs are not expected here
            if (code < 0x80)
            {
                value += static_cast<char>(code);
            }
            else if (code < 0x800)
            {
                value += static_cast<char>(0xc0 | (code >> 6));
                value += static_cast<char>(0x80 | (code & 0x3f));
            }
            else
            {
                value += static_cast<char>(0xe0 | (code >> 12));
                value += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                value += static_cast<char>(0x80 | (code & 0x3f));
            }
            break;
        }
        default:
            return false;
        }
    }

    return false;
}

//! reads the flat string members of a JSON object; anything else is skipped
inline bool nRtcTryParseJsonObject(std::string const& text, std::map<std::string, std::string>& values)
{
    values.clear();

    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || text[pos] != '{')
        return false;

    ++pos;

    for (;;)
    {
        pos = text.find_first_not_of(" \t\r\n", pos);
        if (pos == std::string::npos)
            return false;

        if (text[pos] == '}')
            return true;

        if (text[pos] == ',')
        {
            ++pos;
            continue;
        }

        std::string key;
        if (!nRtcTryReadJsonString(text, pos, key))
            return false;

        pos = text.find_first_not_of(" \t\r\n", pos);
        if (pos == std::string::npos || text[pos] != ':')
            return false;

        pos = text.find_first_not_of(" \t\r\n", pos + 1);
        if (pos == std::string::npos)
            return false;

        if (text[pos] == '"')
        {
            std::string value;
            if (!nRtcTryReadJsonString(text, pos, value))
                return false;

            values[key] = value;
            continue;
        }

        // members that are not strings are of no interest, but must be stepped over
        int depth = 0;
        for (; pos < text.size(); ++pos)
        {
            char const c = text[pos];
            if (c == '{' || c == '[')
            {
                ++depth;
            }
            else if (c == '}' || c == ']')
            {
                if (depth == 0)
                    break;
                --depth;
            }
            else if (c == ',' && depth == 0)
            {
                break;
            }
            else if (c == '"')
            {
                std::string ignored;
                if (!nRtcTryReadJsonString(text, pos, ignored))
                    return false;
                --pos; // the loop increment steps past the closing quote
            }
        }
    }
}

//! parses one signalling message; false if it is malformed or of an unknown type
inline bool nRtcTryParseSignal(std::string const& text, nRtcSignal& signal)
{
    std::map<std::string, std::string> values;
    if (!nRtcTryParseJsonObject(text, values))
        return false;

    std::map<std::string, std::string>::const_iterator type = values.find("type");
    if (type == values.end())
        return false;

    signal = nRtcSignal();
    signal.type = type->second;

    std::map<std::string, std::string>::const_iterator sdp = values.find("sdp");
    if (sdp != values.end())
        signal.sdp = sdp->second;

    std::map<std::string, std::string>::const_iterator candidate = values.find("candidate");
    if (candidate != values.end())
        signal.candidate = candidate->second;

    std::map<std::string, std::string>::const_iterator mid = values.find("mid");
    if (mid != values.end())
        signal.mid = mid->second;

    std::map<std::string, std::string>::const_iterator reason = values.find("reason");
    if (reason != values.end())
        signal.reason = reason->second;

    if (signal.type == "offer" || signal.type == "answer")
        return !signal.sdp.empty();

    if (signal.type == "candidate")
        return !signal.candidate.empty();

    if (signal.type == "websocket" || signal.type == "refused")
        return true;

    return false;
}

//! renders one signalling message as JSON
inline std::string nRtcFormatSignal(nRtcSignal const& signal)
{
    std::string out = "{\"type\":";
    nRtcAppendJsonString(signal.type, out);

    if (signal.type == "candidate")
    {
        out += ",\"candidate\":";
        nRtcAppendJsonString(signal.candidate, out);
        out += ",\"mid\":";
        nRtcAppendJsonString(signal.mid, out);
    }
    else if (signal.type == "refused")
    {
        out += ",\"reason\":";
        nRtcAppendJsonString(signal.reason, out);
    }
    else if (signal.type != "websocket")
    {
        out += ",\"sdp\":";
        nRtcAppendJsonString(signal.sdp, out);
    }

    out += '}';

    return out;
}

//! true if the text is a dotted quad
inline bool nRtcIsIPv4(std::string const& address)
{
    int groups = 0;
    size_t pos = 0;

    while (pos < address.size())
    {
        size_t const start = pos;
        while (pos < address.size() && address[pos] >= '0' && address[pos] <= '9')
            ++pos;

        if (pos == start || pos - start > 3)
            return false;

        ++groups;

        if (pos == address.size())
            break;

        if (address[pos] != '.')
            return false;

        ++pos;
    }

    return groups == 4 && address[address.size() - 1] != '.';
}

/* Rewrites the address of host candidates. Hosts behind 1:1 NAT (most cloud VMs)
   only see their private IP, and the browser cannot reach that; libdatachannel
   issue #436 recommends patching the candidate before signalling it. */

//! rewrites one "a=candidate:..." or "candidate:..." line, false if it was left alone
inline bool nRtcTryRewriteHostCandidate(std::string const& line, std::string const& publicIp, std::string& rewritten)
{
    size_t const start = line.compare(0, 2, "a=") == 0 ? 2 : 0;
    if (line.compare(start, 10, "candidate:") != 0)
        return false;

    // foundation component transport priority address port "typ" type ...
    std::vector<std::string> fields;
    size_t pos = start;
    while (pos < line.size())
    {
        size_t const end = line.find(' ', pos);
        fields.push_back(line.substr(pos, end == std::string::npos ? std::string::npos : end - pos));
        if (end == std::string::npos)
            break;
        pos = end + 1;
    }

    if (fields.size() < 8 || fields[6] != "typ" || fields[7] != "host" || !nRtcIsIPv4(fields[4]))
        return false;

    fields[4] = publicIp;

    rewritten = line.substr(0, start);
    for (size_t i = 0; i < fields.size(); ++i)
    {
        if (i > 0)
            rewritten += ' ';
        rewritten += fields[i];
    }

    return true;
}

//! rewrites the address of every host candidate in an SDP; returns the SDP unchanged for an empty IP
inline std::string nRtcRewriteHostCandidates(std::string const& sdp, std::string const& publicIp)
{
    if (publicIp.empty())
        return sdp;

    std::string out;
    out.reserve(sdp.size());

    size_t pos = 0;
    while (pos <= sdp.size())
    {
        size_t end = sdp.find('\n', pos);
        bool const last = end == std::string::npos;
        if (last)
            end = sdp.size();

        // the line ending stays exactly as it was
        size_t lineEnd = end;
        if (lineEnd > pos && sdp[lineEnd - 1] == '\r')
            --lineEnd;

        std::string const line = sdp.substr(pos, lineEnd - pos);
        std::string rewritten;
        out += nRtcTryRewriteHostCandidate(line, publicIp, rewritten) ? rewritten : line;
        out += sdp.substr(lineEnd, end - lineEnd);

        if (last)
            break;

        out += '\n';
        pos = end + 1;
    }

    return out;
}

/* Connection IDs end up in the port field of the peer's virtual nAddress and
   identify it for the rest of its session. Reusing one too eagerly would let
   late packets of a closed session be attributed to a fresh one, so a closed ID
   only returns to the pool after `grace` further allocations. */
class nRtcConnectionIds
{
public:
    explicit nRtcConnectionIds(unsigned int grace = 32) : grace_(grace) {}

    //! hands out an unused connection ID, false if all 65535 are in flight
    bool TryAllocate(unsigned short& id)
    {
        if (!retired_.empty())
        {
            id = retired_.front();
            retired_.pop_front();
            return true;
        }

        if (next_ > 0xffff)
            return false;

        id = static_cast<unsigned short>(next_++);
        return true;
    }

    //! gives a closed connection's ID back; it stays out of circulation for a while
    void Release(unsigned short id)
    {
        cooling_.push_back(id);
        while (cooling_.size() > grace_)
        {
            retired_.push_back(cooling_.front());
            cooling_.pop_front();
        }
    }

private:
    unsigned int next_ = 1;
    unsigned int grace_;
    std::deque<unsigned short> cooling_{}; //!< recently closed, not reusable yet
    std::deque<unsigned short> retired_{}; //!< closed long enough ago to be reused
};

/* Client address attribution. A browser's packets reach the server
   from the address ICE selected, which for a relayed browser is the TURN server's,
   so that address says nothing about who the player is. The signalling WebSocket
   is the one connection that comes from the browser itself; through the reverse
   proxy that terminates TLS its socket peer is the proxy, and the proxy names the
   real client in the request target (libdatachannel 0.24 parses the HTTP headers
   of the upgrade request and drops them; the path is all it exposes), as
   `?X-Forwarded-For=<list>` or `?X-Real-IP=<address>`. That value is believed only
   when the socket peer is a trusted proxy (WEBRTC_TRUSTED_PROXIES). The virtual
   nAddress has room for IPv4 only; an IPv6 client is counted as a stable
   pseudo-address in 240.0.0.0/4 derived from its /64. */

//! an IPv4 or IPv6 address; an IPv4-mapped IPv6 address is stored as IPv4
struct nRtcIp
{
    bool v6 = false;
    unsigned char bytes[16] = {}; //!< network order; IPv4 uses the first four
};

//! a trusted address range: base address and prefix length in bits
struct nRtcIpRange
{
    nRtcIp base{};
    int prefix = 0;
};

//! where the address a peer is attributed to came from
enum nRtcClientSource
{
    nRtcClient_Unknown,   //!< the socket peer is unreadable
    nRtcClient_Socket,    //!< the signalling socket's peer, not a trusted proxy
    nRtcClient_Forwarded, //!< named by a trusted proxy
    nRtcClient_Proxy      //!< a trusted proxy that named no usable client address
};

//! strips the port from an "ip:port" or "[ip]:port" string libdatachannel hands out
//! (its IPv6 form is unbracketed "::1:port": the port is always after the last colon)
inline std::string nRtcStripPort(std::string const& address)
{
    if (!address.empty() && address[0] == '[')
    {
        size_t const end = address.find(']');
        return end == std::string::npos ? address : address.substr(1, end - 1);
    }

    size_t const colon = address.rfind(':');
    return colon == std::string::npos ? address : address.substr(0, colon);
}

//! parses a strict dotted quad (no leading zeros, no missing parts)
inline bool nRtcTryParseIPv4Bytes(std::string const& text, unsigned char out[4])
{
    size_t pos = 0;
    for (int i = 0; i < 4; ++i)
    {
        size_t const start = pos;
        unsigned int value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && pos - start < 3)
            value = value * 10 + static_cast<unsigned int>(text[pos++] - '0');

        size_t const digits = pos - start;
        if (digits == 0 || value > 255 || (digits > 1 && text[start] == '0'))
            return false;

        out[i] = static_cast<unsigned char>(value);

        if (i < 3)
        {
            if (pos >= text.size() || text[pos] != '.')
                return false;
            ++pos;
        }
    }

    return pos == text.size();
}

//! parses an IPv6 address in any RFC 4291 text form (an "%zone" suffix is ignored)
inline bool nRtcTryParseIPv6Bytes(std::string text, unsigned char out[16])
{
    size_t const zone = text.find('%');
    if (zone != std::string::npos)
        text.resize(zone);

    size_t const gap = text.find("::");
    if (gap != std::string::npos && text.find("::", gap + 1) != std::string::npos)
        return false;

    // reads the colon separated groups of one side of the gap
    struct Groups
    {
        static bool TryRead(std::string const& part, bool allowIPv4Tail, std::vector<unsigned int>& groups)
        {
            if (part.empty())
                return true;

            size_t start = 0;
            for (;;)
            {
                size_t const colon = part.find(':', start);
                std::string const group = part.substr(start, colon == std::string::npos ? std::string::npos : colon - start);

                if (colon == std::string::npos && allowIPv4Tail && group.find('.') != std::string::npos)
                {
                    unsigned char v4[4];
                    if (!nRtcTryParseIPv4Bytes(group, v4))
                        return false;
                    groups.push_back((v4[0] << 8) | v4[1]);
                    groups.push_back((v4[2] << 8) | v4[3]);
                    return true;
                }

                if (group.empty() || group.size() > 4)
                    return false;

                unsigned int value = 0;
                for (char c : group)
                {
                    int digit = -1;
                    if (c >= '0' && c <= '9')
                        digit = c - '0';
                    else if (c >= 'a' && c <= 'f')
                        digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        digit = c - 'A' + 10;
                    if (digit < 0)
                        return false;
                    value = value * 16 + static_cast<unsigned int>(digit);
                }
                groups.push_back(value);

                if (colon == std::string::npos)
                    return true;
                start = colon + 1;
            }
        }
    };

    std::vector<unsigned int> head, tail;
    if (gap == std::string::npos)
    {
        if (!Groups::TryRead(text, true, head) || head.size() != 8)
            return false;
    }
    else
    {
        if (!Groups::TryRead(text.substr(0, gap), false, head) ||
            !Groups::TryRead(text.substr(gap + 2), true, tail) ||
            head.size() + tail.size() > 7)
            return false;
    }

    std::vector<unsigned int> groups(head);
    groups.resize(8 - tail.size(), 0);
    groups.insert(groups.end(), tail.begin(), tail.end());

    for (int i = 0; i < 8; ++i)
    {
        out[2 * i] = static_cast<unsigned char>(groups[i] >> 8);
        out[2 * i + 1] = static_cast<unsigned char>(groups[i] & 0xff);
    }

    return true;
}

/* Parses one address as proxies write them: "1.2.3.4", "2001:db8::1",
   "[2001:db8::1]", with an optional port after an IPv4 or a bracketed IPv6
   ("1.2.3.4:5678", "[2001:db8::1]:5678"), surrounding blanks ignored. An
   IPv4-mapped IPv6 address comes out as IPv4. */
inline bool nRtcTryParseIp(std::string const& raw, nRtcIp& ip)
{
    size_t const first = raw.find_first_not_of(" \t");
    if (first == std::string::npos)
        return false;
    std::string text = raw.substr(first, raw.find_last_not_of(" \t") - first + 1);

    nRtcIp parsed;
    if (text[0] == '[')
    {
        size_t const end = text.find(']');
        if (end == std::string::npos)
            return false;

        std::string const rest = text.substr(end + 1);
        if (!rest.empty() && (rest[0] != ':' || rest.size() < 2 ||
                              rest.find_first_not_of("0123456789", 1) != std::string::npos))
            return false;

        text = text.substr(1, end - 1);
        if (text.find(':') == std::string::npos)
            return false;
    }
    else if (text.find(':') != std::string::npos && text.find(':') == text.rfind(':'))
    {
        // exactly one colon: IPv4 with a port (IPv6 has at least two)
        size_t const colon = text.find(':');
        std::string const port = text.substr(colon + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos)
            return false;
        text.resize(colon);
    }

    if (text.find(':') == std::string::npos)
    {
        if (!nRtcTryParseIPv4Bytes(text, parsed.bytes))
            return false;
        ip = parsed;
        return true;
    }

    if (!nRtcTryParseIPv6Bytes(text, parsed.bytes))
        return false;

    static unsigned char const mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    bool isMapped = true;
    for (int i = 0; i < 12; ++i)
        isMapped = isMapped && parsed.bytes[i] == mapped[i];

    if (isMapped)
    {
        for (int i = 0; i < 4; ++i)
            parsed.bytes[i] = parsed.bytes[12 + i];
        for (int i = 4; i < 16; ++i)
            parsed.bytes[i] = 0;
    }
    else
    {
        parsed.v6 = true;
    }

    ip = parsed;
    return true;
}

//! formats an address the canonical way (RFC 5952 for IPv6)
inline std::string nRtcFormatIp(nRtcIp const& ip)
{
    std::string out;
    if (!ip.v6)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (i)
                out += '.';
            out += std::to_string(static_cast<unsigned int>(ip.bytes[i]));
        }
        return out;
    }

    unsigned int groups[8];
    for (int i = 0; i < 8; ++i)
        groups[i] = (static_cast<unsigned int>(ip.bytes[2 * i]) << 8) | ip.bytes[2 * i + 1];

    // the longest run of two or more zero groups becomes "::"
    int bestStart = -1, bestLength = 1;
    for (int i = 0; i < 8;)
    {
        if (groups[i] != 0)
        {
            ++i;
            continue;
        }
        int j = i;
        while (j < 8 && groups[j] == 0)
            ++j;
        if (j - i > bestLength)
        {
            bestStart = i;
            bestLength = j - i;
        }
        i = j;
    }

    static char const* hex = "0123456789abcdef";
    for (int i = 0; i < 8; ++i)
    {
        if (i == bestStart)
        {
            out += "::";
            i += bestLength - 1;
            continue;
        }
        if (!out.empty() && out[out.size() - 1] != ':')
            out += ':';

        bool leading = true;
        for (int shift = 12; shift >= 0; shift -= 4)
        {
            unsigned int const digit = (groups[i] >> shift) & 0xf;
            if (leading && digit == 0 && shift > 0)
                continue;
            leading = false;
            out += hex[digit];
        }
    }

    return out;
}

//! parses "address" or "address/prefix"; a bare address is a range of one
inline bool nRtcTryParseIpRange(std::string const& text, nRtcIpRange& range)
{
    size_t const slash = text.find('/');
    nRtcIp base;
    if (!nRtcTryParseIp(text.substr(0, slash), base))
        return false;

    int const bits = base.v6 ? 128 : 32;
    int prefix = bits;
    if (slash != std::string::npos)
    {
        std::string const length = text.substr(slash + 1);
        if (length.empty() || length.size() > 3 || length.find_first_not_of("0123456789") != std::string::npos)
            return false;
        prefix = std::stoi(length);

        // "::ffff:10.0.0.0/104" is parsed as IPv4: shift the prefix along
        if (!base.v6 && text.find(':') != std::string::npos)
            prefix -= 96;

        if (prefix < 0 || prefix > bits)
            return false;
    }

    range.base = base;
    range.prefix = prefix;
    return true;
}

//! parses a list of ranges separated by commas or blanks; unparsable entries go to rejected
inline std::vector<nRtcIpRange> nRtcParseIpRanges(std::string const& list, std::vector<std::string>* rejected = nullptr)
{
    std::vector<nRtcIpRange> ranges;
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t const start = list.find_first_not_of(", \t", pos);
        if (start == std::string::npos)
            break;
        size_t const end = list.find_first_of(", \t", start);
        std::string const entry = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
        pos = end == std::string::npos ? list.size() : end;

        nRtcIpRange range;
        if (nRtcTryParseIpRange(entry, range))
            ranges.push_back(range);
        else if (rejected)
            rejected->push_back(entry);
    }
    return ranges;
}

//! true if the address lies in one of the ranges (IPv4 ranges match IPv4 only, IPv6 IPv6)
inline bool nRtcRangesContain(std::vector<nRtcIpRange> const& ranges, nRtcIp const& ip)
{
    for (nRtcIpRange const& range : ranges)
    {
        if (range.base.v6 != ip.v6)
            continue;

        bool match = true;
        for (int bit = 0; bit < range.prefix && match; bit += 8)
        {
            int const index = bit / 8;
            int const bits = range.prefix - bit >= 8 ? 8 : range.prefix - bit;
            unsigned char const mask = static_cast<unsigned char>(0xff << (8 - bits));
            match = (range.base.bytes[index] & mask) == (ip.bytes[index] & mask);
        }
        if (match)
            return true;
    }
    return false;
}

/* Every value of the query parameter `name` in a request target, decoded, in
   order. The name matches exactly, case included: that is how the proxy (Caddy's
   `uri query`, Go's url.Values) replaces the parameter, so a browser's own
   "x-forwarded-for=..." survives the proxy and must not count. */
inline std::vector<std::string> nRtcQueryValues(std::string const& target, std::string const& name)
{
    std::vector<std::string> values;

    size_t const question = target.find('?');
    if (question == std::string::npos)
        return values;
    size_t const fragment = target.find('#', question);
    std::string const query = target.substr(question + 1, fragment == std::string::npos ? std::string::npos : fragment - question - 1);

    struct Decode
    {
        static int Hex(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        // '+' is a blank, %XX a byte; a broken escape stays as it is
        static std::string Component(std::string const& in)
        {
            std::string out;
            for (size_t i = 0; i < in.size(); ++i)
            {
                if (in[i] == '+')
                    out += ' ';
                else if (in[i] == '%' && i + 2 < in.size() && Hex(in[i + 1]) >= 0 && Hex(in[i + 2]) >= 0)
                {
                    out += static_cast<char>(Hex(in[i + 1]) * 16 + Hex(in[i + 2]));
                    i += 2;
                }
                else
                    out += in[i];
            }
            return out;
        }
    };

    size_t pos = 0;
    while (pos <= query.size())
    {
        size_t const amp = query.find('&', pos);
        std::string const pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        pos = amp == std::string::npos ? query.size() + 1 : amp + 1;

        size_t const equals = pair.find('=');
        if (equals == std::string::npos)
            continue;
        if (Decode::Component(pair.substr(0, equals)) == name)
            values.push_back(Decode::Component(pair.substr(equals + 1)));
    }

    return values;
}

/* Picks the client out of what a trusted proxy forwarded. X-Forwarded-For is a
   comma separated list every proxy appends its peer to (repeated parameters are
   one list, in order); the client is the right-most entry that is not itself a
   trusted proxy, and if they all are, the left-most. A malformed entry on the way
   ends the search unsuccessfully: nothing left of it can be vouched for. Without
   X-Forwarded-For, the last X-Real-IP counts. */
inline bool nRtcTryResolveForwarded(std::vector<std::string> const& forwardedFor,
                                    std::vector<std::string> const& realIp,
                                    std::vector<nRtcIpRange> const& trusted,
                                    nRtcIp& client)
{
    std::vector<std::string> entries;
    for (std::string const& value : forwardedFor)
    {
        size_t start = 0;
        for (;;)
        {
            size_t const comma = value.find(',', start);
            std::string const entry = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (entry.find_first_not_of(" \t") != std::string::npos)
                entries.push_back(entry);
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
    }

    if (!entries.empty())
    {
        nRtcIp leftmost;
        for (size_t i = entries.size(); i-- > 0;)
        {
            nRtcIp ip;
            if (!nRtcTryParseIp(entries[i], ip))
                return false;
            if (!nRtcRangesContain(trusted, ip))
            {
                client = ip;
                return true;
            }
            leftmost = ip;
        }
        client = leftmost;
        return true;
    }

    if (!realIp.empty())
        return nRtcTryParseIp(realIp.back(), client);

    return false;
}

//! decides whom a signalling connection belongs to; socketPeer without port, target as the proxy sent it
inline nRtcClientSource nRtcResolveClient(std::string const& socketPeer,
                                          std::string const& requestTarget,
                                          std::vector<nRtcIpRange> const& trusted,
                                          nRtcIp& client)
{
    nRtcIp peer;
    if (!nRtcTryParseIp(socketPeer, peer))
        return nRtcClient_Unknown;

    if (!nRtcRangesContain(trusted, peer))
    {
        client = peer;
        return nRtcClient_Socket;
    }

    if (nRtcTryResolveForwarded(nRtcQueryValues(requestTarget, "X-Forwarded-For"),
                                nRtcQueryValues(requestTarget, "X-Real-IP"), trusted, client))
        return nRtcClient_Forwarded;

    client = peer;
    return nRtcClient_Proxy;
}

//! where the address a peer is pinned to came from, best first
enum nRtcAddressSource
{
    nRtcAddress_None,       //!< nothing usable
    nRtcAddress_Forwarded,  //!< the client a trusted proxy named
    nRtcAddress_IceDirect,  //!< the selected ICE pair's remote address, direct (host, srflx, prflx)
    nRtcAddress_Signalling, //!< the signalling socket's peer, not a trusted proxy
    nRtcAddress_IceRelayed, //!< the selected pair's remote address through a relay (the TURN server)
    nRtcAddress_Proxy       //!< the signalling peer that is a trusted proxy naming nobody
};

/* Picks the IPv4 (network order, attribution form) a peer is known by for bans,
   per-IP limits and logs, and says where it came from:
   1. the client a trusted proxy named (official rooms behind their Caddy or gateway);
   2. the direct ICE remote address: where the packets really come from, so a
      signalling gateway we do not trust (Direct Connect) never becomes everyone's address;
   3. the signalling socket's peer;
   4. a relayed ICE address (every relayed player looks like the TURN server);
   5. a trusted proxy's own address, when it named nobody.
   client/clientIp: what nRtcResolveClient() found (clientIp after any shared proxy
   substitution, 0 if unknown); iceIp 0 when there is no selected pair (the WebSocket
   path); socketIp: the signalling socket's peer. */
inline nRtcAddressSource nRtcChoosePeerAddress(nRtcClientSource client, unsigned int clientIp, unsigned int iceIp,
                                               bool iceDirect, unsigned int socketIp, unsigned int& ip)
{
    nRtcAddressSource source = nRtcAddress_None;
    ip = 0;
    if (client == nRtcClient_Forwarded && clientIp)
    {
        source = nRtcAddress_Forwarded;
        ip = clientIp;
    }
    else if (iceIp && iceDirect)
    {
        source = nRtcAddress_IceDirect;
        ip = iceIp;
    }
    else if (client == nRtcClient_Socket && clientIp)
    {
        source = nRtcAddress_Signalling;
        ip = clientIp;
    }
    else if (iceIp)
    {
        source = nRtcAddress_IceRelayed;
        ip = iceIp;
    }
    else if (socketIp)
    {
        source = nRtcAddress_Proxy;
        ip = socketIp;
    }
    return source;
}

//! the UDP ports ICE uses
struct nRtcIcePorts
{
    int begin = 0;    //!< first port; 0: the library picks any
    int end = 0;      //!< last port
    bool mux = false; //!< one socket for every peer (ICE UDP mux, libjuice)
};

//! offset of the default ICE port from SERVER_PORT: clear of the game ports of neighbouring
//! servers (hosts often run several on 4534, 4535, 4536, ...)
int const nRtcIceMuxOffset = 1000;

/* The ICE ports from WEBRTC_PORT_MIN/MAX. An explicit range wins; a range of one
   port is multiplexed, since one plain socket per peer would admit a single
   player. 0/0 means one multiplexed port, SERVER_PORT + nRtcIceMuxOffset, so a
   host opens exactly one UDP port for every browser player; if that is out of
   range, the library's choice (any port). */
inline nRtcIcePorts nRtcChooseIcePorts(int serverPort, int portMin, int portMax)
{
    nRtcIcePorts ports;
    if (portMin > 0 && portMax >= portMin && portMax <= 65535)
    {
        ports.begin = portMin;
        ports.end = portMax;
        ports.mux = portMin == portMax;
        return ports;
    }

    int const port = serverPort + nRtcIceMuxOffset;
    if (serverPort > 0 && port > 1024 && port <= 65535)
    {
        ports.begin = ports.end = port;
        ports.mux = true;
    }
    return ports;
}

//! "UDP 5534", "UDP 50000-50019" or "any UDP port", for the console
inline std::string nRtcDescribeIcePorts(nRtcIcePorts const& ports)
{
    if (!ports.begin)
        return "any UDP port (set WEBRTC_PORT_MIN/MAX to pin them)";
    if (ports.begin == ports.end)
        return "UDP " + std::to_string(ports.begin);
    return "UDP " + std::to_string(ports.begin) + "-" + std::to_string(ports.end);
}

/* Handshakes in flight. A peer is
   pending from the accepted socket until its data channel opens or it agreed on the
   WebSocket path, and after that until the game has a logged in connection at its
   address (it is claimed); players in the game never count, since many of them share
   one source address behind the Retrocycles gateway. Counting the established peers
   that never log in keeps a client from completing handshakes over and over until the
   connection IDs run out. */

//! whether a new signalling connection may start a handshake
enum nRtcAdmission
{
    nRtcAdmit,         //!< go ahead
    nRtcRefuse_Source, //!< its source address has too many handshakes in flight
    nRtcRefuse_Global  //!< the server has too many handshakes in flight
};

//! pendingFromSource / pendingTotal: handshakes already in flight; a cap of 0 or less is no cap
inline nRtcAdmission nRtcAdmitPending(int pendingFromSource, int pendingTotal, int sourceCap, int globalCap)
{
    if (globalCap > 0 && pendingTotal >= globalCap)
        return nRtcRefuse_Global;
    if (sourceCap > 0 && pendingFromSource >= sourceCap)
        return nRtcRefuse_Source;
    return nRtcAdmit;
}

/* The address a pending peer counts against WEBRTC_PENDING_PER_SOURCE (attribution form),
   0 for none (it counts only towards WEBRTC_PENDING_MAX). An untrusted socket peer, our
   Direct Connect gateway included, is its own source from accept on. A trusted proxy
   (Caddy on loopback, a region gateway) stands for everyone behind it: it is no source;
   its connection counts against the client it forwards once the first message named it
   (resolved, source nRtcClient_Forwarded), and against nobody if it named none. */
inline unsigned int nRtcPendingSourceKey(bool trustedPeer, bool resolved, nRtcClientSource source, unsigned int socketIp,
                                         unsigned int clientIp)
{
    if (!trustedPeer)
        return socketIp;
    if (resolved && source == nRtcClient_Forwarded)
        return clientIp;
    return 0;
}

//! how many keys equal key; a key of 0 matches nothing
inline int nRtcCountSource(std::vector<unsigned int> const& keys, unsigned int key)
{
    int count = 0;
    if (key)
        for (unsigned int other : keys)
            count += other == key;
    return count;
}

//! the "refused" reason for a connection that was not admitted
inline char const* nRtcAdmissionReason(nRtcAdmission admission)
{
    switch (admission)
    {
    case nRtcRefuse_Source:
        return "too many connections being set up from your address, try again in a moment";
    case nRtcRefuse_Global:
        return "too many connections being set up, try again in a moment";
    default:
        return "";
    }
}

//! why a pending peer is given up
enum nRtcPendingExpiry
{
    nRtcPending_Alive,     //!< still within its time
    nRtcPending_Silent,    //!< no valid signalling message in time
    nRtcPending_Handshake, //!< the data channel did not open in time
    nRtcPending_Login      //!< established, but no logged in game connection in time
};

/* ageMs: since the socket was accepted; gotMessage: a valid signalling message arrived.
   A timeout of 0 or less never expires. */
inline nRtcPendingExpiry nRtcPendingExpired(long long ageMs, bool gotMessage, long long silentTimeoutMs,
                                            long long handshakeTimeoutMs)
{
    if (!gotMessage && silentTimeoutMs > 0 && ageMs >= silentTimeoutMs)
        return nRtcPending_Silent;
    if (handshakeTimeoutMs > 0 && ageMs >= handshakeTimeoutMs)
        return nRtcPending_Handshake;
    return nRtcPending_Alive;
}

/* unclaimedMs: how long an established peer has been without a logged in game connection
   (never logged in, or its game connection ended). A timeout of 0 or less never expires. */
inline nRtcPendingExpiry nRtcUnclaimedExpired(long long unclaimedMs, long long loginTimeoutMs)
{
    if (loginTimeoutMs > 0 && unclaimedMs >= loginTimeoutMs)
        return nRtcPending_Login;
    return nRtcPending_Alive;
}

//! whether a candidate type ("host", "srflx", "prflx", "relay", as in SDP) means packets come straight from the client
inline bool nRtcIceTypeIsDirect(std::string const& type)
{
    return type == "host" || type == "srflx" || type == "prflx";
}

//! a few words for the console: which rule of nRtcChoosePeerAddress() won
inline char const* nRtcAddressSourceName(nRtcAddressSource source)
{
    switch (source)
    {
    case nRtcAddress_Forwarded:
        return "forwarded by a trusted proxy";
    case nRtcAddress_IceDirect:
        return "direct ICE pair";
    case nRtcAddress_Signalling:
        return "signalling peer";
    case nRtcAddress_IceRelayed:
        return "relayed ICE pair";
    case nRtcAddress_Proxy:
        return "trusted proxy's own address";
    default:
        return "unknown";
    }
}

/* The IPv4 (network byte order) a virtual nAddress carries for this client.
   IPv4 is itself. IPv6 becomes 240.0.0.0/4 plus 28 bits of an FNV-1a hash of
   the /64 prefix: the reserved block never holds a real client, one /64 is one
   subscriber the way one IPv4 behind NAT is, and the same prefix always gets the
   same value, so bans and per-IP limits keep working across sessions. prefixBytes is
   how much of an IPv6 address is hashed; the default 8 is the /64. */
inline unsigned int nRtcAttributionIPv4(nRtcIp const& ip, int prefixBytes = 8)
{
    unsigned char out[4];
    if (!ip.v6)
    {
        for (int i = 0; i < 4; ++i)
            out[i] = ip.bytes[i];
    }
    else
    {
        unsigned int hash = 2166136261u;
        for (int i = 0; i < prefixBytes && i < 16; ++i)
        {
            hash ^= ip.bytes[i];
            hash *= 16777619u;
        }
        out[0] = static_cast<unsigned char>(0xf0 | ((hash >> 24) & 0x0f));
        out[1] = static_cast<unsigned char>((hash >> 16) & 0xff);
        out[2] = static_cast<unsigned char>((hash >> 8) & 0xff);
        out[3] = static_cast<unsigned char>(hash & 0xff);
        if (out[0] == 0xff && out[1] == 0xff && out[2] == 0xff && out[3] == 0xff)
            out[3] = 0xfe; // never the broadcast address
    }

    unsigned int result = 0;
    std::memcpy(&result, out, sizeof(result));
    return result;
}

/* The source a handshake counts against for WEBRTC_PENDING_PER_SOURCE
   (nRtcPendingSourceKey). IPv4 is the address itself. IPv6 is grouped by its /48, not
   the /64 the player is attributed to: a subscriber commonly holds a /56 or a /48,
   that is hundreds of /64s, each of which would get a cap of its own. Only ever
   compared with other source keys. */
inline unsigned int nRtcPendingSourceIPv4(nRtcIp const& ip)
{
    return nRtcAttributionIPv4(ip, 6);
}

/* The IPv4 (network byte order) for one connection of a shared proxy's client
   (WEBRTC_SHARED_PROXIES: a proxy that stands for many players and does not say
   which, such as the one in front of a Discord Activity). In 240.0.0.0/4 like an
   IPv6 client's, from an FNV-1a hash of the server's salt and the connection's
   serial number: every connection gets its own address, so players behind the
   same proxy never count as one machine (per-IP limits, vote grouping, spam). */
inline unsigned int nRtcSharedProxyIPv4(unsigned int salt, unsigned int serial)
{
    unsigned int hash = 2166136261u;
    unsigned int const words[2] = {salt, serial};
    for (unsigned int word : words)
        for (int i = 0; i < 4; ++i)
        {
            hash ^= (word >> (8 * i)) & 0xffu;
            hash *= 16777619u;
        }

    unsigned char out[4];
    out[0] = static_cast<unsigned char>(0xf0 | ((hash >> 24) & 0x0f));
    out[1] = static_cast<unsigned char>((hash >> 16) & 0xff);
    out[2] = static_cast<unsigned char>((hash >> 8) & 0xff);
    out[3] = static_cast<unsigned char>(hash & 0xff);
    if (out[0] == 0xff && out[1] == 0xff && out[2] == 0xff && out[3] == 0xff)
        out[3] = 0xfe; // never the broadcast address

    unsigned int result = 0;
    std::memcpy(&result, out, sizeof(result));
    return result;
}

#endif // ArmageTron_NTRANSPORTRTCSUPPORT_H
