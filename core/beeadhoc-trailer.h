// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The source-route trailer every data packet carries ([PG460] §12.1.2,
// Fig. 12.6; §13.7.1), and its versioned wire format (golden rule 3). Written
// fresh from the report and docs/fidelity.md (golden rule 12). The kernel
// module carries the route and the 24-bit info field as IP options; this
// repository carries them in a trailer (fidelity row "Strict source
// routing"), in a byte layout of its own on swarm-core's little-endian
// helpers:
//
//   u8 version | u32 info (24 bits used) | u16 n | n x i32 route
//   | u8 has_piggyback | [u32 piggyback info (24 bits) | u16 m | m x i32 piggyback route]
//
// `route` is the whole source route, the source first and the destination
// last. Each forwarding host shifts its 3-bit battery level into `info`
// (FORWARD chain, Listing 13.7); the source writes none (the field starts at
// 0, Listing 13.6). `piggyback` is at most one 'foreign' forager riding home
// to its owner (§13.7.1; fidelity row "Foreign foragers piggybacked per data
// packet": 1): its route runs from the node that held it (this packet's
// source) to its owner (this packet's destination), with the info it was
// rated from.
#ifndef BEEADHOC_CORE_TRAILER_H
#define BEEADHOC_CORE_TRAILER_H

#include "beeadhoc-config.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace beeadhoc::core {

struct RouteTrailer {
    /// Bump on any change to a field or its units (golden rule 3).
    static constexpr std::uint8_t kWireVersion = 1;
    /// Fixed bytes: version, info, the route's count, the piggyback flag.
    static constexpr std::size_t kFixedWireSize = 1 + 4 + 2 + 1;
    /// Fixed bytes of a piggybacked forager: its info and its route's count.
    static constexpr std::size_t kPiggybackFixedWireSize = 4 + 2;

    struct Piggyback {
        std::vector<NodeAddress> route;  ///< its holder first, its owner last
        std::uint32_t info = 0;          ///< the 24-bit levels it was rated from

        bool operator==(const Piggyback&) const = default;
    };

    std::vector<NodeAddress> route;  ///< the source first, the destination last
    std::uint32_t info = 0;          ///< 24-bit per-hop levels, the newest in the low bits
    std::optional<Piggyback> piggyback;

    /// The encoded size in bytes: what the trailer adds to a data packet.
    std::size_t wireSize() const {
        return kFixedWireSize + 4 * route.size() +
               (piggyback ? kPiggybackFixedWireSize + 4 * piggyback->route.size() : 0);
    }

    bool operator==(const RouteTrailer&) const = default;
};

/// The encoded trailer.
std::vector<std::uint8_t> encodeTrailer(const RouteTrailer& t);

/// Decodes untrusted bytes. Fails (nullopt) on a wrong version, info bits
/// above 24, a route or a piggybacked route longer than `maxRouteNodes`, a
/// flag byte other than 0 or 1, a short buffer or trailing bytes.
std::optional<RouteTrailer> decodeTrailer(std::span<const std::uint8_t> bytes,
                                          std::size_t maxRouteNodes);

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_TRAILER_H
