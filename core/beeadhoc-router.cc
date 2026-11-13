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
               swarm::core::IEnergy& energy, Sink sink, RouteReady routeReady,
               swarm::core::IRouterObserver* observer)
    : self_(self), config_(config), clock_(clock), rng_(rng), timers_(timers), energy_(energy),
      sink_(std::move(sink)), routeReady_(std::move(routeReady)), observer_(observer),
      store_(config, observer), seen_(config.scoutSeenListLength) {
    // The MAXAGE timer of [PG460] §13.3, read as the same age-based collection
    // run every MAXAGE (fidelity row IPT_BEEADHOC_MAXAGE).
    if (config_.maxAge > 0.0) timers_.schedule(config_.maxAge, [this] { collectTimer(); });
}

std::uint8_t Router::ownLevel() const {
    return batteryLevel(energy_.residualEnergyFraction());
}

DataDecision Router::decide(NodeAddress dest, DataDecision d) {
    if (observer_) observer_->onDataDecision(dest, d.action);
    return d;
}

void Router::emit(const ScoutDecision& d) {
    // DiscardPending is the adapter's cue to drop held packets, not a send.
    if (observer_ && d.message)
        observer_->onControlSent(static_cast<std::uint8_t>(d.message->mode),
                                 d.action == RouteAction::Broadcast,
                                 static_cast<std::uint32_t>(d.message->wireSize()));
    sink_(d);
}

std::size_t Router::maxRouteNodes() const {
    return static_cast<std::size_t>(std::max(config_.maxHops, 1)) + 1;
}

namespace {

/// A route from the wire visits no node twice: a repeated address would send
/// a packet back and forth between two of its nodes (a loop).
bool loopFree(const std::vector<NodeAddress>& route) {
    for (auto it = route.begin(); it != route.end(); ++it)
        if (std::find(it + 1, route.end(), *it) != route.end()) return false;
    return true;
}

}  // namespace

DataDecision Router::routeOutput(NodeAddress dest, ForagerType type) {
    if (auto f = store_.take(dest, type, clock_.now(), rng_); f && !f->route.empty()) {
        DataDecision d{RouteAction::Unicast, std::move(f->route), {}};
        d.trailer.route.reserve(d.route.size() + 1);
        d.trailer.route.push_back(self_);
        d.trailer.route.insert(d.trailer.route.end(), d.route.begin(), d.route.end());
        d.trailer.info = 0;  // the source writes no level (Listing 13.6)
        // One foreign forager rides home to its owner, `dest` (§13.7.1;
        // fidelity row "Foreign foragers piggybacked per data packet").
        if (auto home = store_.takeForeign(dest, type, clock_.now())) {
            RouteTrailer::Piggyback p;
            p.route.reserve(home->route.size() + 1);
            p.route.push_back(self_);
            p.route.insert(p.route.end(), home->route.begin(), home->route.end());
            p.info = home->info;
            d.trailer.piggyback = std::move(p);
        }
        return decide(dest, std::move(d));
    }
    if (searches_.contains(dest)) return decide(dest, {RouteAction::Queue, {}, {}});
    if (searches_.size() >= config_.scoutRetransmitListLength)
        return decide(dest, {RouteAction::Drop, {}, {}});
    Search& search = searches_[dest];
    // A TTL of 0 would never grow, so the ring could never give up: at least 1.
    search.ttl = std::max<std::uint8_t>(std::min(config_.initialScoutTtl, config_.maxScoutTtl), 1);
    sendScout(dest, search);
    return decide(dest, {RouteAction::Queue, {}, {}});
}

NodeAddress Router::forwardData(RouteTrailer& trailer) {
    const NodeAddress dest =
        trailer.route.empty() ? swarm::core::kInvalidAddress : trailer.route.back();
    const auto at = std::find(trailer.route.begin(), trailer.route.end(), self_);
    if (trailer.route.size() > maxRouteNodes() || at == trailer.route.end() ||
        at == trailer.route.begin() || at + 1 == trailer.route.end() || !loopFree(trailer.route)) {
        if (observer_) observer_->onDataDecision(dest, RouteAction::Drop);
        return swarm::core::kInvalidAddress;
    }
    // FORWARD chain (Listing 13.7): shift left 3 bits, write this host's level.
    trailer.info = pushInfo(trailer.info, ownLevel());
    if (observer_) observer_->onDataDecision(dest, RouteAction::Unicast);
    return *(at + 1);
}

void Router::deliverData(const RouteTrailer& trailer, ForagerType type) {
    const auto& route = trailer.route;
    if (route.size() < 2 || route.size() > maxRouteNodes() || route.back() != self_ ||
        !loopFree(route))
        return;
    if (observer_) observer_->onDataDecision(self_, RouteAction::Deliver);
    const NodeAddress source = route.front();
    const Time now = clock_.now();

    // INPUT chain (§12.1.3, §13.7.1): the packet's route, reversed, is a
    // 'foreign' forager for its source. Its L values are the L - 1 forwarders'
    // levels and this node's own, the L-th (maintainer decision 2026-11-10,
    // #4). The rating reads only their minimum and average, so the order the
    // levels were written in does not matter for the reversed route.
    Forager foreign;
    foreign.route.assign(route.rbegin() + 1, route.rend());
    foreign.info = pushInfo(trailer.info, ownLevel());
    foreign.dances = rateForager(foreign.route.size(), foreign.info, config_);
    foreign.created = now;
    foreign.own = false;
    store_.put(source, type, std::move(foreign), now, rng_);

    // A forager of this node's that rode home on the packet (§13.7.1): from
    // the source back to this node, so reversed it leads from here to the
    // source. It is 'own' again, rated afresh from the info it carries (repo
    // choice, #36: the report does not say how a returning forager's dances
    // are set; a fresh rating is what a returned scout gets, Listing 13.28).
    if (const auto& p = trailer.piggyback;
        p && p->route.size() >= 2 && p->route.size() <= maxRouteNodes() &&
        p->route.front() == source && p->route.back() == self_ && loopFree(p->route)) {
        Forager mine;
        mine.route.assign(p->route.rbegin() + 1, p->route.rend());
        mine.info = p->info;
        mine.dances = rateForager(mine.route.size(), mine.info, config_);
        mine.created = now;
        mine.own = true;
        store_.put(source, type, std::move(mine), now, rng_);
    }
    routeFound(source);
}

void Router::routeFound(NodeAddress dest) {
    // Any route to `dest` ends its search, an earlier ring's scout included,
    // and the held packets go.
    if (searches_.erase(dest) == 0) return;
    routeReady_(dest);
}

void Router::sendScout(NodeAddress dest, Search& search) {
    search.id = nextScoutId_++;
    if (search.ttl >= std::max<std::uint8_t>(config_.maxScoutTtl, 1)) ++search.roundsAtMax;
    Scout s;
    s.mode = ScoutMode::Forward;
    s.source = self_;
    s.destination = dest;
    s.id = search.id;
    s.ttl = search.ttl;
    s.route = {self_};
    seen_.record(self_, s.id);  // its own scout echoed back is a repeat
    emit(ScoutDecision::broadcast(std::move(s)));
    const std::uint32_t id = search.id;
    timers_.schedule(config_.scoutResendInterval, [this, dest, id] { onResendTimer(dest, id); });
}

void Router::onResendTimer(NodeAddress dest, std::uint32_t id) {
    auto it = searches_.find(dest);
    if (it == searches_.end() || it->second.id != id) return;  // answered or superseded
    Search& search = it->second;
    if (search.ttl >= std::max<std::uint8_t>(config_.maxScoutTtl, 1) &&
        search.roundsAtMax >= config_.maxRoundsAtMaxTtl) {
        searches_.erase(it);
        sink_(ScoutDecision::discardPending(dest));
        return;
    }
    search.ttl = static_cast<std::uint8_t>(
        std::min<int>(2 * static_cast<int>(search.ttl), std::max<int>(config_.maxScoutTtl, 1)));
    sendScout(dest, search);
}

void Router::collectTimer() {
    store_.collect(clock_.now());
    timers_.schedule(config_.maxAge, [this] { collectTimer(); });
}

void Router::onScout(const Scout& scout, NodeAddress /*from*/) {
    if (observer_)
        observer_->onControlReceived(static_cast<std::uint8_t>(scout.mode),
                                     static_cast<std::uint32_t>(scout.wireSize()));
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
        emit(ScoutDecision::unicast(previous, std::move(back)));
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
        emit(ScoutDecision::broadcast(std::move(s)));
    });
}

void Router::onBackwardScout(const Scout& scout) {
    const auto at = std::find(scout.route.begin(), scout.route.end(), self_);
    if (at == scout.route.end() || scout.route.back() != scout.destination) return;
    if (at != scout.route.begin()) {
        emit(ScoutDecision::unicast(*(at - 1), scout));
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
