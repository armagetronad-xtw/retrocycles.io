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

// nTransport.h: packet transport for peers that are not reachable over UDP.

#ifndef ArmageTron_NTRANSPORT_H
#define ArmageTron_NTRANSPORT_H

#include "nSocket.h"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

/* Address family of peers reached through an nTransport. A private value that is
   deliberately not a real AF_* constant, so nAddress::Compare (which compares the
   family first) can never match a virtual peer against a UDP peer. */
#define AF_ARMA_VIRTUAL 0xAA

/* Abstract raw packet transport.

   Threading rule: an implementation's producer
   thread may only call Enqueue() and signal its wake FD. Everything that touches
   nMessage happens on the game thread, which drains the queue through ReadPacket().
*/
class nTransport
{
public:
    virtual ~nTransport();

    // sends one raw packet to a virtual peer; false if it was dropped
    bool TrySend(nAddress const& peer, int8 const* data, int len) { return DoTrySend(peer, data, len); }

    // pops the oldest inbound packet; false if the queue is empty
    bool TryReceive(int8* buffer, int maxLen, int& len, nAddress& from);

    // a file descriptor that becomes readable when a packet arrives, -1 if the transport needs none
    int WakeFd() const { return DoWakeFd(); }

    // consumes a pending wakeup on that file descriptor
    void DrainWakeFd() { OnDrainWakeFd(); }

    // number of packets waiting in the inbound queue
    int GetPending() const;

    /* The inbound queue is bounded like a UDP socket's receive buffer: a packet that
       would take it past either limit is dropped. Generous for a full server's browser
       players between two frames; it only stops a flood from growing memory without end. */
    static int const MaxQueuedPackets = 4096;
    static int const MaxQueuedBytes = 2 * 1024 * 1024;

    /* Registry. Only one transport can be active; registering a second
       one while another is active is an error. */
    static void Register(nTransport* transport);
    static void Unregister(nTransport* transport);
    static nTransport* Instance() noexcept { return s_instance_; }

    /* The two seams, both called on the game thread. They also do the tRecorder
       bookkeeping ("TREAD"/"TSEND"), mirroring what nSocket does for UDP. Both are
       no-ops returning failure when no transport is registered. */

    // sends one packet to a virtual peer; false if it was dropped
    static bool TrySendPacket(nAddress const& peer, int8 const* data, int len);

    // reads one packet; returns its length, negative if there was none
    static int ReadPacket(int8* buffer, int maxLen, nAddress& from);

    // the private room ticket a peer brought when it connected, empty if none
    // or no transport is registered; game thread
    static std::string TicketOf(nAddress const& peer);

protected:
    nTransport();

    // copies one inbound packet into the queue; safe to call from a producer thread.
    // false if it was dropped because the queue is full
    bool Enqueue(nAddress const& from, int8 const* data, int len);

private:
    virtual bool DoTrySend(nAddress const& peer, int8 const* data, int len) = 0;
    virtual int DoWakeFd() const;
    virtual std::string DoTicket(nAddress const& peer) const;
    virtual void OnDrainWakeFd();

    // one received packet, tagged with its sender's virtual address
    struct nInboundPacket
    {
        nAddress from{};
        std::vector<int8> payload{};
    };

    mutable std::mutex mutex_{};
    std::deque<nInboundPacket> inbound_{};
    size_t inboundBytes_ = 0; //!< payload bytes in inbound_; under mutex_

    static nTransport* s_instance_;

    // no copying, the registry holds raw pointers
    nTransport(nTransport const&) = delete;
    nTransport& operator=(nTransport const&) = delete;
};

#endif // ArmageTron_NTRANSPORT_H
