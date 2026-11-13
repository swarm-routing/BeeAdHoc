// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The swarm ([PG460] §13.7.2-§13.7.5, Listing 13.30, Listing 13.33,
// Listing 13.35) and its versioned wire format (golden rule 3), written fresh
// from the report and docs/fidelity.md (golden rule 12). When a flow looks
// one-way, a destination holds many 'foreign' foragers for its source and few
// of its own: it packs SWARMSIZE of them into one source-routed control
// packet and sends them home. The sender reverses each forager's route
// (§13.7.3), so at home each one is a route from there back to the sender,
// unpacked into the array as an 'own' forager. The kernel module sends swarms
// as UDP datagrams (port 1125, §13.5); the byte layout here is this
// repository's own, on swarm-core's little-endian helpers:
//
//   u8 version | u8 kind (3) | i32 source | i32 destination | u8 forager type
//   | u16 n | n x i32 route | u16 k | k x (u32 info | u16 m | m x i32 route)
//
// `route` is the swarm's own source route, its sender (`source`) first and
// its destination (the foragers' owner) last. Each carried forager's route
// runs from its owner first to the swarm's sender last.
#ifndef BEEADHOC_CORE_SWARM_H
#define BEEADHOC_CORE_SWARM_H

#include "beeadhoc-config.h"
#include "beeadhoc-forager.h"
#include "beeadhoc-scout.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace beeadhoc::core {

struct Swarm {
    /// Bump on any change to a field or its units (golden rule 3).
    static constexpr std::uint8_t kWireVersion = 1;
    /// The kind byte after the version (kSwarmKind, named in kKindNames).
    static constexpr std::uint8_t kKind = kSwarmKind;
    /// Fixed bytes before the route's addresses, and after them the cargo count.
    static constexpr std::size_t kFixedWireSize = 1 + 1 + 4 + 4 + 1 + 2 + 2;
    /// Fixed bytes of each carried forager: its info and its route's count.
    static constexpr std::size_t kCargoFixedWireSize = 4 + 2;

    struct Cargo {
        std::vector<NodeAddress> route;  ///< its owner first, the swarm's sender last
        std::uint32_t info = 0;          ///< the 24-bit levels it was rated from

        bool operator==(const Cargo&) const = default;
    };

    NodeAddress source = swarm::core::kInvalidAddress;       ///< the node that built it
    NodeAddress destination = swarm::core::kInvalidAddress;  ///< the foragers' owner
    ForagerType type = ForagerType::Energy;  ///< the array the foragers came from and go to
    std::vector<NodeAddress> route;          ///< source first, destination last
    std::vector<Cargo> foragers;

    /// The encoded size in bytes: what the router reports to its observer.
    std::size_t wireSize() const {
        std::size_t n = kFixedWireSize + 4 * route.size();
        for (const Cargo& c : foragers)
            n += kCargoFixedWireSize + 4 * c.route.size();
        return n;
    }

    bool operator==(const Swarm&) const = default;
};

/// The encoded swarm; empty (a frame no decoder accepts) when a route or the
/// cargo is longer than its u16 count can say. The router never builds one:
/// its routes are capped at Config::maxHops + 1 nodes and its cargo at
/// SWARMSIZE.
std::vector<std::uint8_t> encodeSwarm(const Swarm& s);

/// Decodes untrusted bytes. Fails (nullopt) on a wrong version or kind, an
/// unknown forager type, a route or a carried route longer than
/// `maxRouteNodes`, more than `maxForagers` carried, info bits above 24, a
/// short buffer or trailing bytes.
std::optional<Swarm> decodeSwarm(std::span<const std::uint8_t> bytes, std::size_t maxRouteNodes,
                                 std::size_t maxForagers);

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_SWARM_H
