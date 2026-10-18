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

// gPlatformToken.h: offline verification of platform login tokens (Ed25519 JWS),
// platform-contract/README.md section 2. Only built with --enable-platform.
// Pure functions: no network, no clock, no global state.

#ifndef ArmageTron_GPLATFORMTOKEN_H
#define ArmageTron_GPLATFORMTOKEN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// base64url (RFC 4648 section 5) without padding
class gPlatformBase64Url
{
public:
    // strict: rejects padding, characters outside the alphabet, impossible lengths
    // and non-zero trailing bits, so every byte string has exactly one encoding
    static bool TryDecode(std::string const& in, std::vector<std::uint8_t>& out);

    static std::string Encode(std::uint8_t const* data, std::size_t len);
};

// one Ed25519 public key of the platform
struct gPlatformKey
{
    std::string kid{};
    std::array<std::uint8_t, 32> publicKey{};
};

// the platform's login token keys (GET /v1/auth/keys)
class gPlatformKeySet
{
public:
    // parses the key endpoint's JSON; keys that are not EdDSA/Ed25519 with a 32 byte x
    // are skipped. False (with a reason) if the document itself is unusable.
    static bool TryParse(std::string const& json, gPlatformKeySet& keys, std::string& error);

    gPlatformKey const* Find(std::string const& kid) const;

    void Add(gPlatformKey const& key) { keys_.push_back(key); }
    std::size_t Size() const noexcept { return keys_.size(); }

private:
    std::vector<gPlatformKey> keys_{};
};

// the jtis of accepted tokens, each remembered until its token's exp + 5
class gPlatformJtiCache
{
public:
    bool Contains(std::string const& jti) const { return entries_.find(jti) != entries_.end(); }
    void Remember(std::string const& jti, std::int64_t until) { entries_[jti] = until; }

    // forgets entries whose time ran out before now
    void Prune(std::int64_t now);

    std::size_t Size() const noexcept { return entries_.size(); }

private:
    std::map<std::string, std::int64_t> entries_{};
};

// why a token was refused, in the order the contract's rules are checked
enum class gPlatformTokenError
{
    None,
    Malformed,            // rule 1: length, parts, encoding, JSON shape
    UnsupportedAlgorithm, // rule 2: alg != EdDSA
    UnknownKey,           // rule 2: kid not in the key set
    BadSignature,         // rule 3
    WrongAudience,        // rule 4: aud != serverId
    Expired,              // rule 5: exp < now - 5
    NotYetValid,          // rule 5: iat > now + 30
    Replayed,             // rule 6: jti accepted before
    ForeignAuthority      // rule 7: gid authority != configured authority, or empty handle
};

// what an accepted token says
struct gPlatformLoginClaims
{
    std::string kid{};
    std::string iss{};
    std::string sub{};
    std::string gid{};
    std::string name{};
    std::string aud{};
    std::int64_t iat{};
    std::int64_t exp{};
    std::string jti{};
    std::string roles{};
};

class gPlatformLoginToken
{
public:
    static std::size_t const MaxLength = 2048;
    static std::int64_t const ExpirySkew = 5;     // seconds a token is still accepted after exp
    static std::int64_t const IssuedAtSkew = 30;  // seconds iat may lie in the future

    // checks rules 1 to 7 in order; on success remembers the jti and fills claims
    static gPlatformTokenError Verify(std::string const& token,
                                      gPlatformKeySet const& keys,
                                      std::string const& serverId,
                                      std::string const& authority,
                                      std::int64_t now,
                                      gPlatformJtiCache& jtis,
                                      gPlatformLoginClaims& claims);

    // a short English reason for logs
    static char const* Describe(gPlatformTokenError error) noexcept;
};

// what an accepted room ticket says (contract section 2, "Room tickets")
struct gPlatformRoomTicketClaims
{
    std::string kid{};
    std::string iss{};
    std::string aud{};
    std::string claim{};
    std::int64_t iat{};
    std::int64_t exp{};
    std::string jti{};
};

// A room ticket lets anyone holding a private room's link in, signed in or not.
// Same keys and JWS shape as a login token, typ "rc-room+jws", no player identity.
class gPlatformRoomTicket
{
public:
    // login token rules 1 to 6 in order (rule 7 has no gid to check); on success remembers
    // the jti and fills claims
    static gPlatformTokenError Verify(std::string const& ticket,
                                      gPlatformKeySet const& keys,
                                      std::string const& serverId,
                                      std::int64_t now,
                                      gPlatformJtiCache& jtis,
                                      gPlatformRoomTicketClaims& claims);
};

#endif // ArmageTron_GPLATFORMTOKEN_H
