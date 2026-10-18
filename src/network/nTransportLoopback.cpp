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

#include "nTransportLoopback.h"

void nTransportLoopback::Link(nAddress const& a, nAddress const& b)
{
    nLoopbackLink link;
    link.a = a;
    link.b = b;
    links_.push_back(link);
}

bool nTransportLoopback::TryGetSource(nAddress const& peer, nAddress& source) const
{
    for (std::vector<nLoopbackLink>::const_iterator iter = links_.begin(); iter != links_.end(); ++iter)
    {
        if (0 == nAddress::Compare(peer, iter->a))
        {
            source = iter->b;
            return true;
        }

        if (0 == nAddress::Compare(peer, iter->b))
        {
            source = iter->a;
            return true;
        }
    }

    return false;
}

bool nTransportLoopback::DoTrySend(nAddress const& peer, int8 const* data, int len)
{
    nAddress source;
    if (len < 0 || !TryGetSource(peer, source))
    {
        ++dropped_;
        return false;
    }

    if (!Enqueue(source, data, len))
    {
        ++dropped_;
        return false;
    }

    return true;
}
