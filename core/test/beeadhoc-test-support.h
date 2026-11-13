// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Test support for BeeAdHoc's core tests, on swarm-core's Checker and
// SeededRng (its core/test/swarm-core-check.h): a discrete-event clock and
// scheduler behind swarm-core's ports, a fixed energy port, and a static
// network that carries scouts between routers through the wire codec.
#ifndef BEEADHOC_CORE_TEST_SUPPORT_H
#define BEEADHOC_CORE_TEST_SUPPORT_H

#include "../beeadhoc-router.h"
#include "test/swarm-core-check.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace beeadhoc::core::test {

using swarm::core::test::Checker;
using swarm::core::test::SeededRng;

/// The clock and timer ports as one discrete-event queue: events run in
/// (time, scheduling order), so a run is fully deterministic.
class EventQueue : public swarm::core::IClock, public swarm::core::ITimerScheduler {
public:
    Time now() const override { return now_; }
    void schedule(Time delay, std::function<void()> callback) override {
        events_.push(Event{now_ + (delay > 0.0 ? delay : 0.0), next_++, std::move(callback)});
    }
    /// Runs every event due at or before `until`, then sets the clock to it.
    void runUntil(Time until) {
        while (!events_.empty() && events_.top().at <= until) {
            Event e = events_.top();
            events_.pop();
            now_ = e.at;
            e.run();
        }
        now_ = until;
    }

private:
    struct Event {
        Time at;
        std::uint64_t order;
        std::function<void()> run;
        bool operator>(const Event& o) const { return at != o.at ? at > o.at : order > o.order; }
    };
    Time now_ = 0.0;
    std::uint64_t next_ = 0;
    std::priority_queue<Event, std::vector<Event>, std::greater<>> events_;
};

/// A battery that reports a fixed residual fraction (NaN: no battery data).
class FixedEnergy : public swarm::core::IEnergy {
public:
    double fraction = std::numeric_limits<double>::quiet_NaN();
    double residualEnergyFraction() const override { return fraction; }
};

/// Routers on a fixed graph. Every scout a router sends is encoded, then
/// decoded by each receiving neighbour after `linkDelay`: the codec is on the
/// path, as in the adapter. `log` records each send for determinism checks.
class Network {
public:
    Network(int nodes, const Config& config, std::uint64_t seed, Time linkDelay = 0.001,
            swarm::core::IRouterObserver* observer = nullptr)
        : config_(config), rng_(seed), linkDelay_(linkDelay), energy_(nodes) {
        for (int i = 0; i < nodes; ++i) {
            energy_[static_cast<std::size_t>(i)] = std::make_unique<FixedEnergy>();
            routers_.push_back(std::make_unique<Router>(
                i, config_, clock_, rng_, clock_, *energy_[static_cast<std::size_t>(i)],
                [this, i](const ScoutDecision& d) { send(i, d); },
                [this, i](NodeAddress dest) { ready.push_back({i, dest}); }, observer));
        }
    }

    void link(int a, int b) {
        links_.insert({a, b});
        links_.insert({b, a});
    }
    /// A chain 0 - 1 - ... - (n - 1).
    void chain() {
        for (int i = 0; i + 1 < static_cast<int>(routers_.size()); ++i)
            link(i, i + 1);
    }

    Router& at(int i) { return *routers_[static_cast<std::size_t>(i)]; }

    /// Carries a data packet of `type` with `trailer` hop by hop through the
    /// codec, at once (no link delay): each forwarder runs forwardData, the
    /// last node deliverData. Returns the trailer as the last node decoded it,
    /// or nullopt when a hop dropped it or a link is missing.
    std::optional<RouteTrailer> carry(RouteTrailer trailer, ForagerType type) {
        const auto maxNodes = static_cast<std::size_t>(config_.maxHops) + 1;
        if (trailer.route.size() < 2) return std::nullopt;
        int from = trailer.route.front();
        int to = trailer.route[1];
        for (;;) {
            if (!links_.contains({from, to})) return std::nullopt;
            auto t = decodeTrailer(encodeTrailer(trailer), maxNodes);
            if (!t) return std::nullopt;
            if (to == t->route.back()) {
                at(to).deliverData(*t, type);
                return t;
            }
            const NodeAddress next = at(to).forwardData(*t);
            if (next == swarm::core::kInvalidAddress) return std::nullopt;
            trailer = std::move(*t);
            from = to;
            to = next;
        }
    }
    FixedEnergy& energy(int i) { return *energy_[static_cast<std::size_t>(i)]; }
    EventQueue& clock() { return clock_; }
    SeededRng& rng() { return rng_; }

    std::vector<std::pair<int, NodeAddress>> ready;     ///< (node, dest) RouteReady calls
    std::vector<std::pair<int, NodeAddress>> discards;  ///< (node, dest) DiscardPending
    std::vector<std::string> log;
    int broadcasts(int node) const {
        auto it = broadcasts_.find(node);
        return it == broadcasts_.end() ? 0 : it->second;
    }

private:
    void send(int from, const ScoutDecision& d) {
        using swarm::core::RouteAction;
        if (d.action == RouteAction::DiscardPending) {
            discards.push_back({from, d.nextHop});
            return;
        }
        const auto bytes = encodeScout(*d.message);
        log.push_back(std::to_string(clock_.now()) + " " + std::to_string(from) + ">" +
                      std::to_string(d.nextHop) + " ttl" + std::to_string(d.message->ttl));
        if (d.action == RouteAction::Broadcast) ++broadcasts_[from];
        for (int to = 0; to < static_cast<int>(routers_.size()); ++to) {
            if (!links_.contains({from, to})) continue;
            if (d.action == RouteAction::Unicast && d.nextHop != to) continue;
            clock_.schedule(linkDelay_, [this, from, to, bytes] {
                const auto maxNodes = static_cast<std::size_t>(config_.maxHops) + 1;
                if (auto s = decodeScout(bytes, maxNodes)) at(to).onScout(*s, from);
            });
        }
    }

    Config config_;
    EventQueue clock_;
    SeededRng rng_;
    Time linkDelay_;
    std::vector<std::unique_ptr<FixedEnergy>> energy_;
    std::vector<std::unique_ptr<Router>> routers_;
    std::set<std::pair<int, int>> links_;
    std::map<int, int> broadcasts_;
};

}  // namespace beeadhoc::core::test

#endif  // BEEADHOC_CORE_TEST_SUPPORT_H
