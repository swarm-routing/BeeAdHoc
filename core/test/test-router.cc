// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Route discovery end to end on small static networks: scouts out and back
// through the codec, foragers at the source, the expanding ring (#5), the
// caps (golden rule 7) and a fixed-seed trace (org rule 5).
#include "beeadhoc-core-tests.h"

#include <string>
#include <utility>
#include <vector>

namespace beeadhoc::core::test {

using swarm::core::RouteAction;

void TestDiscovery(Checker& c) {
    Config config;
    config.initialScoutTtl = 4;
    Network net(4, config, 1);
    net.chain();
    const auto first = net.at(0).routeOutput(3, ForagerType::Energy);
    c.check(first.action == RouteAction::Queue && net.at(0).searching(3), "no forager: queue");
    c.check(net.at(0).routeOutput(3, ForagerType::Delay).action == RouteAction::Queue &&
                net.broadcasts(0) == 1,
            "one scout per destination while it is out");
    net.clock().runUntil(1.0);
    c.check(net.ready == std::vector<std::pair<int, NodeAddress>>{{0, 3}}, "RouteReady once");
    c.check(!net.at(0).searching(3), "the search ends");
    for (ForagerType t : {ForagerType::Energy, ForagerType::Delay, ForagerType::Throughput})
        c.check(net.at(0).foragers().size(3, t) == 1, "one own forager per type (Listing 13.28)");
    c.check(net.at(1).foragers().destinations() == 0, "only the source keeps foragers");

    const auto d = net.at(0).routeOutput(3, ForagerType::Energy);
    c.check(d.action == RouteAction::Unicast && d.route == std::vector<NodeAddress>{1, 2, 3},
            "the forager's route, destination last");
    // 3 links at level 7 rate 8: the forager dances and stays.
    c.check(net.at(0).foragers().size(3, ForagerType::Energy) == 1, "a rated forager dances");
}

void TestDestinationLevel(Checker& c) {
    Config config;
    config.initialScoutTtl = 4;
    Network net(3, config, 1);
    net.chain();
    net.energy(2).fraction = 0.05;  // level 0 at the destination only
    net.at(0).routeOutput(2, ForagerType::Energy);
    net.clock().runUntil(1.0);
    const auto d = net.at(0).routeOutput(2, ForagerType::Energy);
    c.check(d.action == RouteAction::Unicast &&
                net.at(0).foragers().size(2, ForagerType::Energy) == 0,
            "the destination's level 0 rates the route 0: used once, no dance (#4)");
}

void TestExpandingRing(Checker& c) {
    const Config config;
    {
        Network net(5, config, 2);
        net.chain();  // 4 links: TTL 2 falls short, TTL 4 reaches
        net.at(0).routeOutput(4, ForagerType::Energy);
        net.clock().runUntil(config.scoutResendInterval - 0.01);
        c.check(net.ready.empty(), "TTL 2 does not reach 4 links");
        net.clock().runUntil(2.0);
        c.check(net.broadcasts(0) == 2 && net.ready.size() == 1, "the TTL 4 resend finds it");
    }
    {
        Network net(3, config, 3);
        net.link(0, 1);  // node 2 is unreachable
        net.at(0).routeOutput(2, ForagerType::Energy);
        net.clock().runUntil(10.0);
        std::vector<std::string> ttls;
        for (const auto& line : net.log)
            if (line.find(" 0>-1 ") != std::string::npos)
                ttls.push_back(line.substr(line.rfind(' ') + 1));
        c.check(ttls == std::vector<std::string>{"ttl2", "ttl4", "ttl8", "ttl10", "ttl10"},
                "TTL 2 -> 4 -> 8 -> 10, two rounds at 10 (#5)");
        c.check(net.discards == std::vector<std::pair<int, NodeAddress>>{{0, 2}} &&
                    !net.at(0).searching(2),
                "then the held packets are dropped");
    }
    {
        Config zero;
        zero.initialScoutTtl = 0;
        zero.maxScoutTtl = 0;
        Network net(2, zero, 7);
        net.at(0).routeOutput(1, ForagerType::Energy);
        net.clock().runUntil(10.0);
        c.check(net.broadcasts(0) == 2 && net.discards.size() == 1,
                "a configured TTL of 0 reads as 1, and the search still gives up");
    }
}

void TestScoutBounds(Checker& c) {
    {
        Config config;
        config.initialScoutTtl = 4;
        Network net(5, config, 4);
        net.link(0, 1);
        net.link(0, 2);
        net.link(1, 3);
        net.link(2, 3);
        net.link(3, 4);
        net.at(0).routeOutput(4, ForagerType::Energy);
        net.clock().runUntil(0.4);
        c.check(net.broadcasts(3) == 1, "the seen list relays a scout once");
        c.check(net.ready.size() == 1, "the diamond's first scout home ends the search");
    }
    {
        Config config;
        config.maxHops = 3;
        config.maxScoutTtl = 10;
        config.initialScoutTtl = 10;
        Network net(6, config, 5);
        net.chain();
        net.at(0).routeOutput(3, ForagerType::Energy);
        net.at(0).routeOutput(5, ForagerType::Energy);
        net.clock().runUntil(5.0);
        c.check(net.ready == std::vector<std::pair<int, NodeAddress>>{{0, 3}},
                "MAXHOPS: a route of 3 links is found, one of 5 is not");
    }
    {
        Config config;
        config.scoutRetransmitListLength = 2;
        Network net(4, config, 6);
        c.check(net.at(0).routeOutput(1, ForagerType::Energy).action == RouteAction::Queue &&
                    net.at(0).routeOutput(2, ForagerType::Energy).action == RouteAction::Queue &&
                    net.at(0).routeOutput(3, ForagerType::Energy).action == RouteAction::Drop,
                "RXMTLISTLENGTH: no search past the cap");
    }
}

namespace {

std::vector<std::string> meshTrace(std::uint64_t seed) {
    Network net(9, Config{}, seed);
    for (int i = 0; i < 9; ++i) {  // a 3 x 3 grid
        if (i % 3 != 2) net.link(i, i + 1);
        if (i < 6) net.link(i, i + 3);
    }
    net.at(0).routeOutput(8, ForagerType::Energy);
    net.at(6).routeOutput(2, ForagerType::Delay);
    net.clock().runUntil(3.0);
    for (int i = 0; i < 20; ++i) {
        const auto d = net.at(0).routeOutput(8, ForagerType::Energy);
        std::string hops;
        for (NodeAddress h : d.route)
            hops += std::to_string(h) + ",";
        net.log.push_back("route " + hops);
    }
    return net.log;
}

}  // namespace

void TestRouterDeterminism(Checker& c) {
    const auto a = meshTrace(42);
    c.check(!a.empty() && a == meshTrace(42), "same seed, byte-identical trace");
    c.check(a != meshTrace(43), "another seed, another trace (the jitter)");
}

}  // namespace beeadhoc::core::test
