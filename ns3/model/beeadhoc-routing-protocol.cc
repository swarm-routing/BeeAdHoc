// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-routing-protocol.h"

#include "beeadhoc-header.h"
#include "beeadhoc-router.h"

#include "ns3/abort.h"
#include "ns3/log.h"

#include <algorithm>

namespace ns3 {
namespace beeadhoc {

NS_LOG_COMPONENT_DEFINE("BeeAdHocRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

TypeId RoutingProtocol::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::beeadhoc::RoutingProtocol")
            .SetParent<SwarmRoutingBase>()
            .SetGroupName("BeeAdHoc")
            .AddConstructor<RoutingProtocol>()
            .AddTraceSource("ScoutTx", "A scout this node sent (mode byte, broadcast, info).",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_scoutTx),
                            "ns3::beeadhoc::RoutingProtocol::ScoutTxCallback");
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
        [this](swarm::core::NodeAddress dest) { ReleaseDeferred(ToIpv4Address(dest)); },
        &Observer());
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

Ipv4Address RoutingProtocol::SelectNextHop(Ipv4Address dst, Ipv4Address /*src*/, Ipv4Address prev,
                                           Ptr<const Packet> p) {
    // A broadcast or multicast address is no core address (255.255.255.255
    // reads as kInvalidAddress): such packets are not routed.
    if (!m_router || !p || dst.IsBroadcast() || dst.IsMulticast()) {
        return Ipv4Address();
    }
    RouteTag tag;
    if (prev == Ipv4Address()) {
        // This node's own data (RouteOutput, or a held packet released): the
        // core decides, and its route rides with the packet.
        core::DataDecision d = m_router->routeOutput(ToNodeAddress(dst), core::ForagerType::Energy);
        if (d.action != swarm::core::RouteAction::Unicast || d.route.empty()) {
            return Ipv4Address();  // Queue (held for the scout) or Drop
        }
        const Ipv4Address next = ToIpv4Address(d.route.front());
        if (!p->PeekPacketTag(tag)) {
            p->AddPacketTag(RouteTag(std::move(d.route)));  // tags are metadata: const is fine
        }
        return next;
    }
    // In transit: the hop after this node on the source's route.
    if (!p->PeekPacketTag(tag)) {
        return Ipv4Address();
    }
    const auto& route = tag.Route();
    const auto at = std::find_if(route.begin(), route.end(), [this](swarm::core::NodeAddress a) {
        return IsMyAddress(ToIpv4Address(a));
    });
    if (at == route.end() || at + 1 == route.end()) {
        return Ipv4Address();
    }
    return ToIpv4Address(*(at + 1));
}

void RoutingProtocol::StartDiscovery(Ipv4Address dst) {
    // The core already decided: routeOutput started a scout and the packet
    // waits for it (Queue), or it had no room for another search, or the
    // packet is in transit without a usable route (Drop). Whatever the core
    // is not searching for is dropped now.
    if (!Searching(dst)) {
        DiscardDeferred(dst);
    }
}

void RoutingProtocol::RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address /*receiver*/) {
    if (!m_router) return;
    const auto scout = DecodeScoutPacket(p);
    if (!scout) {
        NS_LOG_DEBUG("dropped a malformed scout from " << from);
        return;
    }
    m_router->onScout(*scout, ToNodeAddress(from));
}

}  // namespace beeadhoc
}  // namespace ns3
