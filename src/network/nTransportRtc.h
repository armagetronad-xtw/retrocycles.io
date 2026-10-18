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

// nTransportRtc.h: server side WebRTC DataChannel transport, built on
// libdatachannel.

#ifndef ArmageTron_NTRANSPORTRTC_H
#define ArmageTron_NTRANSPORTRTC_H

#include "config.h"

#ifdef HAVE_LIBDATACHANNEL

#include "nTransport.h"
#include "nTransportRtcSupport.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtc
{
class DataChannel;
class PeerConnection;
class WebSocket;
class WebSocketServer;
} // namespace rtc

/* One rtc::PeerConnection per browser client, each with one unreliable,
   unordered DataChannel labelled "aa" that carries the ordinary game packets.
   Signalling runs over libdatachannel's WebSocket server, bound to loopback so
   a reverse proxy can terminate TLS in front of it.

   Threading: every libdatachannel callback runs on a library thread and may
   only touch this class's own members. Packets go into the base class queue and
   a byte into the wake FD; closed peers are only torn down on the game thread,
   in Expire(), which DrainWakeFd() calls.
*/
class nTransportRtc : public nTransport
{
public:
    ~nTransportRtc() override;

    //! creates the transport and registers it, if WEBRTC_ENABLE is set; false on failure
    static bool TryStartServer();

    //! unregisters and destroys the transport, if there is one
    static void StopServer();

private:
    nTransportRtc();

    //! one browser client: its peer connection, its data channel and its identity
    struct nRtcPeer
    {
        std::shared_ptr<rtc::PeerConnection> connection{};
        std::shared_ptr<rtc::DataChannel> channel{};
        std::shared_ptr<rtc::WebSocket> signal{};
        nAddress address{};                //!< virtual address, fixed once the channel opens; written under peerMutex_
        unsigned int signalIp = 0;         //!< IPv4 of the signalling connection, network order
        std::atomic<unsigned int> clientIp{0};    //!< client IPv4 the signalling says, network order; 0 = not known
        std::atomic<int> clientSource{nRtcClient_Unknown}; //!< how ResolveClient() found it (nRtcClientSource)
        std::atomic<bool> clientResolved{false};  //!< whether ResolveClient() ran
        std::string clientNote{};          //!< how the client was found, for the console; under peerMutex_
        unsigned short id = 0;             //!< connection ID, also the virtual port
        std::atomic<bool> closed{false};   //!< set by the library thread, acted on in Expire()
        std::atomic<bool> announced{false};//!< whether the first packet was logged
        std::atomic<bool> websocket{false};//!< game packets travel on the signalling socket, no data channel
        std::string ticket{};              //!< ?ticket= of the signalling URL (private rooms); under peerMutex_
        std::chrono::steady_clock::time_point accepted{}; //!< when the socket was accepted; fixed
        std::atomic<bool> gotMessage{false};  //!< a valid signalling message arrived
        std::atomic<bool> established{false}; //!< data channel open or WebSocket path agreed
        std::atomic<bool> claimed{false};     //!< the game has a logged in connection at its address: no longer pending
        std::chrono::steady_clock::time_point unclaimedSince{}; //!< established without a game connection since; game thread, epoch = not seen so
        bool pinned = false;                  //!< its one data channel or the WebSocket path was taken; under peerMutex_
        std::atomic<bool> refused{false};     //!< turned away (caps); not pending either
        bool trustedPeer = false;             //!< the socket peer is a trusted proxy; fixed at accept
        std::atomic<unsigned int> sourceKey{0}; //!< what it counts against per source (nRtcPendingSourceKey), 0 none
    };

    typedef std::shared_ptr<nRtcPeer> nRtcPeerPtr;

    bool TryListen();
    void Shutdown();

    void OnSignalClient(std::shared_ptr<rtc::WebSocket> const& socket);
    bool CreateConnection(nRtcPeerPtr const& peer);
    void Refuse(nRtcPeerPtr const& peer, std::string const& reason);
    void ExpirePending();
    void Watchdog();
    std::shared_ptr<rtc::WebSocket> GetSignal(nRtcPeerPtr const& peer);
    void OnSignalMessage(nRtcPeerPtr const& peer, std::string const& text);
    bool ResolveClient(nRtcPeerPtr const& peer);
    int CountPendingFrom(unsigned int key, nRtcPeer const* except, int* total) const;
    void OnLocalDescription(nRtcPeerPtr const& peer);
    void OnChannel(nRtcPeerPtr const& peer, std::shared_ptr<rtc::DataChannel> const& channel);
    void OnWebSocketRequest(nRtcPeerPtr const& peer);
    void PinAddress(nRtcPeerPtr const& peer, unsigned int iceIp, bool iceDirect, std::string const& iceClient,
                    std::string const& route);
    void OnPayload(nRtcPeerPtr const& peer, int8 const* data, int len);

    //! destroys the peers their library threads reported as gone; game thread only
    void Expire();

    //! queues a line for the console; the only way a library thread may report anything
    void Report(std::string const& message);

    void Wake();

    bool DoTrySend(nAddress const& peer, int8 const* data, int len) override;
    int DoWakeFd() const override;
    std::string DoTicket(nAddress const& peer) const override;
    void OnDrainWakeFd() override;

    std::unique_ptr<rtc::WebSocketServer> signalServer_{};

    mutable std::mutex peerMutex_{};
    std::map<unsigned short, nRtcPeerPtr> peers_{};
    nRtcConnectionIds ids_{};
    std::vector<std::string> reports_{}; //!< console lines waiting for the game thread
    std::vector<nRtcIpRange> trustedProxies_{}; //!< WEBRTC_TRUSTED_PROXIES, fixed while listening
    std::vector<nRtcIpRange> sharedProxies_{};  //!< WEBRTC_SHARED_PROXIES, fixed while listening
    unsigned int sharedSalt_ = 0;               //!< per start: shared-proxy addresses differ between runs
    std::atomic<unsigned int> sharedSerial_{0}; //!< one per shared-proxy connection
    nRtcIcePorts icePorts_{};                   //!< WEBRTC_PORT_MIN/MAX, fixed while listening

    std::chrono::steady_clock::time_point lastClaimCheck_{}; //!< when ExpirePending() last looked at claimed peers; game thread

    // wakes the game thread once a second while there are peers, so ExpirePending() runs
    std::thread watchdog_{};
    std::mutex watchdogMutex_{};
    std::condition_variable watchdogWake_{};
    bool watchdogStop_ = false; //!< under watchdogMutex_

    int wakeRead_ = -1;
    int wakeWrite_ = -1;

    static nTransportRtc* s_server_;
};

#endif // HAVE_LIBDATACHANNEL

#endif // ArmageTron_NTRANSPORTRTC_H
