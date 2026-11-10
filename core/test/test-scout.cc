// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The scout's wire header (golden rule 3): roundtrip, fail-closed decoding,
// and a seeded fuzz.
#include "../beeadhoc-scout.h"
#include "beeadhoc-core-tests.h"

#include <cstdint>
#include <vector>

namespace beeadhoc::core::test {

namespace {

Scout sample() {
    Scout s;
    s.mode = ScoutMode::Backward;
    s.source = 3;
    s.destination = -9;
    s.id = 0xA1B2C3D4u;
    s.ttl = 7;
    s.info = pushInfo(pushInfo(0, 5), 7);
    s.route = {3, 11, 400, -9};
    return s;
}

}  // namespace

void TestScoutRoundtrip(Checker& c) {
    const Scout s = sample();
    const auto bytes = encodeScout(s);
    c.check(bytes.size() == Scout::kFixedWireSize + 4 * s.route.size(), "encoded size");
    c.check(bytes[0] == Scout::kWireVersion && bytes[1] == 2, "version byte first, then mode");
    c.check(bytes[2] == 3 && bytes[3] == 0 && bytes[4] == 0 && bytes[5] == 0, "little-endian");
    const auto back = decodeScout(bytes, 11);
    c.check(back.has_value() && *back == s, "scout round-trips");

    Scout empty;
    empty.route.clear();
    const auto e = decodeScout(encodeScout(empty), 11);
    c.check(e.has_value() && *e == empty, "an empty route round-trips");
    c.check(pushInfo(0xFFFFFFu, 2) == 0xFFFFFAu, "pushInfo keeps 24 bits, newest lowest");
}

void TestScoutFailsClosed(Checker& c) {
    const auto good = encodeScout(sample());
    auto bad = good;
    bad[0] = Scout::kWireVersion + 1;
    c.check(!decodeScout(bad, 11), "wrong version");
    bad = good;
    bad[1] = 3;
    c.check(!decodeScout(bad, 11), "unknown mode");
    bad = good;
    bad[18] = 0x01;  // info's top byte
    c.check(!decodeScout(bad, 11), "info bits above 24");
    c.check(!decodeScout(good, 3), "route longer than the cap");
    bad = good;
    bad.pop_back();
    c.check(!decodeScout(bad, 11), "truncated");
    bad = good;
    bad.push_back(0);
    c.check(!decodeScout(bad, 11), "trailing byte");
    c.check(!decodeScout({}, 11), "empty buffer");
}

void TestScoutFuzz(Checker& c) {
    SeededRng rng(20261110);
    const auto good = encodeScout(sample());
    int decoded = 0;
    for (int round = 0; round < 4000; ++round) {
        std::vector<std::uint8_t> bytes;
        if (round % 2 == 0) {
            // Mutations of a valid frame: flipped bytes, cut or extended.
            bytes = good;
            const int flips = 1 + rng.uniformInt(3);
            for (int i = 0; i < flips; ++i)
                bytes[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(bytes.size())))] ^=
                    static_cast<std::uint8_t>(1 + rng.uniformInt(255));
            if (rng.uniformInt(4) == 0) bytes.resize(static_cast<std::size_t>(rng.uniformInt(40)));
        } else {
            bytes.resize(static_cast<std::size_t>(rng.uniformInt(64)));
            for (auto& b : bytes)
                b = static_cast<std::uint8_t>(rng.uniformInt(256));
        }
        if (const auto s = decodeScout(bytes, 11)) {
            ++decoded;
            c.check(encodeScout(*s) == bytes, "a decoded frame re-encodes to the same bytes");
            c.check(s->route.size() <= 11 && s->info <= kInfoMask, "decoded within bounds");
        }
    }
    c.check(decoded > 0, "some mutations still decode");
}

}  // namespace beeadhoc::core::test
