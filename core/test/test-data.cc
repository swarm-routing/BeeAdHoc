// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The data path (#36 slice 2): the trailer's wire format (golden rule 3), the
// FORWARD-chain level writes (Listing 13.7), the INPUT chain's foreign forager
// with the destination's level as the L-th value (§12.1.3, #4), and one
// foreign forager piggybacked home per packet (§13.7.1).
#include "../beeadhoc-trailer.h"
#include "beeadhoc-core-tests.h"

#include <cstdint>
#include <vector>

namespace beeadhoc::core::test {

using swarm::core::RouteAction;

namespace {

RouteTrailer sample() {
    RouteTrailer t;
    t.route = {3, 11, 400, -9};
    t.info = pushInfo(pushInfo(0, 5), 7);
    t.piggyback = RouteTrailer::Piggyback{{3, 12, -9}, pushInfo(pushInfo(0, 6), 2)};
    return t;
}

/// A config whose first scout reaches 10 hops.
Config farScouts() {
    Config config;
    config.initialScoutTtl = 10;
    return config;
}

/// Makes `net` a chain 0 - 1 - ... - (n - 1) where 0 found n - 1 by a scout.
void discover(Network& net, int n) {
    net.chain();
    net.at(0).routeOutput(n - 1, ForagerType::Energy);
    net.clock().runUntil(1.0);
}

}  // namespace

void TestTrailerRoundtrip(Checker& c) {
    const RouteTrailer t = sample();
    const auto bytes = encodeTrailer(t);
    c.check(bytes.size() == t.wireSize() &&
                bytes.size() == RouteTrailer::kFixedWireSize + 4 * 4 +
                                    RouteTrailer::kPiggybackFixedWireSize + 4 * 3,
            "encoded size");
    c.check(bytes[0] == RouteTrailer::kWireVersion, "version byte first");
    c.check(bytes[1] == 0x2F && bytes[2] == 0 && bytes[3] == 0 && bytes[4] == 0,
            "info little-endian after the version");
    c.check(bytes[5] == 4 && bytes[6] == 0 && bytes[7] == 3, "route count, then the route");
    const auto back = decodeTrailer(bytes, 11);
    c.check(back.has_value() && *back == t, "trailer with a piggyback round-trips");

    RouteTrailer bare = t;
    bare.piggyback.reset();
    const auto b = encodeTrailer(bare);
    c.check(b.size() == RouteTrailer::kFixedWireSize + 4 * 4 && b.back() == 0,
            "no piggyback: one flag byte, 0");
    const auto bb = decodeTrailer(b, 11);
    c.check(bb.has_value() && *bb == bare, "trailer without a piggyback round-trips");
}

void TestTrailerFailsClosed(Checker& c) {
    const auto good = encodeTrailer(sample());
    auto bad = good;
    bad[0] = RouteTrailer::kWireVersion + 1;
    c.check(!decodeTrailer(bad, 11), "wrong version");
    bad = good;
    bad[4] = 0x01;  // info's top byte
    c.check(!decodeTrailer(bad, 11), "info bits above 24");
    const std::size_t flag = RouteTrailer::kFixedWireSize - 1 + 4 * 4;
    bad = good;
    bad[flag] = 2;
    c.check(!decodeTrailer(bad, 11), "a flag byte other than 0 or 1");
    bad = good;
    bad[flag + 4] = 0x01;  // the piggyback's info top byte
    c.check(!decodeTrailer(bad, 11), "piggyback info bits above 24");
    c.check(!decodeTrailer(good, 3), "route longer than the cap");
    RouteTrailer longPiggy = sample();
    longPiggy.piggyback->route = {1, 2, 3, 4, 5};
    c.check(!decodeTrailer(encodeTrailer(longPiggy), 4), "piggybacked route longer than the cap");
    bad = good;
    bad.pop_back();
    c.check(!decodeTrailer(bad, 11), "truncated");
    bad = good;
    bad.push_back(0);
    c.check(!decodeTrailer(bad, 11), "trailing byte");
    c.check(!decodeTrailer({}, 11), "empty buffer");
    RouteTrailer huge;
    huge.route.assign(65536, 1);
    c.check(encodeTrailer(huge).empty(), "a route past the u16 count is not encoded");
    RouteTrailer hugePiggy = sample();
    hugePiggy.piggyback->route.assign(65536, 1);
    c.check(encodeTrailer(hugePiggy).empty(), "nor is a piggybacked one");
}

void TestTrailerFuzz(Checker& c) {
    SeededRng rng(20261113);
    const auto good = encodeTrailer(sample());
    int decoded = 0;
    for (int round = 0; round < 4000; ++round) {
        std::vector<std::uint8_t> bytes;
        if (round % 2 == 0) {
            bytes = good;
            const int flips = 1 + rng.uniformInt(3);
            for (int i = 0; i < flips; ++i)
                bytes[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(bytes.size())))] ^=
                    static_cast<std::uint8_t>(1 + rng.uniformInt(255));
            if (rng.uniformInt(4) == 0) bytes.resize(static_cast<std::size_t>(rng.uniformInt(48)));
        } else {
            bytes.resize(static_cast<std::size_t>(rng.uniformInt(64)));
            for (auto& b : bytes)
                b = static_cast<std::uint8_t>(rng.uniformInt(256));
        }
        if (const auto t = decodeTrailer(bytes, 11)) {
            ++decoded;
            c.check(encodeTrailer(*t) == bytes, "a decoded frame re-encodes to the same bytes");
            c.check(t->route.size() <= 11 && t->info <= kInfoMask &&
                        (!t->piggyback ||
                         (t->piggyback->route.size() <= 11 && t->piggyback->info <= kInfoMask)),
                    "decoded within bounds");
        }
    }
    c.check(decoded > 0, "some mutations still decode");
}

void TestForwardChain(Checker& c) {
    Network net(4, farScouts(), 11);
    discover(net, 4);
    net.energy(1).fraction = 0.30;  // level 3
    net.energy(2).fraction = 0.12;  // level 1
    net.energy(3).fraction = 0.50;  // level 5, the destination's
    const auto d = net.at(0).routeOutput(3, ForagerType::Energy);
    c.check(d.action == RouteAction::Unicast &&
                d.trailer.route == std::vector<NodeAddress>{0, 1, 2, 3} && d.trailer.info == 0 &&
                !d.trailer.piggyback,
            "the source writes the whole route, info 0 (Listing 13.6), nothing to carry home");
    c.check(d.route == std::vector<NodeAddress>{1, 2, 3}, "route: the hops after the source");
    const auto got = net.carry(d.trailer, ForagerType::Energy);
    c.check(got.has_value() && got->info == pushInfo(pushInfo(0, 3), 1),
            "each forwarder shifts in its level (Listing 13.7); the source and destination do not");

    // The destination keeps the reversed route as a foreign forager for the
    // source: 3 links, levels {3, 1, 5} -> min 1, avg 3: (8 * 1 * 3 + 1) / 44 = 0.
    c.check(net.at(3).foragers().size(0, ForagerType::Energy) == 1 &&
                net.at(3).foragers().size(0, ForagerType::Delay) == 0,
            "one foreign forager, in the packet's type array");
    const auto back = net.at(3).routeOutput(0, ForagerType::Energy);
    c.check(back.action == RouteAction::Unicast && back.route == std::vector<NodeAddress>{2, 1, 0},
            "the reversed route leads home to the source");
    c.check(net.at(3).foragers().size(0, ForagerType::Energy) == 0,
            "rated 0 with the destination's level as the L-th value: used once (#4)");

    // forwardData refuses a route that does not name the node between its ends.
    RouteTrailer stray{{0, 2, 3}, 0, std::nullopt};
    c.check(net.at(1).forwardData(stray) == swarm::core::kInvalidAddress && stray.info == 0,
            "not on the route: drop, info untouched");
    RouteTrailer atEnd{{0, 1}, 0, std::nullopt};
    c.check(net.at(1).forwardData(atEnd) == swarm::core::kInvalidAddress,
            "the destination does not forward");
    RouteTrailer atSource{{1, 2, 3}, 0, std::nullopt};
    c.check(net.at(1).forwardData(atSource) == swarm::core::kInvalidAddress,
            "the source does not forward its own packet back in");
    RouteTrailer loop{{0, 1, 2, 1, 3}, 0, std::nullopt};
    c.check(net.at(2).forwardData(loop) == swarm::core::kInvalidAddress && loop.info == 0,
            "a route that visits a node twice: drop");
    c.check(!net.carry(loop, ForagerType::Energy).has_value(),
            "carried, it ends at the first repeat instead of bouncing");
    RouteTrailer loopHome{{0, 2, 0, 3}, 0, std::nullopt};
    const std::size_t had = net.at(3).foragers().size(0, ForagerType::Throughput);
    net.at(3).deliverData(loopHome, ForagerType::Throughput);
    c.check(net.at(3).foragers().size(0, ForagerType::Throughput) == had,
            "a delivered route with a repeat leaves no forager");
}

void TestForeignHome(Checker& c) {
    Network net(3, farScouts(), 12);  // every level 7
    discover(net, 3);
    const auto d = net.at(0).routeOutput(2, ForagerType::Energy);
    c.check(net.carry(d.trailer, ForagerType::Energy).has_value(), "0 -> 2 delivered");
    // 2 links at level 7: (9 * 7 * 7 + 1) / 44 = 10 dances.
    c.check(net.at(2).foragers().size(0, ForagerType::Energy) == 1, "a foreign forager at 2");

    // 2 has no own forager for 0: it uses the foreign one (§13.7.1), which
    // dances, and carries the oldest foreign one home on the same packet.
    const auto r = net.at(2).routeOutput(0, ForagerType::Energy);
    c.check(r.action == RouteAction::Unicast && r.route == std::vector<NodeAddress>{1, 0} &&
                r.trailer.piggyback.has_value() &&
                r.trailer.piggyback->route == std::vector<NodeAddress>{2, 1, 0} &&
                r.trailer.piggyback->info == pushInfo(pushInfo(0, 7), 7),
            "a foreign forager rides home, holder first, owner last, with its info");
    c.check(net.at(2).foragers().size(0, ForagerType::Energy) == 0, "it left 2's store");
    const std::size_t before = net.at(0).foragers().size(2, ForagerType::Energy);
    c.check(net.carry(r.trailer, ForagerType::Energy).has_value(), "2 -> 0 delivered");
    // At 0: the forager that came home ('own') and the foreign one 2's
    // packet itself leaves, both in the carrying packet's type array.
    c.check(net.at(0).foragers().size(2, ForagerType::Energy) == before + 2 &&
                net.at(0).foragers().size(2, ForagerType::Delay) == 1,
            "home: the returning forager and the packet's own foreign forager, energy array only");

    // A piggyback whose owner is not this node, or whose holder is not the
    // packet's source, is not taken.
    RouteTrailer wrong{{2, 1, 0}, 0, RouteTrailer::Piggyback{{1, 0}, 0}};
    const std::size_t had = net.at(0).foragers().size(2, ForagerType::Throughput);
    net.at(0).deliverData(wrong, ForagerType::Throughput);
    c.check(net.at(0).foragers().size(2, ForagerType::Throughput) == had + 1,
            "only the foreign forager from the packet itself");
    RouteTrailer notMine{{2, 1, 0}, 0, std::nullopt};
    net.at(1).deliverData(notMine, ForagerType::Energy);
    c.check(net.at(1).foragers().destinations() == 0, "a packet for another node is not taken");
}

void TestDataEndsSearch(Checker& c) {
    Config config;
    config.initialScoutTtl = 10;
    Network net(3, config, 13);
    net.chain();
    net.at(2).routeOutput(0, ForagerType::Energy);
    net.clock().runUntil(1.0);  // 2 knows 0
    net.ready.clear();
    c.check(net.at(0).routeOutput(2, ForagerType::Energy).action == RouteAction::Queue &&
                net.at(0).searching(2),
            "0 searches for 2");
    const auto d = net.at(2).routeOutput(0, ForagerType::Energy);
    c.check(net.carry(d.trailer, ForagerType::Energy).has_value(), "2 -> 0 delivered");
    c.check(!net.at(0).searching(2) &&
                net.ready == std::vector<std::pair<int, NodeAddress>>{{0, 2}},
            "a packet from 2 leaves a route to 2: the search ends, held packets go");
    c.check(net.at(0).routeOutput(2, ForagerType::Energy).action == RouteAction::Unicast,
            "the foreign forager carries 0's next packet");
}

}  // namespace beeadhoc::core::test
