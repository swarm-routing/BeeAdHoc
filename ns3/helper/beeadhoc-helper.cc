// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "beeadhoc-helper.h"

namespace ns3 {

BeeAdHocHelper::BeeAdHocHelper() : SwarmRoutingHelper("ns3::beeadhoc::RoutingProtocol") {}

BeeAdHocHelper* BeeAdHocHelper::Copy() const {
    return new BeeAdHocHelper(*this);
}

}  // namespace ns3
