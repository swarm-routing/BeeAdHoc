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
//  - data: RouteInput takes every unicast data packet (RouteOutput sends a
//    node's own through the loopback first, so its transport header is on
//    it). This node's own: core::Router::routeOutput. A Unicast carries the
//    core's RouteTrailer (the route, the info field, at most one piggybacked
//    foreign forager) right after the UDP header and goes to the route's
//    first hop; a Queue holds the packet in the base's deferred queue while
//    the core's scout is out, the core's RouteReady offers it to routeOutput
//    again, and its DiscardPending drops it; a Drop drops it at once. In
//    transit: core::Router::forwardData writes this node's level into the
//    trailer (FORWARD chain, Listing 13.7) and names the next hop, or the
//    packet is dropped. At the destination: core::Router::deliverData (INPUT
//    chain: the foreign forager, a piggybacked forager home, Listing 13.30's
//    swarm trigger), then the trailer comes off and the packet goes up.
//  - scouts and swarms: the core's Broadcast and Unicast decisions, as one-hop
//    UDP sends; a received control payload is a swarm when its kind byte says
//    so (beeadhoc-header.h), else a scout.
//  - link failure: core::Router::onLinkFailure removes the foragers over the
//    failed link (fidelity row "A failed next link"); the packet under it is
//    lost, as the row says.
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
struct RouteTrailer;
}  // namespace beeadhoc::core

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
    /// A swarm this node sent or relayed: (foragers carried).
    typedef void (*SwarmTxCallback)(uint32_t foragers);

    // Ipv4RoutingProtocol: every unicast data packet passes through here.
    bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override;

protected:
    void DoInitialize() override;
    void DoDispose() override;

    uint16_t ControlPort() const override { return kBeeAdHocPort; }
    Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address src, Ipv4Address prev,
                              Ptr<const Packet> p) override;
    void StartDiscovery(Ipv4Address dst) override;
    void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) override;
    void OnLinkFailure(Ipv4Address neighbor) override;

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

    /// The core's RouteReady: offers the packets held for `dest` to
    /// routeOutput again.
    void Resume(Ipv4Address dest);
    /// Sends `payload` (an IPv4 payload) with `trailer` to `next`.
    void SendData(Ptr<const Packet> payload, const Ipv4Header& header,
                  const core::RouteTrailer& trailer, Ipv4Address next,
                  const UnicastForwardCallback& ucb);
    static void DropData(Ptr<const Packet> payload, const Ipv4Header& header,
                         const ErrorCallback& ecb);

    /// The core's clock port. Clock() is const and the router takes a
    /// mutable reference; SwarmClock is stateless, so this one reads the same.
    SwarmClock m_clock;
    EnergyPort m_energy{*this};
    std::unique_ptr<core::Router> m_router;
    TracedCallback<uint8_t, bool, uint32_t> m_scoutTx;
    TracedCallback<uint32_t> m_swarmTx;
};

}  // namespace beeadhoc
}  // namespace ns3

#endif  // BEEADHOC_ROUTING_PROTOCOL_H
