// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// BeeAdHoc's protocol constants, as PG 460's Linux kernel module (chs. 12-13,
// §14.3.2) gives them, the version #16 chose. Each default cites its
// docs/fidelity.md Parameters row: `settled` where the report gives the value,
// otherwise the maintainer decision of 2026-11-10 on #4, #5 or #13 that
// chose it (golden rule 11), or a repo choice made here and recorded there.
#ifndef BEEADHOC_CORE_CONFIG_H
#define BEEADHOC_CORE_CONFIG_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>

namespace beeadhoc::core {

using swarm::core::NodeAddress;
using swarm::core::Time;

struct Config {
    /// `IPT_BEEADHOC_MAX_DANCES`: 8, settled ([PG460] §14.3.2). Also the
    /// direct neighbour's rating (Listing 13.15). Not a cap (#4).
    int maxDances = 8;
    /// `IPT_BEEADHOC_FORAGER_LIFETIME`: 2 s, settled ([PG460] §14.3.2; seconds
    /// by Listing 13.17).
    Time foragerLifetime = 2.0;
    /// `IPT_BEEADHOC_FORAGER_DANCETIME`: 2 s, settled ([PG460] §14.3.2).
    Time foragerDanceTime = 2.0;
    /// `IPT_BEEADHOC_MAXHOPS`: the longest route, 10 links ([PG460] §12.1.2;
    /// maintainer decision 2026-11-10, #13).
    int maxHops = 10;
    /// `SCOUTLISTLENGTH`: the scout seen list, 30 entries, settled ([PG460]
    /// §14.3.2), oldest forgotten first.
    std::size_t scoutSeenListLength = 30;
    /// `RXMTLISTLENGTH`: scouts awaiting their return, 30, settled ([PG460]
    /// §14.3.2). One entry per destination being searched; at the cap a packet
    /// for a further destination is dropped (repo choice, #36).
    std::size_t scoutRetransmitListLength = 30;
    /// `SCOUTRESENDRATE`: 500 jiffies, 0.5 s at HZ = 1000 ([PG460] §14.3.2).
    Time scoutResendInterval = 0.5;
    /// Initial scout TTL: 2 (maintainer decision 2026-11-10, #5).
    std::uint8_t initialScoutTtl = 2;
    /// The ring's TTL doubles up to this, the longest route (#5).
    std::uint8_t maxScoutTtl = 10;
    /// Scouts sent at maxScoutTtl before the held packets are dropped: 2 (#5).
    int maxRoundsAtMaxTtl = 2;
    /// Scout relay jitter U[0, 10 ms) (maintainer decision 2026-11-10, #13).
    Time scoutJitterMax = 0.010;
    /// `IPT_BEEADHOC_ARRAYSIZE`: foragers per destination and type, 64
    /// (maintainer decision 2026-11-10, #13). A full array drops its oldest
    /// forager (repo choice, #36).
    std::size_t foragerArraySize = 64;
    /// `IPT_BEEADHOC_MAXAGE`: 30 s without traffic removes a destination
    /// (maintainer decision 2026-11-10, #13).
    Time maxAge = 30.0;
    /// `IPT_BEEADHOC_GARBAGE_PROBAB`: 1, collect on every put without a draw
    /// (maintainer decision 2026-11-10, #13).
    double garbageProbability = 1.0;
    /// `IPT_BEEADHOC_SWARMSIZE`: 20, settled ([PG460] §14.3.2): a swarm leaves
    /// when more foreign foragers than this wait for one source, and carries
    /// this many (Listing 13.30, §13.7.3).
    std::size_t swarmSize = 20;
    /// `IPT_BEEADHOC_SWARMSIZE_OWN`: 11, settled ([PG460] §14.3.2): ... and
    /// fewer own foragers than this exist toward it (Listing 13.30).
    std::size_t swarmSizeOwn = 11;
    /// Destinations held in the forager store: 256, the least recently used
    /// evicted at the cap (repo choice under golden rule 7, #36; fidelity row
    /// "Destinations held in the forager store").
    std::size_t maxDestinations = 256;
};

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_CONFIG_H
