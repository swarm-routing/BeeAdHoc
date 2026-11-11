// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// BeeAdHoc's ns-3 adapter: an ns3::SwarmRoutingBase that hosts one
// core::Router (core/beeadhoc-router.h) per node and carries out its
// decisions (golden rule 10). It only converts: scouts between ScoutHeader
// bytes and core::Scout, IPv4 addresses to core addresses, and the router's
// decisions into SwarmRoutingBase's sends. Time, randomness, timers and the
// battery are swarm-core's ports over ns-3 (Simulator::Now, a stream
// AssignStreams pins, Simulator::Schedule, the node's EnergyMetricHook), so a
// fixed seed gives byte-identical runs (org rule 5). Written fresh on
// swarm-core (golden rule 12); the control port is the one AntHocNet's
// `beeadhoc` arm used.
//
// What the core decides, the adapter does:
//  - data this node originates: core::Router::routeOutput. A Unicast carries
//    the forager's whole route (a RouteTag on the packet) and goes to its
//    first hop. A Queue holds the packet in the base's deferred queue while
//    the core's scout is out; the core's RouteReady releases it, and its
//    DiscardPending drops it. A Drop (RXMTLISTLENGTH searches already out)
//    drops it at once: StartDiscovery discards whatever the core is not
//    searching for.
//  - data in transit: the packet follows the route its source wrote (strict
//    source routing, [PG460] §12.1.2); a packet without a route, or whose
//    route does not name this node, is dropped. The FORWARD-chain battery
//    writes, the destination's foreign forager and piggybacking ([PG460]
//    §12.1.3, §13.7) arrive with the core's data-path slice (#36, slice 2),
//    link failure with slice 4; until then the adapter neither writes levels
//    into data packets nor reacts to a link failure.
//  - scouts: the core's Broadcast and Unicast decisions, as one-hop UDP sends.
//  - the battery: the node's EnergyMetricHook through the base's Energy()
//    port; with none, the core reads "no battery data", level 7 (§13.1.2).
// Every data packet is the energy type: the kernel module measures only
// energy (§13.2.2), and the base hands the adapter no IP header to read a TOS
// from (accepted by the maintainer on 2026-11-11,
// https://github.com/swarm-routing/BeeAdHoc/issues/47#issuecomment-6783813490).
#ifndef BEEADHOC_ROUTING_PROTOCOL_H
#define BEEADHOC_ROUTING_PROTOCOL_H

#include "ns3/swarm-ns3-ports.h"
#include "ns3/swarm-routing-base.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace beeadhoc::core {
class Router;
}

namespace ns3 {
namespace beeadhoc {

/// BeeAdHoc's simulator-free core (core/), from inside ns3::beeadhoc.
namespace core = ::beeadhoc::core;

class RoutingProtocol : public SwarmRoutingBase {
public:
    /// UDP port of scouts (AntHocNet's `beeadhoc` arm: 5544).
    static constexpr uint16_t kBeeAdHocPort = 5544;

    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    /// True while this node's scout for `dst` is out.
    bool Searching(Ipv4Address dst) const;
    /// Foragers this node holds for `dst`, over the three types.
    uint32_t Foragers(Ipv4Address dst) const;

    /// A scout this node sent: (core::ScoutMode as its wire byte, broadcast,
    /// the 24-bit info field it carries).
    typedef void (*ScoutTxCallback)(uint8_t mode, bool broadcast, uint32_t info);

protected:
    void DoInitialize() override;
    void DoDispose() override;

    uint16_t ControlPort() const override { return kBeeAdHocPort; }
    Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address src, Ipv4Address prev,
                              Ptr<const Packet> p) override;
    void StartDiscovery(Ipv4Address dst) override;
    void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) override;

private:
    /// The core's energy port: the base's Energy() read on every call, so an
    /// EnergyMetricHook installed after the protocol is still seen.
    class EnergyPort : public swarm::core::IEnergy {
    public:
        explicit EnergyPort(const RoutingProtocol& owner) : m_owner(owner) {}
        double residualEnergyFraction() const override;

    private:
        const RoutingProtocol& m_owner;
    };

    /// The core's clock port. Clock() is const and the router takes a
    /// mutable reference; SwarmClock is stateless, so this one reads the same.
    SwarmClock m_clock;
    EnergyPort m_energy{*this};
    std::unique_ptr<core::Router> m_router;
    TracedCallback<uint8_t, bool, uint32_t> m_scoutTx;
};

}  // namespace beeadhoc
}  // namespace ns3

#endif  // BEEADHOC_ROUTING_PROTOCOL_H
