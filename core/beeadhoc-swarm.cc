// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "beeadhoc-swarm.h"

#include "beeadhoc-scout.h"
#include "swarm-wire.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace beeadhoc::core {

using swarm::core::WireReader;
using swarm::core::WireWriter;

namespace {

std::uint16_t cap16(std::size_t n) {
    return static_cast<std::uint16_t>(
        std::min<std::size_t>(n, std::numeric_limits<std::uint16_t>::max()));
}

void writeRoute(WireWriter& w, const std::vector<NodeAddress>& route) {
    w.u16(static_cast<std::uint16_t>(route.size()));
    for (NodeAddress a : route)
        w.i32(a);
}

std::vector<NodeAddress> readRoute(WireReader& r, std::size_t maxRouteNodes) {
    const std::size_t n = r.count(cap16(maxRouteNodes), 4);
    std::vector<NodeAddress> route;
    route.reserve(n);
    for (std::size_t i = 0; i < n && r.ok(); ++i)
        route.push_back(r.i32());
    return route;
}

}  // namespace

std::vector<std::uint8_t> encodeSwarm(const Swarm& s) {
    constexpr std::size_t kMaxCount = std::numeric_limits<std::uint16_t>::max();
    if (s.route.size() > kMaxCount || s.foragers.size() > kMaxCount) return {};
    for (const Swarm::Cargo& c : s.foragers)
        if (c.route.size() > kMaxCount) return {};
    std::vector<std::uint8_t> out;
    out.reserve(s.wireSize());
    WireWriter w(out);
    w.version(Swarm::kWireVersion);
    w.u8(Swarm::kKind);
    w.i32(s.source);
    w.i32(s.destination);
    w.u8(static_cast<std::uint8_t>(s.type));
    writeRoute(w, s.route);
    w.u16(static_cast<std::uint16_t>(s.foragers.size()));
    for (const Swarm::Cargo& c : s.foragers) {
        w.u32(c.info & kInfoMask);
        writeRoute(w, c.route);
    }
    return out;
}

std::optional<Swarm> decodeSwarm(std::span<const std::uint8_t> bytes, std::size_t maxRouteNodes,
                                 std::size_t maxForagers) {
    WireReader r(bytes);
    Swarm s;
    r.expectVersion(Swarm::kWireVersion);
    if (r.u8() != Swarm::kKind) r.fail();
    s.source = r.i32();
    s.destination = r.i32();
    const std::uint8_t type = r.u8();
    if (type >= kForagerTypes) r.fail();
    s.type = static_cast<ForagerType>(type);
    s.route = readRoute(r, maxRouteNodes);
    // The smallest carried forager is its info and an empty route's count.
    const std::size_t k = r.count(cap16(maxForagers), Swarm::kCargoFixedWireSize);
    s.foragers.reserve(k);
    for (std::size_t i = 0; i < k && r.ok(); ++i) {
        Swarm::Cargo c;
        c.info = r.u32();
        if (c.info > kInfoMask) r.fail();
        c.route = readRoute(r, maxRouteNodes);
        s.foragers.push_back(std::move(c));
    }
    if (!r.atEnd()) return std::nullopt;
    return s;
}

}  // namespace beeadhoc::core
