// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-header.h"

#include <algorithm>
#include <cstdint>

namespace ns3 {
namespace beeadhoc {

std::size_t MaxScoutRouteNodes() {
    return static_cast<std::size_t>(std::max(core::Config{}.maxHops, 1)) + 1;
}

NS_OBJECT_ENSURE_REGISTERED(ScoutHeader);

TypeId ScoutHeader::GetTypeId() {
    static TypeId tid = TypeId("ns3::beeadhoc::ScoutHeader")
                            .SetParent<Header>()
                            .SetGroupName("BeeAdHoc")
                            .AddConstructor<ScoutHeader>();
    return tid;
}

TypeId ScoutHeader::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t ScoutHeader::GetSerializedSize() const {
    return static_cast<uint32_t>(core::Scout::kFixedWireSize + 4 * m_scout.route.size());
}

void ScoutHeader::Serialize(Buffer::Iterator start) const {
    const std::vector<uint8_t> bytes = core::encodeScout(m_scout);
    start.Write(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

uint32_t ScoutHeader::Deserialize(Buffer::Iterator start) {
    constexpr auto kFixed = static_cast<uint32_t>(core::Scout::kFixedWireSize);
    uint32_t n = std::min(start.GetRemainingSize(), kFixed);
    std::vector<uint8_t> bytes(n);
    if (n > 0) start.Read(bytes.data(), n);  // data() may be null when empty
    if (n == kFixed) {
        // The route count is the fixed part's last two bytes, little endian.
        const uint32_t count = bytes[kFixed - 2] | (uint32_t{bytes[kFixed - 1]} << 8);
        const uint32_t more = std::min(start.GetRemainingSize(), 4 * count);
        bytes.resize(n + more);
        if (more > 0) start.Read(bytes.data() + n, more);
        n += more;
    }
    const auto scout = core::decodeScout(bytes, MaxScoutRouteNodes());
    m_valid = scout.has_value();
    m_scout = scout.value_or(core::Scout{});
    return n;
}

void ScoutHeader::Print(std::ostream& os) const {
    if (!m_valid) {
        os << "invalid scout";
        return;
    }
    os << (m_scout.mode == core::ScoutMode::Forward ? "SCOUT-FWD" : "SCOUT-BWD")
       << " src=" << m_scout.source << " dst=" << m_scout.destination << " id=" << m_scout.id
       << " ttl=" << static_cast<uint32_t>(m_scout.ttl) << " hops=" << m_scout.route.size();
}

Ptr<Packet> MakeScoutPacket(const core::Scout& scout) {
    Ptr<Packet> p = Create<Packet>();
    p->AddHeader(ScoutHeader(scout));
    return p;
}

std::optional<core::Scout> DecodeScoutPacket(Ptr<const Packet> p) {
    if (!p || p->GetSize() == 0) return std::nullopt;
    std::vector<uint8_t> bytes(p->GetSize());
    p->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return core::decodeScout(bytes, MaxScoutRouteNodes());
}

NS_OBJECT_ENSURE_REGISTERED(RouteTag);

TypeId RouteTag::GetTypeId() {
    static TypeId tid = TypeId("ns3::beeadhoc::RouteTag")
                            .SetParent<Tag>()
                            .SetGroupName("BeeAdHoc")
                            .AddConstructor<RouteTag>();
    return tid;
}

TypeId RouteTag::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t RouteTag::GetSerializedSize() const {
    return static_cast<uint32_t>(1 + 4 * m_route.size());
}

void RouteTag::Serialize(TagBuffer i) const {
    i.WriteU8(static_cast<uint8_t>(m_route.size()));
    for (core::NodeAddress a : m_route)
        i.WriteU32(static_cast<uint32_t>(a));
}

void RouteTag::Deserialize(TagBuffer i) {
    const std::size_t n = std::min<std::size_t>(i.ReadU8(), MaxScoutRouteNodes());
    m_route.clear();
    for (std::size_t k = 0; k < n; ++k)
        m_route.push_back(static_cast<core::NodeAddress>(i.ReadU32()));
}

void RouteTag::Print(std::ostream& os) const {
    os << "route=";
    for (std::size_t k = 0; k < m_route.size(); ++k)
        os << (k ? "," : "") << m_route[k];
}

}  // namespace beeadhoc
}  // namespace ns3
