// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Swarms (#36 slice 3): the wire format (golden rule 3), Listing 13.30's
// trigger, the routes reversed by the sender and unpacked at home (§13.7.3,
// §13.7.5). Link failure (#36 slice 4): the foragers over a failed next link
// go (fidelity row "A failed next link").
#include "../beeadhoc-swarm.h"
#include "beeadhoc-core-tests.h"

#include <cstdint>
#include <vector>

namespace beeadhoc::core::test {

using swarm::core::RouteAction;

namespace {

Swarm sample() {
    Swarm s;
    s.source = 9;
    s.destination = -2;
    s.type = ForagerType::Delay;
    s.route = {9, 4, -2};
    s.foragers = {{{-2, 4, 9}, pushInfo(pushInfo(0, 7), 6)}, {{-2, 5, 6, 9}, 0x123456u}};
    return s;
}

/// Node 0 found `last` by a scout on a chain, then sent it `packets` data
/// packets along the chain (one-way traffic; the trailer is written here so
/// that the source's forager supply does not matter).
Network oneWay(int nodes, int packets, Config config = {}) {
    config.initialScoutTtl = 10;
    Network net(nodes, config, 21);
    net.chain();
    const int last = nodes - 1;
    net.at(0).routeOutput(last, ForagerType::Energy);
    net.clock().runUntil(1.0);
    RouteTrailer t;
    for (int i = 0; i < nodes; ++i)
        t.route.push_back(i);
    for (int i = 0; i < packets; ++i) {
        net.carry(t, ForagerType::Energy);
        net.clock().runUntil(net.clock().now() + 0.01);
    }
    net.clock().runUntil(net.clock().now() + 0.1);
    return net;
}

}  // namespace

void TestSwarmRoundtrip(Checker& c) {
    const Swarm s = sample();
    const auto bytes = encodeSwarm(s);
    c.check(bytes.size() == s.wireSize() && bytes.size() == Swarm::kFixedWireSize + 4 * 3 +
                                                                2 * Swarm::kCargoFixedWireSize +
                                                                4 * (3 + 4),
            "encoded size");
    c.check(bytes[0] == Swarm::kWireVersion && bytes[1] == kSwarmKind && bytes[1] == 3,
            "version byte, then the kind, 3");
    c.check(bytes[2] == 9 && bytes[3] == 0 && bytes[10] == 1, "little-endian source; type byte");
    const auto back = decodeSwarm(bytes, 11, 20);
    c.check(back.has_value() && *back == s, "swarm round-trips");
    c.check(std::string(swarm::core::findKindName(kKindNames, kSwarmKind)) == "SWARM",
            "the kind is named SWARM");
    Swarm empty;
    const auto e = decodeSwarm(encodeSwarm(empty), 11, 20);
    c.check(e.has_value() && *e == empty, "an empty swarm round-trips");
}

void TestSwarmFailsClosed(Checker& c) {
    const auto good = encodeSwarm(sample());
    auto bad = good;
    bad[0] = Swarm::kWireVersion + 1;
    c.check(!decodeSwarm(bad, 11, 20), "wrong version");
    bad = good;
    bad[1] = 1;
    c.check(!decodeSwarm(bad, 11, 20), "a scout's kind is not a swarm");
    bad = good;
    bad[10] = static_cast<std::uint8_t>(kForagerTypes);
    c.check(!decodeSwarm(bad, 11, 20), "unknown forager type");
    bad = good;
    bad[Swarm::kFixedWireSize - 2 + 4 * 3 + 2 + 3] = 0x01;  // first cargo's info top byte
    c.check(!decodeSwarm(bad, 11, 20), "info bits above 24");
    c.check(!decodeSwarm(good, 3, 20), "a route longer than the cap");
    c.check(!decodeSwarm(good, 11, 1), "more foragers than SWARMSIZE");
    bad = good;
    bad.pop_back();
    c.check(!decodeSwarm(bad, 11, 20), "truncated");
    bad = good;
    bad.push_back(0);
    c.check(!decodeSwarm(bad, 11, 20), "trailing byte");
    c.check(!decodeSwarm({}, 11, 20), "empty buffer");
    c.check(!decodeScout(good, 11), "the scout decoder refuses a swarm");
}

void TestSwarmFuzz(Checker& c) {
    SeededRng rng(20261114);
    const auto good = encodeSwarm(sample());
    int decoded = 0;
    for (int round = 0; round < 4000; ++round) {
        std::vector<std::uint8_t> bytes;
        if (round % 2 == 0) {
            bytes = good;
            const int flips = 1 + rng.uniformInt(3);
            for (int i = 0; i < flips; ++i)
                bytes[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(bytes.size())))] ^=
                    static_cast<std::uint8_t>(1 + rng.uniformInt(255));
            if (rng.uniformInt(4) == 0) bytes.resize(static_cast<std::size_t>(rng.uniformInt(64)));
        } else {
            bytes.resize(static_cast<std::size_t>(rng.uniformInt(80)));
            for (auto& b : bytes)
                b = static_cast<std::uint8_t>(rng.uniformInt(256));
        }
        if (const auto s = decodeSwarm(bytes, 11, 20)) {
            ++decoded;
            c.check(encodeSwarm(*s) == bytes, "a decoded frame re-encodes to the same bytes");
            bool bounded = s->route.size() <= 11 && s->foragers.size() <= 20;
            for (const auto& f : s->foragers)
                bounded = bounded && f.route.size() <= 11 && f.info <= kInfoMask;
            c.check(bounded, "decoded within bounds");
        }
    }
    c.check(decoded > 0, "some mutations still decode");
}

void TestSwarmTrigger(Checker& c) {
    const Config config;
    {
        // 20 packets leave 20 foreign foragers at 3: not more than SWARMSIZE.
        Network net = oneWay(4, 20);
        c.check(net.swarms.empty() &&
                    net.at(3).foragers().count(0, ForagerType::Energy, false) == 20,
                "20 foreign foragers: no swarm yet (Listing 13.30: more than SWARMSIZE)");
    }
    Network net = oneWay(4, 21);
    // The 21st packet tips it: 21 > 20 foreign, 0 < 11 own at 3.
    c.check(net.swarms == std::vector<std::pair<int, NodeAddress>>{{3, 2}, {2, 1}, {1, 0}},
            "one swarm, relayed hop by hop along its source route");
    c.check(net.at(3).foragers().count(0, ForagerType::Energy, false) == 1,
            "SWARMSIZE foragers left with it, the newest stays");
    // At 0: the scout's own forager and the 20 that came home, energy only.
    c.check(net.at(0).foragers().count(3, ForagerType::Energy, true) == 21 &&
                net.at(0).foragers().count(3, ForagerType::Energy, false) == 0 &&
                net.at(0).foragers().size(3, ForagerType::Delay) == 1,
            "unpacked at home as own foragers, in the swarm's type array");
    const auto d = net.at(0).routeOutput(3, ForagerType::Energy);
    c.check(d.action == RouteAction::Unicast && d.route == std::vector<NodeAddress>{1, 2, 3},
            "each one reversed by the sender: from home back to the sender");

    Config noOwnRoom = config;
    noOwnRoom.swarmSizeOwn = 0;  // "own < 0" never holds
    Network quiet = oneWay(3, 25, noOwnRoom);
    c.check(quiet.swarms.empty(), "no swarm while own foragers reach SWARMSIZE_OWN");

    Network unsunk(3, Config{}, 22);
    unsunk.at(0).setSwarmSink({});  // no sink: the trigger never fires
    for (int i = 0; i < 30; ++i) {
        RouteTrailer t{{1, 0}, 0, std::nullopt};
        unsunk.at(0).deliverData(t, ForagerType::Energy);
    }
    c.check(unsunk.at(0).foragers().count(1, ForagerType::Energy, false) == 30 &&
                unsunk.swarms.empty(),
            "without a swarm sink the foreign foragers stay");
}

void TestSwarmReceive(Checker& c) {
    Network net(3, Config{}, 23);
    net.chain();
    Swarm s;
    s.source = 2;
    s.destination = 0;
    s.type = ForagerType::Throughput;
    s.route = {2, 1, 0};
    s.foragers = {{{0, 1, 2}, pushInfo(pushInfo(0, 7), 7)},  // home: kept
                  {{1, 2}, 0},                               // not from here: skipped
                  {{0, 1, 5}, 0}};                           // not to the sender: skipped
    c.check(net.at(0).searching(2) == false, "no search");
    net.at(0).routeOutput(2, ForagerType::Energy);  // a search for 2 is out
    net.at(0).onSwarm(s, 1);
    c.check(net.at(0).foragers().count(2, ForagerType::Throughput, true) == 1,
            "only the cargo that leads from here to the sender is unpacked");
    c.check(!net.at(0).searching(2) && !net.ready.empty() && net.ready.back().second == 2,
            "a swarm from 2 ends the search for 2");
    Swarm stray = s;
    stray.route = {2, 5, 0};
    const std::size_t before = net.swarms.size();
    net.at(1).onSwarm(stray, 2);
    c.check(net.swarms.size() == before, "a node not on the route does not relay it");
}

void TestLinkFailure(Checker& c) {
    Config config;
    config.initialScoutTtl = 10;
    Network net(4, config, 24);
    net.link(0, 1);
    net.link(1, 2);
    net.link(0, 3);
    net.at(0).routeOutput(2, ForagerType::Energy);
    net.at(0).routeOutput(3, ForagerType::Energy);
    net.clock().runUntil(1.0);
    c.check(net.at(0).foragers().size(2, ForagerType::Energy) == 1 &&
                net.at(0).foragers().size(3, ForagerType::Energy) == 1,
            "foragers toward 2 (via 1) and 3 (direct)");
    c.check(net.at(0).onLinkFailure(5) == 0, "an unknown neighbour removes nothing");
    c.check(net.at(0).onLinkFailure(1) == kForagerTypes,
            "every forager over the failed link goes, in every type array");
    c.check(net.at(0).foragers().size(2, ForagerType::Energy) == 0 &&
                net.at(0).foragers().size(3, ForagerType::Energy) == 1,
            "the others stay");
    c.check(net.at(0).routeOutput(2, ForagerType::Energy).action == RouteAction::Queue &&
                net.at(0).searching(2),
            "the next packet for 2 waits for a new scout");
}

}  // namespace beeadhoc::core::test
