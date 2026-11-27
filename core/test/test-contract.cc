// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// swarm-core's protocol contract suite (core/testbench/swarm-contract-tests.h,
// its README "Protocol contract suite: how a protocol opts in"; #198) on
// BeeAdHoc's core: determinism (org rule 5), null and sample-less optional
// ports, and scouts with no TTL left or back at their source dropped.
//
// The testbench adapter below carries out the Router's decisions as the ns-3
// adapter does: scouts, swarms and data trailers cross the air through their
// wire codecs, a data packet with no forager waits for RouteReady, and an
// undelivered unicast is a link failure. Each node's traffic is part of its
// adapter (started in start()), so the suite's contents digest can wrap it.
#include "../beeadhoc-router.h"
#include "testbench/swarm-contract-tests.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace tb = swarm::core::testbench;
using beeadhoc::core::Config;
using beeadhoc::core::DataDecision;
using beeadhoc::core::ForagerType;
using beeadhoc::core::NodeAddress;
using beeadhoc::core::Router;
using beeadhoc::core::RouteTrailer;
using beeadhoc::core::Scout;
using beeadhoc::core::ScoutDecision;
using beeadhoc::core::ScoutMode;
using beeadhoc::core::SwarmDecision;
using swarm::core::RouteAction;
using swarm::core::Time;

/// The data packet's kind on the testbench air: after the control kinds
/// (kKindNames: the two scout modes, the swarm).
constexpr std::uint8_t kDataKind = 4;

/// One frame on the air: its kind, the forager type (data only) and the
/// encoded header (scout, swarm or RouteTrailer).
struct Frame {
    std::uint8_t kind = 0;
    std::uint8_t type = 0;
    std::vector<std::uint8_t> bytes;
};

std::uint64_t fnv1a(const Frame& f) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&h](std::uint8_t b) {
        h ^= b;
        h *= 0x100000001B3ull;
    };
    mix(f.kind);
    mix(f.type);
    for (std::uint8_t b : f.bytes)
        mix(b);
    return h;
}

/// The testbench clock is const; the Router takes a mutable IClock.
class ClockRef : public swarm::core::IClock {
public:
    explicit ClockRef(const swarm::core::IClock& clock) : clock_(clock) {}
    Time now() const override { return clock_.now(); }

private:
    const swarm::core::IClock& clock_;
};

/// The suite hands an IEnergy (or none); the Router reads an IEnergyProvider.
/// None is the NullEnergyProvider (Unknown); a present port is a battery
/// reading its fraction (NaN when it has no sample).
class EnergyBridge : public swarm::core::IEnergyProvider {
public:
    explicit EnergyBridge(const swarm::core::IEnergy* energy) : energy_(energy) {}
    swarm::core::EnergySnapshot snapshot() const override {
        if (energy_ == nullptr) return {};
        return {swarm::core::PowerSource::Battery, energy_->residualEnergyFraction()};
    }

private:
    const swarm::core::IEnergy* energy_;
};

/// The data packets each node originates: (time, destination).
using Traffic = std::map<NodeAddress, std::vector<std::pair<Time, NodeAddress>>>;

class BeeNode : public tb::INodeAdapter<Frame> {
public:
    BeeNode(tb::NodePorts<Frame>& ports, const tb::ContractProviders& providers,
            const Config& config, const Traffic& traffic)
        : ports_(ports), config_(config), clock_(ports.clock()), energy_(providers.energy),
          router_(
              ports.address(), config_, clock_, ports.rng(), ports.timers(), energy_,
              [this](const ScoutDecision& d) { onScoutDecision(d); },
              [this](NodeAddress dest) { onRouteReady(dest); }, providers.observer) {
        router_.setSwarmSink([this](const SwarmDecision& d) { onSwarmDecision(d); });
        if (auto it = traffic.find(ports.address()); it != traffic.end()) sends_ = it->second;
    }

    void start() override {
        for (const auto& [at, dest] : sends_)
            ports_.timers().schedule(at, [this, dest] { originate(dest); });
    }

    void receive(NodeAddress from, const Frame& f) override {
        const std::size_t maxNodes = static_cast<std::size_t>(config_.maxHops) + 1;
        if (f.kind == static_cast<std::uint8_t>(ScoutMode::Forward) ||
            f.kind == static_cast<std::uint8_t>(ScoutMode::Backward)) {
            if (auto s = beeadhoc::core::decodeScout(f.bytes, maxNodes)) router_.onScout(*s, from);
        } else if (f.kind == beeadhoc::core::kSwarmKind) {
            if (auto s = beeadhoc::core::decodeSwarm(f.bytes, maxNodes, config_.swarmSize))
                router_.onSwarm(*s, from);
        } else if (f.kind == kDataKind) {
            auto t = beeadhoc::core::decodeTrailer(f.bytes, maxNodes);
            if (!t || t->route.empty()) return;
            const auto type = static_cast<ForagerType>(f.type);
            if (t->route.back() == ports_.address()) {
                router_.deliverData(*t, type);
                return;
            }
            const NodeAddress next = router_.forwardData(*t);
            if (next != swarm::core::kInvalidAddress) ports_.unicast(next, data(*t, type));
        }
    }

    void undelivered(NodeAddress to, const Frame& /*f*/) override { router_.onLinkFailure(to); }

private:
    static Frame data(const RouteTrailer& t, ForagerType type) {
        return {kDataKind, static_cast<std::uint8_t>(type), beeadhoc::core::encodeTrailer(t)};
    }

    void originate(NodeAddress dest) {
        const DataDecision d = router_.routeOutput(dest, ForagerType::Energy);
        if (d.action == RouteAction::Unicast && !d.route.empty())
            ports_.unicast(d.route.front(), data(d.trailer, ForagerType::Energy));
        else if (d.action == RouteAction::Queue)
            ++held_[dest];
    }

    void onRouteReady(NodeAddress dest) {
        auto it = held_.find(dest);
        if (it == held_.end()) return;
        const int n = it->second;
        held_.erase(it);
        for (int i = 0; i < n; ++i)
            originate(dest);
    }

    void onScoutDecision(const ScoutDecision& d) {
        if (d.action == RouteAction::DiscardPending) {
            held_.erase(d.nextHop);
            return;
        }
        if (!d.message) return;
        const Frame f{static_cast<std::uint8_t>(d.message->mode), 0,
                      beeadhoc::core::encodeScout(*d.message)};
        if (d.action == RouteAction::Broadcast)
            ports_.broadcast(f);
        else if (d.action == RouteAction::Unicast)
            ports_.unicast(d.nextHop, f);
    }

    void onSwarmDecision(const SwarmDecision& d) {
        if (d.action != RouteAction::Unicast || !d.message) return;
        ports_.unicast(d.nextHop,
                       {beeadhoc::core::kSwarmKind, 0, beeadhoc::core::encodeSwarm(*d.message)});
    }

    tb::NodePorts<Frame>& ports_;
    Config config_;
    ClockRef clock_;
    EnergyBridge energy_;
    Router router_;
    std::vector<std::pair<Time, NodeAddress>> sends_;
    std::map<NodeAddress, int> held_;  ///< packets waiting for a route, per destination
};

}  // namespace

int main() {
    // A 6-node line (the suite's default), 10 s: node 0 sends to node 5 every
    // 0.1 s from 1 s on (enough foreign foragers at node 5 for swarms), node 4
    // to node 1 every 0.25 s, and the 2-3 link is down from 6 s to 7 s (link
    // failure, then a new search).
    Traffic traffic;
    for (int i = 0; i < 80; ++i)
        traffic[0].push_back({1.0 + 0.1 * i, 5});
    for (int i = 0; i < 30; ++i)
        traffic[4].push_back({1.5 + 0.25 * i, 1});
    const Config config{};

    tb::ContractSpec<Frame> spec;
    spec.factory = [config, traffic](tb::NodePorts<Frame>& p, const tb::ContractProviders& prov) {
        return std::make_unique<BeeNode>(p, prov, config, traffic);
    };
    spec.kindOf = [](const Frame& f) { return f.kind; };
    spec.script = [](tb::Network<Frame>& net) {
        net.linkAt(6.0, 2, 3, false);
        net.linkAt(7.0, 2, 3, true);
    };
    spec.payloadHash = fnv1a;
    // A scout's TTL is its hop limit: a forward scout from `origin` toward
    // `dest`, as its source sends it. Its id is one no source here reaches.
    spec.hopLimited = [](NodeAddress origin, NodeAddress dest, int hopLimit) {
        Scout s;
        s.mode = ScoutMode::Forward;
        s.source = origin;
        s.destination = dest;
        s.id = 0xFFFFFFFFu;
        s.ttl = static_cast<std::uint8_t>(hopLimit);
        s.route = {origin};
        return Frame{static_cast<std::uint8_t>(ScoutMode::Forward), 0,
                     beeadhoc::core::encodeScout(s)};
    };
    return tb::runProtocolContract(spec) == 0 ? 0 : 1;
}
