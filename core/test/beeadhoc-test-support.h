// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Test support for BeeAdHoc's core tests, on the Checker and SeededRng of
// beeadhoc-check.h (swarm-core's, copied): a discrete-event clock and
// scheduler behind swarm-core's ports, a fixed energy port, and a static
// network that carries scouts between routers through the wire codec.
#ifndef BEEADHOC_CORE_TEST_SUPPORT_H
#define BEEADHOC_CORE_TEST_SUPPORT_H

#include "../beeadhoc-router.h"
#include "beeadhoc-check.h"

#include <cmath>
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

/// The clock and timer ports as one discrete-event queue: events run in
/// (time, scheduling order), so a run is fully deterministic. It implements
/// scheduleCancellable and cancel (#97): a cancelled event never runs.
class EventQueue : public swarm::core::IClock, public swarm::core::ITimerScheduler {
public:
    Time now() const override { return now_; }
    void schedule(Time delay, std::function<void()> callback) override {
        push(delay, std::move(callback), swarm::core::kNoTimer);
    }
    swarm::core::TimerHandle scheduleCancellable(Time delay,
                                                 std::function<void()> callback) override {
        const swarm::core::TimerHandle handle = ++lastHandle_;
        pending_.insert(handle);
        push(delay, std::move(callback), handle);
        return handle;
    }
    void cancel(swarm::core::TimerHandle timer) override {
        if (pending_.erase(timer) != 0) ++cancelled_;
    }
    /// Runs every event due at or before `until`, then sets the clock to it.
    void runUntil(Time until) {
        while (!events_.empty() && events_.top().at <= until) {
            Event e = events_.top();
            events_.pop();
            now_ = e.at;
            if (e.handle != swarm::core::kNoTimer && pending_.erase(e.handle) == 0)
                continue;  // cancelled
            e.run();
        }
        now_ = until;
    }
    /// Cancellable timers scheduled and not yet run or cancelled.
    std::size_t pendingCancellable() const { return pending_.size(); }
    /// Timers cancelled before they ran.
    std::size_t cancelled() const { return cancelled_; }

private:
    struct Event {
        Time at;
        std::uint64_t order;
        std::function<void()> run;
        swarm::core::TimerHandle handle;
        bool operator>(const Event& o) const { return at != o.at ? at > o.at : order > o.order; }
    };
    void push(Time delay, std::function<void()> callback, swarm::core::TimerHandle handle) {
        events_.push(
            Event{now_ + (delay > 0.0 ? delay : 0.0), next_++, std::move(callback), handle});
    }
    Time now_ = 0.0;
    std::uint64_t next_ = 0;
    swarm::core::TimerHandle lastHandle_ = swarm::core::kNoTimer;
    std::set<swarm::core::TimerHandle> pending_;
    std::size_t cancelled_ = 0;
    std::priority_queue<Event, std::vector<Event>, std::greater<>> events_;
};

/// A battery that reports a fixed residual fraction (NaN: no battery data,
/// an Unknown source, as the adapter's SwarmEnergyProvider gives).
class FixedEnergy : public swarm::core::IEnergyProvider {
public:
    double fraction = std::numeric_limits<double>::quiet_NaN();
    swarm::core::EnergySnapshot snapshot() const override {
        if (std::isnan(fraction)) return {};
        return {swarm::core::PowerSource::Battery, fraction};
    }
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
            routers_.back()->setSwarmSink([this, i](const SwarmDecision& d) { sendSwarm(i, d); });
        }
    }

    // The routers and callbacks point at this object: it never moves.
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;

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
    std::vector<std::pair<int, NodeAddress>> swarms;  ///< (from, to) swarm sends
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

    void sendSwarm(int from, const SwarmDecision& d) {
        swarms.push_back({from, d.nextHop});
        if (!d.message || !links_.contains({from, d.nextHop})) return;
        const auto bytes = encodeSwarm(*d.message);
        const int to = d.nextHop;
        clock_.schedule(linkDelay_, [this, from, to, bytes] {
            const auto maxNodes = static_cast<std::size_t>(config_.maxHops) + 1;
            if (auto s = decodeSwarm(bytes, maxNodes, config_.swarmSize)) at(to).onSwarm(*s, from);
        });
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
