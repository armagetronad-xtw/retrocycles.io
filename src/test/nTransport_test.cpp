#include "nTransport.h"
#include "nTransportLoopback.h"

#include "doctest.h"
#include "nNetwork.h"
#include "tMemManager.h"

#include <cstring>

// Tests for the transport abstraction and its in-process loopback implementation.

namespace
{
// registers a transport for the duration of a scope
class cTransportGuard final
{
public:
    explicit cTransportGuard(nTransport& transport) : transport_(transport) { nTransport::Register(&transport_); }
    ~cTransportGuard() { nTransport::Unregister(&transport_); }

    cTransportGuard(cTransportGuard const&) = delete;
    cTransportGuard& operator=(cTransportGuard const&) = delete;

private:
    nTransport& transport_;
};

// 10.0.0.1 and 10.0.0.2 in network byte order
unsigned int const testIpA = htonl(0x0A000001);
unsigned int const testIpB = htonl(0x0A000002);
}

TEST_SUITE("nAddress Virtual Family")
{
    TEST_CASE("Virtual Addresses Are Recognized")
    {
        GIVEN("a virtual address")
        {
            nAddress const peer = nAddress::Virtual(testIpA, 7);

            THEN("it reports itself as virtual")
            {
                CHECK(peer.IsVirtual());
            }
            AND_THEN("it carries the connection ID and the real IP")
            {
                CHECK(7 == peer.GetConnectionId());
                CHECK(testIpA == peer.GetIPv4());
                CHECK(tString("10.0.0.1") == peer.GetAddress());
            }
            AND_THEN("it stringifies as IP:connectionId without a DNS lookup")
            {
                CHECK(tString("10.0.0.1:7") == peer.ToString());
                CHECK(tString("10.0.0.1") == peer.GetHostname());
            }
            AND_THEN("it counts as set")
            {
                CHECK(peer.IsSet());
            }
        }

        GIVEN("a default address")
        {
            nAddress const peer;

            THEN("it is not virtual")
            {
                CHECK(!peer.IsVirtual());
            }
        }

        GIVEN("a virtual address without a real IP")
        {
            nAddress const peer = nAddress::Virtual(0, 3);

            THEN("it is not set, just like an unbound UDP address")
            {
                CHECK(!peer.IsSet());
            }
        }
    }

    TEST_CASE("Virtual Addresses Compare Like UDP Addresses")
    {
        GIVEN("two virtual peers from the same IP")
        {
            nAddress const first = nAddress::Virtual(testIpA, 1);
            nAddress const second = nAddress::Virtual(testIpA, 2);

            THEN("they differ, like two UDP clients behind one NAT")
            {
                CHECK(first != second);
            }
            AND_THEN("the mismatch is reported as a port mismatch, so duplicate IP detection still sees one machine")
            {
                CHECK(1 == nAddress::Compare(first, second));
            }
            AND_THEN("the same connection ID matches")
            {
                CHECK(first == nAddress::Virtual(testIpA, 1));
            }
        }

        GIVEN("virtual peers from different IPs")
        {
            THEN("they never match")
            {
                CHECK(-1 == nAddress::Compare(nAddress::Virtual(testIpA, 1), nAddress::Virtual(testIpB, 1)));
            }
        }

        GIVEN("a virtual peer and a UDP peer with the same IP and port")
        {
            nAddress const virtualPeer = nAddress::Virtual(testIpA, 4534);

            nAddress udpPeer;
            udpPeer.SetAddress("10.0.0.1");
            udpPeer.SetPort(4534);

            THEN("they never match, the address family is checked first")
            {
                CHECK(-1 == nAddress::Compare(virtualPeer, udpPeer));
                CHECK(-1 == nAddress::Compare(udpPeer, virtualPeer));
            }
            AND_THEN("the UDP peer is not virtual")
            {
                CHECK(!udpPeer.IsVirtual());
            }
        }
    }

    TEST_CASE("Same Host Detection Crosses The Address Family")
    {
        nAddress udpPeer;
        udpPeer.SetAddress("10.0.0.1");
        udpPeer.SetPort(4534);

        GIVEN("a virtual peer and a UDP peer from the same IP")
        {
            nAddress const virtualPeer = nAddress::Virtual(testIpA, 9);

            THEN("they do not compare equal")
            {
                CHECK(0 != nAddress::Compare(virtualPeer, udpPeer));
            }
            AND_THEN("they are still recognized as one host, so per-IP limits cover both")
            {
                CHECK(nAddress::SameHost(virtualPeer, udpPeer));
                CHECK(nAddress::SameHost(udpPeer, virtualPeer));
            }
        }

        GIVEN("peers from different IPs")
        {
            THEN("they are different hosts, whatever their family")
            {
                CHECK(!nAddress::SameHost(nAddress::Virtual(testIpB, 9), udpPeer));
                CHECK(!nAddress::SameHost(nAddress::Virtual(testIpA, 9), nAddress::Virtual(testIpB, 9)));
            }
        }

        GIVEN("two UDP peers from one IP on different ports")
        {
            nAddress other;
            other.SetAddress("10.0.0.1");
            other.SetPort(4535);

            THEN("they are the same host, as before")
            {
                CHECK(nAddress::SameHost(udpPeer, other));
                CHECK(1 == nAddress::Compare(udpPeer, other));
            }
        }
    }
}

TEST_SUITE("nTransport")
{
    TEST_CASE("Unregistered Transport Is Inert")
    {
        GIVEN("no registered transport")
        {
            REQUIRE(nullptr == nTransport::Instance());

            nAddress from;
            int8 buffer[16] = {};

            THEN("reading yields nothing")
            {
                CHECK(nTransport::ReadPacket(buffer, sizeof(buffer), from) < 0);
            }
            AND_THEN("sending fails")
            {
                CHECK(!nTransport::TrySendPacket(nAddress::Virtual(testIpA, 1), buffer, 4));
            }
        }
    }

    TEST_CASE("Loopback Round Trip")
    {
        GIVEN("a loopback linking a server and a client endpoint")
        {
            nAddress const server = nAddress::Virtual(testIpA, 1);
            nAddress const client = nAddress::Virtual(testIpB, 2);

            nTransportLoopback loopback;
            loopback.Link(server, client);
            cTransportGuard const guard(loopback);

            REQUIRE(&loopback == nTransport::Instance());

            WHEN("a packet is sent to the client")
            {
                int8 const payload[] = {'a', 'r', 'm', 'a', 0, 1, 2, 3};
                REQUIRE(nTransport::TrySendPacket(client, payload, sizeof(payload)));

                THEN("it shows up in the inbound queue")
                {
                    CHECK(1 == loopback.GetPending());
                }

                AND_THEN("it reads back byte for byte, coming from the server")
                {
                    nAddress from;
                    int8 received[32] = {};
                    int const len = nTransport::ReadPacket(received, sizeof(received), from);

                    CHECK(static_cast<int>(sizeof(payload)) == len);
                    CHECK(0 == memcmp(payload, received, sizeof(payload)));
                    CHECK(from == server);
                    CHECK(from.IsVirtual());
                    CHECK(1 == from.GetConnectionId());
                }
            }

            WHEN("a packet is sent to an unlinked peer")
            {
                int8 const payload[] = {'x', 'y'};

                THEN("it is dropped")
                {
                    CHECK(!nTransport::TrySendPacket(nAddress::Virtual(testIpA, 99), payload, sizeof(payload)));
                    CHECK(1 == loopback.GetDropped());
                    CHECK(0 == loopback.GetPending());
                }
            }

            WHEN("the queue runs dry")
            {
                nAddress from;
                int8 received[8] = {};

                THEN("reading reports failure like a drained socket")
                {
                    CHECK(nTransport::ReadPacket(received, sizeof(received), from) < 0);
                }
            }
        }
    }

    TEST_CASE("Inbound Queue Is FIFO")
    {
        GIVEN("a loopback with three queued packets")
        {
            nAddress const server = nAddress::Virtual(testIpA, 1);
            nAddress const client = nAddress::Virtual(testIpB, 2);

            nTransportLoopback loopback;
            loopback.Link(server, client);
            cTransportGuard const guard(loopback);

            for (int8 i = 0; i < 3; ++i)
            {
                int8 const payload[] = {i, static_cast<int8>(i + 10)};
                REQUIRE(nTransport::TrySendPacket(client, payload, sizeof(payload)));
            }

            REQUIRE(3 == loopback.GetPending());

            THEN("they come out in the order they went in")
            {
                for (int8 i = 0; i < 3; ++i)
                {
                    nAddress from;
                    int8 received[4] = {};
                    int const len = nTransport::ReadPacket(received, sizeof(received), from);

                    CHECK(2 == len);
                    CHECK(i == received[0]);
                    CHECK(static_cast<int8>(i + 10) == received[1]);
                }

                CHECK(0 == loopback.GetPending());
            }
        }
    }

    TEST_CASE("Oversized Packets Are Truncated")
    {
        GIVEN("a queued packet larger than the read buffer")
        {
            nAddress const server = nAddress::Virtual(testIpA, 1);
            nAddress const client = nAddress::Virtual(testIpB, 2);

            nTransportLoopback loopback;
            loopback.Link(server, client);
            cTransportGuard const guard(loopback);

            int8 const payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
            REQUIRE(nTransport::TrySendPacket(client, payload, sizeof(payload)));

            THEN("the read is capped at the buffer size, as recvfrom would")
            {
                nAddress from;
                int8 received[4] = {};

                CHECK(4 == nTransport::ReadPacket(received, sizeof(received), from));
                CHECK(0 == memcmp(payload, received, sizeof(received)));
            }
        }
    }

    TEST_CASE("A Full Inbound Queue Drops Like A Socket Buffer")
    {
        nAddress const server = nAddress::Virtual(testIpA, 1);
        nAddress const client = nAddress::Virtual(testIpB, 2);

        GIVEN("a loopback that received as many packets as the queue holds")
        {
            nTransportLoopback loopback;
            loopback.Link(server, client);

            int8 const payload[] = {1, 2};
            int accepted = 0;
            for (int i = 0; i < nTransport::MaxQueuedPackets; ++i)
                accepted += loopback.TrySend(client, payload, sizeof(payload));
            REQUIRE(nTransport::MaxQueuedPackets == accepted);

            THEN("the next packet is dropped")
            {
                CHECK(!loopback.TrySend(client, payload, sizeof(payload)));
                CHECK(1 == loopback.GetDropped());
                CHECK(nTransport::MaxQueuedPackets == loopback.GetPending());
            }
            AND_THEN("reading one makes room for one")
            {
                int8 received[4] = {};
                int len = 0;
                nAddress from;
                REQUIRE(loopback.TryReceive(received, sizeof(received), len, from));
                CHECK(loopback.TrySend(client, payload, sizeof(payload)));
                CHECK(!loopback.TrySend(client, payload, sizeof(payload)));
            }
        }

        GIVEN("a loopback receiving large packets")
        {
            nTransportLoopback loopback;
            loopback.Link(server, client);

            std::vector<int8> const big(64 * 1024, 7);
            int const fit = nTransport::MaxQueuedBytes / static_cast<int>(big.size());
            int accepted = 0;
            for (int i = 0; i < fit; ++i)
                accepted += loopback.TrySend(client, big.data(), static_cast<int>(big.size()));
            REQUIRE(fit == accepted);

            THEN("the byte limit drops the next one, but a small packet that fits still goes in")
            {
                CHECK(!loopback.TrySend(client, big.data(), static_cast<int>(big.size())));
                CHECK(fit == loopback.GetPending());
                CHECK(loopback.TrySend(client, big.data(), 0));
            }
            AND_THEN("draining the queue frees the bytes again")
            {
                int8 received[16] = {};
                int len = 0;
                nAddress from;
                while (loopback.TryReceive(received, sizeof(received), len, from))
                    ;
                CHECK(0 == loopback.GetPending());
                CHECK(loopback.TrySend(client, big.data(), static_cast<int>(big.size())));
            }
        }
    }

    TEST_CASE("Send Buffer Routes Virtual Peers Through The Transport")
    {
        GIVEN("a send buffer holding one message and a linked loopback")
        {
            nAddress const server = nAddress::Virtual(testIpA, 1);
            nAddress const client = nAddress::Virtual(testIpB, 2);

            nTransportLoopback loopback;
            loopback.Link(server, client);
            cTransportGuard const guard(loopback);

            // one message on the wire: descriptor, message ID, data length, data
            unsigned short wire[] = {htons(42), htons(1234), htons(2), htons(0xbeef), htons(0xf00d)};
            unsigned short* read = wire;
            tJUST_CONTROLLED_PTR<nMessage> message = tNEW(nMessage)(read, 0, 5);

            REQUIRE(42 == message->Descriptor());
            REQUIRE(2 == message->DataLen());

            int const netIDBack = sn_myNetID;
            sn_myNetID = 3;

            nSendBuffer sendBuffer;
            sendBuffer.AddMessage(*message, nullptr);

            WHEN("it is sent to a virtual peer")
            {
                nSocket const unusedSocket;
                sendBuffer.Send(unusedSocket, client, nullptr);
                sn_myNetID = netIDBack;

                THEN("the buffer is cleared and the packet reached the transport")
                {
                    CHECK(0 == sendBuffer.Len());
                    CHECK(1 == loopback.GetPending());
                    CHECK(0 == loopback.GetDropped());
                }

                AND_THEN("the packet carries the message and the trailing sender net ID")
                {
                    nAddress from;
                    unsigned short received[16] = {};
                    int const len = nTransport::ReadPacket(reinterpret_cast<int8*>(received), sizeof(received), from);

                    // three header words, two data words, one net ID word
                    REQUIRE(12 == len);
                    CHECK(42 == ntohs(received[0]));
                    CHECK(1234 == ntohs(received[1]));
                    CHECK(2 == ntohs(received[2]));
                    CHECK(0xbeef == ntohs(received[3]));
                    CHECK(0xf00d == ntohs(received[4]));
                    CHECK(3 == ntohs(received[5]));
                    CHECK(from == server);
                }
            }
        }
    }
}
