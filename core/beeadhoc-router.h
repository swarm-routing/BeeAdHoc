// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// BeeAdHoc's per-node state and decisions, written fresh from PG 460's Linux
// kernel module ([PG460] chs. 12-13) and docs/fidelity.md (golden rule 12).
// Route discovery: a packet with no forager starts a scout (§13.5), relays
// flood it with jitter and a seen list, the destination turns it back with its
// own battery level (#4), the source turns the returned scout into one own
// forager per type (Listing 13.28), and the expanding ring resends it (#5).
// The data path (#36 slice 2): the source writes the forager's route into the
// packet's RouteTrailer with one foreign forager riding home (§13.7.1), each
// forwarding host writes its level (FORWARD chain, Listing 13.7), and the
// destination keeps the packet's reversed route as a foreign forager for the
// source, adding its own level as the L-th value (INPUT chain, §12.1.3, #4).
// Swarms (#36 slice 3): a destination holding more than SWARMSIZE foreign
// foragers for a source and fewer than SWARMSIZE_OWN own ones sends SWARMSIZE
// of them home in one source-routed swarm (Listing 13.30, §13.7.3). Link
// failure (#36 slice 4): every forager over a failed next link is removed.
//
// The router never touches a simulator (golden rule 10). Time, randomness,
// energy and deferred work come through swarm-core's ports; every scout leaves
// through the Sink as a swarm-core RouteDecision, which the adapter carries
// out. A fixed seed of the injected IRng gives byte-identical behaviour (org
// rule 5): the draws are the relay jitter (one uniform() per relayed scout) and
// the forager choice (ForagerStore::take); the data path draws nothing else.
#ifndef BEEADHOC_CORE_ROUTER_H
#define BEEADHOC_CORE_ROUTER_H

#include "beeadhoc-config.h"
#include "beeadhoc-forager.h"
#include "beeadhoc-scout.h"
#include "beeadhoc-swarm.h"
#include "beeadhoc-trailer.h"
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
using SwarmDecision = swarm::core::RouteDecision<Swarm>;

/// What to do with a data packet this node originates.
struct DataDecision {
    /// Unicast (to route.front(), carrying `trailer`), Queue (hold it until
    /// RouteReady or DiscardPending for the destination) or Drop.
    swarm::core::RouteAction action = swarm::core::RouteAction::Drop;
    std::vector<NodeAddress> route;  ///< the hops after this node, destination last
    /// On Unicast, what the packet carries: the whole route (this node first),
    /// info 0 (Listing 13.6) and at most one foreign forager riding home.
    RouteTrailer trailer;
};

class Router {
public:
    /// Receives every scout this node sends (Broadcast, or Unicast to a
    /// neighbour) and DiscardPending(dest) when a search gives up.
    using Sink = std::function<void(const ScoutDecision&)>;
    /// Receives every swarm this node sends or relays (Unicast to a neighbour).
    using SwarmSink = std::function<void(const SwarmDecision&)>;
    /// Called when foragers for `dest` arrived: send the packets held for it.
    using RouteReady = std::function<void(NodeAddress dest)>;

    /// The ports must outlive the router, and the timers it schedules must
    /// not fire after it is destroyed (the adapter cancels them with it).
    /// `observer` (optional, null: none, one pointer check per event) is only
    /// read from: it hears each scout sent and received (kind = the scout's
    /// mode byte, kKindNames; bytes = its encoded size), each forager added or
    /// removed as a route change, each data decision (routeOutput's Unicast,
    /// Queue and Drop; forwardData's Unicast and Drop; deliverData's Deliver,
    /// toward the packet's destination) and the forager store's
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

    /// A data packet in transit at this node (FORWARD chain): the hop after
    /// this node on the trailer's route, having shifted this node's battery
    /// level into `trailer.info` (Listing 13.7). kInvalidAddress (drop it)
    /// when the route does not name this node between its two ends, or
    /// visits a node twice.
    NodeAddress forwardData(RouteTrailer& trailer);

    /// A data packet of `type` delivered to this node, the trailer's last
    /// node (INPUT chain, §12.1.3): its reversed route becomes a 'foreign'
    /// forager for its source, rated on its L values, this node's own level
    /// the L-th (#4); a piggybacked forager whose owner is this node comes
    /// home as an 'own' forager toward the packet's source (§13.7.1). A search
    /// for the source ends, and RouteReady releases its held packets. A
    /// trailer whose route is not source .. this node, or visits a node
    /// twice, changes nothing.
    void deliverData(const RouteTrailer& trailer, ForagerType type);

    /// Where swarms go. Without one (the default) no swarm is built: the
    /// foreign foragers stay in the store.
    void setSwarmSink(SwarmSink sink) { swarmSink_ = std::move(sink); }

    /// A swarm arrived from the one-hop neighbour `from`: relayed along its
    /// route, or, at its destination, unpacked into the type's array as 'own'
    /// foragers toward its sender (§13.7.3, §13.7.5). A search for the sender
    /// ends, and RouteReady releases its held packets.
    void onSwarm(const Swarm& swarm, NodeAddress from);

    /// The link to the one-hop neighbour `neighbor` failed: every forager
    /// whose first hop it is goes (fidelity row "A failed next link"; repo
    /// choice, #36). Returns how many were removed. The packet under the
    /// failed link is the adapter's to drop.
    std::size_t onLinkFailure(NodeAddress neighbor);

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
    /// Listing 13.30's trigger for the foragers of `type` held for `source`.
    void maybeSwarm(NodeAddress source, ForagerType type);
    void emitSwarm(const SwarmDecision& d);
    /// Ends a search for `dest` now that foragers for it exist.
    void routeFound(NodeAddress dest);
    std::size_t maxRouteNodes() const;

    NodeAddress self_;
    Config config_;
    swarm::core::IClock& clock_;
    swarm::core::IRng& rng_;
    swarm::core::ITimerScheduler& timers_;
    swarm::core::IEnergy& energy_;
    Sink sink_;
    RouteReady routeReady_;
    SwarmSink swarmSink_;
    swarm::core::IRouterObserver* observer_;

    ForagerStore store_;
    swarm::core::DedupHistory seen_;          // SCOUTLISTLENGTH: (source, id)
    std::map<NodeAddress, Search> searches_;  // RXMTLISTLENGTH
    std::uint32_t nextScoutId_ = 1;
};

}  // namespace beeadhoc::core

#endif  // BEEADHOC_CORE_ROUTER_H
