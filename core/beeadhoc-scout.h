// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The scout and its versioned wire header (golden rule 3). Written fresh from
// [PG460] §13.5, Listings 13.21, 13.26 and 13.27; the kernel module sends
// scouts as UDP datagrams (port 1124, §13.5), and the byte layout here is this
// repository's own, built on swarm-core's little-endian helpers:
//
//   u8 version | u8 mode | i32 source | i32 destination | u32 id | u8 ttl
//   | u32 info (24 bits used) | u16 n | n x i32 route
//
// A forward scout (mode 1) is broadcast; each relay appends itself to the
// route and shifts its 3-bit battery level into the info field. The
// destination appends itself, adds its own level and turns the scout back
// (mode 2), unicast along the reversed route to the source.
#ifndef BEEADHOC_CORE_SCOUT_H
#define BEEADHOC_CORE_SCOUT_H

#include "beeadhoc-config.h"
#include "swarm-observer.h"

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace beeadhoc::core {

/// Bits of the info field: 3 per hop, 8 values ([PG460] §12.1.2).
inline constexpr int kInfoBitsPerHop = 3;
inline constexpr int kInfoValues = 8;
inline constexpr std::uint32_t kInfoMask = 0xFFFFFFu;

/// The info field after one more writer: shift left 3 bits, write the 3-bit
/// level, keep 24 bits ([PG460] Listing 13.7, Listing 13.27).
constexpr std::uint32_t pushInfo(std::uint32_t info, std::uint8_t level) {
    return ((info << kInfoBitsPerHop) | (level & 0x7u)) & kInfoMask;
}

enum class ScoutMode : std::uint8_t {
    Forward = 1,   ///< broadcast toward the destination
    Backward = 2,  ///< unicast back along the reversed route
};

struct Scout {
    /// Bump on any change to a field or its units (golden rule 3).
    static constexpr std::uint8_t kWireVersion = 1;
    /// Fixed bytes before the route's addresses.
    static constexpr std::size_t kFixedWireSize = 1 + 1 + 4 + 4 + 4 + 1 + 4 + 2;

    ScoutMode mode = ScoutMode::Forward;
    NodeAddress source = swarm::core::kInvalidAddress;
    NodeAddress destination = swarm::core::kInvalidAddress;
    std::uint32_t id = 0;  ///< unique per source, so (source, id) names the scout
    std::uint8_t ttl = 0;
    std::uint32_t info = 0;  ///< 24-bit per-hop levels, the newest in the low bits
    /// The source first, then each relay; on a backward scout the destination last.
    std::vector<NodeAddress> route;

    /// The encoded size in bytes: what the router reports to its observer.
    std::size_t wireSize() const { return kFixedWireSize + 4 * route.size(); }

    bool operator==(const Scout&) const = default;
};

/// The control kinds a reader names ("FORWARD_SCOUT", not "1"): the scout's
/// wire mode byte is its kind. Foragers are not control messages in this
/// slice (a forager is table state, built from a returned scout), so they
/// have no kind yet; the data path (#36) adds one with its packet.
inline constexpr std::array<swarm::core::KindName, 2> kKindNames{{
    {static_cast<std::uint8_t>(ScoutMode::Forward), "FORWARD_SCOUT"},
    {static_cast<std::uint8_t>(ScoutMode::Backward), "BACKWARD_SCOUT"},
}};

/// The encoded header.
std::vector<std::uint8_t> encodeScout(const Scout& s);

/// Decodes untrusted bytes. Fails (nullopt) on a wrong version, an unknown
/// mode, info bits above 24, a route longer than `maxRouteNodes`, a short
/// buffer or trailing bytes.
std::optional<Scout> decodeScout(std::span<const std::uint8_t> bytes, std::size_t maxRouteNodes);

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_SCOUT_H
