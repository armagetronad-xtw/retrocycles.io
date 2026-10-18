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

#include "nTransport.h"

#include "tError.h"
#include "tRecorder.h"

#include <algorithm>

nTransport* nTransport::s_instance_ = nullptr;

int const nTransport::MaxQueuedPackets;
int const nTransport::MaxQueuedBytes;

nTransport::nTransport() {}

nTransport::~nTransport()
{
    if (this == s_instance_)
        s_instance_ = nullptr;
}

int nTransport::DoWakeFd() const
{
    return -1;
}

void nTransport::OnDrainWakeFd() {}

std::string nTransport::DoTicket(nAddress const&) const
{
    return std::string();
}

std::string nTransport::TicketOf(nAddress const& peer)
{
    return s_instance_ ? s_instance_->DoTicket(peer) : std::string();
}

void nTransport::Register(nTransport* transport)
{
    tASSERT(transport);
    tASSERT(!s_instance_);
    s_instance_ = transport;
}

void nTransport::Unregister(nTransport* transport)
{
    tASSERT(transport == s_instance_);
    s_instance_ = nullptr;
}

bool nTransport::Enqueue(nAddress const& from, int8 const* data, int len)
{
    if (len < 0)
        return false;

    nInboundPacket packet;
    packet.from = from;
    packet.payload.assign(data, data + len);

    std::lock_guard<std::mutex> lock(mutex_);

    // a full queue drops, like a full socket buffer; the sender's resends cover it
    if (inbound_.size() >= static_cast<size_t>(MaxQueuedPackets) ||
        inboundBytes_ + static_cast<size_t>(len) > static_cast<size_t>(MaxQueuedBytes))
        return false;

    inbound_.push_back(std::move(packet));
    inboundBytes_ += static_cast<size_t>(len);
    return true;
}

bool nTransport::TryReceive(int8* buffer, int maxLen, int& len, nAddress& from)
{
    nInboundPacket packet;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inbound_.empty())
            return false;

        packet = std::move(inbound_.front());
        inbound_.pop_front();
        inboundBytes_ -= packet.payload.size();
    }

    from = packet.from;

    // oversized packets are truncated, just like an undersized recvfrom buffer would
    len = static_cast<int>(packet.payload.size());
    if (len > maxLen)
        len = maxLen;
    std::copy(packet.payload.begin(), packet.payload.begin() + len, buffer);

    return true;
}

int nTransport::GetPending() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(inbound_.size());
}

static char const* sn_transportSectionRead = "TREAD";
static char const* sn_transportSectionSend = "TSEND";

/* Archives one received packet, the counterpart of nSocket.cpp's ReadArchiver.
   The address is stored as raw IP plus connection ID; nAddress does not survive
   its own string round trip with the virtual family intact.

   Unlike "READ", the section is not skipped to: a recording made without a
   transport has no "TREAD" sections at all, and skipping would eat it whole. */
template <class Archiver> class nTransportReadArchiver
{
public:
    static bool Archive(int8* buf, int& len, nAddress& addr)
    {
        Archiver archive;
        if (!archive.Initialize(sn_transportSectionRead))
            return false;

        archive.Archive(len);
        if (len < 0)
            return true;

        unsigned int ip = addr.GetIPv4();
        unsigned short connectionId = addr.GetConnectionId();
        archive.Archive(ip);
        archive.Archive(connectionId);
        addr = nAddress::Virtual(ip, connectionId);

        for (int i = 0; i < len; ++i)
            archive.Archive(buf[i]);

        archive.Separator();

        return true;
    }
};

int nTransport::ReadPacket(int8* buffer, int maxLen, nAddress& from)
{
    int len = -1;

    // during playback the transport is never polled, the recording is the source
    if (tRecorder::IsPlayingBack())
    {
        if (!nTransportReadArchiver<tPlaybackBlock>::Archive(buffer, len, from))
            return -1;

        return len;
    }

    nTransport* transport = Instance();
    if (!transport)
        return -1;

    if (!transport->TryReceive(buffer, maxLen, len, from))
        len = -1;

    nTransportReadArchiver<tRecordingBlock>::Archive(buffer, len, from);

    return len;
}

bool nTransport::TrySendPacket(nAddress const& peer, int8 const* data, int len)
{
    int sent = -1;

    if (tRecorder::IsPlayingBack())
    {
        // the recording decides, and nothing is really sent
        tRecorder::Playback(sn_transportSectionSend, sent);
        return sent >= 0;
    }

    nTransport* transport = Instance();
    if (transport && transport->TrySend(peer, data, len))
        sent = len;

    tRecorder::Record(sn_transportSectionSend, sent);

    return sent >= 0;
}
