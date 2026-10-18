#include "config.h"

#include <nlohmann/json.hpp>
#include "monocypher-ed25519.h"

#include "gPlatformToken.h"

#include "doctest.h"

#include <fstream>
#include <sstream>

// Tests for platform login token verification (platform-contract/README.md section 2),
// driven by the contract's test vectors. Only built with --enable-platform.

namespace
{
std::string const s_serverId = "dev-local-1";
std::string const s_authority = "retrocycles";
std::int64_t const s_now = 1790000030;

std::vector<std::uint8_t> Decode(std::string const& text)
{
    std::vector<std::uint8_t> out;
    REQUIRE(gPlatformBase64Url::TryDecode(text, out));
    return out;
}

std::string EncodeText(std::string const& text)
{
    return gPlatformBase64Url::Encode(reinterpret_cast<std::uint8_t const*>(text.data()), text.size());
}

bool TryDecodes(std::string const& text)
{
    std::vector<std::uint8_t> out;
    return gPlatformBase64Url::TryDecode(text, out);
}

nlohmann::json LoadVectors()
{
    std::ifstream file(std::string(PLATFORM_CONTRACT_DIR) + "/test-vectors/login-token-v0.json");
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    return nlohmann::json::parse(text.str());
}

// the key of the test vectors, as the key endpoint would publish it
gPlatformKeySet VectorKeys(nlohmann::json const& vectors)
{
    nlohmann::json keys;
    keys["keys"] = nlohmann::json::array({vectors["key"]});
    gPlatformKeySet set;
    std::string error;
    REQUIRE(gPlatformKeySet::TryParse(keys.dump(), set, error));
    REQUIRE(1 == set.Size());
    return set;
}

// signs tokens with the test vectors' private key, for cases the vectors do not cover
class cTokenMinter
{
public:
    explicit cTokenMinter(nlohmann::json const& vectors)
    {
        std::vector<std::uint8_t> seed = Decode(vectors["key"]["d_TEST_ONLY"].get<std::string>());
        REQUIRE(32 == seed.size());
        crypto_ed25519_key_pair(secretKey_, publicKey_, seed.data());
    }

    std::uint8_t const* PublicKey() const { return publicKey_; }

    std::string Sign(std::string const& header, std::string const& payload) const
    {
        std::string const input = EncodeText(header) + "." + EncodeText(payload);
        std::uint8_t signature[64];
        crypto_ed25519_sign(signature, secretKey_, reinterpret_cast<std::uint8_t const*>(input.data()), input.size());
        return input + "." + gPlatformBase64Url::Encode(signature, sizeof(signature));
    }

    static nlohmann::json Header()
    {
        return nlohmann::json{{"alg", "EdDSA"}, {"typ", "rc-login+jws"}, {"kid", "test-2026-09"}};
    }

    static nlohmann::json Payload(std::string const& jti)
    {
        return nlohmann::json{{"iss", "retrocycles"}, {"sub", "u_test_0009"}, {"gid", "g-00000009@retrocycles"},
                              {"name", "Minted"}, {"aud", s_serverId}, {"iat", s_now - 30},
                              {"exp", s_now + 30}, {"jti", jti}, {"roles", ""}};
    }

    std::string Sign(nlohmann::json const& header, nlohmann::json const& payload) const { return Sign(header.dump(), payload.dump()); }

private:
    std::uint8_t secretKey_[64]{};
    std::uint8_t publicKey_[32]{};
};

gPlatformTokenError Verify(std::string const& token, gPlatformKeySet const& keys, gPlatformJtiCache& jtis, std::int64_t now = s_now)
{
    gPlatformLoginClaims claims;
    return gPlatformLoginToken::Verify(token, keys, s_serverId, s_authority, now, jtis, claims);
}

// maps the vectors' "reason" texts to our errors
gPlatformTokenError ExpectedError(std::string const& reason)
{
    if (reason == "jti already used")
        return gPlatformTokenError::Replayed;
    if (reason.find("exp < now") == 0)
        return gPlatformTokenError::Expired;
    if (reason == "aud != serverId")
        return gPlatformTokenError::WrongAudience;
    if (reason == "gid authority != configured authority")
        return gPlatformTokenError::ForeignAuthority;
    if (reason == "signature does not verify")
        return gPlatformTokenError::BadSignature;
    if (reason == "unknown kid")
        return gPlatformTokenError::UnknownKey;
    FAIL("unknown reason in the test vectors: " << reason);
    return gPlatformTokenError::None;
}
} // namespace

TEST_SUITE("gPlatformBase64Url")
{
    TEST_CASE("Round Trips")
    {
        for (std::string const text : {"", "f", "fo", "foo", "foob", "fooba", "foobar"})
        {
            std::string const encoded = EncodeText(text);
            std::vector<std::uint8_t> const decoded = Decode(encoded);
            CHECK(text == std::string(decoded.begin(), decoded.end()));
        }
        // RFC 4648 test vectors, without padding
        CHECK("Zm9vYmFy" == EncodeText("foobar"));
        CHECK("Zm9vYg" == EncodeText("foob"));
        CHECK("Zg" == EncodeText("f"));
    }

    TEST_CASE("The URL Alphabet Is Used")
    {
        std::uint8_t const bytes[] = {0xfb, 0xff, 0xbf};
        CHECK("-_-_" == gPlatformBase64Url::Encode(bytes, sizeof(bytes)));
        std::vector<std::uint8_t> const decoded = Decode("-_-_");
        CHECK(std::vector<std::uint8_t>(bytes, bytes + 3) == decoded);
    }

    TEST_CASE("Decoding Is Strict")
    {
        CHECK(!TryDecodes("Zg=="));   // padding
        CHECK(!TryDecodes("Zm9v="));  // padding
        CHECK(!TryDecodes("+/+/"));   // standard alphabet
        CHECK(!TryDecodes("Zm9 v"));  // whitespace
        CHECK(!TryDecodes("Zm9v\n")); // newline
        CHECK(!TryDecodes("Z"));      // impossible length
        CHECK(!TryDecodes("Zm9vY"));  // impossible length
        CHECK(!TryDecodes("Zh"));     // "f" with non-zero trailing bits (canonical: "Zg")
        CHECK(!TryDecodes("Zm9"));    // "fo" with non-zero trailing bits (canonical: "Zm8")
        CHECK(TryDecodes("Zm8"));
    }
}

TEST_SUITE("gPlatformKeySet")
{
    TEST_CASE("Only Ed25519 Keys With 32 Byte X Are Taken")
    {
        std::string const json = R"({"keys":[
            {"kid":"good","alg":"EdDSA","crv":"Ed25519","x":"CE3yncjveytbx7-YWdWvpxrf1ORyq8l8ktSE87ngPtQ"},
            {"kid":"rsa","alg":"RS256","crv":"Ed25519","x":"CE3yncjveytbx7-YWdWvpxrf1ORyq8l8ktSE87ngPtQ"},
            {"kid":"x448","alg":"EdDSA","crv":"Ed448","x":"CE3yncjveytbx7-YWdWvpxrf1ORyq8l8ktSE87ngPtQ"},
            {"kid":"short","alg":"EdDSA","crv":"Ed25519","x":"CE3ync"},
            {"kid":"padded","alg":"EdDSA","crv":"Ed25519","x":"CE3yncjveytbx7-YWdWvpxrf1ORyq8l8ktSE87ngPtQ="},
            "garbage"]})";
        gPlatformKeySet keys;
        std::string error;
        REQUIRE(gPlatformKeySet::TryParse(json, keys, error));
        CHECK(1 == keys.Size());
        REQUIRE(keys.Find("good"));
        CHECK(0x08 == keys.Find("good")->publicKey[0]);
        CHECK(!keys.Find("rsa"));
    }

    TEST_CASE("Unusable Documents Are Refused")
    {
        gPlatformKeySet keys;
        std::string error;
        CHECK(!gPlatformKeySet::TryParse("not json", keys, error));
        CHECK(!gPlatformKeySet::TryParse("[]", keys, error));
        CHECK(!gPlatformKeySet::TryParse(R"({"keys":{}})", keys, error));
        CHECK(!error.empty());
        CHECK(gPlatformKeySet::TryParse(R"({"keys":[]})", keys, error));
        CHECK(0 == keys.Size());
    }
}

TEST_SUITE("gPlatformLoginToken")
{
    TEST_CASE("Contract Test Vectors")
    {
        nlohmann::json const vectors = LoadVectors();
        REQUIRE(s_serverId == vectors["serverId"].get<std::string>());
        REQUIRE(s_authority == vectors["authority"].get<std::string>());

        gPlatformKeySet const keys = VectorKeys(vectors);
        gPlatformJtiCache jtis; // shared: the replay case follows the valid one
        std::string validToken;
        int cases = 0;

        for (auto const& vector : vectors["cases"])
        {
            std::string const name = vector["name"].get<std::string>();
            CAPTURE(name);

            std::string token = vector["token"].get<std::string>();
            if (token == "SAME_AS:valid")
                token = validToken;
            REQUIRE(!token.empty());

            std::int64_t const now = vector.contains("now") ? vector["now"].get<std::int64_t>() : s_now;
            std::string const serverId = vector.contains("serverId") ? vector["serverId"].get<std::string>() : s_serverId;
            gPlatformKeySet const caseKeys = vector.contains("keys") ? gPlatformKeySet{} : keys;
            if (vector.contains("keys"))
                REQUIRE(vector["keys"].empty());

            gPlatformLoginClaims claims;
            gPlatformTokenError const error = gPlatformLoginToken::Verify(token, caseKeys, serverId, s_authority, now, jtis, claims);

            if (vector["expect"] == "accept")
            {
                CHECK(gPlatformTokenError::None == error);
                CHECK(vector["gid"].get<std::string>() == claims.gid);
                CHECK(vector["sub"].get<std::string>() == claims.sub);
                CHECK("Tester" == claims.name);
                CHECK("" == claims.roles);
                CHECK(1790000060 == claims.exp);
                if (name == "valid")
                    validToken = token;
            }
            else
            {
                CHECK(ExpectedError(vector["reason"].get<std::string>()) == error);
                CHECK(claims.gid.empty());
            }
            ++cases;
        }

        CHECK(8 == cases);
        CHECK(1 == jtis.Size());
    }

    TEST_CASE("The Vector Key Pair Is Consistent")
    {
        nlohmann::json const vectors = LoadVectors();
        cTokenMinter const minter(vectors);
        gPlatformKeySet const keys = VectorKeys(vectors);
        CHECK(std::equal(keys.Find("test-2026-09")->publicKey.begin(), keys.Find("test-2026-09")->publicKey.end(), minter.PublicKey()));
    }

    TEST_CASE("Rules Beyond The Vectors")
    {
        nlohmann::json const vectors = LoadVectors();
        cTokenMinter const minter(vectors);
        gPlatformKeySet const keys = VectorKeys(vectors);
        gPlatformJtiCache jtis;
        auto const header = cTokenMinter::Header();

        SUBCASE("a freshly minted token is accepted once")
        {
            std::string const token = minter.Sign(header, cTokenMinter::Payload("m1"));
            CHECK(gPlatformTokenError::None == Verify(token, keys, jtis));
            CHECK(gPlatformTokenError::Replayed == Verify(token, keys, jtis));
        }
        SUBCASE("the replay memory ends 5 s after exp")
        {
            std::string const token = minter.Sign(header, cTokenMinter::Payload("m2"));
            CHECK(gPlatformTokenError::None == Verify(token, keys, jtis));
            jtis.Prune(s_now + 30 + 5 + 1);
            CHECK(0 == jtis.Size());
        }
        SUBCASE("the time window has 5 s of expiry skew and 30 s for iat")
        {
            auto payload = cTokenMinter::Payload("m3");
            std::string const token = minter.Sign(header, payload);
            CHECK(gPlatformTokenError::Expired == Verify(token, keys, jtis, s_now + 30 + 6));
            CHECK(gPlatformTokenError::None == Verify(token, keys, jtis, s_now + 30 + 5));

            payload["jti"] = "m4";
            payload["iat"] = s_now + 31;
            payload["exp"] = s_now + 91;
            CHECK(gPlatformTokenError::NotYetValid == Verify(minter.Sign(header, payload), keys, jtis));
            payload["iat"] = s_now + 30;
            CHECK(gPlatformTokenError::None == Verify(minter.Sign(header, payload), keys, jtis));
        }
        SUBCASE("the algorithm must be EdDSA")
        {
            auto badHeader = header;
            badHeader["alg"] = "none";
            CHECK(gPlatformTokenError::UnsupportedAlgorithm == Verify(minter.Sign(badHeader, cTokenMinter::Payload("m5")), keys, jtis));
        }
        SUBCASE("the header must have the login token type")
        {
            auto badHeader = header;
            badHeader["typ"] = "JWT";
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(badHeader, cTokenMinter::Payload("m6")), keys, jtis));
        }
        SUBCASE("every payload member is required, with its type")
        {
            for (char const* member : {"iss", "sub", "gid", "name", "aud", "iat", "exp", "jti", "roles"})
            {
                CAPTURE(member);
                auto payload = cTokenMinter::Payload("m7");
                payload.erase(member);
                CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header, payload), keys, jtis));
            }
            auto payload = cTokenMinter::Payload("m8");
            payload["exp"] = static_cast<double>(s_now) + 30.5;
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header, payload), keys, jtis));
            payload = cTokenMinter::Payload("m8");
            payload["exp"] = std::to_string(s_now + 30);
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header, payload), keys, jtis));
            payload = cTokenMinter::Payload("m8");
            payload["roles"] = nlohmann::json::array({"moderator"});
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header, payload), keys, jtis));
        }
        SUBCASE("unknown members are ignored")
        {
            auto payload = cTokenMinter::Payload("m9");
            payload["future"] = "whatever";
            auto extendedHeader = header;
            extendedHeader["x5u"] = "https://example.invalid/";
            CHECK(gPlatformTokenError::None == Verify(minter.Sign(extendedHeader, payload), keys, jtis));
        }
        SUBCASE("the gid needs a handle and our authority after the last @")
        {
            auto payload = cTokenMinter::Payload("m10");
            payload["gid"] = "@retrocycles";
            CHECK(gPlatformTokenError::ForeignAuthority == Verify(minter.Sign(header, payload), keys, jtis));
            payload["gid"] = "g-1@retrocycles.evil";
            CHECK(gPlatformTokenError::ForeignAuthority == Verify(minter.Sign(header, payload), keys, jtis));
            payload["gid"] = "no-authority";
            CHECK(gPlatformTokenError::ForeignAuthority == Verify(minter.Sign(header, payload), keys, jtis));
            payload["gid"] = "odd@name@retrocycles";
            CHECK(gPlatformTokenError::None == Verify(minter.Sign(header, payload), keys, jtis));
        }
        SUBCASE("rejected tokens do not use up their jti")
        {
            auto payload = cTokenMinter::Payload("m11");
            payload["aud"] = "other-server";
            CHECK(gPlatformTokenError::WrongAudience == Verify(minter.Sign(header, payload), keys, jtis));
            CHECK(0 == jtis.Size());
        }
        SUBCASE("structure")
        {
            std::string const token = minter.Sign(header, cTokenMinter::Payload("m12"));
            CHECK(gPlatformTokenError::Malformed == Verify(token + ".x", keys, jtis));
            CHECK(gPlatformTokenError::Malformed == Verify(token.substr(0, token.rfind('.')), keys, jtis));
            CHECK(gPlatformTokenError::Malformed == Verify(token + "=", keys, jtis));
            CHECK(gPlatformTokenError::Malformed == Verify("", keys, jtis));
            CHECK(gPlatformTokenError::Malformed == Verify("..", keys, jtis));
            // a truncated signature
            CHECK(gPlatformTokenError::Malformed == Verify(token.substr(0, token.size() - 4), keys, jtis));
            // the payload is not an object
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header.dump(), "[1,2]"), keys, jtis));
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header.dump(), "{"), keys, jtis));
        }
        SUBCASE("tokens longer than 2048 bytes are refused before anything else")
        {
            auto payload = cTokenMinter::Payload("m13");
            payload["padding"] = std::string(2000, 'x');
            std::string const token = minter.Sign(header, payload);
            REQUIRE(token.size() > gPlatformLoginToken::MaxLength);
            CHECK(gPlatformTokenError::Malformed == Verify(token, keys, jtis));
        }
    }
}

TEST_SUITE("gPlatformRoomTicket")
{
    TEST_CASE("Room Tickets Are Verified Like Login Tokens, Without A Player")
    {
        nlohmann::json const vectors = LoadVectors();
        gPlatformKeySet const keys = VectorKeys(vectors);
        cTokenMinter const minter(vectors);
        gPlatformJtiCache jtis;

        nlohmann::json const header{{"alg", "EdDSA"}, {"typ", "rc-room+jws"}, {"kid", "test-2026-09"}};
        auto const payload = [](std::string const& jti) {
            return nlohmann::json{{"iss", "retrocycles"}, {"aud", s_serverId}, {"claim", "pc_test"},
                                  {"iat", s_now - 30}, {"exp", s_now + 30}, {"jti", jti}};
        };
        auto const verify = [&](std::string const& ticket, std::int64_t now = s_now) {
            gPlatformRoomTicketClaims claims;
            return gPlatformRoomTicket::Verify(ticket, keys, s_serverId, now, jtis, claims);
        };

        SUBCASE("a good ticket is accepted once and says its claim")
        {
            std::string const ticket = minter.Sign(header, payload("t1"));
            gPlatformRoomTicketClaims claims;
            CHECK(gPlatformTokenError::None == gPlatformRoomTicket::Verify(ticket, keys, s_serverId, s_now, jtis, claims));
            CHECK("pc_test" == claims.claim);
            CHECK(gPlatformTokenError::Replayed == verify(ticket));
        }
        SUBCASE("another room's ticket")
        {
            auto other = payload("t2");
            other["aud"] = "other-server";
            CHECK(gPlatformTokenError::WrongAudience == verify(minter.Sign(header, other)));
        }
        SUBCASE("an old ticket")
        {
            CHECK(gPlatformTokenError::Expired == verify(minter.Sign(header, payload("t3")), s_now + 60));
        }
        SUBCASE("a forged ticket")
        {
            std::string ticket = minter.Sign(header, payload("t4"));
            ticket[ticket.size() - 6] = ticket[ticket.size() - 6] == 'A' ? 'B' : 'A';
            CHECK(gPlatformTokenError::BadSignature == verify(ticket));
        }
        SUBCASE("a login token is not a ticket, and a ticket is not a login token")
        {
            CHECK(gPlatformTokenError::Malformed == verify(minter.Sign(cTokenMinter::Header(), cTokenMinter::Payload("t5"))));
            auto loginShaped = cTokenMinter::Payload("t6");
            loginShaped["claim"] = "pc_test";
            CHECK(gPlatformTokenError::Malformed == Verify(minter.Sign(header, loginShaped), keys, jtis));
        }
        SUBCASE("a ticket without a claim")
        {
            auto bare = payload("t7");
            bare.erase("claim");
            CHECK(gPlatformTokenError::Malformed == verify(minter.Sign(header, bare)));
        }
        SUBCASE("nothing")
        {
            CHECK(gPlatformTokenError::Malformed == verify(""));
        }
    }
}
