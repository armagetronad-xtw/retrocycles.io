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
#include "monocypher-ed25519.h"

#include "gPlatformToken.h"

#include <limits>

std::size_t const gPlatformLoginToken::MaxLength;
std::int64_t const gPlatformLoginToken::ExpirySkew;
std::int64_t const gPlatformLoginToken::IssuedAtSkew;

// *************************************************************
// base64url
// *************************************************************

static char const sg_base64UrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// the 6 bit value of an alphabet character, -1 for anything else
static int sg_Base64UrlValue(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

bool gPlatformBase64Url::TryDecode(std::string const& in, std::vector<std::uint8_t>& out)
{
    out.clear();

    // one leftover character carries only 6 bits, not a whole byte
    if (in.size() % 4 == 1)
        return false;

    out.reserve(in.size() * 3 / 4);

    std::uint32_t buffer = 0;
    int bits = 0;
    for (char c : in)
    {
        int const value = sg_Base64UrlValue(c);
        if (value < 0)
            return false;

        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
        }
    }

    // the unused low bits of the last character must be zero
    if (bits > 0 && (buffer & ((1u << bits) - 1)) != 0)
        return false;

    return true;
}

std::string gPlatformBase64Url::Encode(std::uint8_t const* data, std::size_t len)
{
    std::string out;
    out.reserve((len * 4 + 2) / 3);

    std::uint32_t buffer = 0;
    int bits = 0;
    for (std::size_t i = 0; i < len; ++i)
    {
        buffer = (buffer << 8) | data[i];
        bits += 8;
        while (bits >= 6)
        {
            bits -= 6;
            out.push_back(sg_base64UrlAlphabet[(buffer >> bits) & 0x3f]);
        }
    }
    if (bits > 0)
        out.push_back(sg_base64UrlAlphabet[(buffer << (6 - bits)) & 0x3f]);

    return out;
}

// *************************************************************
// keys and jti cache
// *************************************************************

bool gPlatformKeySet::TryParse(std::string const& text, gPlatformKeySet& keys, std::string& error)
{
    keys = gPlatformKeySet{};

    nlohmann::json const document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object())
    {
        error = "not a JSON object";
        return false;
    }

    auto const list = document.find("keys");
    if (list == document.end() || !list->is_array())
    {
        error = "no \"keys\" array";
        return false;
    }

    for (auto const& entry : *list)
    {
        if (!entry.is_object())
            continue;

        auto const kid = entry.find("kid");
        auto const alg = entry.find("alg");
        auto const crv = entry.find("crv");
        auto const x = entry.find("x");
        if (kid == entry.end() || !kid->is_string() ||
            alg == entry.end() || !alg->is_string() || alg->get<std::string>() != "EdDSA" ||
            crv == entry.end() || !crv->is_string() || crv->get<std::string>() != "Ed25519" ||
            x == entry.end() || !x->is_string())
            continue;

        std::vector<std::uint8_t> raw;
        if (!gPlatformBase64Url::TryDecode(x->get<std::string>(), raw) || raw.size() != 32)
            continue;

        gPlatformKey key;
        key.kid = kid->get<std::string>();
        std::copy(raw.begin(), raw.end(), key.publicKey.begin());
        keys.Add(key);
    }

    return true;
}

gPlatformKey const* gPlatformKeySet::Find(std::string const& kid) const
{
    for (auto const& key : keys_)
        if (key.kid == kid)
            return &key;
    return nullptr;
}

void gPlatformJtiCache::Prune(std::int64_t now)
{
    for (auto i = entries_.begin(); i != entries_.end();)
    {
        if (i->second < now)
            i = entries_.erase(i);
        else
            ++i;
    }
}

// *************************************************************
// token verification
// *************************************************************

namespace
{
// reads a required string member
bool sg_TryGetString(nlohmann::json const& object, char const* name, std::string& value)
{
    auto const member = object.find(name);
    if (member == object.end() || !member->is_string())
        return false;
    value = member->get<std::string>();
    return true;
}

// reads a required integer member (a JSON number without fraction or exponent)
bool sg_TryGetInteger(nlohmann::json const& object, char const* name, std::int64_t& value)
{
    auto const member = object.find(name);
    if (member == object.end())
        return false;
    if (member->is_number_unsigned())
    {
        auto const unsignedValue = member->get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return false;
        value = static_cast<std::int64_t>(unsignedValue);
        return true;
    }
    if (member->is_number_integer())
    {
        value = member->get<std::int64_t>();
        return true;
    }
    return false;
}

// decodes one base64url part into a JSON object
bool sg_TryDecodeObject(std::string const& part, nlohmann::json& object)
{
    std::vector<std::uint8_t> raw;
    if (!gPlatformBase64Url::TryDecode(part, raw))
        return false;

    object = nlohmann::json::parse(raw.begin(), raw.end(), nullptr, false);
    return !object.is_discarded() && object.is_object();
}
} // namespace

gPlatformTokenError gPlatformLoginToken::Verify(std::string const& token,
                                                gPlatformKeySet const& keys,
                                                std::string const& serverId,
                                                std::string const& authority,
                                                std::int64_t now,
                                                gPlatformJtiCache& jtis,
                                                gPlatformLoginClaims& claims)
{
    claims = gPlatformLoginClaims{};

    // rule 1: size, three parts, JSON objects of the right shape
    if (token.size() > MaxLength)
        return gPlatformTokenError::Malformed;

    std::size_t const firstDot = token.find('.');
    if (firstDot == std::string::npos)
        return gPlatformTokenError::Malformed;
    std::size_t const secondDot = token.find('.', firstDot + 1);
    if (secondDot == std::string::npos || token.find('.', secondDot + 1) != std::string::npos)
        return gPlatformTokenError::Malformed;

    std::string const headerPart = token.substr(0, firstDot);
    std::string const payloadPart = token.substr(firstDot + 1, secondDot - firstDot - 1);
    std::string const signaturePart = token.substr(secondDot + 1);

    nlohmann::json header;
    nlohmann::json payload;
    std::vector<std::uint8_t> signature;
    if (!sg_TryDecodeObject(headerPart, header) ||
        !sg_TryDecodeObject(payloadPart, payload) ||
        !gPlatformBase64Url::TryDecode(signaturePart, signature) || signature.size() != 64)
        return gPlatformTokenError::Malformed;

    std::string alg;
    std::string typ;
    gPlatformLoginClaims read;
    if (!sg_TryGetString(header, "alg", alg) ||
        !sg_TryGetString(header, "typ", typ) || typ != "rc-login+jws" ||
        !sg_TryGetString(header, "kid", read.kid))
        return gPlatformTokenError::Malformed;

    if (!sg_TryGetString(payload, "iss", read.iss) ||
        !sg_TryGetString(payload, "sub", read.sub) ||
        !sg_TryGetString(payload, "gid", read.gid) ||
        !sg_TryGetString(payload, "name", read.name) ||
        !sg_TryGetString(payload, "aud", read.aud) ||
        !sg_TryGetInteger(payload, "iat", read.iat) ||
        !sg_TryGetInteger(payload, "exp", read.exp) ||
        !sg_TryGetString(payload, "jti", read.jti) ||
        !sg_TryGetString(payload, "roles", read.roles))
        return gPlatformTokenError::Malformed;

    // rule 2: algorithm and key
    if (alg != "EdDSA")
        return gPlatformTokenError::UnsupportedAlgorithm;
    gPlatformKey const* key = keys.Find(read.kid);
    if (!key)
        return gPlatformTokenError::UnknownKey;

    // rule 3: Ed25519 over the ASCII bytes of header "." payload
    std::string const signingInput = token.substr(0, secondDot);
    if (crypto_ed25519_check(signature.data(), key->publicKey.data(),
                             reinterpret_cast<std::uint8_t const*>(signingInput.data()),
                             signingInput.size()) != 0)
        return gPlatformTokenError::BadSignature;

    // rule 4: bound to this server
    if (read.aud != serverId)
        return gPlatformTokenError::WrongAudience;

    // rule 5: time window
    if (read.exp < now - ExpirySkew)
        return gPlatformTokenError::Expired;
    if (read.iat > now + IssuedAtSkew)
        return gPlatformTokenError::NotYetValid;

    // rule 6: single use
    jtis.Prune(now);
    if (jtis.Contains(read.jti))
        return gPlatformTokenError::Replayed;

    // rule 7: our authority, a handle before it
    std::size_t const at = read.gid.rfind('@');
    if (at == std::string::npos || at == 0 || read.gid.compare(at + 1, std::string::npos, authority) != 0)
        return gPlatformTokenError::ForeignAuthority;

    jtis.Remember(read.jti, read.exp + ExpirySkew);
    claims = read;
    return gPlatformTokenError::None;
}

gPlatformTokenError gPlatformRoomTicket::Verify(std::string const& ticket,
                                                gPlatformKeySet const& keys,
                                                std::string const& serverId,
                                                std::int64_t now,
                                                gPlatformJtiCache& jtis,
                                                gPlatformRoomTicketClaims& claims)
{
    claims = gPlatformRoomTicketClaims{};

    // rule 1: size, three parts, JSON objects of the right shape
    if (ticket.size() > gPlatformLoginToken::MaxLength)
        return gPlatformTokenError::Malformed;

    std::size_t const firstDot = ticket.find('.');
    if (firstDot == std::string::npos)
        return gPlatformTokenError::Malformed;
    std::size_t const secondDot = ticket.find('.', firstDot + 1);
    if (secondDot == std::string::npos || ticket.find('.', secondDot + 1) != std::string::npos)
        return gPlatformTokenError::Malformed;

    nlohmann::json header;
    nlohmann::json payload;
    std::vector<std::uint8_t> signature;
    if (!sg_TryDecodeObject(ticket.substr(0, firstDot), header) ||
        !sg_TryDecodeObject(ticket.substr(firstDot + 1, secondDot - firstDot - 1), payload) ||
        !gPlatformBase64Url::TryDecode(ticket.substr(secondDot + 1), signature) || signature.size() != 64)
        return gPlatformTokenError::Malformed;

    // a login token is never a ticket, nor the other way round: typ tells them apart
    std::string alg;
    std::string typ;
    gPlatformRoomTicketClaims read;
    if (!sg_TryGetString(header, "alg", alg) ||
        !sg_TryGetString(header, "typ", typ) || typ != "rc-room+jws" ||
        !sg_TryGetString(header, "kid", read.kid))
        return gPlatformTokenError::Malformed;

    if (!sg_TryGetString(payload, "iss", read.iss) ||
        !sg_TryGetString(payload, "aud", read.aud) ||
        !sg_TryGetString(payload, "claim", read.claim) ||
        !sg_TryGetInteger(payload, "iat", read.iat) ||
        !sg_TryGetInteger(payload, "exp", read.exp) ||
        !sg_TryGetString(payload, "jti", read.jti))
        return gPlatformTokenError::Malformed;

    // rule 2: algorithm and key
    if (alg != "EdDSA")
        return gPlatformTokenError::UnsupportedAlgorithm;
    gPlatformKey const* key = keys.Find(read.kid);
    if (!key)
        return gPlatformTokenError::UnknownKey;

    // rule 3: Ed25519 over the ASCII bytes of header "." payload
    std::string const signingInput = ticket.substr(0, secondDot);
    if (crypto_ed25519_check(signature.data(), key->publicKey.data(),
                             reinterpret_cast<std::uint8_t const*>(signingInput.data()),
                             signingInput.size()) != 0)
        return gPlatformTokenError::BadSignature;

    // rule 4: bound to this server
    if (read.aud != serverId)
        return gPlatformTokenError::WrongAudience;

    // rule 5: time window
    if (read.exp < now - gPlatformLoginToken::ExpirySkew)
        return gPlatformTokenError::Expired;
    if (read.iat > now + gPlatformLoginToken::IssuedAtSkew)
        return gPlatformTokenError::NotYetValid;

    // rule 6: single use
    jtis.Prune(now);
    if (jtis.Contains(read.jti))
        return gPlatformTokenError::Replayed;

    jtis.Remember(read.jti, read.exp + gPlatformLoginToken::ExpirySkew);
    claims = read;
    return gPlatformTokenError::None;
}

char const* gPlatformLoginToken::Describe(gPlatformTokenError error) noexcept
{
    switch (error)
    {
    case gPlatformTokenError::None:
        return "accepted";
    case gPlatformTokenError::Malformed:
        return "malformed token";
    case gPlatformTokenError::UnsupportedAlgorithm:
        return "unsupported algorithm";
    case gPlatformTokenError::UnknownKey:
        return "unknown kid";
    case gPlatformTokenError::BadSignature:
        return "signature does not verify";
    case gPlatformTokenError::WrongAudience:
        return "aud != serverId";
    case gPlatformTokenError::Expired:
        return "expired";
    case gPlatformTokenError::NotYetValid:
        return "issued in the future";
    case gPlatformTokenError::Replayed:
        return "jti already used";
    case gPlatformTokenError::ForeignAuthority:
        return "gid authority != configured authority";
    }
    return "unknown error";
}
