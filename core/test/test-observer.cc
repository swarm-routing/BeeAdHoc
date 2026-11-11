// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The router observer (swarm-routing/ns3-swarm-core#42, BeeAdHoc#54): a
// recording observer hears every callback from the router and the forager
// store, and attaching it changes no decision and no trace (org rule 5).
#include "beeadhoc-core-tests.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace beeadhoc::core::test {

using swarm::core::CountingObserver;
using swarm::core::IRouterObserver;
using swarm::core::RouteAction;

namespace {

struct Sent {
    std::uint8_t kind;
    bool broadcast;
    std::uint32_t bytes;
};
struct Changed {
    NodeAddress dest;
    NodeAddress nextHop;
    bool added;
};

/// Records every event; it has no state a router could read back.
struct Recorder : IRouterObserver {
    std::vector<Sent> sent;
    std::vector<std::pair<std::uint8_t, std::uint32_t>> received;
    std::vector<Changed> changes;
    std::vector<std::pair<NodeAddress, RouteAction>> decisions;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> states;

    void onControlSent(std::uint8_t k, bool b, std::uint32_t n) override {
        sent.push_back({k, b, n});
    }
    void onControlReceived(std::uint8_t k, std::uint32_t n) override { received.push_back({k, n}); }
    void onRouteChanged(NodeAddress d, NodeAddress h, bool a) override {
        changes.push_back({d, h, a});
    }
    void onDataDecision(NodeAddress d, RouteAction a) override { decisions.push_back({d, a}); }
    void onStateSize(std::uint32_t e, std::uint32_t b) override { states.push_back({e, b}); }
};

int count(const std::vector<Changed>& v, bool added) {
    int n = 0;
    for (const auto& ch : v)
        n += ch.added == added;
    return n;
}

}  // namespace

void TestObserverEvents(Checker& c) {
    Config config;
    config.initialScoutTtl = 4;
    Recorder rec;
    Network net(4, config, 1, 0.001, &rec);  // one observer for the whole chain
    net.chain();
    c.check(net.at(0).routeOutput(3, ForagerType::Energy).action == RouteAction::Queue, "queued");
    net.clock().runUntil(1.0);
    const auto d = net.at(0).routeOutput(3, ForagerType::Energy);
    c.check(d.action == RouteAction::Unicast, "forwarded on the forager");

    // Sent: the origin broadcast + two relays (ttl 4: nodes 1, 2) = 3 forward
    // scouts, then the destination's backward scout, relayed by 2 and 1.
    int forward = 0, backward = 0;
    for (const auto& s : rec.sent) {
        const bool fwd = s.kind == static_cast<std::uint8_t>(ScoutMode::Forward);
        forward += fwd;
        backward += !fwd;
        c.check(s.broadcast == fwd, "forward scouts broadcast, backward ones unicast");
        c.check(s.bytes >= Scout::kFixedWireSize + 4, "bytes include the header and a route");
    }
    c.check(forward == 3 && backward == 3, "control sent, by kind");
    int rxForward = 0, rxBackward = 0;
    for (const auto& r : rec.received) {
        (r.first == static_cast<std::uint8_t>(ScoutMode::Forward) ? rxForward : rxBackward)++;
        c.check(r.second >= Scout::kFixedWireSize + 4, "received bytes include header and route");
    }
    c.check(rxForward >= forward && rxBackward == backward,
            "control received, by kind (a broadcast has several receivers)");

    // The reported size is the encoded size (the Network encodes every send).
    Scout probe;
    probe.route = {0, 1, 2};
    c.check(probe.wireSize() == encodeScout(probe).size(), "wireSize is the encoded size");

    // Three own foragers (one per type) were added; the first take danced.
    c.check(count(rec.changes, true) == 3 && count(rec.changes, false) == 0, "forager adds");
    for (const auto& ch : rec.changes)
        c.check(ch.dest == 3 && ch.nextHop == 1, "route toward 3 through route.front()");
    c.check(rec.decisions.size() == 2 && rec.decisions[0].second == RouteAction::Queue &&
                rec.decisions[1].second == RouteAction::Unicast && rec.decisions[1].first == 3,
            "data decisions: queue then forward");
    c.check(!rec.states.empty() && rec.states.back().first == 3, "footprint counts 3 foragers");
    // 1 destination (12) + 3 foragers x (17 + 4 x 3 hops) = 99.
    c.check(rec.states.back().second == 12 + 3 * (17 + 4 * 3), "footprint bytes follow the model");
    for (std::size_t i = 1; i < rec.states.size(); ++i)
        c.check(rec.states[i] != rec.states[i - 1], "footprint is reported only when it changes");

    // Kind names resolve, and cover every kind sent.
    for (const auto& s : rec.sent)
        c.check(swarm::core::findKindName(kKindNames, s.kind) != nullptr, "kind is named");
    c.check(std::string(swarm::core::findKindName(kKindNames, 2)) == "BACKWARD_SCOUT",
            "BACKWARD_SCOUT");

    // MAXAGE collection removes the three foragers and shrinks the footprint.
    net.clock().runUntil(config.maxAge * 3 + 1.0);
    c.check(count(rec.changes, false) == 3 && rec.states.back() == std::make_pair(0u, 0u),
            "collection: removes reported, footprint back to zero");

    // RXMTLISTLENGTH: a Drop is a reported decision too.
    Config small;
    small.scoutRetransmitListLength = 1;
    Recorder rec2;
    Network net2(3, small, 2, 0.001, &rec2);
    net2.at(0).routeOutput(1, ForagerType::Energy);
    net2.at(0).routeOutput(2, ForagerType::Energy);
    c.check(rec2.decisions.back().second == RouteAction::Drop, "drop reported");

    // CountingObserver on the same events: bounded, ordered counters.
    swarm::core::CountingObserver counting(net.clock());
    Network net3(4, config, 1, 0.001, &counting);
    net3.chain();
    net3.at(0).routeOutput(3, ForagerType::Energy);
    net3.clock().runUntil(1.0);
    const auto stats = counting.stats();
    c.check(stats.control.size() == 2 && stats.routeAdds == 3, "counting observer: 2 kinds");
}

namespace {

struct Outcome {
    std::vector<std::string> log;
    std::vector<std::pair<int, NodeAddress>> ready;
    std::vector<std::string> routes;
    std::vector<int> draws;
};

Outcome mesh(std::uint64_t seed, IRouterObserver* observer) {
    Network net(9, Config{}, seed, 0.001, observer);
    for (int i = 0; i < 9; ++i) {
        if (i % 3 != 2) net.link(i, i + 1);
        if (i < 6) net.link(i, i + 3);
    }
    Outcome o;
    auto note = [&](NodeAddress dest, ForagerType t, int node) {
        const auto d = net.at(node).routeOutput(dest, t);
        std::string s = std::to_string(static_cast<int>(d.action)) + ":";
        for (NodeAddress h : d.route)
            s += std::to_string(h) + ",";
        o.routes.push_back(s);
    };
    note(8, ForagerType::Energy, 0);
    note(2, ForagerType::Delay, 6);
    net.clock().runUntil(3.0);
    for (int i = 0; i < 20; ++i)
        note(8, ForagerType::Energy, 0);
    net.clock().runUntil(Config{}.maxAge * 3);  // collection too
    o.log = net.log;
    o.ready = net.ready;
    // The rng's next draw shows whether the observer consumed any.
    o.draws.push_back(net.rng().uniformInt(1000000));
    return o;
}

}  // namespace

void TestObserverReadOnly(Checker& c) {
    Recorder rec;
    const Outcome with = mesh(42, &rec);
    const Outcome without = mesh(42, nullptr);
    c.check(!rec.sent.empty() && !rec.changes.empty(), "the observer heard the run");
    c.check(with.log == without.log, "same trace with and without an observer");
    c.check(with.ready == without.ready, "same RouteReady calls");
    c.check(with.routes == without.routes, "same data decisions");
    c.check(with.draws == without.draws, "the observer drew nothing from the rng");
}

void TestObserverStore(Checker& c) {
    Config config;
    config.foragerArraySize = 2;
    config.maxDestinations = 2;
    config.garbageProbability = 0.0;
    Recorder rec;
    ForagerStore store(config, &rec);
    SeededRng rng(1);
    auto f = [](NodeAddress hop) {
        Forager fg;
        fg.route = {hop, 9};
        fg.dances = 0;
        return fg;
    };
    store.put(5, ForagerType::Energy, f(1), 0.0, rng);
    store.put(5, ForagerType::Energy, f(2), 0.1, rng);
    store.put(5, ForagerType::Energy, f(3), 0.2, rng);  // array cap: the oldest (hop 1) goes
    c.check(count(rec.changes, true) == 3 && count(rec.changes, false) == 1 &&
                rec.changes[2].nextHop == 1 && !rec.changes[2].added,
            "array cap: oldest removed and reported before the add");
    store.put(6, ForagerType::Energy, f(4), 0.3, rng);
    store.put(7, ForagerType::Energy, f(5), 0.4, rng);  // destination cap: evicts 5 (LRU)
    int evicted = 0;
    for (const auto& ch : rec.changes)
        evicted += !ch.added && ch.dest == 5;
    c.check(evicted == 3, "LRU eviction reports each forager of the destination");
    c.check(rec.states.back() == std::make_pair(2u, 2u * 12 + 2u * (17 + 8)), "footprint after");
    // A taken forager with no dances left is used up: removed.
    const std::size_t before = rec.changes.size();
    c.check(store.take(6, ForagerType::Energy, 0.5, rng).has_value(), "take");
    c.check(rec.changes.size() == before + 1 && !rec.changes.back().added, "use removes it");
    // Taking a missing destination reports nothing.
    const std::size_t at = rec.changes.size();
    c.check(!store.take(99, ForagerType::Energy, 0.6, rng).has_value() && rec.changes.size() == at,
            "nothing to take, nothing reported");
}

}  // namespace beeadhoc::core::test
