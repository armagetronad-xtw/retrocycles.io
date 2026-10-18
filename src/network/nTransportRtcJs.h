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

// nTransportRtcJs.h: browser side WebRTC DataChannel transport. The peer
// connection lives in JavaScript; this is the wasm end of it.

#ifndef ArmageTron_NTRANSPORTRTCJS_H
#define ArmageTron_NTRANSPORTRTCJS_H

#ifdef __EMSCRIPTEN__

#include "nTransport.h"

#include <string>

/* The browser has no libdatachannel; RTCPeerConnection, the signalling
   WebSocket and the "aa" DataChannel are driven by the EM_JS glue in the
   implementation file. There are no threads: every JS callback runs on the
   same thread as the game, between two Asyncify yields.

   One JS side registry keyed by an integer handle backs the instances, so a
   second transport (a spectator connection, say) would work without touching
   the glue.
*/
class nTransportRtcJs : public nTransport
{
public:
    //! how far the JS side has got; mirrors the RTCPeerConnection's fate
    enum nRtcJsState
    {
        Rtc_Idle = 0,       //!< nothing started yet
        Rtc_Connecting = 1, //!< signalling or ICE in progress
        Rtc_Open = 2,       //!< the data channel is open, packets flow
        Rtc_Failed = 3,     //!< signalling, ICE or DTLS gave up
        Rtc_Closed = 4      //!< the channel was open and went away
    };

    nTransportRtcJs();
    ~nTransportRtcJs() override;

    /* starts signalling; the handshake continues while the game thread sleeps.
       iceConfig: "" (no ICE servers: host candidates only, the default) or a JSON
       object {"iceServers": [RTCIceServer...], "iceTransportPolicy": "relay"} for the
       browser's RTCPeerConnection, e.g. a TURN relay for networks without outbound
       UDP. It holds TURN credentials: never log it. */
    bool TryOpen(std::string const& signallingUrl, unsigned int serverIPv4, std::string const& iceConfig = std::string());

    //! closes the peer connection and forgets the JS side registry entry
    void Close();

    nRtcJsState GetState() const;

    /* the selected ICE candidate pair as a JSON object {"local", "remote", "protocol",
       "relayProtocol"} ("local": "relay" = through a TURN server), "" until the data
       channel is open and the browser's stats have it; kept after Close() */
    std::string const& GetRoute() const;

    /* how far the handshake got: 0 nothing yet, 1 the server answered over the
       signalling socket, 2 the data channel opened (ICE + DTLS done). Only grows
       during a connection and is kept after Close(); the page's connect-stage
       telemetry (the API's "handshake" event) is built on it */
    int GetProgress() const;

    //! the server's virtual address, peers[0] on the client
    nAddress const& GetServerAddress() const { return server_; }

    //! packets the JS side handed us, and packets DoTrySend refused
    int GetReceivedPackets() const noexcept { return received_; }
    int GetDroppedPackets() const noexcept { return dropped_; }

    //! called from the JS glue, on the game thread, for every inbound message
    void Deliver(int8 const* data, int len);

    /* Optional hook run right after Deliver, still inside the JS onmessage
       task: the game uses it to read and answer the packet at once instead of
       at the next frame. Only ever called between two frames (there is one
       thread and no Asyncify), so the callee need not guard against
       re-entering a frame. */
    typedef void (*DeliverCallback)();
    void SetDeliverCallback(DeliverCallback callback) { deliverCallback_ = callback; }
    DeliverCallback GetDeliverCallback() const { return deliverCallback_; }

    //! the instance a JS handle belongs to, NULL if it is gone
    static nTransportRtcJs* Lookup(int handle);

private:
    bool DoTrySend(nAddress const& peer, int8 const* data, int len) override;

    int handle_{-1};
    nAddress server_{};
    int received_{};
    int dropped_{};
    mutable std::string route_;
    mutable int progress_{};
    DeliverCallback deliverCallback_{};
};

#endif // __EMSCRIPTEN__

#endif // ArmageTron_NTRANSPORTRTCJS_H
