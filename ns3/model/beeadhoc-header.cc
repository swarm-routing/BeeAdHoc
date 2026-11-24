// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-header.h"

#include "ns3/swarm-wire-buffer.h"
#include "ns3/udp-header.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ns3 {
namespace beeadhoc {

namespace {

/// MaxScoutRouteNodes(), at compile time: it sizes the stack buffers below.
constexpr std::size_t kMaxRouteNodes =
    static_cast<std::size_t>(std::max(core::Config{}.maxHops, 1)) + 1;
/// The longest scout a decoder accepts.
constexpr std::size_t kMaxScoutSize = core::Scout::kFixedWireSize + 4 * kMaxRouteNodes;
/// The longest swarm a decoder accepts: SWARMSIZE carried foragers, every
/// route at the cap.
constexpr std::size_t kMaxSwarmSize =
    core::Swarm::kFixedWireSize + 4 * kMaxRouteNodes +
    core::Config{}.swarmSize * (core::Swarm::kCargoFixedWireSize + 4 * kMaxRouteNodes);
/// The most bytes a trailer can take: both routes at the cap.
constexpr std::size_t kMaxTrailerSize = core::RouteTrailer::kFixedWireSize +
                                        core::RouteTrailer::kPiggybackFixedWireSize +
                                        8 * kMaxRouteNodes;

}  // namespace

std::size_t MaxScoutRouteNodes() {
    return kMaxRouteNodes;
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
    BufferWireWriter w(start);
    core::encodeScout(m_scout, w);
}

uint32_t ScoutHeader::Deserialize(Buffer::Iterator start) {
    constexpr auto kFixed = static_cast<uint32_t>(core::Scout::kFixedWireSize);
    std::array<uint8_t, kMaxScoutSize> scratch;
    std::span<const uint8_t> bytes = ReadWireBytes(start, scratch, kFixed);
    auto n = static_cast<uint32_t>(bytes.size());
    if (n == kFixed) {
        // The route count is the fixed part's last two bytes, little endian.
        const uint32_t count = bytes[kFixed - 2] | (uint32_t{bytes[kFixed - 1]} << 8);
        const uint32_t more = std::min(start.GetRemainingSize(), 4 * count);
        if (n + more > scratch.size()) {
            // More nodes than the cap: decodeScout refuses it. Consume what
            // the count announces, as for any scout.
            start.Next(more);
            m_valid = false;
            m_scout = core::Scout{};
            return n + more;
        }
        n +=
            static_cast<uint32_t>(ReadWireBytes(start, std::span(scratch).subspan(n), more).size());
        bytes = std::span<const uint8_t>(scratch).first(n);
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
    // A longer payload is no scout decodeScout accepts: not copied.
    if (!p || p->GetSize() == 0 || p->GetSize() > kMaxScoutSize) return std::nullopt;
    std::array<uint8_t, kMaxScoutSize> scratch;
    const uint32_t n = p->CopyData(scratch.data(), p->GetSize());
    return core::decodeScout(std::span(scratch).first(n), MaxScoutRouteNodes());
}

Ptr<Packet> MakeSwarmPacket(const core::Swarm& swarm) {
    swarm::core::FixedWireWriter<kMaxSwarmSize> w;
    if (core::encodeSwarm(swarm, w) && w.ok())
        return Create<Packet>(w.bytes().data(), static_cast<uint32_t>(w.size()));
    // Longer than any swarm the router builds (SWARMSIZE foragers, routes at
    // the cap): the vector encoder, so the bytes stay the same.
    const std::vector<uint8_t> bytes = core::encodeSwarm(swarm);
    if (bytes.empty()) return Create<Packet>();  // data() may be null
    return Create<Packet>(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

std::optional<core::Swarm> DecodeSwarmPacket(Ptr<const Packet> p) {
    // A longer payload is no swarm decodeSwarm accepts: not copied.
    if (!p || p->GetSize() == 0 || p->GetSize() > kMaxSwarmSize) return std::nullopt;
    std::array<uint8_t, kMaxSwarmSize> scratch;
    const uint32_t n = p->CopyData(scratch.data(), p->GetSize());
    return core::decodeSwarm(std::span(scratch).first(n), MaxScoutRouteNodes(),
                             core::Config{}.swarmSize);
}

namespace {

constexpr uint8_t kUdp = 17;
/// A UDP header's 8 bytes (UdpHeader::GetSerializedSize), before the trailer.
constexpr uint32_t kUdpHeaderSize = 8;

/// The trailer at the start of `bytes`: walks the layout
/// (core/beeadhoc-trailer.h) to its end, each count bounded first, then
/// decodes exactly those bytes.
std::optional<RouteTrailerAt> TrailerIn(std::span<const uint8_t> bytes) {
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
    const auto t = core::decodeTrailer(bytes.first(size), cap);
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
    BufferWireWriter w(start);
    core::encodeTrailer(m_trailer, w);  // writes nothing when it cannot be said
}

uint32_t RouteTrailerHeader::Deserialize(Buffer::Iterator start) {
    std::array<uint8_t, kMaxTrailerSize> scratch;
    const auto bytes = ReadWireBytes(start, scratch, kMaxTrailerSize);
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
    // The trailer sits after the UDP header for UDP, at the front otherwise;
    // a UDP payload shorter than a UDP header has none.
    const uint32_t at = protocol == kUdp ? kUdpHeaderSize : 0;
    if (p->GetSize() < at) return std::nullopt;
    std::array<uint8_t, kUdpHeaderSize + kMaxTrailerSize> scratch;
    const uint32_t n = p->CopyData(
        scratch.data(), std::min(p->GetSize(), at + static_cast<uint32_t>(kMaxTrailerSize)));
    return TrailerIn(std::span<const uint8_t>(scratch).subspan(at, n - at));
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
