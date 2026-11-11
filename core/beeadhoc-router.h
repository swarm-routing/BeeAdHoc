// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// BeeAdHoc's per-node state and decisions, written fresh from PG 460's Linux
// kernel module ([PG460] chs. 12-13) and docs/fidelity.md (golden rule 12).
// This first slice is route discovery: a packet with no forager starts a
// scout (§13.5), relays flood it with jitter and a seen list, the destination
// turns it back with its own battery level (#4), the source turns the
// returned scout into one own forager per type (Listing 13.28), and the
// expanding ring resends it (#5). The data path (the source-route trailer,
// FORWARD-chain levels, INPUT-chain foreign foragers, piggybacking), swarms
// and link failure are the next slices (#36).
//
// The router never touches a simulator (golden rule 10). Time, randomness,
// energy and deferred work come through swarm-core's ports; every scout leaves
// through the Sink as a swarm-core RouteDecision, which the adapter carries
// out. A fixed seed of the injected IRng gives byte-identical behaviour (org
// rule 5): the draws are the relay jitter (one uniform() per relayed scout) and
// the forager choice (ForagerStore::take).
#ifndef BEEADHOC_CORE_ROUTER_H
#define BEEADHOC_CORE_ROUTER_H

#include "beeadhoc-config.h"
#include "beeadhoc-forager.h"
#include "beeadhoc-scout.h"
#include "swarm-dedup-history.h"
#include "swarm-observer.h"
#include "swarm-ports.h"
#include "swarm-route-decision.h"

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace beeadhoc::core {

using ScoutDecision = swarm::core::RouteDecision<Scout>;

/// What to do with a data packet this node originates.
struct DataDecision {
    /// Unicast (to route.front(), carrying `route`), Queue (hold it until
    /// RouteReady or DiscardPending for the destination) or Drop.
    swarm::core::RouteAction action = swarm::core::RouteAction::Drop;
    std::vector<NodeAddress> route;  ///< the hops after this node, destination last
};

class Router {
public:
    /// Receives every scout this node sends (Broadcast, or Unicast to a
    /// neighbour) and DiscardPending(dest) when a search gives up.
    using Sink = std::function<void(const ScoutDecision&)>;
    /// Called when foragers for `dest` arrived: send the packets held for it.
    using RouteReady = std::function<void(NodeAddress dest)>;

    /// The ports must outlive the router, and the timers it schedules must
    /// not fire after it is destroyed (the adapter cancels them with it).
    /// `observer` (optional, null: none, one pointer check per event) is only
    /// read from: it hears each scout sent and received (kind = the scout's
    /// mode byte, kKindNames; bytes = its encoded size), each forager added or
    /// removed as a route change, each routeOutput decision (Unicast, Queue,
    /// Drop; Deliver belongs to the data path, #36) and the forager store's
    /// footprint when it changes. It never alters a decision or draws from
    /// `rng`, so a fixed seed behaves the same with and without one.
    Router(NodeAddress self, const Config& config, swarm::core::IClock& clock,
           swarm::core::IRng& rng, swarm::core::ITimerScheduler& timers,
           swarm::core::IEnergy& energy, Sink sink, RouteReady routeReady,
           swarm::core::IRouterObserver* observer = nullptr);

    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    /// A data packet of `type` this node originates for `dest` (OUTPUT chain,
    /// [PG460] §12.1.3): a forager's route when one exists; otherwise Queue,
    /// and a scout goes out unless one for `dest` is already out; Drop when
    /// RXMTLISTLENGTH searches are already out.
    DataDecision routeOutput(NodeAddress dest, ForagerType type);

    /// A scout arrived from the one-hop neighbour `from`.
    void onScout(const Scout& scout, NodeAddress from);

    /// True while a scout for `dest` is out.
    bool searching(NodeAddress dest) const { return searches_.contains(dest); }

    const ForagerStore& foragers() const { return store_; }
    NodeAddress self() const { return self_; }

private:
    struct Search {
        std::uint32_t id = 0;  ///< of the latest scout
        std::uint8_t ttl = 0;
        int roundsAtMax = 0;  ///< scouts sent at maxScoutTtl
    };

    DataDecision decide(NodeAddress dest, DataDecision d);
    void emit(const ScoutDecision& d);
    std::uint8_t ownLevel() const;
    void sendScout(NodeAddress dest, Search& search);
    void onResendTimer(NodeAddress dest, std::uint32_t id);
    void collectTimer();
    void onForwardScout(const Scout& scout);
    void onBackwardScout(const Scout& scout);

    NodeAddress self_;
    Config config_;
    swarm::core::IClock& clock_;
    swarm::core::IRng& rng_;
    swarm::core::ITimerScheduler& timers_;
    swarm::core::IEnergy& energy_;
    Sink sink_;
    RouteReady routeReady_;
    swarm::core::IRouterObserver* observer_;

    ForagerStore store_;
    swarm::core::DedupHistory seen_;          // SCOUTLISTLENGTH: (source, id)
    std::map<NodeAddress, Search> searches_;  // RXMTLISTLENGTH
    std::uint32_t nextScoutId_ = 1;
};

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_ROUTER_H
