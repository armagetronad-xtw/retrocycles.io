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

// nTransportLoopback.h: in-process nTransport for tests. No sockets, no threads.

#ifndef ArmageTron_NTRANSPORTLOOPBACK_H
#define ArmageTron_NTRANSPORTLOOPBACK_H

#include "nTransport.h"

/* Routes packets back into the inbound queue instead of sending them anywhere.
   Link() defines the routes: a packet addressed to one end of a link is delivered
   as if it came from the other end, so a send/receive round trip exercises the
   same code the real transport will. Packets to unknown peers are dropped. */
class nTransportLoopback : public nTransport
{
public:
    nTransportLoopback() = default;
    ~nTransportLoopback() override = default;

    // makes packets sent to a arrive from b, and the other way round
    void Link(nAddress const& a, nAddress const& b);

    // packets dropped because no route matched or the receiving queue was full
    int GetDropped() const noexcept { return dropped_; }

private:
    bool DoTrySend(nAddress const& peer, int8 const* data, int len) override;

    // the peer a packet sent to "peer" appears to come from; false if there is no route
    bool TryGetSource(nAddress const& peer, nAddress& source) const;

    struct nLoopbackLink
    {
        nAddress a{};
        nAddress b{};
    };

    std::vector<nLoopbackLink> links_{};
    int dropped_{};
};

#endif // ArmageTron_NTRANSPORTLOOPBACK_H
