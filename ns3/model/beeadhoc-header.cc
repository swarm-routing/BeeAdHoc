// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-header.h"

#include "ns3/udp-header.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

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

Ptr<Packet> MakeSwarmPacket(const core::Swarm& swarm) {
    const std::vector<uint8_t> bytes = core::encodeSwarm(swarm);
    if (bytes.empty()) return Create<Packet>();  // data() may be null
    return Create<Packet>(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

std::optional<core::Swarm> DecodeSwarmPacket(Ptr<const Packet> p) {
    if (!p || p->GetSize() == 0) return std::nullopt;
    std::vector<uint8_t> bytes(p->GetSize());
    p->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return core::decodeSwarm(bytes, MaxScoutRouteNodes(), core::Config{}.swarmSize);
}

namespace {

constexpr uint8_t kUdp = 17;

/// A copy of `p` from where the trailer sits: after the UDP header for UDP,
/// all of it otherwise. Null when a UDP payload is shorter than a UDP header.
Ptr<Packet> AtTrailer(Ptr<const Packet> p, uint8_t protocol) {
    Ptr<Packet> q = p->Copy();
    if (protocol == kUdp) {
        UdpHeader udp;
        if (q->GetSize() < udp.GetSerializedSize()) return nullptr;
        q->RemoveHeader(udp);
    }
    return q;
}

}  // namespace

namespace {

/// The most bytes a trailer can take: both routes at the cap.
uint32_t MaxTrailerSize() {
    const auto cap = static_cast<uint32_t>(MaxScoutRouteNodes());
    return static_cast<uint32_t>(core::RouteTrailer::kFixedWireSize +
                                 core::RouteTrailer::kPiggybackFixedWireSize) +
           8 * cap;
}

/// The trailer at the start of `bytes`: walks the layout
/// (core/beeadhoc-trailer.h) to its end, each count bounded first, then
/// decodes exactly those bytes.
std::optional<RouteTrailerAt> TrailerIn(const std::vector<uint8_t>& bytes) {
    const std::size_t cap = MaxScoutRouteNodes();
    auto count = [&bytes](std::size_t at) -> std::optional<std::size_t> {
        if (bytes.size() < at + 2) return std::nullopt;
        return std::size_t{bytes[at]} | (std::size_t{bytes[at + 1]} << 8);
    };
    std::size_t size = 1 + 4;  // version, info
    const auto n = count(size);
    if (!n || *n > cap) return std::nullopt;
    size += 2 + 4 * *n;
    if (bytes.size() < size + 1) return std::nullopt;
    const bool piggyback = bytes[size] == 1;
    size += 1;
    if (piggyback) {
        size += 4;
        const auto m = count(size);
        if (!m || *m > cap) return std::nullopt;
        size += 2 + 4 * *m;
    }
    if (bytes.size() < size) return std::nullopt;
    const auto t = core::decodeTrailer(std::span(bytes).first(size), cap);
    if (!t) return std::nullopt;
    return RouteTrailerAt{*t, static_cast<uint32_t>(size)};
}

}  // namespace

NS_OBJECT_ENSURE_REGISTERED(RouteTrailerHeader);

TypeId RouteTrailerHeader::GetTypeId() {
    static TypeId tid = TypeId("ns3::beeadhoc::RouteTrailerHeader")
                            .SetParent<Header>()
                            .SetGroupName("BeeAdHoc")
                            .AddConstructor<RouteTrailerHeader>();
    return tid;
}

TypeId RouteTrailerHeader::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t RouteTrailerHeader::GetSerializedSize() const {
    return m_valid ? static_cast<uint32_t>(m_trailer.wireSize()) : m_size;
}

void RouteTrailerHeader::Serialize(Buffer::Iterator start) const {
    const std::vector<uint8_t> bytes = core::encodeTrailer(m_trailer);
    if (!bytes.empty()) start.Write(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

uint32_t RouteTrailerHeader::Deserialize(Buffer::Iterator start) {
    std::vector<uint8_t> bytes(std::min(start.GetRemainingSize(), MaxTrailerSize()));
    if (!bytes.empty()) start.Read(bytes.data(), static_cast<uint32_t>(bytes.size()));
    const auto t = TrailerIn(bytes);
    m_valid = t.has_value();
    m_trailer = t ? t->trailer : core::RouteTrailer{};
    m_size = t ? t->size : static_cast<uint32_t>(bytes.size());
    return m_size;
}

void RouteTrailerHeader::Print(std::ostream& os) const {
    if (!m_valid) {
        os << "invalid route trailer";
        return;
    }
    os << "route=" << m_trailer.route.size() << " info=" << m_trailer.info
       << (m_trailer.piggyback ? " piggyback" : "");
}

std::optional<RouteTrailerAt> ReadRouteTrailer(Ptr<const Packet> p, uint8_t protocol) {
    if (!p) return std::nullopt;
    Ptr<Packet> q = AtTrailer(p, protocol);
    if (!q) return std::nullopt;
    std::vector<uint8_t> bytes(std::min(q->GetSize(), MaxTrailerSize()));
    if (!bytes.empty()) q->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return TrailerIn(bytes);
}

Ptr<Packet> AddRouteTrailer(Ptr<const Packet> payload, uint8_t protocol,
                            const core::RouteTrailer& t) {
    Ptr<Packet> q = payload->Copy();
    UdpHeader udp;
    const bool udpFirst = protocol == kUdp && q->GetSize() >= udp.GetSerializedSize();
    if (udpFirst) q->RemoveHeader(udp);
    q->AddHeader(RouteTrailerHeader(t));
    if (udpFirst) q->AddHeader(udp);
    return q;
}

Ptr<Packet> StripRouteTrailer(Ptr<const Packet> p, uint8_t protocol, uint32_t size) {
    Ptr<Packet> q = p->Copy();
    UdpHeader udp;
    const bool udpFirst = protocol == kUdp && q->GetSize() >= udp.GetSerializedSize();
    if (udpFirst) q->RemoveHeader(udp);
    q->RemoveAtStart(size);
    if (udpFirst) q->AddHeader(udp);
    return q;
}

}  // namespace beeadhoc
}  // namespace ns3
