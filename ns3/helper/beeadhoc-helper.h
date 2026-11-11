// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Installs BeeAdHoc on nodes: swarm-core's SwarmRoutingHelper for
// ns3::beeadhoc::RoutingProtocol, so `Install` also adds the monitors that
// apply to each node (a WifiLinkMonitor; an EnergyMetricHook, the battery the
// core reads, where an energy source is installed).
//
//   BeeAdHocHelper bee;
//   bee.Install(nodes);           // IPv4 stack + BeeAdHoc + monitors
//   ...energy sources, if any, then SwarmMonitorHelper().Install(nodes)...
//   ...assign addresses...
//   bee.AssignStreams(nodes, 0);  // before Simulator::Run
#ifndef BEEADHOC_HELPER_H
#define BEEADHOC_HELPER_H

#include "ns3/swarm-routing-helper.h"

namespace ns3 {

class BeeAdHocHelper : public SwarmRoutingHelper {
public:
    BeeAdHocHelper();
    BeeAdHocHelper* Copy() const override;
};

}  // namespace ns3

#endif  // BEEADHOC_HELPER_H
