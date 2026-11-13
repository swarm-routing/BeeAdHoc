// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// `test.py -s beeadhoc`. Needs ns-3.42+ (scoped TestCase::Duration).
//
// - core/'s tests (core/test/), one TestCase each, so the debug and sanitized
//   legs run them inside ns-3 as core.yml runs them alone;
// - the ns-3 scout header: roundtrip through a Packet with the byte layout
//   pinned, and a seeded fuzz of the decoder a received control payload goes
//   through (org rule 3); the route trailer after a UDP header and the swarm
//   payload, roundtrip, and a seeded fuzz of their readers;
// - the adapter on a static four-node 802.11b chain: a scout flooded and
//   returned, the packet that started it held and then sent, every packet
//   delivered over the source route 0 -> 1 -> 2 -> 3, and foreign foragers
//   left at the destination (INPUT chain); one-way traffic past SWARMSIZE
//   foreign foragers sends a swarm home;
// - the battery port: a relay draining an ns-3 energy source shows up, through
//   the EnergyMetricHook, as its 3-bit level in the returned scout's info;
// - determinism: the same seed gives the same scout and delivery trace,
//   another seed another (org rule 5).
// Through the include path (core/, resolved past the contrib link), not
// "../../core/...": gcov would collapse that beside the link, not its target.
#include "test/beeadhoc-core-tests.h"
#include "../model/beeadhoc-header.h"

#include "ns3/basic-energy-source.h"
#include "ns3/beeadhoc-helper.h"
#include "ns3/beeadhoc-routing-protocol.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/simple-device-energy-model.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/swarm-core-module.h"
#include "ns3/test.h"
#include "ns3/udp-header.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using ::beeadhoc::core::Scout;
using ::beeadhoc::core::ScoutMode;

namespace {

/// One core/ test function, its failures reported through ns-3.
class BeeAdHocCoreTestCase : public TestCase {
public:
    explicit BeeAdHocCoreTestCase(const ::beeadhoc::core::test::NamedTest& t)
        : TestCase(std::string("core: ") + t.name), m_test(t) {}

private:
    void DoRun() override {
        ::beeadhoc::core::test::Checker c;
        m_test.run(c);
        // Each failure prints as "got <file:line: check>, expected ''".
        for (const auto& f : c.failures()) {
            NS_TEST_EXPECT_MSG_EQ(f, std::string(), "core check failed");
        }
    }

    ::beeadhoc::core::test::NamedTest m_test;
};

std::vector<uint8_t> Bytes(Ptr<const Packet> p) {
    std::vector<uint8_t> b(p->GetSize());
    if (!b.empty()) p->CopyData(b.data(), static_cast<uint32_t>(b.size()));
    return b;
}

Ptr<Packet> FromBytes(const std::vector<uint8_t>& b) {
    if (b.empty()) return Create<Packet>();  // data() may be null
    return Create<Packet>(b.data(), static_cast<uint32_t>(b.size()));
}

Scout SampleScout() {
    Scout s;
    s.mode = ScoutMode::Backward;
    s.source = 0x0A010001;
    s.destination = 0x0A010004;
    s.id = 7;
    s.ttl = 2;
    s.info = 0x1FF;
    s.route = {0x0A010001, 0x0A010002, 0x0A010003, 0x0A010004};
    return s;
}

class HeaderRoundtripTestCase : public TestCase {
public:
    HeaderRoundtripTestCase() : TestCase("header: ScoutHeader roundtrip through a Packet") {}

private:
    void DoRun() override {
        Scout forward;
        forward.source = -2;
        forward.destination = 0x7FFFFFFF;
        forward.id = 0xFFFFFFFFu;
        forward.ttl = 255;
        forward.info = 0xFFFFFF;
        forward.route = {-2};
        const std::vector<Scout> scouts{SampleScout(), forward, Scout{}};
        for (const Scout& s : scouts) {
            Ptr<Packet> p = ns3::beeadhoc::MakeScoutPacket(s);
            const uint32_t size = static_cast<uint32_t>(Scout::kFixedWireSize + 4 * s.route.size());
            NS_TEST_ASSERT_MSG_EQ(p->GetSize(), size, "fixed part + 4 bytes per route node");
            NS_TEST_EXPECT_MSG_EQ(Bytes(p)[0], Scout::kWireVersion, "leading version byte");
            const auto decoded = ns3::beeadhoc::DecodeScoutPacket(p);
            NS_TEST_ASSERT_MSG_EQ(decoded.has_value(), true, "decodes");
            NS_TEST_EXPECT_MSG_EQ((*decoded == s), true, "same scout back");
            ns3::beeadhoc::ScoutHeader h;
            NS_TEST_EXPECT_MSG_EQ(p->Copy()->RemoveHeader(h), size,
                                  "RemoveHeader consumes the scout");
            NS_TEST_EXPECT_MSG_EQ(h.IsValid(), true, "header valid");
            NS_TEST_EXPECT_MSG_EQ((h.Get() == s), true, "same scout through the header");
        }
        // The core's byte layout, little endian (core/beeadhoc-scout.h).
        Scout small = SampleScout();
        small.route = {0x0A010001, 0x0A010004};
        const auto b = Bytes(ns3::beeadhoc::MakeScoutPacket(small));
        const std::vector<uint8_t> want{1, 2,    0x01, 0x00, 0x01, 0x0A, 0x04, 0x00, 0x01, 0x0A,
                                        7, 0,    0,    0,    2,    0xFF, 0x01, 0,    0,    2,
                                        0, 0x01, 0x00, 0x01, 0x0A, 0x04, 0x00, 0x01, 0x0A};
        NS_TEST_EXPECT_MSG_EQ((b == want), true, "wire layout");
        // A wrong version through the header: decoded as invalid, not trusted.
        std::vector<uint8_t> bad = want;
        bad[0] = Scout::kWireVersion + 1;
        ns3::beeadhoc::ScoutHeader h;
        FromBytes(bad)->RemoveHeader(h);
        NS_TEST_EXPECT_MSG_EQ(h.IsValid(), false, "a wrong version is invalid");
        // A route count past the bytes left: reads what is there, invalid.
        std::vector<uint8_t> cut(want.begin(), want.begin() + 25);
        ns3::beeadhoc::ScoutHeader c;
        NS_TEST_EXPECT_MSG_EQ(FromBytes(cut)->RemoveHeader(c), 25u, "reads what is left");
        NS_TEST_EXPECT_MSG_EQ(c.IsValid(), false, "a truncated route is invalid");
        std::ostringstream os;
        h.Print(os);
        c.Print(os);
        ns3::beeadhoc::ScoutHeader(small).Print(os);
        NS_TEST_EXPECT_MSG_NE(os.str().find("SCOUT-BWD"), std::string::npos, "prints");

        // The route trailer through a UDP datagram: right after the UDP
        // header, so the ports read as before; stripped, the datagram as it was.
        ::beeadhoc::core::RouteTrailer t;
        t.route = {1, 2, 3, 4};
        t.info = 0x3F;
        t.piggyback = ::beeadhoc::core::RouteTrailer::Piggyback{{1, 2, 4}, 0x1FF};
        Ptr<Packet> datagram = Create<Packet>(64);
        UdpHeader udp;
        udp.SetSourcePort(49153);
        udp.SetDestinationPort(9);
        datagram->AddHeader(udp);
        const std::vector<uint8_t> plain = Bytes(datagram);
        Ptr<Packet> d = ns3::beeadhoc::AddRouteTrailer(datagram, 17, t);
        NS_TEST_EXPECT_MSG_EQ(d->GetSize(), datagram->GetSize() + t.wireSize(), "trailer bytes on");
        UdpHeader first;
        d->PeekHeader(first);
        NS_TEST_EXPECT_MSG_EQ(first.GetDestinationPort(), 9u, "the UDP header stays first");
        NS_TEST_EXPECT_MSG_EQ(Bytes(d)[8], ::beeadhoc::core::RouteTrailer::kWireVersion,
                              "the trailer right after it");
        const auto read = ns3::beeadhoc::ReadRouteTrailer(d, 17);
        NS_TEST_ASSERT_MSG_EQ(read.has_value(), true, "the trailer reads back");
        NS_TEST_EXPECT_MSG_EQ((read->trailer == t), true, "the same trailer");
        NS_TEST_EXPECT_MSG_EQ(read->size, static_cast<uint32_t>(t.wireSize()), "its size");
        NS_TEST_EXPECT_MSG_EQ((Bytes(ns3::beeadhoc::StripRouteTrailer(d, 17, read->size)) == plain),
                              true, "stripped: the datagram as it was");
        ns3::beeadhoc::RouteTrailerHeader th(t);
        th.Print(os);

        // A swarm through a control payload: kind 3, not a scout.
        ::beeadhoc::core::Swarm sw;
        sw.source = 4;
        sw.destination = 1;
        sw.route = {4, 3, 2, 1};
        sw.foragers = {{{1, 2, 3, 4}, 0x1FF}};
        Ptr<Packet> sp = ns3::beeadhoc::MakeSwarmPacket(sw);
        NS_TEST_EXPECT_MSG_EQ(Bytes(sp)[1], ::beeadhoc::core::kSwarmKind, "the swarm's kind byte");
        const auto swBack = ns3::beeadhoc::DecodeSwarmPacket(sp);
        NS_TEST_EXPECT_MSG_EQ((swBack.has_value() && *swBack == sw), true, "the same swarm");
        NS_TEST_EXPECT_MSG_EQ(ns3::beeadhoc::DecodeScoutPacket(sp).has_value(), false,
                              "not a scout");
    }
};

/// SplitMix64, as core/'s tests: the same bytes on every platform.
uint64_t Mix(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class HeaderFuzzTestCase : public TestCase {
public:
    HeaderFuzzTestCase()
        : TestCase("header: seeded fuzz of the scout, swarm and route-trailer readers") {}

private:
    void DoRun() override {
        uint64_t s = 0xBEE5;
        const std::vector<uint8_t> valid = ::beeadhoc::core::encodeScout(SampleScout());
        uint32_t accepted = 0;
        for (int i = 0; i < 4000; ++i) {
            std::vector<uint8_t> b;
            if (i % 2 == 0) {
                // Random bytes, 0 to 63 long.
                b.resize(Mix(s) % 64);
                for (auto& x : b)
                    x = static_cast<uint8_t>(Mix(s));
                if (!b.empty() && i % 4 == 0) b[0] = Scout::kWireVersion;
            } else {
                // A valid scout with one byte changed, cut short or extended.
                b = valid;
                b[Mix(s) % b.size()] = static_cast<uint8_t>(Mix(s));
                const uint64_t shape = Mix(s) % 3;
                if (shape == 1) b.resize(Mix(s) % b.size());
                if (shape == 2) b.push_back(static_cast<uint8_t>(Mix(s)));
            }
            const auto scout = ns3::beeadhoc::DecodeScoutPacket(FromBytes(b));
            ns3::beeadhoc::ScoutHeader h;  // the header path must not overrun either
            FromBytes(b)->RemoveHeader(h);
            if (!scout) continue;
            ++accepted;
            // Whatever is accepted is exactly one canonical scout.
            NS_TEST_ASSERT_MSG_EQ((::beeadhoc::core::encodeScout(*scout) == b), true,
                                  "accepted bytes re-encode identically");
            NS_TEST_ASSERT_MSG_EQ(h.IsValid(), true, "the header accepts it too");
        }
        NS_TEST_EXPECT_MSG_GT(accepted, 0u, "the fuzz reaches the accepting path");

        // The data packet's trailer and the swarm: the other two readers a
        // received packet goes through.
        ::beeadhoc::core::RouteTrailer tr;
        tr.route = {1, 2, 3};
        tr.piggyback = ::beeadhoc::core::RouteTrailer::Piggyback{{1, 3}, 7};
        const std::vector<uint8_t> trailer = ::beeadhoc::core::encodeTrailer(tr);
        ::beeadhoc::core::Swarm sw;
        sw.source = 3;
        sw.destination = 1;
        sw.route = {3, 2, 1};
        sw.foragers = {{{1, 2, 3}, 0x3F}, {{1, 3}, 0}};
        const std::vector<uint8_t> swarm = ::beeadhoc::core::encodeSwarm(sw);
        uint32_t trailers = 0;
        uint32_t swarms = 0;
        for (int i = 0; i < 6000; ++i) {
            const std::vector<uint8_t>& seed = (i % 2 == 0) ? trailer : swarm;
            std::vector<uint8_t> b;
            if (i % 3 == 0) {
                b.resize(Mix(s) % 80);
                for (auto& x : b)
                    x = static_cast<uint8_t>(Mix(s));
            } else {
                b = seed;
                b[Mix(s) % b.size()] = static_cast<uint8_t>(Mix(s));
                const uint64_t shape = Mix(s) % 3;
                if (shape == 1) b.resize(Mix(s) % b.size());
                if (shape == 2) b.push_back(static_cast<uint8_t>(Mix(s)));
            }
            if (const auto t = ns3::beeadhoc::ReadRouteTrailer(FromBytes(b), 0)) {
                ++trailers;
                const auto again = ::beeadhoc::core::encodeTrailer(t->trailer);
                NS_TEST_ASSERT_MSG_EQ(again.size(), t->size, "the size read is the encoded size");
                NS_TEST_ASSERT_MSG_EQ(
                    (std::vector<uint8_t>(b.begin(), b.begin() + t->size) == again), true,
                    "an accepted trailer re-encodes identically");
            }
            ns3::beeadhoc::RouteTrailerHeader th;  // the header path must not overrun either
            FromBytes(b)->RemoveHeader(th);
            if (const auto w = ns3::beeadhoc::DecodeSwarmPacket(FromBytes(b))) {
                ++swarms;
                NS_TEST_ASSERT_MSG_EQ((::beeadhoc::core::encodeSwarm(*w) == b), true,
                                      "an accepted swarm re-encodes identically");
            }
        }
        NS_TEST_EXPECT_MSG_GT(trailers, 0u, "the trailer fuzz reaches the accepting path");
        NS_TEST_EXPECT_MSG_GT(swarms, 0u, "the swarm fuzz reaches the accepting path");
        NS_TEST_EXPECT_MSG_EQ(ns3::beeadhoc::DecodeScoutPacket(Create<Packet>()).has_value(), false,
                              "empty payload");
        std::vector<uint8_t> trailing = valid;
        trailing.push_back(0);
        NS_TEST_EXPECT_MSG_EQ(ns3::beeadhoc::DecodeScoutPacket(FromBytes(trailing)).has_value(),
                              false, "trailing byte");
        std::vector<uint8_t> mode = valid;
        mode[1] = 3;
        NS_TEST_EXPECT_MSG_EQ(ns3::beeadhoc::DecodeScoutPacket(FromBytes(mode)).has_value(), false,
                              "unknown mode");
        Scout longRoute = SampleScout();
        longRoute.route.assign(ns3::beeadhoc::MaxScoutRouteNodes() + 1, 5);
        NS_TEST_EXPECT_MSG_EQ(
            ns3::beeadhoc::DecodeScoutPacket(ns3::beeadhoc::MakeScoutPacket(longRoute)).has_value(),
            false, "a route past MaxHops + 1 nodes");
    }
};

// --- the adapter on a chain ------------------------------------------------------

constexpr uint16_t kDataPort = 9;

/// What one chain run observed.
struct ChainRun {
    std::vector<std::string> trace;      ///< scout sends and data receptions, in order
    std::vector<uint32_t> returnedInfo;  ///< info of the destination's backward scouts
    uint32_t sent = 0;
    uint32_t received = 0;
    uint64_t forward = 0;   ///< forward scouts sent (source and relays)
    uint64_t backward = 0;  ///< backward scouts sent (each hop back)
    DeferredQueueStats queue0;
    uint32_t foragers0 = 0;  ///< node 0's foragers for node 3 after the run
    uint32_t foragers3 = 0;  ///< node 3's (foreign) foragers for node 0 after the run
    uint32_t swarms = 0;     ///< swarms sent or relayed
    uint32_t swarmForagers = 0;
    // The base's trace sources, when the run connected them (#54).
    uint32_t controlTx = 0;
    uint32_t controlRx = 0;
    uint32_t routeAdded = 0;
    uint32_t dataDecisions = 0;
    uint32_t stateSizes = 0;
    std::string kindForward, kindBackward, kindSwarm;
};

class Chain {
public:
    /// `drainRelay`: give node 1 a 10 J battery drained at 8 J/s until
    /// t = 0.85 s (the source's first update, at 0.1 s, starts the draw), so
    /// 4 J, 40 %, is left when the first scout passes at t = 1 s.
    ChainRun Run(uint32_t seed, uint32_t packets, bool drainRelay = false, bool observe = false) {
        SwarmCoreSeed(seed);
        NodeContainer nodes;
        nodes.Create(4);
        MobilityHelper mobility;
        Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
        for (uint32_t i = 0; i < 4; ++i)
            pos->Add(Vector(200.0 * i, 0.0, 0.0));
        mobility.SetPositionAllocator(pos);
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);

        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                     StringValue("DsssRate2Mbps"), "ControlMode",
                                     StringValue("DsssRate1Mbps"));
        YansWifiChannelHelper channel;
        channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
        channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange",
                                   DoubleValue(250.0));
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");
        NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

        BeeAdHocHelper bee;
        bee.Install(nodes);
        if (drainRelay) {
            Ptr<energy::BasicEnergySource> battery = CreateObject<energy::BasicEnergySource>();
            battery->SetInitialEnergy(10.0);
            battery->SetSupplyVoltage(1.0);
            battery->SetEnergyUpdateInterval(Seconds(0.1));
            battery->SetNode(nodes.Get(1));
            // Aggregated, so the node's disposal disposes the source and its
            // device model (they hold each other) at Simulator::Destroy.
            nodes.Get(1)->AggregateObject(battery);
            Ptr<energy::SimpleDeviceEnergyModel> load =
                CreateObject<energy::SimpleDeviceEnergyModel>();
            load->SetEnergySource(battery);
            load->SetNode(nodes.Get(1));
            battery->AppendDeviceEnergyModel(load);
            load->SetCurrentA(8.0);
            Simulator::Schedule(Seconds(0.85), &energy::SimpleDeviceEnergyModel::SetCurrentA, load,
                                0.0);
            CreateObject<EnergyMetricHook>()->Install(Ptr<energy::EnergySource>(battery));
        }
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        // Every stream pinned, so a second run in this process draws as the first.
        int64_t stream = bee.AssignStreams(nodes, 0);
        stream += WifiHelper::AssignStreams(devs, stream);
        InternetStackHelper().AssignStreams(nodes, stream);

        m_run = ChainRun{};
        for (uint32_t i = 0; i < 4; ++i) {
            Proto(nodes.Get(i))
                ->TraceConnect("ScoutTx", std::to_string(i), MakeCallback(&Chain::ScoutTx, this));
            Proto(nodes.Get(i))
                ->TraceConnect("SwarmTx", std::to_string(i), MakeCallback(&Chain::SwarmTx, this));
            if (observe) {
                auto p = Proto(nodes.Get(i));
                p->TraceConnectWithoutContext("ControlTx", MakeCallback(&Chain::ControlTx, this));
                p->TraceConnectWithoutContext("ControlRx", MakeCallback(&Chain::ControlRx, this));
                p->TraceConnectWithoutContext("RouteChanged",
                                              MakeCallback(&Chain::RouteChanged, this));
                p->TraceConnectWithoutContext("DataDecision",
                                              MakeCallback(&Chain::DataDecision, this));
                p->TraceConnectWithoutContext("StateSize", MakeCallback(&Chain::StateSize, this));
            }
        }
        Ptr<Socket> sink = Socket::CreateSocket(nodes.Get(3), UdpSocketFactory::GetTypeId());
        sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
        sink->SetRecvCallback(MakeCallback(&Chain::Receive, this));
        m_src = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        m_dst = ifs.GetAddress(3);
        for (uint32_t k = 0; k < packets; ++k) {
            Simulator::Schedule(Seconds(1.0 + 0.5 * k), &Chain::Send, this);
        }
        Simulator::Stop(Seconds(2.0 + 0.5 * packets));
        Simulator::Run();
        m_run.queue0 = Proto(nodes.Get(0))->GetDeferredQueue()->Stats();
        m_run.foragers0 = Proto(nodes.Get(0))->Foragers(m_dst);
        m_run.foragers3 = Proto(nodes.Get(3))->Foragers(ifs.GetAddress(0));
        m_run.kindForward = Proto(nodes.Get(0))->KindName(static_cast<uint8_t>(ScoutMode::Forward));
        m_run.kindBackward =
            Proto(nodes.Get(0))->KindName(static_cast<uint8_t>(ScoutMode::Backward));
        m_run.kindSwarm = Proto(nodes.Get(0))->KindName(::beeadhoc::core::kSwarmKind);
        m_src = nullptr;
        Simulator::Destroy();
        return m_run;
    }

private:
    static Ptr<ns3::beeadhoc::RoutingProtocol> Proto(Ptr<Node> n) {
        return n->GetObject<ns3::beeadhoc::RoutingProtocol>();
    }
    void ScoutTx(std::string node, uint8_t mode, bool broadcast, uint32_t info) {
        if (mode == static_cast<uint8_t>(ScoutMode::Forward)) {
            ++m_run.forward;
        } else {
            ++m_run.backward;
            if (node == "3") m_run.returnedInfo.push_back(info);
        }
        std::ostringstream os;
        os << Simulator::Now().GetNanoSeconds() << " n" << node << " tx mode=" << int{mode}
           << (broadcast ? " bcast" : " ucast") << " info=" << info;
        m_run.trace.push_back(os.str());
    }
    void SwarmTx(std::string node, uint32_t foragers) {
        ++m_run.swarms;
        if (node == "3") m_run.swarmForagers += foragers;
        std::ostringstream os;
        os << Simulator::Now().GetNanoSeconds() << " n" << node << " tx swarm=" << foragers;
        m_run.trace.push_back(os.str());
    }
    void ControlTx(uint8_t, bool, uint32_t) { ++m_run.controlTx; }
    void ControlRx(uint8_t, uint32_t) { ++m_run.controlRx; }
    void RouteChanged(Ipv4Address, Ipv4Address, bool added) {
        if (added) ++m_run.routeAdded;
    }
    void DataDecision(Ipv4Address, swarm::core::RouteAction) { ++m_run.dataDecisions; }
    void StateSize(uint32_t, uint32_t) { ++m_run.stateSizes; }
    void Send() {
        ++m_run.sent;
        m_src->SendTo(Create<Packet>(64), 0, InetSocketAddress(m_dst, kDataPort));
    }
    void Receive(Ptr<Socket> s) {
        while (Ptr<Packet> p = s->Recv()) {
            ++m_run.received;
            std::ostringstream os;
            os << Simulator::Now().GetNanoSeconds() << " n3 rx data";
            m_run.trace.push_back(os.str());
        }
    }

    ChainRun m_run;
    Ptr<Socket> m_src;
    Ipv4Address m_dst;
};

class ChainDeliveryTestCase : public TestCase {
public:
    ChainDeliveryTestCase()
        : TestCase("adapter: discovery, a held packet released, source-routed delivery") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 20);
        NS_TEST_ASSERT_MSG_EQ(r.sent, 20u, "packets sent");
        NS_TEST_EXPECT_MSG_GT(r.forward, 0u, "scouts flooded");
        NS_TEST_EXPECT_MSG_GT(r.backward, 0u, "scouts returned");
        // The first packet finds no forager: it starts the scout and is held
        // until the scout returns, then sent; the rest follow foragers.
        NS_TEST_EXPECT_MSG_EQ(r.received, r.sent, "every packet delivered");
        NS_TEST_EXPECT_MSG_GT(r.queue0.enqueued, 0u, "the first packet waited for a scout");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.released, r.queue0.enqueued, "and was sent after it");
        NS_TEST_EXPECT_MSG_EQ(r.queue0.discarded + r.queue0.expired, 0u, "nothing lost");
        // No energy source: every hop reports level 7, so the destination's
        // backward scout carries 7, 7, 7 (relay 1, relay 2, itself).
        NS_TEST_ASSERT_MSG_GT(r.returnedInfo.size(), 0u, "the destination turned a scout back");
        NS_TEST_EXPECT_MSG_EQ(r.returnedInfo.front(), 0x1FFu, "levels 7, 7, 7");
        // INPUT chain (§12.1.3): each delivered packet left node 3 a foreign
        // forager for node 0, carried by the route trailer.
        NS_TEST_EXPECT_MSG_GT(r.foragers3, 0u, "foreign foragers at the destination");
        NS_TEST_EXPECT_MSG_EQ(r.swarms, 0u, "20 packets: not more than SWARMSIZE foreign");
    }
};

class SwarmTestCase : public TestCase {
public:
    SwarmTestCase()
        : TestCase("adapter: one-way traffic sends a swarm home (Listing 13.30, §13.7.3)") {}

private:
    void DoRun() override {
        // 30 packets one way, one every 0.5 s: node 3's foreign foragers for
        // node 0 pass SWARMSIZE (20) while it has no own forager for node 0.
        const ChainRun r = Chain().Run(1, 30);
        NS_TEST_EXPECT_MSG_EQ(r.received, r.sent, "every packet delivered");
        NS_TEST_EXPECT_MSG_GT(r.swarms, 0u, "a swarm left node 3 and was relayed");
        NS_TEST_EXPECT_MSG_EQ(r.swarmForagers % 20u, 0u, "SWARMSIZE foragers per swarm");
        NS_TEST_EXPECT_MSG_GT(r.foragers0, 1u, "node 0 holds the foragers that came home");
    }
};

class BatteryTestCase : public TestCase {
public:
    BatteryTestCase() : TestCase("adapter: a relay's battery reaches the scout's info") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 1, true);
        NS_TEST_ASSERT_MSG_GT(r.returnedInfo.size(), 0u, "the destination turned a scout back");
        const uint32_t info = r.returnedInfo.front();
        // Relay 1 wrote first (bits 6-8), relay 2 next, the destination last.
        // Relay 1 has 4 J of 10 left: level 4 (34.4-44.6 %).
        NS_TEST_EXPECT_MSG_EQ((info >> 6) & 7u, 4u, "relay 1's drained battery: level 4");
        NS_TEST_EXPECT_MSG_EQ(info & 0x3Fu, 0x3Fu, "relay 2 and the destination: no data, 7");
        NS_TEST_EXPECT_MSG_EQ(r.received, 1u, "delivered");
    }
};

class ObserverTestCase : public TestCase {
public:
    ObserverTestCase()
        : TestCase("observer: the base's trace sources fire and change no output (#54)") {}

private:
    void DoRun() override {
        const ChainRun plain = Chain().Run(1, 10);
        const ChainRun seen = Chain().Run(1, 10, false, true);
        NS_TEST_ASSERT_MSG_GT(seen.controlTx, 0u, "ControlTx: scouts sent");
        NS_TEST_EXPECT_MSG_GT(seen.controlRx, 0u, "ControlRx: scouts received");
        NS_TEST_EXPECT_MSG_GT(seen.routeAdded, 0u, "RouteChanged: foragers added");
        NS_TEST_EXPECT_MSG_GT(seen.dataDecisions, 0u, "DataDecision: data routed");
        NS_TEST_EXPECT_MSG_GT(seen.stateSizes, 0u, "StateSize: the forager store grew");
        NS_TEST_EXPECT_MSG_EQ(seen.kindForward, std::string("FORWARD_SCOUT"), "kind 0 named");
        NS_TEST_EXPECT_MSG_EQ(seen.kindBackward, std::string("BACKWARD_SCOUT"), "kind 1 named");
        NS_TEST_EXPECT_MSG_EQ(seen.kindSwarm, std::string("SWARM"), "kind 3 named");
        // Connected sources never alter the run (org rule 5, observer contract).
        NS_TEST_EXPECT_MSG_EQ((plain.trace == seen.trace), true, "identical trace");
        NS_TEST_EXPECT_MSG_EQ(plain.received, seen.received, "identical delivery");
    }
};

class GuardsTestCase : public TestCase {
public:
    GuardsTestCase()
        : TestCase("adapter: short UDP payloads and subnet broadcasts reach no trailer reader") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);
        WifiHelper wifi;
        wifi.SetStandard(WIFI_STANDARD_80211b);
        YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
        YansWifiPhyHelper phy;
        phy.SetChannel(channel.Create());
        WifiMacHelper mac;
        mac.SetType("ns3::AdhocWifiMac");
        NetDeviceContainer devs = wifi.Install(phy, mac, nodes);
        BeeAdHocHelper bee;
        bee.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        a.Assign(devs);
        Ptr<ns3::beeadhoc::RoutingProtocol> p1 =
            nodes.Get(1)->GetObject<ns3::beeadhoc::RoutingProtocol>();

        Simulator::Schedule(Seconds(0.1), [this, p1, devs]() {
            const auto ucb = MakeCallback(&GuardsTestCase::Unicast, this);
            const auto mcb = MakeCallback(&GuardsTestCase::Multicast, this);
            const auto lcb = MakeCallback(&GuardsTestCase::Deliver, this);
            const auto ecb = MakeCallback(&GuardsTestCase::Error, this);
            Ipv4Header h;
            h.SetSource(Ipv4Address("10.1.0.1"));
            h.SetProtocol(17);
            h.SetTtl(64);
            // Three bytes of "UDP" for another node: no UDP header to peek, no
            // trailer: dropped through the error callback, nothing read past it.
            h.SetDestination(Ipv4Address("10.1.0.3"));
            p1->RouteInput(Create<Packet>(3), h, devs.Get(1), ucb, mcb, lcb, ecb);
            // A subnet-directed broadcast: the base's, delivered locally.
            h.SetDestination(Ipv4Address("10.1.0.255"));
            Ptr<Packet> b = Create<Packet>(16);
            UdpHeader udp;
            udp.SetSourcePort(49153);
            udp.SetDestinationPort(9);
            b->AddHeader(udp);
            p1->RouteInput(b, h, devs.Get(1), ucb, mcb, lcb, ecb);
        });
        Simulator::Stop(Seconds(0.2));
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_EXPECT_MSG_EQ(m_errors, 1u, "the short packet dropped");
        NS_TEST_EXPECT_MSG_EQ(m_delivered, 1u, "the subnet broadcast delivered by the base");
    }

    void Unicast(Ptr<Ipv4Route>, Ptr<const Packet>, const Ipv4Header&) {}
    void Multicast(Ptr<Ipv4MulticastRoute>, Ptr<const Packet>, const Ipv4Header&) {}
    void Deliver(Ptr<const Packet>, const Ipv4Header&, uint32_t) { ++m_delivered; }
    void Error(Ptr<const Packet>, const Ipv4Header&, Socket::SocketErrno) { ++m_errors; }

    uint32_t m_delivered = 0;
    uint32_t m_errors = 0;
};

class DeterminismTestCase : public TestCase {
public:
    DeterminismTestCase() : TestCase("determinism: same seed same trace, another seed another") {}

private:
    void DoRun() override {
        const ChainRun a = Chain().Run(1, 10);
        const ChainRun b = Chain().Run(1, 10);
        const ChainRun c = Chain().Run(2, 10);
        NS_TEST_ASSERT_MSG_GT(a.trace.size(), 0u, "something happened");
        NS_TEST_EXPECT_MSG_EQ((a.trace == b.trace), true, "seed 1 twice: identical");
        // The scout relay jitter is drawn from the seeded stream, so relays
        // leave at other times under another seed.
        NS_TEST_EXPECT_MSG_EQ((a.trace != c.trace), true, "seed 2: a different trace");
    }
};

class BeeAdHocTestSuite : public TestSuite {
public:
    BeeAdHocTestSuite() : TestSuite("beeadhoc", Type::UNIT) {
        for (const auto& t : ::beeadhoc::core::test::AllTests()) {
            AddTestCase(new BeeAdHocCoreTestCase(t), TestCase::Duration::QUICK);
        }
        AddTestCase(new HeaderRoundtripTestCase, TestCase::Duration::QUICK);
        AddTestCase(new HeaderFuzzTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ChainDeliveryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new SwarmTestCase, TestCase::Duration::QUICK);
        AddTestCase(new BatteryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ObserverTestCase, TestCase::Duration::QUICK);
        AddTestCase(new GuardsTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterminismTestCase, TestCase::Duration::QUICK);
    }
};

BeeAdHocTestSuite g_beeAdHocTestSuite;

}  // namespace
