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

#include "nTransportRtcJs.h"

#ifdef __EMSCRIPTEN__

#include <emscripten.h>

#include <map>

// the JS glue needs to convert the signalling URL / ICE configuration, to write the
// route back, and to reach malloc/free
EM_JS_DEPS(nTransportRtcJs, "$UTF8ToString,$stringToUTF8");

// *******************************************************************************************
// *
// *	JavaScript side
// *
// *******************************************************************************************

/* One registry object keyed by handle, so a second connection would need no new
   glue. The signalling messages are exactly what nTransportRtcSupport.h codes on
   the server: flat JSON, string values only, offer out, answer (and possibly
   trickled candidates) back.

   icePtr: "" or a JSON object {"iceServers": [RTCIceServer...], "iceTransportPolicy":
   "relay"} (both optional) for the RTCPeerConnection: the page's STUN/TURN servers
   (web/wasm/API.md "ICE servers"; aa_api.js has already checked the shape). It holds
   TURN credentials: never log it. With "transport": "websocket" in it there is no
   peer connection at all: the glue asks the server for the game packets on the
   signalling socket ({"type":"websocket"}, answered the same or "refused"), for pages
   where WebRTC is not allowed (a Discord Activity). */

EM_JS(int, nRtcJs_Open, (int handle, char const* urlPtr, char const* icePtr), {
    var url = UTF8ToString(urlPtr);
    var iceText = icePtr ? UTF8ToString(icePtr) : "";

    if (!globalThis.__aaRtc)
        globalThis.__aaRtc = {};
    var registry = globalThis.__aaRtc;

    var peer = { state : 1, ws : null, pc : null, dc : null, answered : false, opened : false, route : "", websocket : false };
    registry[handle] = peer;

    // the RTCConfiguration: no ICE servers unless the page gave some
    var config = { iceServers : [] };
    if (iceText)
    {
        try
        {
            var ice = JSON.parse(iceText);
            if (ice && Array.isArray(ice.iceServers))
                config.iceServers = ice.iceServers;
            if (ice && ice.iceTransportPolicy === "relay")
                config.iceTransportPolicy = "relay";
            if (ice && ice.transport === "websocket")
                peer.websocket = true;
        }
        catch (e)
        {
            console.log("rtc: malformed ICE configuration ignored");
        }
    }

    /* The selected candidate pair, once the channel is up: "relay" as the local
       type means the packets go through a TURN server. Kept as
       a JSON text in peer.route for nRtcJs_Route. */
    function readRoute()
    {
        if (!peer.pc || !peer.pc.getStats)
            return;
        peer.pc.getStats().then(function(stats) {
            var pair = null;
            stats.forEach(function(s) {
                if (s.type === "transport" && s.selectedCandidatePairId && !pair)
                    pair = stats.get(s.selectedCandidatePairId) || null;
            });
            if (!pair)
            {
                // Firefox: no transport stats, the pair carries "selected"
                stats.forEach(function(s) {
                    if (s.type === "candidate-pair" && !pair && (s.selected || (s.nominated && s.state === "succeeded")))
                        pair = s;
                });
            }
            if (!pair)
                return;
            var local = stats.get(pair.localCandidateId);
            var remote = stats.get(pair.remoteCandidateId);
            if (!local || !remote)
                return;
            peer.route = JSON.stringify({
                local : String(local.candidateType || ""),
                remote : String(remote.candidateType || ""),
                protocol : String(local.protocol || ""),
                relayProtocol : local.relayProtocol ? String(local.relayProtocol) : null
            });
        }).catch(function(e) {});
    }

    // Connecting -> Failed, Open -> Closed; both end the session
    function fail(why)
    {
        if (why)
            console.log("rtc: " + why);
        peer.state = (peer.state == 2) ? 4 : 3;
        try
        {
            if (peer.ws)
                peer.ws.close();
        }
        catch (e)
        {
        }
    }

    function deliver(buffer)
    {
        var bytes = new Uint8Array(buffer);
        var ptr = _malloc(bytes.length);
        HEAPU8.set(bytes, ptr);
        _nRtcJsDeliver(handle, ptr, bytes.length);
        _free(ptr);
    }

    function signal(text)
    {
        var message = JSON.parse(text);
        if (peer.websocket)
        {
            if (message.type === "websocket" && peer.state == 1)
            {
                // agreed: from now on binary frames on this socket are the game's packets
                peer.answered = true;
                peer.opened = true;
                peer.state = 2;
                peer.route = JSON.stringify({ local : "websocket", remote : "websocket", protocol : "tcp", relayProtocol : null });
            }
            else if (message.type === "refused")
                fail("the server refused game packets over WebSocket: " + (message.reason || "no reason given"));
            return;
        }
        if (message.type === "answer")
        {
            peer.answered = true;
            peer.pc.setRemoteDescription({ type : "answer", sdp : message.sdp });
        }
        else if (message.type === "candidate")
        {
            peer.pc.addIceCandidate({ candidate : message.candidate, sdpMid : message.mid || "0" });
        }
    }

    try
    {
        peer.ws = new WebSocket(url);
    }
    catch (e)
    {
        peer.state = 3;
        return 0;
    }

    peer.ws.binaryType = "arraybuffer";
    peer.ws.onerror = function() { fail("signalling error"); };
    peer.ws.onclose = function()
    {
        // on the WebSocket path the socket is the connection
        if (peer.websocket)
        {
            if (peer.state == 2)
                peer.state = 4;
            else if (peer.state == 1)
                fail("signalling closed before the server agreed to WebSocket");
            return;
        }
        // the server closes signalling as soon as the connection is up
        if (peer.state == 1 && !peer.answered)
            fail("signalling closed before the answer arrived");
    };

    peer.ws.onmessage = function(event)
    {
        var data = event.data;
        if (typeof data === "string")
            signal(data);
        else if (peer.websocket)
        {
            // a game packet; nothing binary is sent before the agreement
            if (peer.state == 2)
                deliver(data);
        }
        else if (typeof Blob !== "undefined" && data instanceof Blob)
            data.text().then(signal);
        else
            signal(new TextDecoder().decode(new Uint8Array(data)));
    };

    peer.ws.onopen = function()
    {
        if (peer.websocket)
        {
            try
            {
                peer.ws.send(JSON.stringify({ type : "websocket" }));
            }
            catch (e)
            {
                fail("WebSocket request failed");
            }
            return;
        }

        try
        {
            /* The server publishes host candidates on its public IP; without ICE
               servers the browser needs outbound UDP to them. A TURN server (the
               page's iceServers) relays to those same candidates when UDP is blocked. */
            peer.pc = new RTCPeerConnection(config);
        }
        catch (e)
        {
            fail("no RTCPeerConnection in this environment");
            return;
        }

        peer.pc.onicecandidate = function(event)
        {
            if (!event.candidate || !event.candidate.candidate)
                return;
            try
            {
                peer.ws.send(JSON.stringify({
                    type : "candidate",
                    candidate : event.candidate.candidate,
                    mid : event.candidate.sdpMid || "0"
                }));
            }
            catch (e)
            {
            }
        };

        peer.pc.onconnectionstatechange = function()
        {
            var state = peer.pc.connectionState;
            if (state === "failed" || state === "closed" || state === "disconnected")
                fail("peer connection " + state);
        };

        // the pair may still change after the channel opened; read it again then
        peer.pc.oniceconnectionstatechange = function()
        {
            var state = peer.pc.iceConnectionState;
            if (peer.state == 2 && (state === "connected" || state === "completed"))
                readRoute();
        };

        // unreliable and unordered, like the UDP socket it stands in for
        peer.dc = peer.pc.createDataChannel("aa", { ordered : false, maxRetransmits : 0 });
        peer.dc.binaryType = "arraybuffer";
        peer.dc.onopen = function()
        {
            peer.state = 2;
            peer.opened = true;
            readRoute();
        };
        peer.dc.onclose = function() { peer.state = 4; };
        peer.dc.onerror = function() { fail("data channel error"); };
        peer.dc.onmessage = function(event)
        {
            var data = event.data;
            if (typeof Blob !== "undefined" && data instanceof Blob)
                data.arrayBuffer().then(deliver);
            else
                deliver(data);
        };

        peer.pc.createOffer()
            .then(function(offer) { return peer.pc.setLocalDescription(offer); })
            .then(function() {
                peer.ws.send(JSON.stringify({ type : "offer", sdp : peer.pc.localDescription.sdp }));
            })
            .catch(function(e) { fail("offer failed: " + e); });
    };

    return 1;
});

EM_JS(int, nRtcJs_State, (int handle), {
    var registry = globalThis.__aaRtc;
    var peer = registry ? registry[handle] : null;
    return peer ? peer.state : 0;
});

/* how far the handshake got (the page's connect_stage): 0 nothing
   yet, 1 the server answered the offer over the signalling socket, 2 the data channel
   opened (ICE and DTLS done); it does not go back when the connection fails later */
EM_JS(int, nRtcJs_Progress, (int handle), {
    var registry = globalThis.__aaRtc;
    var peer = registry ? registry[handle] : null;
    if (!peer)
        return 0;
    return peer.opened ? 2 : (peer.answered ? 1 : 0);
});

// the selected candidate pair as JSON (see readRoute) into buf; 0 while unknown
EM_JS(int, nRtcJs_Route, (int handle, char* buf, int len), {
    var registry = globalThis.__aaRtc;
    var peer = registry ? registry[handle] : null;
    if (!peer || !peer.route)
        return 0;
    stringToUTF8(peer.route, buf, len);
    return 1;
});

EM_JS(int, nRtcJs_Send, (int handle, char const* data, int len), {
    var registry = globalThis.__aaRtc;
    var peer = registry ? registry[handle] : null;
    if (!peer)
        return 0;

    // the data channel, or on the WebSocket path the signalling socket once agreed
    var channel = peer.websocket ? (peer.state == 2 ? peer.ws : null) : peer.dc;
    if (!channel || (peer.websocket ? channel.readyState !== 1 : channel.readyState !== "open"))
        return 0;

    // a backed up channel drops, like a congested UDP link would
    if (channel.bufferedAmount > 4 * 300)
        return 0;

    try
    {
        // slice(), not subarray(): the heap moves and send() may be asynchronous
        channel.send(HEAPU8.slice(data, data + len));
    }
    catch (e)
    {
        return 0;
    }

    return 1;
});

EM_JS(void, nRtcJs_Close, (int handle), {
    var registry = globalThis.__aaRtc;
    var peer = registry ? registry[handle] : null;
    if (!peer)
        return;

    try { if (peer.dc) peer.dc.close(); } catch (e) {}
    try { if (peer.pc) peer.pc.close(); } catch (e) {}
    try { if (peer.ws) peer.ws.close(); } catch (e) {}

    delete registry[handle];
});

// *******************************************************************************************
// *
// *	C++ side
// *
// *******************************************************************************************

namespace
{
std::map<int, nTransportRtcJs*>& sn_RtcJsRegistry()
{
    static std::map<int, nTransportRtcJs*> registry;
    return registry;
}

int sn_rtcJsNextHandle = 1;
} // namespace

//! the JS glue's only way back into wasm; runs on the game thread, between two frames
extern "C" EMSCRIPTEN_KEEPALIVE void nRtcJsDeliver(int handle, char const* data, int len)
{
    nTransportRtcJs* transport = nTransportRtcJs::Lookup(handle);
    if (transport)
    {
        transport->Deliver(data, len);
        nTransportRtcJs::DeliverCallback callback = transport->GetDeliverCallback();
        if (callback)
            callback();
    }
}

nTransportRtcJs::nTransportRtcJs() {}

nTransportRtcJs::~nTransportRtcJs()
{
    Close();
}

// *******************************************************************************************
// *
// *	Lookup
// *
// *******************************************************************************************
//!
//!		@param	handle	the JS side handle
//!		@return			the transport it belongs to, NULL if it is gone
//!
// *******************************************************************************************

nTransportRtcJs* nTransportRtcJs::Lookup(int handle)
{
    std::map<int, nTransportRtcJs*>::const_iterator found = sn_RtcJsRegistry().find(handle);
    return found == sn_RtcJsRegistry().end() ? nullptr : found->second;
}

// *******************************************************************************************
// *
// *	TryOpen
// *
// *******************************************************************************************
//!
//!		@param	signallingUrl	ws:// or wss:// URL of the server's signalling socket
//!		@param	serverIPv4		the server's IPv4 in network byte order, for peers[0]
//!		@param	iceConfig		"" (no ICE servers: host candidates only) or a JSON object
//!								{"iceServers": [...], "iceTransportPolicy": "relay"} for the
//!								RTCPeerConnection; holds TURN credentials, never logged
//!		@return					true if the handshake was started
//!
// *******************************************************************************************

bool nTransportRtcJs::TryOpen(std::string const& signallingUrl, unsigned int serverIPv4, std::string const& iceConfig)
{
    if (handle_ > 0)
        return false;

    /* Connection ID 1: the client only ever talks to one server, and the ID is
       just the port half of the virtual address. The server's own ID for us is a
       different number and never travels over the wire. */
    server_ = nAddress::Virtual(serverIPv4, 1);

    handle_ = sn_rtcJsNextHandle++;
    sn_RtcJsRegistry()[handle_] = this;

    route_.clear();
    progress_ = 0;
    if (nRtcJs_Open(handle_, signallingUrl.c_str(), iceConfig.c_str()))
        return true;

    Close();

    return false;
}

// *******************************************************************************************
// *
// *	Close
// *
// *******************************************************************************************

void nTransportRtcJs::Close()
{
    if (handle_ <= 0)
        return;

    GetRoute(); // last chance: the JS side forgets it
    GetProgress();
    nRtcJs_Close(handle_);
    sn_RtcJsRegistry().erase(handle_);
    handle_ = -1;
}

// *******************************************************************************************
// *
// *	GetState
// *
// *******************************************************************************************
//!
//!		@return		how far the JS side has got
//!
// *******************************************************************************************

nTransportRtcJs::nRtcJsState nTransportRtcJs::GetState() const
{
    if (handle_ <= 0)
        return Rtc_Idle;

    return static_cast<nRtcJsState>(nRtcJs_State(handle_));
}

// *******************************************************************************************
// *
// *	GetRoute
// *
// *******************************************************************************************
//!
//!		@return		the selected candidate pair as a JSON object, {"local": "host"|"srflx"|
//!					"prflx"|"relay", "remote": ..., "protocol": "udp"|"tcp", "relayProtocol":
//!					"udp"|"tcp"|"tls"|null}, or "" while the channel is not open yet. Stays
//!					readable after Close().
//!
// *******************************************************************************************

int nTransportRtcJs::GetProgress() const
{
    if (handle_ > 0)
    {
        int const progress = nRtcJs_Progress(handle_);
        if (progress > progress_)
            progress_ = progress;
    }
    return progress_;
}

std::string const& nTransportRtcJs::GetRoute() const
{
    if (handle_ > 0)
    {
        char buffer[256];
        if (nRtcJs_Route(handle_, buffer, sizeof(buffer)))
            route_ = buffer;
    }
    return route_;
}

// *******************************************************************************************
// *
// *	Deliver
// *
// *******************************************************************************************
//!
//!		@param	data	the packet the data channel received
//!		@param	len		its length
//!
// *******************************************************************************************

void nTransportRtcJs::Deliver(int8 const* data, int len)
{
    if (len < 0)
        return;

    ++received_;
    Enqueue(server_, data, len);
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

bool nTransportRtcJs::DoTrySend(nAddress const& peer, int8 const* data, int len)
{
    if (len < 0 || handle_ <= 0 || 0 != nAddress::Compare(peer, server_))
        return false;

    if (nRtcJs_Send(handle_, data, len))
        return true;

    ++dropped_;

    return false;
}

#endif // __EMSCRIPTEN__
