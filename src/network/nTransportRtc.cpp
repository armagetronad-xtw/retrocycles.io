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

#include "nTransportRtc.h"

#ifdef HAVE_LIBDATACHANNEL

#include "nNetwork.h"
#include "tConfiguration.h"
#include "tConsole.h"
#include "tString.h"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/eventfd.h>
#endif

// *******************************************************************************************
// settings
// *******************************************************************************************

//! whether the dedicated server accepts browser clients at all; on wherever it is compiled in
static bool sn_webRtcEnable = true;
static tSettingItem<bool> sn_webRtcEnableConf("WEBRTC_ENABLE", sn_webRtcEnable);

//! TCP port of the signalling WebSocket server; 0 means SERVER_PORT + 1
static int sn_webRtcSignalPort = 0;
static tSettingItem<int> sn_webRtcSignalPortConf("WEBRTC_SIGNAL_PORT", sn_webRtcSignalPort);

/* address the signalling server binds to; empty (the default) is every interface, IPv4 and
   IPv6, so browsers reach a server that has only an IP and a port, through the Retrocycles
   signalling gateway. Behind a reverse proxy that terminates TLS: 127.0.0.1 */
static tString sn_webRtcSignalBind("");
static tSettingItem<tString> sn_webRtcSignalBindConf("WEBRTC_SIGNAL_BIND", sn_webRtcSignalBind);

/* UDP ports ICE uses: an explicit range, one port per peer (one port alone is shared by all
   peers, ICE UDP mux); 0/0 (the default) is one shared port, SERVER_PORT + 1000 */
static int sn_webRtcPortMin = 0;
static tSettingItem<int> sn_webRtcPortMinConf("WEBRTC_PORT_MIN", sn_webRtcPortMin);

static int sn_webRtcPortMax = 0;
static tSettingItem<int> sn_webRtcPortMaxConf("WEBRTC_PORT_MAX", sn_webRtcPortMax);

//! address host candidates are rewritten to, for hosts behind 1:1 NAT
static tString sn_webRtcPublicIp("");
static tSettingItem<tString> sn_webRtcPublicIpConf("WEBRTC_PUBLIC_IP", sn_webRtcPublicIp);

/* Proxies whose word on the client address is believed: addresses
   or ranges ("127.0.0.0/8 ::1 10.0.0.5 fd00::/8"). A signalling connection from
   one of them is attributed to the client it names in the request target
   (?X-Forwarded-For=..., the right-most entry that is not a trusted proxy itself,
   or ?X-Real-IP=...); every other connection to its own socket peer. Read when
   the signalling server starts. The whole line counts (blanks separate entries). */
static tString sn_webRtcTrustedProxies("127.0.0.0/8 ::1");
static tConfItemLine sn_webRtcTrustedProxiesConf("WEBRTC_TRUSTED_PROXIES", sn_webRtcTrustedProxies);

/* whether a browser may ask for its game packets on the signalling WebSocket instead of a
   data channel, for browsers that are not allowed WebRTC (a Discord Activity); off, it is
   told "refused" */
static bool sn_webRtcWebSocketEnable = false;
static tSettingItem<bool> sn_webRtcWebSocketEnableConf("WEBRTC_WEBSOCKET_ENABLE", sn_webRtcWebSocketEnable);

/* bytes the library may hold back for a WebSocket client before packets are dropped; TCP
   delivers everything late rather than nothing, so keep it as small as a data channel's */
static int sn_webRtcWebSocketSendCap = 4 * 300;
static tSettingItem<int> sn_webRtcWebSocketSendCapConf("WEBRTC_WEBSOCKET_SEND_CAP", sn_webRtcWebSocketSendCap);

/* Shared proxies: addresses or ranges of proxies that stand for many players
   and do not say which, such as the one in front of a Discord Activity (Cloudflare's).
   A client resolved to one of them gets its own address in 240.0.0.0/4 for each
   connection, so per-IP limits, vote grouping and spam scoring do not lump everyone
   behind it into one machine; IP bans cannot reach such a player (account bans through
   the platform still do). Read when the signalling server starts, like
   WEBRTC_TRUSTED_PROXIES; empty (the default) treats nobody so. */
static tString sn_webRtcSharedProxies("");
static tConfItemLine sn_webRtcSharedProxiesConf("WEBRTC_SHARED_PROXIES", sn_webRtcSharedProxies);

/* Handshakes in flight: the signalling port is public by default and every
   handshake costs a peer connection, so a source address and the whole server may only
   have so many sockets that have not become players yet (logged in to the game; an IPv6
   source is its /48). Players are never counted: behind the Retrocycles signalling
   gateway many of them share one address, so the per-source cap leaves room for a burst
   of joins. 0 is no cap. */
static int sn_webRtcPendingPerSource = 16;
static tSettingItem<int> sn_webRtcPendingPerSourceConf("WEBRTC_PENDING_PER_SOURCE", sn_webRtcPendingPerSource);

static int sn_webRtcPendingMax = 64;
static tSettingItem<int> sn_webRtcPendingMaxConf("WEBRTC_PENDING_MAX", sn_webRtcPendingMax);

//! seconds a signalling socket may stay without a valid message before it is closed; 0 never
static int sn_webRtcSignalTimeout = 10;
static tSettingItem<int> sn_webRtcSignalTimeoutConf("WEBRTC_SIGNAL_TIMEOUT", sn_webRtcSignalTimeout);

//! seconds from the accepted socket to an open data channel (or the WebSocket path) before it is given up; 0 never
static int sn_webRtcHandshakeTimeout = 30;
static tSettingItem<int> sn_webRtcHandshakeTimeoutConf("WEBRTC_HANDSHAKE_TIMEOUT", sn_webRtcHandshakeTimeout);

/* seconds an established peer may stay without a logged in game connection (before its
   login, or after its game connection ended) before it is closed; 0 never. Until it logs
   in it also counts against WEBRTC_PENDING_PER_SOURCE/MAX. Generous, so a client waiting
   for a free slot is not cut off */
static int sn_webRtcLoginTimeout = 60;
static tSettingItem<int> sn_webRtcLoginTimeoutConf("WEBRTC_LOGIN_TIMEOUT", sn_webRtcLoginTimeout);

//! label of the one data channel that carries the game protocol
static char const* sn_webRtcChannelLabel = "aa";

/* The game's own ack layer resends what matters, so a backed up channel should
   drop like a congested UDP link instead of delivering stale state late. Four
   full packets of slack absorb a scheduling hiccup and nothing more. */
static size_t const sn_webRtcSendCap = 4 * 300;

// *******************************************************************************************

nTransportRtc* nTransportRtc::s_server_ = nullptr;

namespace
{
//! the IPv4 (network order) an address without port is attributed to; 0 if it is no address
unsigned int sn_AttributionIPv4(std::string const& address)
{
    nRtcIp ip;
    if (!nRtcTryParseIp(address, ip))
        return 0;
    return nRtcAttributionIPv4(ip);
}

//! "1.2.3.4", or "2001:db8::1 (counted as 240.1.2.3)" for IPv6
std::string sn_DescribeClient(nRtcIp const& ip)
{
    std::string text = nRtcFormatIp(ip);
    if (ip.v6)
    {
        nRtcIp shown;
        unsigned int const v4 = nRtcAttributionIPv4(ip);
        std::memcpy(shown.bytes, &v4, sizeof(v4));
        text += " (counted as " + nRtcFormatIp(shown) + ")";
    }
    return text;
}

char const* sn_CandidateType(rtc::Candidate const& candidate)
{
    switch (candidate.type())
    {
    case rtc::Candidate::Type::Host:
        return "host";
    case rtc::Candidate::Type::ServerReflexive:
        return "srflx";
    case rtc::Candidate::Type::PeerReflexive:
        return "prflx";
    case rtc::Candidate::Type::Relayed:
        return "relay";
    default:
        return "unknown";
    }
}
} // namespace

nTransportRtc::nTransportRtc() {}

nTransportRtc::~nTransportRtc()
{
    Shutdown();
}

// *******************************************************************************************
// *
// *	TryStartServer
// *
// *******************************************************************************************
//!
//!		@return		true if a transport is now registered
//!
// *******************************************************************************************

bool nTransportRtc::TryStartServer()
{
    if (s_server_)
        return true;

    if (!sn_webRtcEnable)
        return false;

    std::unique_ptr<nTransportRtc> server(new nTransportRtc());
    if (!server->TryListen())
        return false;

    s_server_ = server.release();
    nTransport::Register(s_server_);

    return true;
}

// *******************************************************************************************
// *
// *	StopServer
// *
// *******************************************************************************************

void nTransportRtc::StopServer()
{
    if (!s_server_)
        return;

    nTransport::Unregister(s_server_);
    delete s_server_;
    s_server_ = nullptr;
}

// *******************************************************************************************
// *
// *	TryListen
// *
// *******************************************************************************************
//!
//!		@return		true if the signalling server came up
//!
// *******************************************************************************************

bool nTransportRtc::TryListen()
{
#ifdef __linux__
    wakeRead_ = wakeWrite_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#elif !defined(WIN32)
    int fds[2] = {-1, -1};
    if (0 == pipe(fds))
    {
        wakeRead_ = fds[0];
        wakeWrite_ = fds[1];
    }
#endif

    int const signalPort = sn_webRtcSignalPort > 0 ? sn_webRtcSignalPort : static_cast<int>(sn_serverPort) + 1;

    // fixed for the lifetime of the signalling server: library threads read it without a lock
    std::vector<std::string> rejected;
    trustedProxies_ = nRtcParseIpRanges(std::string(static_cast<char const*>(sn_webRtcTrustedProxies)), &rejected);
    for (std::string const& entry : rejected)
        con << "WebRTC: WEBRTC_TRUSTED_PROXIES: ignoring \"" << entry.c_str() << "\", not an address or range.\n";

    rejected.clear();
    sharedProxies_ = nRtcParseIpRanges(std::string(static_cast<char const*>(sn_webRtcSharedProxies)), &rejected);
    for (std::string const& entry : rejected)
        con << "WebRTC: WEBRTC_SHARED_PROXIES: ignoring \"" << entry.c_str() << "\", not an address or range.\n";
    sharedSalt_ = std::random_device{}();

    // fixed as well: one multiplexed socket cannot move while peers use it
    icePorts_ = nRtcChooseIcePorts(static_cast<int>(sn_serverPort), sn_webRtcPortMin, sn_webRtcPortMax);

    std::string bind(static_cast<char const*>(sn_webRtcSignalBind));
    for (int attempt = 0; attempt < 2 && !signalServer_; ++attempt)
    {
        try
        {
            rtc::WebSocketServer::Configuration config;
            config.port = static_cast<uint16_t>(signalPort);
            if (!bind.empty())
                config.bindAddress = bind;

            signalServer_.reset(new rtc::WebSocketServer(config));
            signalServer_->onClient([this](std::shared_ptr<rtc::WebSocket> socket) { OnSignalClient(socket); });
        }
        catch (std::exception const& e)
        {
            signalServer_.reset();
            if (bind.empty())
            {
                // every interface means IPv6 and IPv4; a host without IPv6 still has IPv4
                con << "WebRTC: signalling on every interface failed (" << e.what() << "), trying 0.0.0.0.\n";
                bind = "0.0.0.0";
                continue;
            }
            con << "WebRTC: signalling server on port " << signalPort << " failed: " << e.what() << "\n";
            return false;
        }
    }
    if (!signalServer_)
        return false;

    bool const loopback = bind.compare(0, 4, "127.") == 0 || bind == "::1" || bind == "localhost";
    con << "WebRTC: accepting browser clients, signalling on " << (bind.empty() ? std::string("every interface") : bind).c_str()
        << ":" << signalPort << ".\n";

    // what a host has to open: the signalling port, unless a local proxy fronts it, and ICE
    std::string const ice = nRtcDescribeIcePorts(icePorts_);
    if (loopback)
        con << "WebRTC: firewall: open " << ice.c_str() << " for browser players (game traffic), next to UDP " << sn_serverPort
            << "; signalling stays on loopback, behind your proxy.\n";
    else
        con << "WebRTC: firewall: open TCP " << signalPort << " (signalling) and " << ice.c_str()
            << " (game traffic) for browser players, next to UDP " << sn_serverPort << ".\n";

    std::string trusted;
    for (nRtcIpRange const& range : trustedProxies_)
        trusted += " " + nRtcFormatIp(range.base) + "/" + std::to_string(range.prefix);
    con << "WebRTC: client addresses forwarded by" << (trusted.empty() ? std::string(" nobody") : trusted).c_str()
        << " are believed (WEBRTC_TRUSTED_PROXIES).\n";
    if (!sharedProxies_.empty())
        con << "WebRTC: clients of " << static_cast<int>(sharedProxies_.size())
            << " shared proxy range(s) get an address per connection (WEBRTC_SHARED_PROXIES).\n";
    con << "WebRTC: at most " << sn_webRtcPendingPerSource << " handshakes per address and " << sn_webRtcPendingMax
        << " in all (WEBRTC_PENDING_PER_SOURCE/MAX); silent sockets close after " << sn_webRtcSignalTimeout
        << " s (WEBRTC_SIGNAL_TIMEOUT), connections without a login after " << sn_webRtcLoginTimeout
        << " s (WEBRTC_LOGIN_TIMEOUT).\n";

    {
        std::lock_guard<std::mutex> lock(watchdogMutex_);
        watchdogStop_ = false;
    }
    watchdog_ = std::thread([this] { Watchdog(); });

    return true;
}

// *******************************************************************************************
// *
// *	Shutdown
// *
// *******************************************************************************************

void nTransportRtc::Shutdown()
{
    if (watchdog_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(watchdogMutex_);
            watchdogStop_ = true;
        }
        watchdogWake_.notify_all();
        watchdog_.join();
    }

    if (signalServer_)
    {
        signalServer_->stop();
        signalServer_.reset();
    }

    std::map<unsigned short, nRtcPeerPtr> peers;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        peers.swap(peers_);
        // an offer still in flight on a library thread must not attach a connection now (CreateConnection)
        for (auto& entry : peers)
            entry.second->closed = true;
    }

    // drop the peers outside the lock; destroying a connection joins its threads
    for (auto& entry : peers)
    {
        if (entry.second->connection)
            entry.second->connection->resetCallbacks();
        if (entry.second->signal)
            entry.second->signal->resetCallbacks();
    }
    peers.clear();

#ifndef WIN32
    if (wakeWrite_ >= 0 && wakeWrite_ != wakeRead_)
        close(wakeWrite_);
    if (wakeRead_ >= 0)
        close(wakeRead_);
#endif
    wakeRead_ = wakeWrite_ = -1;
}

// *******************************************************************************************
// *
// *	OnSignalClient
// *
// *******************************************************************************************
//!
//!		@param	socket	the freshly accepted signalling connection
//!
// *******************************************************************************************

//! pending peers (not closed, claimed by a game connection or refused) other than except that
//! count against key; their number in all in *total. The caller holds peerMutex_.
int nTransportRtc::CountPendingFrom(unsigned int key, nRtcPeer const* except, int* total) const
{
    std::vector<unsigned int> keys;
    for (auto const& entry : peers_)
    {
        nRtcPeer const& other = *entry.second;
        if (&other == except || other.closed || other.claimed || other.refused)
            continue;
        keys.push_back(other.sourceKey);
    }
    if (total)
        *total = static_cast<int>(keys.size());
    return nRtcCountSource(keys, key);
}

void nTransportRtc::OnSignalClient(std::shared_ptr<rtc::WebSocket> const& socket)
{
    nRtcPeerPtr peer(new nRtcPeer());
    peer->signal = socket;
    peer->accepted = std::chrono::steady_clock::now();

    /* A placeholder identity until the data channel opens; OnChannel() fixes the
       real one before the first packet arrives, so the virtual address never
       changes during a session. */
    unsigned int signalSource = 0;
    if (auto remote = socket->remoteAddress())
    {
        nRtcIp ip;
        if (nRtcTryParseIp(nRtcStripPort(*remote), ip))
        {
            peer->signalIp = nRtcAttributionIPv4(ip);
            signalSource = nRtcPendingSourceIPv4(ip);
            peer->trustedPeer = nRtcRangesContain(trustedProxies_, ip);
        }
    }
    // a trusted proxy counts per source only once it named its client (ResolveClient)
    peer->sourceKey = nRtcPendingSourceKey(peer->trustedPeer, false, nRtcClient_Unknown, signalSource, 0);

    // handshakes in flight from this source and overall (never players in the game)
    nRtcAdmission admission = nRtcAdmit;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        int total = 0;
        int const fromSource = CountPendingFrom(peer->sourceKey, nullptr, &total);
        admission = nRtcAdmitPending(fromSource, total, sn_webRtcPendingPerSource, sn_webRtcPendingMax);
        peer->refused = admission != nRtcAdmit;

        if (!ids_.TryAllocate(peer->id))
            peer->id = 0;
        else
        {
            peer->address = nAddress::Virtual(peer->signalIp, peer->id);
            peers_[peer->id] = peer;
        }
    }

    if (!peer->id)
    {
        // every connection ID is in flight; nothing to give this client
        socket->close();
        return;
    }

    std::weak_ptr<nRtcPeer> weak(peer);

    if (admission != nRtcAdmit)
    {
        // say why once the upgrade is done, then close; Expire() frees the ID
        std::string const reason = nRtcAdmissionReason(admission);
        socket->onOpen(
            [this, weak, reason]()
            {
                if (nRtcPeerPtr strong = weak.lock())
                    Refuse(strong, reason);
            });
        socket->onClosed(
            [this, weak]()
            {
                if (nRtcPeerPtr strong = weak.lock())
                {
                    strong->closed = true;
                    Wake();
                }
            });
        if (socket->isOpen())
            Refuse(peer, reason);
        return;
    }

    socket->onMessage(
        [this, weak](rtc::binary data)
        {
            // game packets, once the peer switched to the WebSocket; ignored before
            nRtcPeerPtr strong = weak.lock();
            if (strong && strong->websocket)
                OnPayload(strong, reinterpret_cast<int8 const*>(data.data()), static_cast<int>(data.size()));
        },
        [this, weak](rtc::string text)
        {
            if (nRtcPeerPtr strong = weak.lock())
                OnSignalMessage(strong, text);
        });

    socket->onClosed(
        [this, weak]()
        {
            /* Only let the socket go. The media path outlives signalling, and we
               close the socket ourselves as soon as the connection is up, which
               can happen before the data channel appears; tearing the peer down
               here would race with that. A client that never completes ICE is
               dealt with by the peer connection going to Failed. A WebSocket peer,
               or one that never got as far as an offer, has no other path: its
               socket closing ends it, and Expire() lets the socket go on the game
               thread.
               Dropping the socket here destroys this very callback: nothing may
               touch the closure (this, weak) after the reset. */
            if (nRtcPeerPtr strong = weak.lock())
            {
                std::lock_guard<std::mutex> lock(peerMutex_);
                if (strong->websocket || !strong->connection)
                {
                    strong->closed = true;
                    Wake();
                    return;
                }
                strong->signal.reset();
            }
        });

    // the watchdog gives up on this socket if nothing useful comes of it (ExpirePending)
}

// *******************************************************************************************
// *
// *	CreateConnection
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer whose offer just arrived
//!		@return			true if it has a peer connection now
//!
//!     Lazy, on the first offer, so a socket that never says anything costs no peer
//!     connection (the signalling port is public by default).
// *******************************************************************************************

bool nTransportRtc::CreateConnection(nRtcPeerPtr const& peer)
{
    if (peer->connection)
        return true;

    rtc::Configuration config;
    if (icePorts_.begin)
    {
        config.portRangeBegin = static_cast<uint16_t>(icePorts_.begin);
        config.portRangeEnd = static_cast<uint16_t>(icePorts_.end);
        config.enableIceUdpMux = icePorts_.mux;
    }

    std::shared_ptr<rtc::PeerConnection> connection;
    try
    {
        connection.reset(new rtc::PeerConnection(config));
    }
    catch (std::exception const& e)
    {
        Report(std::string("WebRTC: peer connection failed: ") + e.what());
        peer->closed = true;
        Wake();
        return false;
    }

    std::weak_ptr<nRtcPeer> weak(peer);

    connection->onGatheringStateChange(
        [this, weak](rtc::PeerConnection::GatheringState state)
        {
            // one shot: the complete local description carries every host candidate
            if (state != rtc::PeerConnection::GatheringState::Complete)
                return;
            if (nRtcPeerPtr strong = weak.lock())
                OnLocalDescription(strong);
        });

    connection->onStateChange(
        [this, weak](rtc::PeerConnection::State state)
        {
            nRtcPeerPtr strong = weak.lock();
            if (!strong)
                return;

            if (state == rtc::PeerConnection::State::Connected)
            {
                // signalling has done its job
                if (std::shared_ptr<rtc::WebSocket> signal = GetSignal(strong))
                    signal->close();
            }
            else if (state == rtc::PeerConnection::State::Disconnected ||
                     state == rtc::PeerConnection::State::Failed ||
                     state == rtc::PeerConnection::State::Closed)
            {
                strong->closed = true;
                Wake();
            }
        });

    connection->onDataChannel(
        [this, weak](std::shared_ptr<rtc::DataChannel> channel)
        {
            if (nRtcPeerPtr strong = weak.lock())
                OnChannel(strong, channel);
        });

    {
        // a peer the game thread gave up on meanwhile (ExpirePending) gets none
        std::lock_guard<std::mutex> lock(peerMutex_);
        if (!peer->closed && peer->signal)
            peer->connection = connection;
    }
    if (!peer->connection)
    {
        connection->resetCallbacks();
        return false;
    }
    return true;
}

// *******************************************************************************************
// *
// *	Refuse
// *
// *******************************************************************************************
//!
//!		@param	peer	a peer turned away at accept
//!		@param	reason	what the client is told
//!
// *******************************************************************************************

void nTransportRtc::Refuse(nRtcPeerPtr const& peer, std::string const& reason)
{
    std::shared_ptr<rtc::WebSocket> signal = GetSignal(peer);
    if (signal && signal->isOpen())
    {
        nRtcSignal answer;
        answer.type = "refused";
        answer.reason = reason;
        try
        {
            signal->send(nRtcFormatSignal(answer));
            signal->close();
        }
        catch (std::exception const&)
        {
        }
    }
    peer->closed = true;
    Wake();
}

// *******************************************************************************************
// *
// *	ExpirePending
// *
// *******************************************************************************************
//!
//!     Game thread: closes the handshakes that took too long, WEBRTC_SIGNAL_TIMEOUT
//!     without a valid message or WEBRTC_HANDSHAKE_TIMEOUT without an open channel, and
//!     the established peers that stay WEBRTC_LOGIN_TIMEOUT without a logged in game
//!     connection. It also marks the peers the game has a connection for as claimed,
//!     which takes them out of the pending caps.
// *******************************************************************************************

void nTransportRtc::ExpirePending()
{
    auto const now = std::chrono::steady_clock::now();

    // the game's connections only say something while it is a server; peers that are
    // claimed already are looked at once a second, the others on every call
    bool const serving = sn_GetNetState() == nSERVER;
    bool const checkClaimed = serving && now - lastClaimCheck_ >= std::chrono::seconds(1);
    if (checkClaimed)
        lastClaimCheck_ = now;

    struct nExpired
    {
        nRtcPeerPtr peer;
        nRtcPendingExpiry expiry;
        nAddress address;
    };
    std::vector<nExpired> expired;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        for (auto const& entry : peers_)
        {
            nRtcPeerPtr const& peer = entry.second;
            if (peer->closed)
                continue;

            nRtcPendingExpiry expiry = nRtcPending_Alive;
            if (peer->established)
            {
                if (!serving || (peer->claimed && !checkClaimed))
                    continue;

                bool const connected = sn_IsPeerConnected(peer->address);
                peer->claimed = connected;
                if (connected)
                {
                    peer->unclaimedSince = std::chrono::steady_clock::time_point{};
                    continue;
                }
                if (peer->unclaimedSince == std::chrono::steady_clock::time_point{})
                    peer->unclaimedSince = now;

                long long const unclaimed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - peer->unclaimedSince).count();
                expiry = nRtcUnclaimedExpired(unclaimed, 1000LL * sn_webRtcLoginTimeout);
            }
            else
            {
                long long const age =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - peer->accepted).count();
                expiry = nRtcPendingExpired(age, peer->gotMessage, 1000LL * sn_webRtcSignalTimeout,
                                            1000LL * sn_webRtcHandshakeTimeout);
            }
            if (expiry == nRtcPending_Alive)
                continue;
            peer->closed = true;
            expired.push_back(nExpired{peer, expiry, peer->address});
        }
    }

    // Expire() lets the peer connection, the channel and the socket go
    for (nExpired const& entry : expired)
    {
        nRtcPeerPtr const& peer = entry.peer;
        if (std::shared_ptr<rtc::WebSocket> signal = GetSignal(peer))
        {
            try
            {
                signal->close();
            }
            catch (std::exception const&)
            {
            }
        }
        if (entry.expiry == nRtcPending_Handshake)
            con << "WebRTC: connection " << peer->id << " from " << static_cast<char const*>(entry.address.GetAddress())
                << " gave up: no data channel within " << sn_webRtcHandshakeTimeout << " s.\n";
        else if (entry.expiry == nRtcPending_Login)
            con << "WebRTC: connection " << peer->id << " from " << static_cast<char const*>(entry.address.GetAddress())
                << " closed: not logged in for " << sn_webRtcLoginTimeout << " s.\n";
    }
}

// *******************************************************************************************
// *
// *	Watchdog
// *
// *******************************************************************************************
//!
//!     Own thread: wakes the game thread once a second while there are peers: handshakes
//!     are timed out, and claimed peers whose game connection ended are noticed.
// *******************************************************************************************

void nTransportRtc::Watchdog()
{
    std::unique_lock<std::mutex> lock(watchdogMutex_);
    while (!watchdogStop_)
    {
        watchdogWake_.wait_for(lock, std::chrono::seconds(1));
        if (watchdogStop_)
            break;

        bool any = false;
        {
            std::lock_guard<std::mutex> peers(peerMutex_);
            for (auto const& entry : peers_)
                if (!entry.second->closed)
                {
                    any = true;
                    break;
                }
        }
        if (any)
            Wake();
    }
}

// *******************************************************************************************
// *
// *	GetSignal
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer to look at
//!		@return			its signalling connection, NULL if it is already gone
//!
// *******************************************************************************************

std::shared_ptr<rtc::WebSocket> nTransportRtc::GetSignal(nRtcPeerPtr const& peer)
{
    std::lock_guard<std::mutex> lock(peerMutex_);
    return peer->signal;
}

// *******************************************************************************************
// *
// *	OnSignalMessage
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer the message belongs to
//!		@param	text	the raw JSON
//!
// *******************************************************************************************

void nTransportRtc::OnSignalMessage(nRtcPeerPtr const& peer, std::string const& text)
{
    // the handshake is over once a message arrives, so the request target is known
    if (!peer->clientResolved.exchange(true) && !ResolveClient(peer))
        return; // refused: its forwarded client has too many handshakes in flight
    if (peer->closed)
        return;

    nRtcSignal signal;
    if (!nRtcTryParseSignal(text, signal) || peer->websocket)
        return;
    peer->gotMessage = true;

    if (signal.type == "websocket")
    {
        OnWebSocketRequest(peer);
        return;
    }

    // the peer connection exists from the offer on; anything before it is ignored
    if (signal.type == "offer" && !CreateConnection(peer))
        return;
    if (!peer->connection)
        return;

    try
    {
        if (signal.type == "offer" || signal.type == "answer")
            peer->connection->setRemoteDescription(rtc::Description(signal.sdp, signal.type));
        else if (signal.type == "candidate")
            peer->connection->addRemoteCandidate(rtc::Candidate(signal.candidate, signal.mid));
    }
    catch (std::exception const& e)
    {
        Report("WebRTC: bad " + signal.type + ": " + e.what());
    }
}

// *******************************************************************************************
// *
// *	ResolveClient
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer whose signalling connection just delivered its first message
//!
//!     Finds what the signalling connection says about the client: the client a trusted
//!     proxy names, else the signalling socket's peer. Stored in peer->clientIp and
//!     peer->clientSource (clientIp 0 if a trusted proxy named nobody); PinAddress()
//!     weighs it against the ICE address. For a trusted proxy's connection
//!     it also applies WEBRTC_PENDING_PER_SOURCE to the forwarded client; false if that
//!     refused it.
// *******************************************************************************************

bool nTransportRtc::ResolveClient(nRtcPeerPtr const& peer)
{
    std::shared_ptr<rtc::WebSocket> signal = GetSignal(peer);
    if (!signal)
        return true;

    std::string remote, target;
    if (auto address = signal->remoteAddress())
        remote = nRtcStripPort(*address);
    if (auto path = signal->path())
        target = *path;

    nRtcIp client;
    std::string note;
    nRtcClientSource const source = nRtcResolveClient(remote, target, trustedProxies_, client);
    switch (source)
    {
    case nRtcClient_Forwarded:
        peer->clientIp = nRtcAttributionIPv4(client);
        note = sn_DescribeClient(client) + ", forwarded by " + remote;
        break;
    case nRtcClient_Socket:
        peer->clientIp = nRtcAttributionIPv4(client);
        note = sn_DescribeClient(client) + ", signalling peer";
        if (!nRtcQueryValues(target, "X-Forwarded-For").empty() || !nRtcQueryValues(target, "X-Real-IP").empty())
            note += " (not a trusted proxy: its forwarded address is ignored)";
        break;
    case nRtcClient_Proxy:
        note = "trusted proxy " + remote + " forwarded no usable client address";
        break;
    case nRtcClient_Unknown:
        note = "signalling peer \"" + remote + "\" unreadable";
        break;
    }
    peer->clientSource = source;

    // a shared proxy's client: its own address for this connection (WEBRTC_SHARED_PROXIES)
    if (peer->clientIp && nRtcRangesContain(sharedProxies_, client))
    {
        nRtcIp own;
        unsigned int const v4 = nRtcSharedProxyIPv4(sharedSalt_, ++sharedSerial_);
        std::memcpy(own.bytes, &v4, sizeof(v4));
        peer->clientIp = v4;
        note = nRtcFormatIp(own) + ", its own address behind the shared proxy " + nRtcFormatIp(client) + " (" + note + ")";
    }

    // a private room's ticket; the game thread checks it when the client logs in
    std::vector<std::string> const tickets = nRtcQueryValues(target, "ticket");

    bool over = false;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        peer->clientNote = note;
        if (!tickets.empty() && tickets.front().size() <= 2048)
            peer->ticket = tickets.front();

        // behind a trusted proxy the per-source cap applies to the client it forwards
        if (peer->trustedPeer)
        {
            unsigned int const key = nRtcPendingSourceKey(true, true, source, peer->signalIp,
                                                          source == nRtcClient_Forwarded ? nRtcPendingSourceIPv4(client) : 0);
            if (key)
            {
                int const fromSource = CountPendingFrom(key, peer.get(), nullptr);
                over = nRtcAdmitPending(fromSource, 0, sn_webRtcPendingPerSource, 0) != nRtcAdmit;
                peer->sourceKey = key;
                peer->refused = over;
            }
        }
    }

    if (over)
    {
        Refuse(peer, nRtcAdmissionReason(nRtcRefuse_Source));
        return false;
    }
    return true;
}

// *******************************************************************************************
// *
// *	OnLocalDescription
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer whose candidate gathering just finished
//!
// *******************************************************************************************

void nTransportRtc::OnLocalDescription(nRtcPeerPtr const& peer)
{
    std::shared_ptr<rtc::WebSocket> signal = GetSignal(peer);
    if (!peer->connection || !signal || !signal->isOpen())
        return;

    auto description = peer->connection->localDescription();
    if (!description)
        return;

    nRtcSignal answer;
    answer.type = description->typeString();
    answer.sdp = nRtcRewriteHostCandidates(std::string(*description),
                                           std::string(static_cast<char const*>(sn_webRtcPublicIp)));

    signal->send(nRtcFormatSignal(answer));
}

// *******************************************************************************************
// *
// *	OnChannel
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer the channel belongs to
//!		@param	channel	the data channel the browser opened
//!
// *******************************************************************************************

void nTransportRtc::OnChannel(nRtcPeerPtr const& peer, std::shared_ptr<rtc::DataChannel> const& channel)
{
    // one game channel per peer: a second one, or one after the WebSocket path was agreed,
    // would pin the identity again in the middle of the session
    bool first = false;
    if (channel->label() == sn_webRtcChannelLabel)
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        if (!peer->pinned)
        {
            peer->pinned = true;
            peer->channel = channel;
            first = true;
        }
    }
    if (!first)
    {
        channel->close();
        return;
    }

    // pin the identity now (PinAddress); everything after this point uses peer->address
    unsigned int iceIp = 0;
    bool iceDirect = false;
    std::string ice = "none", iceClient;
    try
    {
        rtc::Candidate local, remote;
        if (peer->connection->getSelectedCandidatePair(&local, &remote))
        {
            nRtcIp parsed;
            auto address = remote.address();
            if (address && nRtcTryParseIp(*address, parsed))
            {
                char const* const type = sn_CandidateType(remote);
                iceIp = nRtcAttributionIPv4(parsed);
                iceDirect = nRtcIceTypeIsDirect(type);
                iceClient = sn_DescribeClient(parsed);
                ice = *address + " (" + type + ")";
            }
        }
    }
    catch (std::exception const&)
    {
        iceIp = 0;
        iceDirect = false;
    }

    PinAddress(peer, iceIp, iceDirect, iceClient, "ICE remote " + ice);

    std::weak_ptr<nRtcPeer> weak(peer);

    channel->onMessage(
        [this, weak](rtc::binary data)
        {
            nRtcPeerPtr strong = weak.lock();
            if (strong)
                OnPayload(strong, reinterpret_cast<int8 const*>(data.data()), static_cast<int>(data.size()));
        },
        [](rtc::string) {});

    channel->onClosed(
        [this, weak]()
        {
            nRtcPeerPtr strong = weak.lock();
            if (strong)
            {
                strong->closed = true;
                Wake();
            }
        });
}

// *******************************************************************************************
// *
// *	OnWebSocketRequest
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer that asked for its game packets on the signalling socket
//!
//!     Agrees ("websocket") if WEBRTC_WEBSOCKET_ENABLE is set, else says "refused" and
//!     closes. From the agreement on, binary frames on the socket are game packets both
//!     ways and the socket closing ends the peer; its peer connection stays idle.
// *******************************************************************************************

void nTransportRtc::OnWebSocketRequest(nRtcPeerPtr const& peer)
{
    std::shared_ptr<rtc::WebSocket> signal = GetSignal(peer);
    if (!signal || !signal->isOpen())
        return;

    nRtcSignal answer;
    bool hasChannel;
    {
        // taken together with the check, so a data channel opening meanwhile cannot pin as well
        std::lock_guard<std::mutex> lock(peerMutex_);
        hasChannel = peer->pinned;
        if (sn_webRtcWebSocketEnable && !hasChannel)
            peer->pinned = true;
    }

    if (!sn_webRtcWebSocketEnable || hasChannel)
    {
        answer.type = "refused";
        answer.reason = hasChannel ? "data channel already open" : "this server does not take game packets over WebSocket";
        try
        {
            signal->send(nRtcFormatSignal(answer));
            signal->close();
        }
        catch (std::exception const&)
        {
        }
        // nothing more will come of this peer: free its connection ID
        peer->closed = true;
        Wake();
        return;
    }

    // the identity is fixed before the first packet can arrive, as for a data channel
    PinAddress(peer, 0, false, std::string(), "game packets over the signalling WebSocket");
    peer->websocket = true;

    answer.type = "websocket";
    try
    {
        signal->send(nRtcFormatSignal(answer));
    }
    catch (std::exception const& e)
    {
        Report(std::string("WebRTC: WebSocket agreement failed: ") + e.what());
        peer->closed = true;
        Wake();
    }
}

// *******************************************************************************************
// *
// *	PinAddress
// *
// *******************************************************************************************
//!
//!		@param	peer	the peer whose game packets are about to flow
//!		@param	iceIp	    the selected ICE pair's remote IPv4 (attribution form), 0 if there is none
//!		@param	iceDirect	whether that pair is direct (host, srflx, prflx), not relayed
//!		@param	iceClient	the ICE remote address for the console
//!		@param	route	    how the packets travel, for the console line
//!
//!     Fixes peer->address for good: the connection ID in the port routes the packets;
//!     the IPv4 is who the player is for bans, per-IP limits and logs, in the order of
//!     nRtcChoosePeerAddress(): the client a trusted proxy named, the
//!     direct ICE address, the signalling peer, a relayed ICE address, the proxy itself.
// *******************************************************************************************

void nTransportRtc::PinAddress(nRtcPeerPtr const& peer, unsigned int iceIp, bool iceDirect,
                               std::string const& iceClient, std::string const& route)
{
    unsigned int ip = 0;
    nRtcAddressSource const source =
        nRtcChoosePeerAddress(static_cast<nRtcClientSource>(peer->clientSource.load()), peer->clientIp, iceIp,
                              iceDirect, peer->signalIp, ip);

    // the game thread reads the address under the lock (DoTrySend, ExpirePending)
    std::string note;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        peer->address = nAddress::Virtual(ip, peer->id);
        note = peer->clientNote;
    }
    if (note.empty())
        note = "no signalling message";

    // the client and which rule found it; what the signalling said when it lost
    std::string client;
    switch (source)
    {
    case nRtcAddress_Forwarded:
    case nRtcAddress_Signalling:
        client = note;
        break;
    case nRtcAddress_IceDirect:
    case nRtcAddress_IceRelayed:
        client = iceClient + ", " + nRtcAddressSourceName(source) + " (signalling: " + note + ")";
        break;
    default:
        client = std::string(static_cast<char const*>(peer->address.GetAddress())) + ", " +
                 nRtcAddressSourceName(source) + " (" + note + ")";
        break;
    }
    Report(std::string("WebRTC: peer ") + static_cast<char const*>(peer->address.ToString()) + " is client " + client +
           "; " + route + ".");

    // no longer a handshake in flight; ExpirePending() claims it once it logs in to the game
    peer->established = true;
}

// *******************************************************************************************
// *
// *	OnPayload
// *
// *******************************************************************************************
//!
//!		@param	peer	the sender
//!		@param	data	the packet
//!		@param	len		its length
//!
// *******************************************************************************************

void nTransportRtc::OnPayload(nRtcPeerPtr const& peer, int8 const* data, int len)
{
    Enqueue(peer->address, data, len);

    if (!peer->announced.exchange(true))
        Report(std::string("WebRTC: first packet from ") + static_cast<char const*>(peer->address.ToString()) + ".");

    Wake();
}

// *******************************************************************************************
// *
// *	Report
// *
// *******************************************************************************************
//!
//!		@param	message	what to tell the user, once the game thread gets around to it
//!
// *******************************************************************************************

void nTransportRtc::Report(std::string const& message)
{
    std::lock_guard<std::mutex> lock(peerMutex_);
    reports_.push_back(message);
}

// *******************************************************************************************
// *
// *	Expire
// *
// *******************************************************************************************

void nTransportRtc::Expire()
{
    std::vector<nRtcPeerPtr> gone;
    std::vector<nAddress> addresses;

    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        for (auto i = peers_.begin(); i != peers_.end();)
        {
            if (i->second->closed)
            {
                ids_.Release(i->first);
                gone.push_back(i->second);
                addresses.push_back(i->second->address);
                i = peers_.erase(i);
            }
            else
            {
                ++i;
            }
        }
    }

    // destroying a peer connection joins its threads; never do that under the lock
    for (nRtcPeerPtr const& peer : gone)
    {
        if (peer->connection)
            peer->connection->resetCallbacks();
        if (peer->signal)
            peer->signal->resetCallbacks();
    }

    // the browser is gone: free its slot as soon as the transport knows, not after the game timeout
    for (nAddress const& address : addresses)
        sn_DisconnectPeer(address, tOutput("$network_kill_transport_closed"));
}

// *******************************************************************************************
// *
// *	Wake
// *
// *******************************************************************************************

void nTransportRtc::Wake()
{
#ifndef WIN32
    if (wakeWrite_ < 0)
        return;

    uint64_t const one = 1;
    ssize_t const written = write(wakeWrite_, &one, wakeWrite_ == wakeRead_ ? sizeof(one) : 1);
    (void)written;
#endif
}

// *******************************************************************************************
// *
// *	DoTrySend
// *
// *******************************************************************************************
//!
//!		@param	peer	the virtual address to send to
//!		@param	data	the packet
//!		@param	len		its length
//!		@return			false if the packet was dropped
//!
// *******************************************************************************************

bool nTransportRtc::DoTrySend(nAddress const& peer, int8 const* data, int len)
{
    if (len < 0 || !peer.IsVirtual())
        return false;

    std::shared_ptr<rtc::DataChannel> channel;
    std::shared_ptr<rtc::WebSocket> socket;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        // the whole address, not just the ID: a reused ID must not reach another client
        auto found = peers_.find(peer.GetConnectionId());
        if (found == peers_.end() || found->second->closed || nAddress::Compare(found->second->address, peer) != 0)
            return false;

        channel = found->second->channel;
        if (found->second->websocket)
            socket = found->second->signal;
    }

    if (socket)
    {
        // the same drop policy; a stalled TCP connection must not pile up stale state
        if (!socket->isOpen() || socket->bufferedAmount() > static_cast<size_t>(std::max(sn_webRtcWebSocketSendCap, 0)))
            return false;

        try
        {
            return socket->send(reinterpret_cast<rtc::byte const*>(data), static_cast<size_t>(len));
        }
        catch (std::exception const&)
        {
            return false;
        }
    }

    if (!channel || !channel->isOpen())
        return false;

    // a congested channel drops, like a congested UDP link would
    if (channel->bufferedAmount() > sn_webRtcSendCap)
        return false;

    try
    {
        return channel->send(reinterpret_cast<rtc::byte const*>(data), static_cast<size_t>(len));
    }
    catch (std::exception const&)
    {
        return false;
    }
}

// *******************************************************************************************
// *
// *	DoWakeFd
// *
// *******************************************************************************************
//!
//!		@return		the file descriptor select() should watch, -1 if there is none
//!
// *******************************************************************************************

int nTransportRtc::DoWakeFd() const
{
    return wakeRead_;
}

std::string nTransportRtc::DoTicket(nAddress const& peer) const
{
    if (!peer.IsVirtual())
        return std::string();

    std::lock_guard<std::mutex> lock(peerMutex_);
    auto const found = peers_.find(peer.GetConnectionId());
    if (found == peers_.end() || nAddress::Compare(found->second->address, peer) != 0)
        return std::string();
    return found->second->ticket;
}

// *******************************************************************************************
// *
// *	OnDrainWakeFd
// *
// *******************************************************************************************

void nTransportRtc::OnDrainWakeFd()
{
#ifndef WIN32
    if (wakeRead_ >= 0)
    {
        char buffer[256];
        while (read(wakeRead_, buffer, sizeof(buffer)) > 0)
            ;
    }
#endif

    // the game thread is here, so this is the place to bury closed peers and to
    // say out loud what the library threads had to report
    std::vector<std::string> reports;
    {
        std::lock_guard<std::mutex> lock(peerMutex_);
        reports.swap(reports_);
    }

    for (std::string const& report : reports)
        con << report.c_str() << "\n";

    ExpirePending();
    Expire();
}

#endif // HAVE_LIBDATACHANNEL
