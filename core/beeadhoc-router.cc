// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "beeadhoc-router.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace beeadhoc::core {

using swarm::core::RouteAction;

Router::Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
               swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers,
               swarm::core::IEnergy& energy, Sink sink, RouteReady routeReady)
    : self_(self), config_(config), clock_(clock), rng_(rng), timers_(timers), energy_(energy),
      sink_(std::move(sink)), routeReady_(std::move(routeReady)), store_(config),
      seen_(config.scoutSeenListLength) {
    // The MAXAGE timer of [PG460] §13.3, read as the same age-based collection
    // run every MAXAGE (fidelity row IPT_BEEADHOC_MAXAGE).
    if (config_.maxAge > 0.0) timers_.schedule(config_.maxAge, [this] { collectTimer(); });
}

std::uint8_t Router::ownLevel() const {
    return batteryLevel(energy_.residualEnergyFraction());
}

DataDecision Router::routeOutput(NodeAddress dest, ForagerType type) {
    if (auto f = store_.take(dest, type, clock_.now(), rng_); f && !f->route.empty())
        return {RouteAction::Unicast, std::move(f->route)};
    if (searches_.contains(dest)) return {RouteAction::Queue, {}};
    if (searches_.size() >= config_.scoutRetransmitListLength) return {RouteAction::Drop, {}};
    Search& search = searches_[dest];
    search.ttl = std::min(config_.initialScoutTtl, config_.maxScoutTtl);
    sendScout(dest, search);
    return {RouteAction::Queue, {}};
}

void Router::sendScout(NodeAddress dest, Search& search) {
    search.id = nextScoutId_++;
    if (search.ttl >= config_.maxScoutTtl) ++search.roundsAtMax;
    Scout s;
    s.mode = ScoutMode::Forward;
    s.source = self_;
    s.destination = dest;
    s.id = search.id;
    s.ttl = search.ttl;
    s.route = {self_};
    seen_.record(self_, s.id);  // its own scout echoed back is a repeat
    sink_(ScoutDecision::broadcast(std::move(s)));
    const std::uint32_t id = search.id;
    timers_.schedule(config_.scoutResendInterval, [this, dest, id] { onResendTimer(dest, id); });
}

void Router::onResendTimer(NodeAddress dest, std::uint32_t id) {
    auto it = searches_.find(dest);
    if (it == searches_.end() || it->second.id != id) return;  // answered or superseded
    Search& search = it->second;
    if (search.ttl >= config_.maxScoutTtl && search.roundsAtMax >= config_.maxRoundsAtMaxTtl) {
        searches_.erase(it);
        sink_(ScoutDecision::discardPending(dest));
        return;
    }
    search.ttl = static_cast<std::uint8_t>(
        std::min<int>(2 * static_cast<int>(search.ttl), config_.maxScoutTtl));
    sendScout(dest, search);
}

void Router::collectTimer() {
    store_.collect(clock_.now());
    timers_.schedule(config_.maxAge, [this] { collectTimer(); });
}

void Router::onScout(const Scout& scout, NodeAddress /*from*/) {
    if (scout.route.empty() || scout.route.front() != scout.source) return;
    if (scout.mode == ScoutMode::Forward)
        onForwardScout(scout);
    else
        onBackwardScout(scout);
}

void Router::onForwardScout(const Scout& scout) {
    if (scout.source == self_ || !seen_.record(scout.source, scout.id)) return;
    if (std::find(scout.route.begin(), scout.route.end(), self_) != scout.route.end()) return;
    const auto maxNodes = static_cast<std::size_t>(std::max(config_.maxHops, 1)) + 1;

    if (scout.destination == self_) {
        // Turn it back (mode 1 -> 2, Listing 13.26) with this node's own level
        // as the route's L-th value (maintainer decision 2026-11-10, #4).
        if (scout.route.size() + 1 > maxNodes) return;
        Scout back = scout;
        back.mode = ScoutMode::Backward;
        back.route.push_back(self_);
        back.info = pushInfo(scout.info, ownLevel());
        const NodeAddress previous = scout.route.back();
        sink_(ScoutDecision::unicast(previous, std::move(back)));
        return;
    }

    // Relay: TTL left, and room for this node and the destination after it.
    if (scout.ttl <= 1 || scout.route.size() + 2 > maxNodes) return;
    Scout relay = scout;
    relay.ttl = static_cast<std::uint8_t>(scout.ttl - 1);
    relay.route.push_back(self_);
    relay.info = pushInfo(scout.info, ownLevel());
    const Time delay = rng_.uniform() * config_.scoutJitterMax;
    timers_.schedule(delay, [this, s = std::move(relay)]() mutable {
        sink_(ScoutDecision::broadcast(std::move(s)));
    });
}

void Router::onBackwardScout(const Scout& scout) {
    const auto at = std::find(scout.route.begin(), scout.route.end(), self_);
    if (at == scout.route.end() || scout.route.back() != scout.destination) return;
    if (at != scout.route.begin()) {
        sink_(ScoutDecision::unicast(*(at - 1), scout));
        return;
    }
    if (scout.source != self_) return;
    // Home: one own forager per type (Listing 13.28, #13), rated on the
    // route's L links (Listing 13.15, #4).
    Forager f;
    f.route.assign(scout.route.begin() + 1, scout.route.end());
    if (f.route.empty()) return;
    f.info = scout.info;
    f.dances = rateForager(f.route.size(), f.info, config_);
    f.created = clock_.now();
    f.own = true;
    for (std::size_t t = 0; t < kForagerTypes; ++t)
        store_.put(scout.destination, static_cast<ForagerType>(t), f, clock_.now(), rng_);
    // Any of the search's scouts answering ends it, an earlier ring's included.
    searches_.erase(scout.destination);
    routeReady_(scout.destination);
}

}  // namespace beeadhoc::core
