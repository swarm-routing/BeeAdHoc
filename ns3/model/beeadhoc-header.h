// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// BeeAdHoc's packets in ns-3. Written fresh for this module.
//
// ScoutHeader: the scout of core/beeadhoc-scout.h as an ns3::Header. The bytes
// are core/'s wire format (version byte, little endian, a fixed part and four
// bytes per route address; org rule 3), written and read by core/'s codec on
// swarm-core's bounds-checked helpers, so ns-3 and the standalone core share
// one encoder and one decoder. A scout is untrusted input: the adapter reads
// it with DecodeScoutPacket, which takes the whole payload and refuses a wrong
// version, an unknown mode, info bits above 24, a route longer than MaxHops
// + 1 nodes, a short buffer or trailing bytes. ScoutHeader::Deserialize
// cannot fail in ns-3's API, so it records whether the bytes decoded instead.
//
// RouteTag: the source route a data packet follows (core::DataDecision's
// route: the hops after the source, the destination last). [PG460] §12.1.2
// puts it in the packet, and core/ has the wire format for it (RouteTrailer,
// beeadhoc-trailer.h: the source route, the info field and the piggybacked
// forager; #36 slice 2). This adapter does not use it yet (#47): until it
// does, the route rides as a packet tag, simulator metadata with no bytes on
// the air. Control overhead in bytes is therefore undercounted against the
// kernel module.
#ifndef BEEADHOC_HEADER_H
#define BEEADHOC_HEADER_H

#include "beeadhoc-scout.h"

#include "ns3/header.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/tag.h"

#include <optional>
#include <vector>

namespace ns3 {
namespace beeadhoc {

/// BeeAdHoc's simulator-free core (core/), from inside ns3::beeadhoc.
namespace core = ::beeadhoc::core;

/// The most route nodes a scout may carry: the source and MaxHops links.
std::size_t MaxScoutRouteNodes();

class ScoutHeader : public Header {
public:
    ScoutHeader() = default;
    explicit ScoutHeader(const core::Scout& scout) : m_scout(scout), m_valid(true) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    /// Reads the fixed part and the route its count announces, or what is
    /// left if fewer; IsValid() says whether they decoded.
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    const core::Scout& Get() const { return m_scout; }
    bool IsValid() const { return m_valid; }

private:
    core::Scout m_scout;
    bool m_valid = false;
};

/// A packet holding exactly one scout.
Ptr<Packet> MakeScoutPacket(const core::Scout& scout);

/// The scout a received control payload carries, or nullopt when the payload
/// is not exactly one valid scout (core::decodeScout's rules).
std::optional<core::Scout> DecodeScoutPacket(Ptr<const Packet> p);

/// The source route of a data packet (see the file comment: interim, no bytes
/// on the air until core/'s data-path trailer exists).
class RouteTag : public Tag {
public:
    RouteTag() = default;
    explicit RouteTag(std::vector<core::NodeAddress> route) : m_route(std::move(route)) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    /// Reads at most MaxScoutRouteNodes() addresses.
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    const std::vector<core::NodeAddress>& Route() const { return m_route; }

private:
    std::vector<core::NodeAddress> m_route;
};

}  // namespace beeadhoc
}  // namespace ns3

#endif  // BEEADHOC_HEADER_H
