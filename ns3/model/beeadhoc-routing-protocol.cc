// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-routing-protocol.h"

#include "beeadhoc-header.h"
#include "beeadhoc-router.h"

#include "ns3/abort.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-header.h"

#include <algorithm>

namespace ns3 {
namespace beeadhoc {

NS_LOG_COMPONENT_DEFINE("BeeAdHocRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

namespace {

constexpr uint8_t kUdpProtocol = 17;
using swarm::core::RouteAction;

/// True when `dst` is the subnet-directed broadcast address of one of the
/// node's interfaces (Ipv4Address::IsBroadcast knows 255.255.255.255 only).
bool SubnetBroadcast(Ptr<Ipv4> ipv4, Ipv4Address dst) {
    for (uint32_t i = 0; i < ipv4->GetNInterfaces(); ++i) {
        for (uint32_t j = 0; j < ipv4->GetNAddresses(i); ++j) {
            if (ipv4->GetAddress(i, j).GetBroadcast() == dst) return true;
        }
    }
    return false;
}

}  // namespace

TypeId RoutingProtocol::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::beeadhoc::RoutingProtocol")
            .SetParent<SwarmRoutingBase>()
            .SetGroupName("BeeAdHoc")
            .AddConstructor<RoutingProtocol>()
            .AddTraceSource("ScoutTx", "A scout this node sent (mode byte, broadcast, info).",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_scoutTx),
                            "ns3::beeadhoc::RoutingProtocol::ScoutTxCallback")
            .AddTraceSource("SwarmTx", "A swarm this node sent or relayed (foragers carried).",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_swarmTx),
                            "ns3::beeadhoc::RoutingProtocol::SwarmTxCallback");
    // The scout kinds' names for the observer's ControlTx/ControlRx readers (#54).
    SwarmRoutingBase::SetKindNames(tid, core::kKindNames);
    return tid;
}

RoutingProtocol::RoutingProtocol() = default;

RoutingProtocol::~RoutingProtocol() = default;

double RoutingProtocol::EnergyPort::residualEnergyFraction() const {
    return m_owner.Energy().residualEnergyFraction();
}

void RoutingProtocol::DoInitialize() {
    // The interfaces are up by now (addresses are assigned before the
    // simulation starts), so the node's address is known. The protocol
    // constants are core::Config's defaults, each sourced in docs/fidelity.md.
    const swarm::core::NodeAddress self = ToNodeAddress(MainAddress());
    m_router = std::make_unique<core::Router>(
        self, core::Config{}, m_clock, Rng(), Timers(), m_energy,
        [this](const core::ScoutDecision& d) {
            if (d.message) {
                m_scoutTx(static_cast<uint8_t>(d.message->mode),
                          d.action == swarm::core::RouteAction::Broadcast, d.message->info);
            }
            const bool done = Execute(d, MakeScoutPacket);
            NS_ASSERT_MSG(done, "the BeeAdHoc core sends scouts or DiscardPending only");
            (void)done;
        },
        [this](swarm::core::NodeAddress dest) { Resume(ToIpv4Address(dest)); }, &Observer());
    m_router->setSwarmSink([this](const core::SwarmDecision& d) {
        if (d.message) m_swarmTx(static_cast<uint32_t>(d.message->foragers.size()));
        Execute(d, MakeSwarmPacket);
    });
    SwarmRoutingBase::DoInitialize();
}

void RoutingProtocol::DoDispose() {
    // The base cancels the core's pending timers first; then the router goes.
    SwarmRoutingBase::DoDispose();
    m_router.reset();
}

bool RoutingProtocol::Searching(Ipv4Address dst) const {
    return m_router && m_router->searching(ToNodeAddress(dst));
}

uint32_t RoutingProtocol::Foragers(Ipv4Address dst) const {
    if (!m_router) return 0;
    uint32_t n = 0;
    for (std::size_t t = 0; t < core::kForagerTypes; ++t) {
        n += static_cast<uint32_t>(
            m_router->foragers().size(ToNodeAddress(dst), static_cast<core::ForagerType>(t)));
    }
    return n;
}

Ipv4Address RoutingProtocol::SelectNextHop(Ipv4Address /*dst*/, Ipv4Address /*src*/,
                                           Ipv4Address /*prev*/, Ptr<const Packet> /*p*/) {
    // No next hop in RouteOutput: the transport header is added after it, and
    // the trailer rides after the transport header. The base sends the packet
    // through the loopback device to RouteInput, which originates it.
    return Ipv4Address();
}

void RoutingProtocol::StartDiscovery(Ipv4Address dst) {
    // The core already decided: routeOutput started a scout and the packet
    // waits for it (Queue). Whatever the core is not searching for is dropped.
    if (!Searching(dst)) {
        DiscardDeferred(dst);
    }
}

void RoutingProtocol::SendData(Ptr<const Packet> payload, const Ipv4Header& header,
                               const core::RouteTrailer& trailer, Ipv4Address next,
                               const UnicastForwardCallback& ucb) {
    Ptr<Packet> q = AddRouteTrailer(payload, header.GetProtocol(), trailer);
    Ipv4Header out = header;
    out.SetPayloadSize(q->GetSize());
    ForwardData(q, out, next, ucb);
}

void RoutingProtocol::DropData(Ptr<const Packet> payload, const Ipv4Header& header,
                               const ErrorCallback& ecb) {
    if (ecb.IsNull()) return;
    Ipv4Header out = header;
    out.SetPayloadSize(payload->GetSize());
    ecb(payload, out, Socket::ERROR_NOROUTETOHOST);
}

void RoutingProtocol::Resume(Ipv4Address dest) {
    // The packets held for `dest`, oldest first, offered to routeOutput again:
    // sent on a forager, held again (keeping their first enqueue time) or
    // dropped.
    for (DeferredPacket& e : GetDeferredQueue()->Release(dest)) {
        if (!m_router) return;
        const core::DataDecision d =
            m_router->routeOutput(ToNodeAddress(dest), core::ForagerType::Energy);
        if (d.action == RouteAction::Unicast && !d.route.empty()) {
            SendData(e.packet, e.header, d.trailer, ToIpv4Address(d.route.front()), e.ucb);
        } else if (d.action == RouteAction::Queue) {
            GetDeferredQueue()->Enqueue(e);
        } else {
            DropData(e.packet, e.header, e.ecb);
        }
    }
}

bool RoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header& header,
                                 Ptr<const NetDevice> idev, const UnicastForwardCallback& ucb,
                                 const MulticastForwardCallback& mcb,
                                 const LocalDeliverCallback& lcb, const ErrorCallback& ecb) {
    const Ipv4Address dst = header.GetDestination();
    Ptr<Ipv4> ipv4 = idev ? idev->GetNode()->GetObject<Ipv4>() : nullptr;
    bool control = false;
    if (header.GetProtocol() == kUdpProtocol) {
        // A control datagram leaves a socket bound to kBeeAdHocPort, so both
        // its ports are; a data datagram keeps its own UDP header first.
        // A datagram shorter than a UDP header is no control (and not peeked).
        UdpHeader udp;
        control = p->GetSize() >= udp.GetSerializedSize() && p->PeekHeader(udp) &&
                  udp.GetSourcePort() == kBeeAdHocPort && udp.GetDestinationPort() == kBeeAdHocPort;
    }
    // Control packets, broadcast (limited or subnet-directed) and multicast
    // are the base's: only unicast data carries a trailer.
    if (!m_router || !ipv4 || control || dst.IsBroadcast() || dst.IsMulticast() ||
        SubnetBroadcast(ipv4, dst)) {
        return SwarmRoutingBase::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
    }
    const int32_t iif = ipv4->GetInterfaceForDevice(idev);
    const bool local = ipv4->IsDestinationAddress(dst, iif);
    if (DynamicCast<const LoopbackNetDevice>(idev)) {
        if (local) {
            return SwarmRoutingBase::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
        }
        // This node's own data, sent here by RouteOutput (OUTPUT chain,
        // §12.1.3). It leaves through ucb -> IpForward, which decrements the
        // TTL for a hop no radio carried: add it back (as the base does).
        Ipv4Header own = header;
        own.SetTtl(header.GetTtl() + 1);
        const core::DataDecision d =
            m_router->routeOutput(ToNodeAddress(dst), core::ForagerType::Energy);
        if (d.action == RouteAction::Unicast && !d.route.empty()) {
            SendData(p, own, d.trailer, ToIpv4Address(d.route.front()), ucb);
        } else if (d.action == RouteAction::Queue) {
            Defer(p, own, ucb, ecb);  // held for the scout; StartDiscovery keeps it
        } else {
            DropData(p, own, ecb);  // RXMTLISTLENGTH searches already out
        }
        return true;
    }
    // From a neighbour: the payload carries the trailer.
    const auto at = ReadRouteTrailer(p, header.GetProtocol());
    if (!at) {
        NS_LOG_DEBUG("no valid BeeAdHoc route trailer on a data packet for " << dst);
        DropData(p, header, ecb);
        return true;
    }
    core::RouteTrailer trailer = at->trailer;
    Ptr<Packet> payload = StripRouteTrailer(p, header.GetProtocol(), at->size);
    if (local) {
        // INPUT chain: the core keeps its foragers, then the packet goes up.
        m_router->deliverData(trailer, core::ForagerType::Energy);
        Ipv4Header up = header;
        up.SetPayloadSize(payload->GetSize());
        if (lcb.IsNull()) return false;
        lcb(payload, up, static_cast<uint32_t>(iif));
        return true;
    }
    // FORWARD chain: the core writes this node's level and names the next hop.
    const swarm::core::NodeAddress next = m_router->forwardData(trailer);
    if (next == swarm::core::kInvalidAddress) {
        DropData(payload, header, ecb);
        return true;
    }
    SendData(payload, header, trailer, ToIpv4Address(next), ucb);
    return true;
}

void RoutingProtocol::OnLinkFailure(Ipv4Address neighbor) {
    if (m_router) m_router->onLinkFailure(ToNodeAddress(neighbor));
}

void RoutingProtocol::RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address /*receiver*/) {
    if (!m_router) return;
    // The byte after the version names the kind: a swarm, or a scout's mode.
    uint8_t head[2] = {0, 0};
    if (p->GetSize() >= 2) p->CopyData(head, 2);
    if (head[1] == core::Swarm::kKind) {
        if (const auto swarm = DecodeSwarmPacket(p)) {
            m_router->onSwarm(*swarm, ToNodeAddress(from));
        } else {
            NS_LOG_DEBUG("dropped a malformed swarm from " << from);
        }
        return;
    }
    const auto scout = DecodeScoutPacket(p);
    if (!scout) {
        NS_LOG_DEBUG("dropped a malformed scout from " << from);
        return;
    }
    m_router->onScout(*scout, ToNodeAddress(from));
}

}  // namespace beeadhoc
}  // namespace ns3
