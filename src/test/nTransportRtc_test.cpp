#include "nTransportRtcSupport.h"
#include "nTransport.h"

#include "doctest.h"

#include <cstdio>
#include <cstring>
#include <set>

// Tests for the libdatachannel independent parts of the WebRTC transport:
// signalling message coding, host candidate rewriting, connection IDs and
// client address attribution.
// The transport itself needs a network and is therefore not tested here.

namespace
{
// a minimal answer SDP with one host candidate, shortened but structurally real
char const* const testSdp =
    "v=0\r\n"
    "o=rtc 1 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=mid:0\r\n"
    "a=candidate:1 1 UDP 2122317823 10.1.2.3 51234 typ host\r\n"
    "a=candidate:2 1 UDP 1686110207 203.0.113.9 51234 typ srflx raddr 10.1.2.3 rport 51234\r\n"
    "a=end-of-candidates\r\n";
}

TEST_SUITE("nTransportRtc Signalling")
{
    TEST_CASE("Offers Are Parsed")
    {
        GIVEN("an offer as a browser sends it")
        {
            nRtcSignal signal;
            REQUIRE(nRtcTryParseSignal("{\"type\":\"offer\",\"sdp\":\"v=0\\r\\no=- 1 2 IN IP4 0.0.0.0\\r\\n\"}", signal));

            THEN("type and SDP come out, with the escapes resolved")
            {
                CHECK(signal.type == "offer");
                CHECK(signal.sdp == "v=0\r\no=- 1 2 IN IP4 0.0.0.0\r\n");
            }
        }

        GIVEN("a trickled candidate")
        {
            nRtcSignal signal;
            REQUIRE(nRtcTryParseSignal(
                "{ \"type\" : \"candidate\", \"candidate\" : \"candidate:1 1 UDP 1 10.0.0.1 1234 typ host\", \"mid\": \"0\" }",
                signal));

            THEN("candidate and mid come out")
            {
                CHECK(signal.candidate == "candidate:1 1 UDP 1 10.0.0.1 1234 typ host");
                CHECK(signal.mid == "0");
                CHECK(signal.sdp.empty());
            }
        }

        GIVEN("members that are not strings")
        {
            nRtcSignal signal;
            REQUIRE(nRtcTryParseSignal("{\"id\":17,\"nested\":{\"a\":\"b\"},\"type\":\"offer\",\"sdp\":\"v=0\"}", signal));

            THEN("they are stepped over instead of confusing the parser")
            {
                CHECK(signal.type == "offer");
                CHECK(signal.sdp == "v=0");
            }
        }

        GIVEN("malformed or incomplete messages")
        {
            nRtcSignal signal;

            THEN("they are all rejected")
            {
                CHECK(!nRtcTryParseSignal("", signal));
                CHECK(!nRtcTryParseSignal("not json", signal));
                CHECK(!nRtcTryParseSignal("{\"type\":\"offer\"}", signal));            // no sdp
                CHECK(!nRtcTryParseSignal("{\"type\":\"candidate\",\"mid\":\"0\"}", signal)); // no candidate
                CHECK(!nRtcTryParseSignal("{\"sdp\":\"v=0\"}", signal));               // no type
                CHECK(!nRtcTryParseSignal("{\"type\":\"bogus\",\"sdp\":\"v=0\"}", signal));
                CHECK(!nRtcTryParseSignal("{\"type\":\"offer\",\"sdp\":\"unterminated}", signal));
            }
        }
    }

    TEST_CASE("The WebSocket Transport Messages")
    {
        GIVEN("a browser asking for its game packets on the signalling socket")
        {
            nRtcSignal signal;
            REQUIRE(nRtcTryParseSignal("{\"type\":\"websocket\"}", signal));

            THEN("it is a request of its own, with nothing else in it")
            {
                CHECK(signal.type == "websocket");
                CHECK(signal.sdp.empty());
            }
        }

        GIVEN("the server's agreement and its refusal")
        {
            nRtcSignal agree;
            agree.type = "websocket";
            nRtcSignal refuse;
            refuse.type = "refused";
            refuse.reason = "not \"here\"";

            THEN("the agreement is the bare type, the refusal carries its reason")
            {
                CHECK(nRtcFormatSignal(agree) == "{\"type\":\"websocket\"}");
                CHECK(nRtcFormatSignal(refuse) == "{\"type\":\"refused\",\"reason\":\"not \\\"here\\\"\"}");
            }
            AND_THEN("both parse back")
            {
                nRtcSignal back;
                REQUIRE(nRtcTryParseSignal(nRtcFormatSignal(refuse), back));
                CHECK(back.type == "refused");
                CHECK(back.reason == refuse.reason);
                REQUIRE(nRtcTryParseSignal(nRtcFormatSignal(agree), back));
                CHECK(back.type == "websocket");
                CHECK(back.reason.empty());
            }
        }
    }

    TEST_CASE("Answers Are Formatted")
    {
        GIVEN("an answer with a multi line SDP")
        {
            nRtcSignal answer;
            answer.type = "answer";
            answer.sdp = "v=0\r\na=mid:0\r\n";

            std::string const json = nRtcFormatSignal(answer);

            THEN("the line breaks are escaped")
            {
                CHECK(json == "{\"type\":\"answer\",\"sdp\":\"v=0\\r\\na=mid:0\\r\\n\"}");
            }
            AND_THEN("it parses back to what went in")
            {
                nRtcSignal back;
                REQUIRE(nRtcTryParseSignal(json, back));
                CHECK(back.type == answer.type);
                CHECK(back.sdp == answer.sdp);
            }
        }

        GIVEN("a candidate with characters that need escaping")
        {
            nRtcSignal candidate;
            candidate.type = "candidate";
            candidate.candidate = "candidate:1 \"quoted\\backslash\"";
            candidate.mid = "0";

            nRtcSignal back;
            REQUIRE(nRtcTryParseSignal(nRtcFormatSignal(candidate), back));

            THEN("the round trip is lossless")
            {
                CHECK(back.candidate == candidate.candidate);
                CHECK(back.mid == "0");
            }
        }
    }

    TEST_CASE("Host Candidates Are Rewritten For One To One NAT")
    {
        GIVEN("an SDP and a public IP")
        {
            std::string const rewritten = nRtcRewriteHostCandidates(testSdp, "198.51.100.7");

            THEN("the host candidate points at the public address")
            {
                CHECK(rewritten.find("a=candidate:1 1 UDP 2122317823 198.51.100.7 51234 typ host") != std::string::npos);
            }
            AND_THEN("candidates of other types are left alone")
            {
                CHECK(rewritten.find("203.0.113.9 51234 typ srflx raddr 10.1.2.3") != std::string::npos);
            }
            AND_THEN("the rest of the session description survives untouched")
            {
                CHECK(rewritten.find("m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n") != std::string::npos);
                CHECK(rewritten.find("a=end-of-candidates\r\n") != std::string::npos);
                CHECK(rewritten.size() == std::string(testSdp).size() + 4);
            }
        }

        GIVEN("no public IP configured")
        {
            THEN("the SDP is handed back as it is")
            {
                CHECK(nRtcRewriteHostCandidates(testSdp, "") == testSdp);
            }
        }

        GIVEN("a bare candidate line, as trickling sends it")
        {
            std::string rewritten;
            REQUIRE(nRtcTryRewriteHostCandidate("candidate:1 1 UDP 100 10.0.0.1 40000 typ host", "198.51.100.7",
                                                rewritten));

            THEN("it is rewritten without the a= prefix")
            {
                CHECK(rewritten == "candidate:1 1 UDP 100 198.51.100.7 40000 typ host");
            }
        }

        GIVEN("lines that are not host candidates")
        {
            std::string rewritten;

            THEN("they are refused")
            {
                CHECK(!nRtcTryRewriteHostCandidate("a=mid:0", "198.51.100.7", rewritten));
                CHECK(!nRtcTryRewriteHostCandidate("a=candidate:1 1 UDP 100 10.0.0.1 40000 typ srflx",
                                                   "198.51.100.7", rewritten));
                // IPv6 host candidates have no business being rewritten to an IPv4
                CHECK(!nRtcTryRewriteHostCandidate("a=candidate:1 1 UDP 100 fe80::1 40000 typ host",
                                                   "198.51.100.7", rewritten));
            }
        }
    }
}

TEST_SUITE("nTransportRtc Connection IDs")
{
    TEST_CASE("Connection IDs Are Reused Only After A Grace Period")
    {
        GIVEN("a pool with a grace of two")
        {
            nRtcConnectionIds ids(2);
            unsigned short id = 0;

            THEN("it starts at one, so the ID is never the unset port")
            {
                REQUIRE(ids.TryAllocate(id));
                CHECK(1 == id);
                REQUIRE(ids.TryAllocate(id));
                CHECK(2 == id);
                REQUIRE(ids.TryAllocate(id));
                CHECK(3 == id);
            }
        }

        GIVEN("a pool with three IDs out and the first one released")
        {
            nRtcConnectionIds ids(2);
            unsigned short id = 0;
            for (int i = 0; i < 3; ++i)
                REQUIRE(ids.TryAllocate(id));

            ids.Release(2);

            THEN("the next allocation is fresh, not the just closed ID")
            {
                REQUIRE(ids.TryAllocate(id));
                CHECK(4 == id);
            }
            AND_THEN("it comes back once two further connections have closed")
            {
                ids.Release(1);
                ids.Release(3);

                REQUIRE(ids.TryAllocate(id));
                CHECK(2 == id);
            }
        }

        GIVEN("a pool that ran out of fresh IDs")
        {
            nRtcConnectionIds ids(0);
            unsigned short id = 0;

            // exhaust the whole range
            for (int i = 0; i < 0xffff; ++i)
                REQUIRE(ids.TryAllocate(id));
            CHECK(0xffff == id);

            THEN("it refuses instead of handing out a live ID")
            {
                CHECK(!ids.TryAllocate(id));
            }
            AND_THEN("a release makes room again")
            {
                ids.Release(17);
                REQUIRE(ids.TryAllocate(id));
                CHECK(17 == id);
            }
        }
    }
}

namespace
{
// the address text of an attribution IPv4, "a.b.c.d"
std::string cDotted(unsigned int networkOrder)
{
    nRtcIp ip;
    std::memcpy(ip.bytes, &networkOrder, sizeof(networkOrder));
    return nRtcFormatIp(ip);
}

// parses or fails the test
nRtcIp cIp(char const* text)
{
    nRtcIp ip;
    REQUIRE(nRtcTryParseIp(text, ip));
    return ip;
}

// the default of WEBRTC_TRUSTED_PROXIES
std::vector<nRtcIpRange> cLoopback()
{
    return nRtcParseIpRanges("127.0.0.0/8 ::1");
}

// how many of the other peers share a peer's host; what CountSameIP (nNetwork.cpp) computes
int cCountSameHost(std::vector<nAddress> const& peers, size_t index)
{
    int count = 0;
    for (size_t i = 0; i < peers.size(); ++i)
        if (i != index && nAddress::SameHost(peers[i], peers[index]))
            ++count;
    return count;
}

// what ResolveClient() and PinAddress() (nTransportRtc.cpp) make of a peer: the address it is
// pinned to and the rule that found it. iceAddress empty: no selected pair (the WebSocket path)
nRtcAddressSource cPin(std::string const& socketPeer, std::string const& target, std::string const& iceAddress,
                       bool iceDirect, std::vector<nRtcIpRange> const& trusted, unsigned int& ip)
{
    nRtcIp client;
    nRtcClientSource const source = nRtcResolveClient(socketPeer, target, trusted, client);
    unsigned int const clientIp =
        (source == nRtcClient_Forwarded || source == nRtcClient_Socket) ? nRtcAttributionIPv4(client) : 0;
    nRtcIp socket;
    unsigned int const socketIp = nRtcTryParseIp(socketPeer, socket) ? nRtcAttributionIPv4(socket) : 0;
    unsigned int const iceIp = iceAddress.empty() ? 0 : nRtcAttributionIPv4(cIp(iceAddress.c_str()));
    return nRtcChoosePeerAddress(source, clientIp, iceIp, iceDirect, socketIp, ip);
}

// the virtual address the transport gives a peer
nAddress cPeerAddress(std::string const& socketPeer, std::string const& target, std::string const& iceAddress,
                      std::vector<nRtcIpRange> const& trusted, unsigned short id, bool iceDirect = false)
{
    unsigned int ip = 0;
    cPin(socketPeer, target, iceAddress, iceDirect, trusted, ip);
    return nAddress::Virtual(ip, id);
}
} // namespace

TEST_SUITE("nTransportRtc Client Address")
{
    TEST_CASE("Addresses Are Parsed The Way Proxies Write Them")
    {
        GIVEN("IPv4 in the forms X-Forwarded-For entries take")
        {
            THEN("plain, padded and with a port all come out the same")
            {
                CHECK("203.0.113.7" == nRtcFormatIp(cIp("203.0.113.7")));
                CHECK("203.0.113.7" == nRtcFormatIp(cIp("  203.0.113.7\t")));
                CHECK("203.0.113.7" == nRtcFormatIp(cIp("203.0.113.7:5678")));
                CHECK(!cIp("203.0.113.7").v6);
            }
        }

        GIVEN("IPv6 in its text forms")
        {
            THEN("compressed, full, bracketed, with port or zone all parse and print canonically")
            {
                CHECK("2001:db8::1" == nRtcFormatIp(cIp("2001:db8::1")));
                CHECK("2001:db8::1" == nRtcFormatIp(cIp("2001:0DB8:0000:0000:0000:0000:0000:0001")));
                CHECK("2001:db8::1" == nRtcFormatIp(cIp("[2001:db8::1]")));
                CHECK("2001:db8::1" == nRtcFormatIp(cIp("[2001:db8::1]:443")));
                CHECK("fe80::1" == nRtcFormatIp(cIp("fe80::1%eth0")));
                CHECK("::1" == nRtcFormatIp(cIp("::1")));
                CHECK("::" == nRtcFormatIp(cIp("::")));
                CHECK("1:0:2::3:0:0" == nRtcFormatIp(cIp("1:0:2:0:0:3:0:0")));
                CHECK("64:ff9b::c000:201" == nRtcFormatIp(cIp("64:ff9b::192.0.2.1")));
                CHECK(cIp("2001:db8::1").v6);
            }
            AND_THEN("an IPv4-mapped address is IPv4")
            {
                nRtcIp const ip = cIp("::ffff:198.51.100.4");
                CHECK(!ip.v6);
                CHECK("198.51.100.4" == nRtcFormatIp(ip));
            }
        }

        GIVEN("malformed values")
        {
            nRtcIp ip;
            THEN("they are all rejected")
            {
                CHECK(!nRtcTryParseIp("", ip));
                CHECK(!nRtcTryParseIp("   ", ip));
                CHECK(!nRtcTryParseIp("unknown", ip));
                CHECK(!nRtcTryParseIp("_hidden", ip));
                CHECK(!nRtcTryParseIp("example.com", ip));
                CHECK(!nRtcTryParseIp("1.2.3", ip));
                CHECK(!nRtcTryParseIp("1.2.3.4.5", ip));
                CHECK(!nRtcTryParseIp("1.2.3.256", ip));
                CHECK(!nRtcTryParseIp("1.2.3.04", ip));   // leading zero: octal or decimal?
                CHECK(!nRtcTryParseIp("1.2.3.4:", ip));
                CHECK(!nRtcTryParseIp("1.2.3.4:http", ip));
                CHECK(!nRtcTryParseIp("1.2.3.4 5.6.7.8", ip));
                CHECK(!nRtcTryParseIp("2001:db8::1::2", ip));
                CHECK(!nRtcTryParseIp("2001:db8:1:2:3:4:5:6:7", ip));
                CHECK(!nRtcTryParseIp("2001:db8::12345", ip));
                CHECK(!nRtcTryParseIp("2001:db8::g", ip));
                CHECK(!nRtcTryParseIp("[2001:db8::1", ip));
                CHECK(!nRtcTryParseIp("[2001:db8::1]x", ip));
                CHECK(!nRtcTryParseIp("[1.2.3.4]", ip));
                CHECK(!nRtcTryParseIp(":::", ip));
            }
        }
    }

    TEST_CASE("Trusted Proxy Ranges")
    {
        GIVEN("the default list and a few more")
        {
            std::vector<std::string> rejected;
            std::vector<nRtcIpRange> const ranges =
                nRtcParseIpRanges("127.0.0.0/8 ::1, 10.1.0.0/16,fd00::/8  192.0.2.9 bogus 10.0.0.0/33", &rejected);

            THEN("good entries are kept, bad ones reported")
            {
                CHECK(5 == ranges.size());
                REQUIRE(2 == rejected.size());
                CHECK("bogus" == rejected[0]);
                CHECK("10.0.0.0/33" == rejected[1]);
            }
            AND_THEN("membership follows the prefix and the family")
            {
                CHECK(nRtcRangesContain(ranges, cIp("127.0.0.1")));
                CHECK(nRtcRangesContain(ranges, cIp("127.255.0.9")));
                CHECK(nRtcRangesContain(ranges, cIp("::ffff:127.0.0.1")));
                CHECK(nRtcRangesContain(ranges, cIp("::1")));
                CHECK(nRtcRangesContain(ranges, cIp("10.1.200.3")));
                CHECK(nRtcRangesContain(ranges, cIp("fd12:3456::1")));
                CHECK(nRtcRangesContain(ranges, cIp("192.0.2.9")));
                CHECK(!nRtcRangesContain(ranges, cIp("192.0.2.10")));
                CHECK(!nRtcRangesContain(ranges, cIp("10.2.0.1")));
                CHECK(!nRtcRangesContain(ranges, cIp("128.0.0.1")));
                CHECK(!nRtcRangesContain(ranges, cIp("::2")));
                CHECK(!nRtcRangesContain(ranges, cIp("fe80::1")));
            }
        }

        GIVEN("an empty list")
        {
            THEN("nobody is trusted")
            {
                CHECK(nRtcParseIpRanges("").empty());
                CHECK(!nRtcRangesContain(nRtcParseIpRanges(""), cIp("127.0.0.1")));
            }
        }

        GIVEN("odd prefixes")
        {
            std::vector<nRtcIpRange> const ranges = nRtcParseIpRanges("198.51.100.64/26 ::ffff:10.0.0.0/104 0.0.0.0/0");
            THEN("partial bytes and mapped IPv4 ranges work, /0 matches all IPv4")
            {
                REQUIRE(3 == ranges.size());
                CHECK(8 == ranges[1].prefix);
                CHECK(nRtcRangesContain({ranges[0]}, cIp("198.51.100.127")));
                CHECK(!nRtcRangesContain({ranges[0]}, cIp("198.51.100.128")));
                CHECK(nRtcRangesContain({ranges[1]}, cIp("10.9.8.7")));
                CHECK(nRtcRangesContain({ranges[2]}, cIp("203.0.113.1")));
                CHECK(!nRtcRangesContain({ranges[2]}, cIp("2001:db8::1")));
            }
        }
    }

    TEST_CASE("Query Parameters Of The Request Target")
    {
        GIVEN("the target Caddy sends upstream")
        {
            std::string const target = "/signal/fort?X-Forwarded-For=203.0.113.7&other=1&X%2DForwarded-For=2001%3Adb8%3A%3A1";
            THEN("every value of the parameter comes out decoded, in order, the name decoded too")
            {
                std::vector<std::string> const values = nRtcQueryValues(target, "X-Forwarded-For");
                REQUIRE(2 == values.size());
                CHECK("203.0.113.7" == values[0]);
                CHECK("2001:db8::1" == values[1]);
            }
        }

        GIVEN("the target after Caddy's `uri query X-Forwarded-For` replaced the browser's own value")
        {
            // what Caddy 2.11 sends for "?X-Forwarded-For=7.7.7.7&x-forwarded-for=8.8.8.8&a=1"
            std::string const target = "/signal/fort?X-Forwarded-For=127.0.0.1&a=1&x-forwarded-for=8.8.8.8";
            THEN("a name in another case is a different parameter, which the proxy left alone")
            {
                std::vector<std::string> const values = nRtcQueryValues(target, "X-Forwarded-For");
                REQUIRE(1 == values.size());
                CHECK("127.0.0.1" == values[0]);
            }
        }

        GIVEN("encoded separators, a fragment, a broken escape")
        {
            THEN("they decode the usual way")
            {
                std::vector<std::string> const list = nRtcQueryValues("/?X-Forwarded-For=9.9.9.9%2C+8.8.8.8#X-Forwarded-For=1.1.1.1", "X-Forwarded-For");
                REQUIRE(1 == list.size());
                CHECK("9.9.9.9, 8.8.8.8" == list[0]);

                std::vector<std::string> const broken = nRtcQueryValues("/?X-Real-IP=1.2.3.4%2", "X-Real-IP");
                REQUIRE(1 == broken.size());
                CHECK("1.2.3.4%2" == broken[0]);
            }
        }

        GIVEN("targets without the parameter")
        {
            THEN("nothing comes out")
            {
                CHECK(nRtcQueryValues("/signal/fort", "X-Forwarded-For").empty());
                CHECK(nRtcQueryValues("/signal/fort?", "X-Forwarded-For").empty());
                CHECK(nRtcQueryValues("/signal/fort?X-Forwarded-For", "X-Forwarded-For").empty());
                CHECK(nRtcQueryValues("/signal/fort?X-Forwarded-Fo=1.2.3.4", "X-Forwarded-For").empty());
                CHECK(nRtcQueryValues("/signal/fort?x-forwarded-for=1.2.3.4", "X-Forwarded-For").empty());
            }
        }
    }

    TEST_CASE("X-Forwarded-For Names The Right-Most Untrusted Entry")
    {
        std::vector<nRtcIpRange> const trusted = nRtcParseIpRanges("127.0.0.0/8 ::1 10.0.0.0/8");
        std::vector<std::string> const none;

        GIVEN("one entry, what our Caddy sends")
        {
            nRtcIp client;
            REQUIRE(nRtcTryResolveForwarded({"203.0.113.7"}, none, trusted, client));
            THEN("that is the client")
            {
                CHECK("203.0.113.7" == nRtcFormatIp(client));
            }
        }

        GIVEN("a client that sent its own header through a chain of proxies")
        {
            // the client claims 6.6.6.6; the edge proxy (not trusted) appended the client's
            // real address, an internal balancer (trusted) appended the edge proxy's
            nRtcIp client;
            REQUIRE(nRtcTryResolveForwarded({"6.6.6.6, 198.51.100.4, 10.0.0.3"}, none, trusted, client));
            THEN("the forged entry on the left is never reached")
            {
                CHECK("198.51.100.4" == nRtcFormatIp(client));
            }
        }

        GIVEN("repeated parameters")
        {
            nRtcIp client;
            REQUIRE(nRtcTryResolveForwarded({"6.6.6.6", "198.51.100.4"}, none, trusted, client));
            THEN("they form one list, the last one appended by our proxy wins")
            {
                CHECK("198.51.100.4" == nRtcFormatIp(client));
            }
        }

        GIVEN("an IPv6 client")
        {
            nRtcIp client;
            REQUIRE(nRtcTryResolveForwarded({"[2001:db8:1:2::5]:51234, ::1"}, none, trusted, client));
            THEN("it is found like any other")
            {
                CHECK(client.v6);
                CHECK("2001:db8:1:2::5" == nRtcFormatIp(client));
            }
        }

        GIVEN("a list of trusted addresses only (a client on the proxy's own LAN)")
        {
            nRtcIp client;
            REQUIRE(nRtcTryResolveForwarded({"10.9.9.9, 127.0.0.1"}, none, trusted, client));
            THEN("the left-most entry is the client")
            {
                CHECK("10.9.9.9" == nRtcFormatIp(client));
            }
        }

        GIVEN("malformed entries")
        {
            nRtcIp client;
            THEN("one right of the client spoils the list, one left of it is never looked at")
            {
                CHECK(!nRtcTryResolveForwarded({"198.51.100.4, garbage"}, none, trusted, client));
                CHECK(!nRtcTryResolveForwarded({"unknown"}, none, trusted, client));
                CHECK(!nRtcTryResolveForwarded({"198.51.100.4, 10.0.0.3:x"}, none, trusted, client));
                REQUIRE(nRtcTryResolveForwarded({"garbage, 198.51.100.4"}, none, trusted, client));
                CHECK("198.51.100.4" == nRtcFormatIp(client));
            }
            AND_THEN("empty list elements are skipped")
            {
                REQUIRE(nRtcTryResolveForwarded({", 198.51.100.4 ,"}, none, trusted, client));
                CHECK("198.51.100.4" == nRtcFormatIp(client));
                CHECK(!nRtcTryResolveForwarded({" , "}, none, trusted, client));
            }
        }

        GIVEN("X-Real-IP")
        {
            nRtcIp client;
            THEN("it counts only without X-Forwarded-For, its last value")
            {
                REQUIRE(nRtcTryResolveForwarded(none, {"6.6.6.6", "198.51.100.9"}, trusted, client));
                CHECK("198.51.100.9" == nRtcFormatIp(client));
                REQUIRE(nRtcTryResolveForwarded({"203.0.113.7"}, {"198.51.100.9"}, trusted, client));
                CHECK("203.0.113.7" == nRtcFormatIp(client));
                CHECK(!nRtcTryResolveForwarded(none, {"not an ip"}, trusted, client));
                CHECK(!nRtcTryResolveForwarded(none, none, trusted, client));
            }
        }
    }

    TEST_CASE("Only A Trusted Proxy Is Believed")
    {
        std::vector<nRtcIpRange> const trusted = cLoopback();
        std::string const forged = "/signal/fort?X-Forwarded-For=6.6.6.6&X-Real-IP=6.6.6.7";

        GIVEN("a signalling connection from the local proxy")
        {
            nRtcIp client;
            THEN("the forwarded client counts")
            {
                CHECK(nRtcClient_Forwarded == nRtcResolveClient("127.0.0.1", "/signal/fort?X-Forwarded-For=198.51.100.4", trusted, client));
                CHECK("198.51.100.4" == nRtcFormatIp(client));
                CHECK(nRtcClient_Forwarded == nRtcResolveClient("::1", "/signal?X-Forwarded-For=2001%3Adb8%3A%3A7", trusted, client));
                CHECK("2001:db8::7" == nRtcFormatIp(client));
                CHECK(nRtcClient_Forwarded == nRtcResolveClient("::ffff:127.0.0.1", "/?X-Real-IP=198.51.100.5", trusted, client));
                CHECK("198.51.100.5" == nRtcFormatIp(client));
            }
            AND_THEN("a parameter the browser added in another case does not count")
            {
                CHECK(nRtcClient_Forwarded == nRtcResolveClient("127.0.0.1", "/signal/fort?X-Forwarded-For=198.51.100.4&x-forwarded-for=6.6.6.6", trusted, client));
                CHECK("198.51.100.4" == nRtcFormatIp(client));
            }
            AND_THEN("without a usable forwarded address it is reported as such")
            {
                CHECK(nRtcClient_Proxy == nRtcResolveClient("127.0.0.1", "/signal/fort", trusted, client));
                CHECK(nRtcClient_Proxy == nRtcResolveClient("127.0.0.1", "/signal/fort?X-Forwarded-For=garbage", trusted, client));
                CHECK("127.0.0.1" == nRtcFormatIp(client));
            }
        }

        GIVEN("a client that connects to the signalling port directly with a forged header")
        {
            nRtcIp client;
            nRtcClientSource const source = nRtcResolveClient("198.51.100.4", forged, trusted, client);
            THEN("it is its own socket address")
            {
                CHECK(nRtcClient_Socket == source);
                CHECK("198.51.100.4" == nRtcFormatIp(client));
            }
        }

        GIVEN("a proxy that is not in the list")
        {
            nRtcIp client;
            nRtcClientSource const source = nRtcResolveClient("172.30.77.40", forged, trusted, client);
            THEN("the proxy is the client, as far as the server can tell")
            {
                CHECK(nRtcClient_Socket == source);
                CHECK("172.30.77.40" == nRtcFormatIp(client));
            }
            AND_THEN("adding it to the list makes it believed")
            {
                CHECK(nRtcClient_Forwarded == nRtcResolveClient("172.30.77.40", forged, nRtcParseIpRanges("127.0.0.1 172.30.77.40"), client));
                CHECK("6.6.6.6" == nRtcFormatIp(client));
            }
        }

        GIVEN("an empty trust list")
        {
            nRtcIp client;
            THEN("not even loopback is believed")
            {
                CHECK(nRtcClient_Socket == nRtcResolveClient("127.0.0.1", forged, nRtcParseIpRanges(""), client));
                CHECK("127.0.0.1" == nRtcFormatIp(client));
            }
        }

        GIVEN("an unreadable socket peer")
        {
            nRtcIp client;
            THEN("nothing is decided")
            {
                CHECK(nRtcClient_Unknown == nRtcResolveClient("", forged, trusted, client));
            }
        }
    }

    TEST_CASE("Attribution Fits The Virtual Address")
    {
        GIVEN("an IPv4 client")
        {
            THEN("it is carried as it is, in network order")
            {
                CHECK("203.0.113.7" == cDotted(nRtcAttributionIPv4(cIp("203.0.113.7"))));
                CHECK(htonl(0xCB007107) == nRtcAttributionIPv4(cIp("203.0.113.7")));
            }
        }

        GIVEN("IPv6 clients")
        {
            unsigned int const a1 = nRtcAttributionIPv4(cIp("2001:db8:1:2::5"));
            unsigned int const a2 = nRtcAttributionIPv4(cIp("2001:db8:1:2:aaaa:bbbb:cccc:dddd"));
            unsigned int const b = nRtcAttributionIPv4(cIp("2001:db8:1:3::5"));

            THEN("they land in 240.0.0.0/4, one value per /64")
            {
                CHECK(0xf0 == (reinterpret_cast<unsigned char const*>(&a1)[0] & 0xf0));
                CHECK(0xf0 == (reinterpret_cast<unsigned char const*>(&b)[0] & 0xf0));
                CHECK(a1 == a2);
                CHECK(a1 != b);
                CHECK(0xffffffffu != a1);
            }
            AND_THEN("the value is stable, so a ban outlives the session")
            {
                CHECK(a1 == nRtcAttributionIPv4(cIp("2001:0db8:0001:0002:0000:0000:0000:0005")));
            }
        }
    }

    TEST_CASE("A Shared Proxy's Clients Get An Address Per Connection")
    {
        GIVEN("connections through one shared proxy")
        {
            std::vector<nRtcIpRange> const shared = nRtcParseIpRanges("104.16.0.0/13 2606:4700::/32");
            std::set<unsigned int> seen;
            for (unsigned int serial = 1; serial <= 1000; ++serial)
                seen.insert(nRtcSharedProxyIPv4(0x5eed1234u, serial));
            unsigned int const one = nRtcSharedProxyIPv4(0x5eed1234u, 1);

            THEN("the proxy's addresses are recognised, a client's own is not")
            {
                CHECK(nRtcRangesContain(shared, cIp("104.22.177.39")));
                CHECK(nRtcRangesContain(shared, cIp("2606:4700::6810:1")));
                CHECK(!nRtcRangesContain(shared, cIp("203.0.113.7")));
            }
            AND_THEN("each connection has its own address in 240.0.0.0/4")
            {
                CHECK(1000 == seen.size());
                CHECK(0xf0 == (reinterpret_cast<unsigned char const*>(&one)[0] & 0xf0));
                CHECK(0xffffffffu != one);
            }
            AND_THEN("another salt (another server start) gives other addresses")
            {
                CHECK(one != nRtcSharedProxyIPv4(0x5eed1235u, 1));
            }
        }
    }

    TEST_CASE("Per-IP Limits See The Real Clients")
    {
        std::vector<nRtcIpRange> const trusted = cLoopback();
        std::string const turn = "172.30.77.20"; // every relayed browser's ICE address

        GIVEN("six relayed browsers from six addresses, all through the local proxy")
        {
            std::vector<nAddress> peers;
            for (int i = 0; i < 6; ++i)
                peers.push_back(cPeerAddress("127.0.0.1", "/signal/fort?X-Forwarded-For=198.51.100." + std::to_string(10 + i),
                                             turn, trusted, static_cast<unsigned short>(i + 1)));

            THEN("no two share a host, so MAX_CLIENTS_SAME_IP_* and MAX_PLAYERS_SAME_IP see one each")
            {
                for (size_t i = 0; i < peers.size(); ++i)
                    CHECK(0 == cCountSameHost(peers, i));
                CHECK("198.51.100.12" == std::string(static_cast<char const*>(peers[2].GetAddress())));
            }
            AND_THEN("the connection ID still tells the connections apart for routing")
            {
                CHECK(3 == peers[2].GetConnectionId());
                CHECK(-1 == nAddress::Compare(peers[0], peers[1]));
            }
        }

        GIVEN("five browsers from one address, direct or relayed")
        {
            std::vector<nAddress> peers;
            for (int i = 0; i < 5; ++i)
                peers.push_back(cPeerAddress("127.0.0.1", "/signal/fort?X-Forwarded-For=198.51.100.10",
                                             i % 2 ? turn : std::string("198.51.100.10"), trusted,
                                             static_cast<unsigned short>(i + 1), i % 2 == 0));

            THEN("they are one host with four others, as before")
            {
                for (size_t i = 0; i < peers.size(); ++i)
                    CHECK(4 == cCountSameHost(peers, i));
            }
            AND_THEN("they are still distinct connections")
            {
                CHECK(1 == nAddress::Compare(peers[0], peers[1]));
                CHECK(0 == nAddress::Compare(peers[3], nAddress::Virtual(peers[3].GetIPv4(), 4)));
            }
        }

        GIVEN("a browser and a desktop client on one machine")
        {
            nAddress const browser = cPeerAddress("127.0.0.1", "/signal?X-Forwarded-For=198.51.100.10", turn, trusted, 1);
            nAddress desktop;
            desktop.SetAddress("198.51.100.10");
            desktop.SetPort(4534);

            THEN("per-IP limits count them together even when the browser is relayed")
            {
                CHECK(nAddress::SameHost(browser, desktop));
            }
        }

        GIVEN("before this change: relayed browsers were known by the ICE address")
        {
            std::vector<nAddress> peers;
            for (int i = 0; i < 5; ++i)
                peers.push_back(cPeerAddress("127.0.0.1", "/signal/fort", turn, trusted, static_cast<unsigned short>(i + 1)));

            THEN("which is still what a proxy that forwards nothing gets: one host")
            {
                CHECK(4 == cCountSameHost(peers, 0));
                CHECK("172.30.77.20" == std::string(static_cast<char const*>(peers[0].GetAddress())));
            }
        }
    }

    TEST_CASE("ICE Ports A Host Can Open")
    {
        GIVEN("no WEBRTC_PORT_MIN/MAX")
        {
            nRtcIcePorts const ports = nRtcChooseIcePorts(4534, 0, 0);

            THEN("one shared port, SERVER_PORT + 1000, clear of neighbouring servers' game ports")
            {
                CHECK(ports.mux);
                CHECK(5534 == ports.begin);
                CHECK(5534 == ports.end);
                CHECK("UDP 5534" == nRtcDescribeIcePorts(ports));
                CHECK(5536 == nRtcChooseIcePorts(4536, 0, 0).begin);
            }
            AND_THEN("a port too high for that leaves the choice to the library")
            {
                nRtcIcePorts const high = nRtcChooseIcePorts(65000, 0, 0);
                CHECK(!high.mux);
                CHECK(0 == high.begin);
                CHECK(0 == nRtcDescribeIcePorts(high).find("any UDP port"));
            }
        }

        GIVEN("an explicit range, as our deploys set")
        {
            nRtcIcePorts const ports = nRtcChooseIcePorts(4534, 50000, 50019);

            THEN("it wins, one port per peer as before")
            {
                CHECK(!ports.mux);
                CHECK(50000 == ports.begin);
                CHECK(50019 == ports.end);
                CHECK("UDP 50000-50019" == nRtcDescribeIcePorts(ports));
            }
        }

        GIVEN("a range of one port")
        {
            nRtcIcePorts const ports = nRtcChooseIcePorts(4534, 4600, 4600);

            THEN("it is shared by every peer")
            {
                CHECK(ports.mux);
                CHECK(4600 == ports.begin);
                CHECK(4600 == ports.end);
            }
        }

        GIVEN("a range that makes no sense")
        {
            THEN("the default applies")
            {
                CHECK(5534 == nRtcChooseIcePorts(4534, 50019, 50000).begin);
                CHECK(5534 == nRtcChooseIcePorts(4534, 50000, 70000).begin);
                CHECK(5534 == nRtcChooseIcePorts(4534, 0, 50000).begin);
            }
        }
    }

    TEST_CASE("Handshakes In Flight Are Capped")
    {
        GIVEN("the defaults, 16 per source and 64 in all")
        {
            THEN("a source may start up to its cap")
            {
                CHECK(nRtcAdmit == nRtcAdmitPending(0, 0, 16, 64));
                CHECK(nRtcAdmit == nRtcAdmitPending(15, 40, 16, 64));
                CHECK(nRtcRefuse_Source == nRtcAdmitPending(16, 40, 16, 64));
            }
            AND_THEN("the server as a whole too, whoever asks")
            {
                CHECK(nRtcRefuse_Global == nRtcAdmitPending(0, 64, 16, 64));
                CHECK(nRtcRefuse_Global == nRtcAdmitPending(16, 64, 16, 64));
            }
            AND_THEN("the refusal says why")
            {
                CHECK(std::string(nRtcAdmissionReason(nRtcRefuse_Source)).find("your address") != std::string::npos);
                CHECK(!std::string(nRtcAdmissionReason(nRtcRefuse_Global)).empty());
                CHECK(std::string(nRtcAdmissionReason(nRtcAdmit)).empty());
            }
        }

        GIVEN("caps of 0")
        {
            THEN("there is no cap")
            {
                CHECK(nRtcAdmit == nRtcAdmitPending(1000, 5000, 0, 0));
                CHECK(nRtcRefuse_Source == nRtcAdmitPending(1000, 5000, 8, 0));
            }
        }
    }

    TEST_CASE("Behind A Trusted Proxy The Forwarded Client Is The Source")
    {
        std::vector<nRtcIpRange> const trusted = cLoopback();
        unsigned int const loopback = nRtcAttributionIPv4(cIp("127.0.0.1"));
        unsigned int const gateway = nRtcAttributionIPv4(cIp("203.0.113.50"));

        // what a connection counts against once its first message resolved the client
        auto resolvedKey = [&](std::string const& socketPeer, std::string const& target)
        {
            nRtcIp client;
            nRtcClientSource const source = nRtcResolveClient(socketPeer, target, trusted, client);
            nRtcIp peer = cIp(socketPeer.c_str());
            return nRtcPendingSourceKey(nRtcRangesContain(trusted, peer), true, source, nRtcPendingSourceIPv4(peer),
                                        source == nRtcClient_Forwarded ? nRtcPendingSourceIPv4(client) : 0);
        };

        GIVEN("connections at accept")
        {
            THEN("an untrusted peer, our gateway too, is its own source; a trusted proxy is none")
            {
                CHECK(gateway == nRtcPendingSourceKey(false, false, nRtcClient_Unknown, gateway, 0));
                CHECK(0 == nRtcPendingSourceKey(true, false, nRtcClient_Unknown, loopback, 0));
            }
        }

        GIVEN("their first message")
        {
            THEN("a trusted proxy's connection counts against the client it names")
            {
                CHECK(nRtcAttributionIPv4(cIp("198.51.100.7")) ==
                      resolvedKey("127.0.0.1", "/signal/qp1?X-Forwarded-For=198.51.100.7"));
                CHECK(nRtcPendingSourceIPv4(cIp("2001:db8:1:ffff::9")) ==
                      resolvedKey("::1", "/signal/qp1?X-Forwarded-For=2001:db8:1:2::5"));
            }
            AND_THEN("one naming nobody counts against nobody; an untrusted peer stays its own source")
            {
                CHECK(0 == resolvedKey("127.0.0.1", "/signal/qp1"));
                CHECK(gateway == resolvedKey("203.0.113.50", "/?X-Forwarded-For=198.51.100.7"));
            }
        }

        GIVEN("30 handshakes through the local Caddy")
        {
            int const cap = 16;
            std::vector<unsigned int> distinct, same;
            int refusedDistinct = 0, refusedSame = 0;
            for (int i = 0; i < 30; ++i)
            {
                // accept: no per-source count for the proxy; then the first message
                CHECK(nRtcAdmit == nRtcAdmitPending(nRtcCountSource(distinct, 0), static_cast<int>(distinct.size()), cap, 64));
                unsigned int const a = resolvedKey("127.0.0.1", "/signal/qp1?X-Forwarded-For=198.51.100." + std::to_string(i + 1));
                if (nRtcAdmitPending(nRtcCountSource(distinct, a), 0, cap, 0) == nRtcAdmit)
                    distinct.push_back(a);
                else
                    ++refusedDistinct;

                unsigned int const b = resolvedKey("127.0.0.1", "/signal/qp1?X-Forwarded-For=198.51.100.200");
                if (nRtcAdmitPending(nRtcCountSource(same, b), 0, cap, 0) == nRtcAdmit)
                    same.push_back(b);
                else
                    ++refusedSame;
            }

            THEN("30 clients are all admitted; one client 30 times is capped at 16")
            {
                CHECK(0 == refusedDistinct);
                CHECK(30 == distinct.size());
                CHECK(16 == same.size());
                CHECK(14 == refusedSame);
            }
            AND_THEN("the global cap still counts them all")
            {
                CHECK(nRtcRefuse_Global == nRtcAdmitPending(0, 64, cap, 64));
                CHECK(0 == nRtcCountSource(distinct, 0));
            }
        }
    }

    TEST_CASE("IPv6 Sources Count By Their /48")
    {
        GIVEN("clients in one /48, on different /64s")
        {
            unsigned int const a = nRtcPendingSourceIPv4(cIp("2001:db8:1:2::5"));
            unsigned int const b = nRtcPendingSourceIPv4(cIp("2001:db8:1:ffff:1:2:3:4"));
            unsigned int const other = nRtcPendingSourceIPv4(cIp("2001:db8:2:2::5"));

            THEN("they are one source for the handshake caps, in 240.0.0.0/4")
            {
                CHECK(a == b);
                CHECK(a != other);
                CHECK(0xf0 == (reinterpret_cast<unsigned char const*>(&a)[0] & 0xf0));
            }
            AND_THEN("but still separate players, attributed by their /64")
            {
                CHECK(nRtcAttributionIPv4(cIp("2001:db8:1:2::5")) != nRtcAttributionIPv4(cIp("2001:db8:1:ffff:1:2:3:4")));
            }
        }

        GIVEN("an IPv4 client")
        {
            THEN("its source is its address, as before")
            {
                CHECK(nRtcAttributionIPv4(cIp("203.0.113.7")) == nRtcPendingSourceIPv4(cIp("203.0.113.7")));
            }
        }

        GIVEN("30 handshakes, each from its own /64 of one /48")
        {
            std::vector<unsigned int> admitted;
            for (int i = 0; i < 30; ++i)
            {
                char address[64];
                std::snprintf(address, sizeof(address), "2001:db8:7:%x::1", i + 1);
                unsigned int const key = nRtcPendingSourceKey(false, false, nRtcClient_Unknown, nRtcPendingSourceIPv4(cIp(address)), 0);
                if (nRtcAdmitPending(nRtcCountSource(admitted, key), static_cast<int>(admitted.size()), 16, 64) == nRtcAdmit)
                    admitted.push_back(key);
            }

            THEN("the per-source cap stops them at 16")
            {
                CHECK(16 == admitted.size());
            }
        }
    }

    TEST_CASE("Handshakes That Take Too Long Are Given Up")
    {
        GIVEN("10 s for a first message, 30 s for the data channel")
        {
            THEN("a silent socket goes after 10 s")
            {
                CHECK(nRtcPending_Alive == nRtcPendingExpired(9999, false, 10000, 30000));
                CHECK(nRtcPending_Silent == nRtcPendingExpired(10000, false, 10000, 30000));
            }
            AND_THEN("one that sent an offer has until 30 s")
            {
                CHECK(nRtcPending_Alive == nRtcPendingExpired(20000, true, 10000, 30000));
                CHECK(nRtcPending_Handshake == nRtcPendingExpired(30000, true, 10000, 30000));
                CHECK(nRtcPending_Silent == nRtcPendingExpired(30000, false, 10000, 30000));
            }
        }

        GIVEN("timeouts of 0")
        {
            THEN("nothing expires")
            {
                CHECK(nRtcPending_Alive == nRtcPendingExpired(1000000, false, 0, 0));
                CHECK(nRtcPending_Handshake == nRtcPendingExpired(1000000, false, 0, 30000));
            }
        }
    }

    TEST_CASE("Connections That Never Log In Are Closed")
    {
        GIVEN("60 s to log in")
        {
            THEN("an established peer without a game connection goes after 60 s")
            {
                CHECK(nRtcPending_Alive == nRtcUnclaimedExpired(0, 60000));
                CHECK(nRtcPending_Alive == nRtcUnclaimedExpired(59999, 60000));
                CHECK(nRtcPending_Login == nRtcUnclaimedExpired(60000, 60000));
            }
        }

        GIVEN("a timeout of 0")
        {
            THEN("it is never closed for that")
            {
                CHECK(nRtcPending_Alive == nRtcUnclaimedExpired(100000000, 0));
            }
        }
    }

    TEST_CASE("Candidate Types That Come Straight From The Client")
    {
        CHECK(nRtcIceTypeIsDirect("host"));
        CHECK(nRtcIceTypeIsDirect("srflx"));
        CHECK(nRtcIceTypeIsDirect("prflx"));
        CHECK(!nRtcIceTypeIsDirect("relay"));
        CHECK(!nRtcIceTypeIsDirect("unknown"));
        CHECK(!nRtcIceTypeIsDirect(""));
    }

    TEST_CASE("The Order A Peer's Address Is Chosen In")
    {
        std::vector<nRtcIpRange> const trusted = cLoopback();
        std::string const turn = "172.30.77.20";
        std::string const gateway = "203.0.113.50"; // our signalling gateway: untrusted, forwards nothing
        unsigned int ip = 0;

        GIVEN("a trusted proxy that names the client (official rooms behind their Caddy)")
        {
            std::string const target = "/signal/fort?X-Forwarded-For=198.51.100.7";

            THEN("the forwarded client wins over every ICE address, as before")
            {
                CHECK(nRtcAddress_Forwarded == cPin("127.0.0.1", target, "192.0.2.9", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_Forwarded == cPin("127.0.0.1", target, "2001:db8:1:2::5", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_Forwarded == cPin("127.0.0.1", target, turn, false, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_Forwarded == cPin("127.0.0.1", target, "", false, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
            }
            AND_THEN("an IPv6 client named over IPv6 loopback is its /64's attribution")
            {
                CHECK(nRtcAddress_Forwarded ==
                      cPin("::1", "/signal?X-Forwarded-For=2001:db8:1:2::5", "192.0.2.9", true, trusted, ip));
                CHECK(nRtcAttributionIPv4(cIp("2001:db8:1:2::77")) == ip);
            }
        }

        GIVEN("a trusted proxy that names nobody")
        {
            THEN("a direct pair is the client, a relayed one next, the proxy itself last")
            {
                CHECK(nRtcAddress_IceDirect == cPin("127.0.0.1", "/signal/fort", "198.51.100.7", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_IceRelayed == cPin("127.0.0.1", "/signal/fort", turn, false, trusted, ip));
                CHECK(turn == cDotted(ip));
                CHECK(nRtcAddress_Proxy == cPin("127.0.0.1", "/signal/fort", "", false, trusted, ip));
                CHECK("127.0.0.1" == cDotted(ip));
            }
        }

        GIVEN("an untrusted signalling peer: our gateway (Direct Connect)")
        {
            THEN("a direct IPv4 pair names the player")
            {
                CHECK(nRtcAddress_IceDirect == cPin(gateway, "/", "198.51.100.7", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_IceDirect == cPin(gateway, "/?to=x", "198.51.100.7", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
            }
            AND_THEN("a direct IPv6 pair is the player's /64, counted in 240.0.0.0/4")
            {
                CHECK(nRtcAddress_IceDirect == cPin(gateway, "/", "2001:db8:1:2::5", true, trusted, ip));
                CHECK(nRtcAttributionIPv4(cIp("2001:db8:1:2::5")) == ip);
                CHECK(0xf0 == (reinterpret_cast<unsigned char const*>(&ip)[0] & 0xf0));
            }
            AND_THEN("a relayed pair or no pair at all falls back to the signalling peer")
            {
                CHECK(nRtcAddress_Signalling == cPin(gateway, "/", turn, false, trusted, ip));
                CHECK(gateway == cDotted(ip));
                CHECK(nRtcAddress_Signalling == cPin(gateway, "/", "", false, trusted, ip));
                CHECK(gateway == cDotted(ip));
            }
            AND_THEN("a forged X-Forwarded-For changes nothing")
            {
                CHECK(nRtcAddress_IceDirect ==
                      cPin(gateway, "/?X-Forwarded-For=192.0.2.1", "198.51.100.7", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_Signalling == cPin(gateway, "/?X-Forwarded-For=192.0.2.1", turn, false, trusted, ip));
                CHECK(gateway == cDotted(ip));
            }
        }

        GIVEN("an untrusted IPv6 signalling peer")
        {
            THEN("a direct IPv4 pair still wins; a relayed one gives the peer's /64")
            {
                CHECK(nRtcAddress_IceDirect == cPin("2001:db8:9::1", "/", "198.51.100.7", true, trusted, ip));
                CHECK("198.51.100.7" == cDotted(ip));
                CHECK(nRtcAddress_Signalling == cPin("2001:db8:9::1", "/", turn, false, trusted, ip));
                CHECK(nRtcAttributionIPv4(cIp("2001:db8:9::1")) == ip);
            }
        }

        GIVEN("five players through the gateway, each on a direct pair")
        {
            std::vector<nAddress> peers;
            for (int i = 0; i < 5; ++i)
                peers.push_back(cPeerAddress(gateway, "/", "198.51.100." + std::to_string(10 + i), trusted,
                                             static_cast<unsigned short>(i + 1), true));

            THEN("they are five hosts: a ban on one of them bans nobody else")
            {
                for (size_t i = 0; i < peers.size(); ++i)
                    CHECK(0 == cCountSameHost(peers, i));
                CHECK("198.51.100.12" == std::string(static_cast<char const*>(peers[2].GetAddress())));
            }
            AND_THEN("a desktop client of one of them is the same host")
            {
                nAddress desktop;
                desktop.SetAddress("198.51.100.12");
                desktop.SetPort(4534);
                CHECK(nAddress::SameHost(peers[2], desktop));
                CHECK(!nAddress::SameHost(peers[1], desktop));
            }
        }

        GIVEN("nothing known at all")
        {
            THEN("there is no address")
            {
                CHECK(nRtcAddress_None == nRtcChoosePeerAddress(nRtcClient_Unknown, 0, 0, false, 0, ip));
                CHECK(0 == ip);
                CHECK(std::string("unknown") == nRtcAddressSourceName(nRtcAddress_None));
                CHECK(std::string("direct ICE pair") == nRtcAddressSourceName(nRtcAddress_IceDirect));
            }
        }
    }
}
