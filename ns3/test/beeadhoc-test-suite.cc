// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// `test.py -s beeadhoc`. Needs ns-3.42+ (scoped TestCase::Duration).
//
// - core/'s tests (core/test/), one TestCase each, so the debug and sanitized
//   legs run them inside ns-3 as core.yml runs them alone;
// - the ns-3 scout header: roundtrip through a Packet with the byte layout
//   pinned, and a seeded fuzz of the decoder a received control payload goes
//   through (org rule 3); the route tag's roundtrip;
// - the adapter on a static four-node 802.11b chain: a scout flooded and
//   returned, the packet that started it held and then sent, every packet
//   delivered over the source route 0 -> 1 -> 2 -> 3;
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

        // The route tag: the hops after the source, through a Packet.
        Ptr<Packet> d = Create<Packet>(8);
        d->AddPacketTag(ns3::beeadhoc::RouteTag({2, 3, 4}));
        ns3::beeadhoc::RouteTag t;
        NS_TEST_ASSERT_MSG_EQ(d->Copy()->PeekPacketTag(t), true, "tag rides with a copy");
        NS_TEST_EXPECT_MSG_EQ((t.Route() == std::vector<::beeadhoc::core::NodeAddress>{2, 3, 4}),
                              true, "same route");
        t.Print(os);
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
    HeaderFuzzTestCase() : TestCase("header: seeded fuzz of the control-payload decoder") {}

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
    // The base's trace sources, when the run connected them (#54).
    uint32_t controlTx = 0;
    uint32_t controlRx = 0;
    uint32_t routeAdded = 0;
    uint32_t dataDecisions = 0;
    uint32_t stateSizes = 0;
    std::string kindForward, kindBackward;
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
        m_run.kindForward = Proto(nodes.Get(0))->KindName(static_cast<uint8_t>(ScoutMode::Forward));
        m_run.kindBackward =
            Proto(nodes.Get(0))->KindName(static_cast<uint8_t>(ScoutMode::Backward));
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
        // Connected sources never alter the run (org rule 5, observer contract).
        NS_TEST_EXPECT_MSG_EQ((plain.trace == seen.trace), true, "identical trace");
        NS_TEST_EXPECT_MSG_EQ(plain.received, seen.received, "identical delivery");
    }
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
        AddTestCase(new BatteryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ObserverTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterminismTestCase, TestCase::Duration::QUICK);
    }
};

BeeAdHocTestSuite g_beeAdHocTestSuite;

}  // namespace
