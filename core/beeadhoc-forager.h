// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Foragers and the forager store of PG 460's Linux kernel module ([PG460]
// §13.3, Listings 13.11-13.17), written fresh from the report and
// docs/fidelity.md (golden rule 12). A forager is a source route to a
// destination; it "dances" (is copied for further packets) for as many
// packets as its rating, within FORAGER_DANCETIME.
//
// The store keeps one array per destination and forager type (energy, delay,
// throughput; §13.3, Listing 13.28, maintainer decision 2026-11-10 on #13).
// Every structure is capped (golden rule 7): ARRAYSIZE foragers per array,
// maxDestinations destinations, and the MAXAGE collector removes idle ones.
#ifndef BEEADHOC_CORE_FORAGER_H
#define BEEADHOC_CORE_FORAGER_H

#include "beeadhoc-config.h"
#include "swarm-observer.h"
#include "swarm-ports.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace beeadhoc::core {

/// A packet's type selects the array ([PG460] §13.2.2: from the IP TOS field).
enum class ForagerType : std::uint8_t { Energy = 0, Delay = 1, Throughput = 2 };
inline constexpr std::size_t kForagerTypes = 3;

struct Forager {
    /// The hops after this node, the destination last: one entry per link.
    std::vector<NodeAddress> route;
    std::uint32_t info = 0;  ///< the 24-bit per-hop levels it was rated from
    int dances = 0;          ///< copies left ([PG460] Listing 13.16)
    Time created = 0.0;
    bool own = true;  ///< 'own' (from this node's scout) or 'foreign' (§13.7.1)

    bool operator==(const Forager&) const = default;
};

/// The 3-bit battery level of [PG460] Listing 13.7 for a residual fraction
/// 0..1: <= 9 % -> 0, (9, 14] -> 1, > 65 % -> 7; levels 2-6 in five equal
/// bins of 10.2 % over (14, 65] (maintainer decision 2026-11-10, #13). No
/// battery data (NaN) reads full, 7 (§13.1.2).
std::uint8_t batteryLevel(double residualFraction);

/// `forager_rate_lifetime` ([PG460] §13.3, Listing 13.15), integer arithmetic:
/// a direct neighbour (1 link) rates maxDances; otherwise
/// ((maxHops - (L - 1)) * min * avg + 1) / 44 over the L per-hop values read
/// from `info`, the newest in the low bits (the L-th being the destination's
/// own level, #4). Values past the 8 the field holds read 0, so 9- and
/// 10-link routes rate 0 (#4). Not capped at maxDances (#4). Never negative.
int rateForager(std::size_t routeLength, std::uint32_t info, const Config& config);

class ForagerStore {
public:
    /// `observer` (optional, null: none) hears every forager added or removed
    /// as a route change toward its destination through route.front(), and the
    /// store's footprint after each change (see footprint()).
    explicit ForagerStore(const Config& config, swarm::core::IRouterObserver* observer = nullptr);

    /// Stores `f` for `dest`. A full array drops its oldest forager; a new
    /// destination at the cap evicts the least recently used one (ties: the
    /// lowest address). Then, with probability GARBAGE_PROBAB, collects
    /// (Listing 13.14): at 1 without drawing from `rng`.
    void put(NodeAddress dest, ForagerType type, Forager f, Time now, swarm::core::IRng& rng);

    /// A route to `dest` ([PG460] Listing 13.17, §13.7.1): a random 'own'
    /// forager of the type, a 'foreign' one when no own is left. One that
    /// still dances (dances left, younger than FORAGER_DANCETIME) is copied
    /// and loses a dance; any other is removed and used, except that one older
    /// than FORAGER_LIFETIME is discarded, and another drawn, unless it is the
    /// array's last. One rng.uniformInt() per draw. nullopt when empty.
    std::optional<Forager> take(NodeAddress dest, ForagerType type, Time now,
                                swarm::core::IRng& rng);

    /// Removes every destination without traffic (a put or a take) for more
    /// than MAXAGE ([PG460] §13.3, Listing 13.14).
    void collect(Time now);

    std::size_t destinations() const { return entries_.size(); }
    std::size_t size(NodeAddress dest, ForagerType type) const;

    /// The routing-state footprint reported as onStateSize: `entries` is the
    /// foragers held; `bytes` counts, per forager, its info, dances, creation
    /// time and flag (17) plus 4 per hop, and per destination its address and
    /// last use (12). A fixed model, not sizeof, so it is the same on every
    /// build.
    struct Footprint {
        std::uint32_t entries = 0;
        std::uint32_t bytes = 0;
    };
    Footprint footprint() const;

private:
    struct Entry {
        std::array<std::vector<Forager>, kForagerTypes> arrays;
        Time lastUsed = 0.0;
    };

    void added(NodeAddress dest, const Forager& f);
    void removed(NodeAddress dest, const Forager& f);
    void reportState();

    Config config_;
    swarm::core::IRouterObserver* observer_;
    Footprint reported_;
    bool reportedAny_ = false;
    std::map<NodeAddress, Entry> entries_;  // ordered: iteration is deterministic
};

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_FORAGER_H
