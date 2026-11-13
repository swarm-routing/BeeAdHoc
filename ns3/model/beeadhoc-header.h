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
// The swarm (core/beeadhoc-swarm.h) is the other control payload: MakeSwarmPacket
// and DecodeSwarmPacket, fail-closed by core::decodeSwarm's rules. The kind
// byte after the version tells a swarm (3) from a scout (1, 2) on one port.
//
// The route trailer (core/beeadhoc-trailer.h, [PG460] §12.1.2, §13.7.1) rides
// on every data packet: in a UDP datagram right after the UDP header, so a
// reader of the ports (the harness counts routing control as UDP not
// addressed to the data port) still sees a data packet; in front of any other
// IP payload. AddRouteTrailer, ReadRouteTrailer and StripRouteTrailer put it
// on, read it without trusting the bytes, and take it off.
#ifndef BEEADHOC_HEADER_H
#define BEEADHOC_HEADER_H

#include "beeadhoc-scout.h"
#include "beeadhoc-swarm.h"
#include "beeadhoc-trailer.h"

#include "ns3/header.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

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

/// A packet holding exactly one swarm.
Ptr<Packet> MakeSwarmPacket(const core::Swarm& swarm);

/// The swarm a received control payload carries, or nullopt when the payload
/// is not exactly one valid swarm (core::decodeSwarm's rules, at most
/// SWARMSIZE foragers).
std::optional<core::Swarm> DecodeSwarmPacket(Ptr<const Packet> p);

/// The core's RouteTrailer as an ns3::Header (it is put in place with
/// AddRouteTrailer). Deserialize cannot fail in ns-3's API, so it records
/// whether the bytes decoded (IsValid).
class RouteTrailerHeader : public Header {
public:
    RouteTrailerHeader() = default;
    explicit RouteTrailerHeader(const core::RouteTrailer& t) : m_trailer(t), m_valid(true) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    const core::RouteTrailer& Get() const { return m_trailer; }
    bool IsValid() const { return m_valid; }

private:
    core::RouteTrailer m_trailer;
    bool m_valid = false;
    uint32_t m_size = 0;  ///< bytes Deserialize consumed
};

/// The route trailer of an IPv4 payload of `protocol` (after the UDP header
/// for UDP, 17; at the front otherwise) and its size in bytes, or nullopt
/// when there is not one valid trailer (core::decodeTrailer's rules, routes of
/// at most MaxScoutRouteNodes() nodes). The bytes after it are not read.
struct RouteTrailerAt {
    core::RouteTrailer trailer;
    uint32_t size = 0;
};
std::optional<RouteTrailerAt> ReadRouteTrailer(Ptr<const Packet> p, uint8_t protocol);

/// A copy of `payload` (an IPv4 payload of `protocol`) carrying `t` where
/// ReadRouteTrailer looks for it; its tags come along.
Ptr<Packet> AddRouteTrailer(Ptr<const Packet> payload, uint8_t protocol,
                            const core::RouteTrailer& t);

/// A copy of `p` without the `size` bytes of the trailer ReadRouteTrailer read.
Ptr<Packet> StripRouteTrailer(Ptr<const Packet> p, uint8_t protocol, uint32_t size);

}  // namespace beeadhoc
}  // namespace ns3

#endif  // BEEADHOC_HEADER_H
