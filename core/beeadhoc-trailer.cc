// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "beeadhoc-trailer.h"

#include "beeadhoc-scout.h"
#include "swarm-wire.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace beeadhoc::core {

using swarm::core::WireReader;
using swarm::core::WireWriter;

namespace {

void writeRoute(WireWriter& w, const std::vector<NodeAddress>& route) {
    w.u16(static_cast<std::uint16_t>(route.size()));
    for (NodeAddress a : route)
        w.i32(a);
}

std::vector<NodeAddress> readRoute(WireReader& r, std::size_t maxRouteNodes) {
    const auto cap = static_cast<std::uint16_t>(
        std::min<std::size_t>(maxRouteNodes, std::numeric_limits<std::uint16_t>::max()));
    const std::size_t n = r.count(cap, 4);
    std::vector<NodeAddress> route;
    route.reserve(n);
    for (std::size_t i = 0; i < n && r.ok(); ++i)
        route.push_back(r.i32());
    return route;
}

}  // namespace

std::vector<std::uint8_t> encodeTrailer(const RouteTrailer& t) {
    constexpr std::size_t kMaxCount = std::numeric_limits<std::uint16_t>::max();
    if (t.route.size() > kMaxCount || (t.piggyback && t.piggyback->route.size() > kMaxCount))
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(t.wireSize());
    WireWriter w(out);
    w.version(RouteTrailer::kWireVersion);
    w.u32(t.info & kInfoMask);
    writeRoute(w, t.route);
    w.boolean(t.piggyback.has_value());
    if (t.piggyback) {
        w.u32(t.piggyback->info & kInfoMask);
        writeRoute(w, t.piggyback->route);
    }
    return out;
}

std::optional<RouteTrailer> decodeTrailer(std::span<const std::uint8_t> bytes,
                                          std::size_t maxRouteNodes) {
    WireReader r(bytes);
    RouteTrailer t;
    r.expectVersion(RouteTrailer::kWireVersion);
    t.info = r.u32();
    if (t.info > kInfoMask) r.fail();
    t.route = readRoute(r, maxRouteNodes);
    if (r.boolean()) {
        RouteTrailer::Piggyback p;
        p.info = r.u32();
        if (p.info > kInfoMask) r.fail();
        p.route = readRoute(r, maxRouteNodes);
        t.piggyback = std::move(p);
    }
    if (!r.atEnd()) return std::nullopt;
    return t;
}

}  // namespace beeadhoc::core
