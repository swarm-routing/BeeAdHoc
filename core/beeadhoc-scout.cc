// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "beeadhoc-scout.h"

#include "swarm-wire.h"

#include <algorithm>
#include <limits>

namespace beeadhoc::core {

using swarm::core::WireReader;
using swarm::core::WireWriter;

std::vector<std::uint8_t> encodeScout(const Scout& s) {
    std::vector<std::uint8_t> out;
    out.reserve(Scout::kFixedWireSize + 4 * s.route.size());
    WireWriter w(out);
    w.version(Scout::kWireVersion);
    w.u8(static_cast<std::uint8_t>(s.mode));
    w.i32(s.source);
    w.i32(s.destination);
    w.u32(s.id);
    w.u8(s.ttl);
    w.u32(s.info & kInfoMask);
    w.u16(static_cast<std::uint16_t>(s.route.size()));
    for (NodeAddress a : s.route)
        w.i32(a);
    return out;
}

std::optional<Scout> decodeScout(std::span<const std::uint8_t> bytes, std::size_t maxRouteNodes) {
    WireReader r(bytes);
    Scout s;
    r.expectVersion(Scout::kWireVersion);
    const std::uint8_t mode = r.u8();
    if (mode != static_cast<std::uint8_t>(ScoutMode::Forward) &&
        mode != static_cast<std::uint8_t>(ScoutMode::Backward))
        r.fail();
    s.mode = static_cast<ScoutMode>(mode);
    s.source = r.i32();
    s.destination = r.i32();
    s.id = r.u32();
    s.ttl = r.u8();
    s.info = r.u32();
    if (s.info > kInfoMask) r.fail();
    const auto cap = static_cast<std::uint16_t>(
        std::min<std::size_t>(maxRouteNodes, std::numeric_limits<std::uint16_t>::max()));
    const std::size_t n = r.count(cap, 4);
    s.route.reserve(n);
    for (std::size_t i = 0; i < n && r.ok(); ++i)
        s.route.push_back(r.i32());
    if (!r.atEnd()) return std::nullopt;
    return s;
}

}  // namespace beeadhoc::core
