// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// BeeAdHoc on a small mobile ad-hoc network: nodes moving by random waypoint in
// 802.11b at 2 Mbit/s with a 300 m disk range, and constant-bit-rate UDP flows
// of 64-byte packets (a demonstration, scaled so the run takes seconds; the
// anchor scenario is docs/fidelity.md's). No energy source is installed, so
// every node reports a full battery, level 7 ([PG460] §13.1.2). Prints one ##FLOW##
// line per data flow and a ##TOTAL## summary on stdout (not the org marker
// lines: those are routing-compare's).
//
// CI's determinism gate runs it twice with --seed=1 (byte-identical output)
// and once with --seed=2 (different output); the seed is not printed (org
// rule 5). It is a demonstration and a smoke, not a benchmark: the paired
// comparison runs through swarm-routing/ns3-tools' routing-compare.
#include "ns3/applications-module.h"
#include "ns3/beeadhoc-helper.h"
#include "ns3/beeadhoc-routing-protocol.h"
#include "ns3/command-line.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/swarm-core-module.h"
#include "ns3/wifi-module.h"

#include <array>
#include <cstdio>
#include <iostream>

using namespace ns3;

namespace {

constexpr uint16_t kDataPort = 9;
std::array<uint64_t, 3> g_scouts{};  // by core::ScoutMode byte: [1] forward, [2] backward

void CountScout(uint8_t mode, bool /*broadcast*/, uint32_t /*info*/) {
    if (mode < g_scouts.size()) ++g_scouts[mode];
}

}  // namespace

int main(int argc, char* argv[]) {
    uint32_t seed = 1;
    uint32_t nodes = 20;
    uint32_t flows = 5;
    double simTime = 60.0;
    double areaX = 1000.0;
    double areaY = 300.0;
    CommandLine cmd(__FILE__);
    cmd.AddValue("seed", "run number of the seeded streams (org rule 5)", seed);
    cmd.AddValue("nodes", "number of nodes", nodes);
    cmd.AddValue("flows", "number of CBR flows (node i to node nodes-1-i)", flows);
    cmd.AddValue("time", "simulated time (s)", simTime);
    cmd.AddValue("areaX", "area width (m)", areaX);
    cmd.AddValue("areaY", "area height (m)", areaY);
    cmd.Parse(argc, argv);
    if (flows > nodes / 2) flows = nodes / 2;

    SwarmCoreSeed(seed);

    NodeContainer n;
    n.Create(nodes);

    MobilityHelper mobility;
    ObjectFactory pos;
    pos.SetTypeId("ns3::RandomRectanglePositionAllocator");
    pos.Set("X",
            StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaX) + "]"));
    pos.Set("Y",
            StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(areaY) + "]"));
    Ptr<PositionAllocator> alloc = pos.Create()->GetObject<PositionAllocator>();
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel", "Speed",
                              StringValue("ns3::UniformRandomVariable[Min=1.0|Max=10.0]"), "Pause",
                              StringValue("ns3::ConstantRandomVariable[Constant=0.0]"),
                              "PositionAllocator", PointerValue(alloc));
    mobility.SetPositionAllocator(alloc);
    mobility.Install(n);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("DsssRate2Mbps"), "ControlMode",
                                 StringValue("DsssRate1Mbps"));
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(300.0));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, n);

    BeeAdHocHelper bee;
    bee.Install(n);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.1.0.0", "255.255.0.0");
    Ipv4InterfaceContainer ifs = addresses.Assign(devices);

    int64_t stream = 0;
    stream += bee.AssignStreams(n, stream);
    stream += WifiHelper::AssignStreams(devices, stream);
    stream += InternetStackHelper().AssignStreams(n, stream);
    stream += mobility.AssignStreams(n, stream);
    alloc->AssignStreams(stream);

    for (uint32_t i = 0; i < nodes; ++i) {
        n.Get(i)->GetObject<ns3::beeadhoc::RoutingProtocol>()->TraceConnectWithoutContext(
            "ScoutTx", MakeCallback(&CountScout));
    }

    for (uint32_t f = 0; f < flows; ++f) {
        const uint32_t dst = nodes - 1 - f;
        UdpServerHelper server(kDataPort);
        ApplicationContainer s = server.Install(n.Get(dst));
        s.Start(Seconds(0.0));
        UdpClientHelper client(ifs.GetAddress(dst), kDataPort);
        client.SetAttribute("MaxPackets", UintegerValue(1000000));
        client.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        client.SetAttribute("PacketSize", UintegerValue(64));
        ApplicationContainer c = client.Install(n.Get(f));
        c.Start(Seconds(1.0 + f));
        c.Stop(Seconds(simTime - 5.0));
    }

    FlowMonitorHelper fm;
    Ptr<FlowMonitor> monitor = fm.InstallAll();

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fm.GetClassifier());
    uint64_t tx = 0;
    uint64_t rx = 0;
    for (const auto& [id, st] : monitor->GetFlowStats()) {
        const Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);
        if (t.destinationPort != kDataPort) continue;  // the scouts' UDP flows
        tx += st.txPackets;
        rx += st.rxPackets;
        std::cout << "##FLOW## src=" << t.sourceAddress << " dst=" << t.destinationAddress
                  << " tx=" << st.txPackets << " rx=" << st.rxPackets << "\n";
    }
    char pdr[32];
    std::snprintf(pdr, sizeof pdr, "%.4f", tx ? static_cast<double>(rx) / tx : 0.0);
    std::cout << "##TOTAL## protocol=beeadhoc nodes=" << nodes << " flows=" << flows << " tx=" << tx
              << " rx=" << rx << " pdr=" << pdr << " scouts=" << g_scouts[1]
              << " backward=" << g_scouts[2] << "\n";

    Simulator::Destroy();
    return 0;
}
