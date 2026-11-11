// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// BeeAdHoc as an arm of swarm-routing/ns3-tools' comparison harness
// (routing-bench, AntHocNet ADR-0027): compiled only when routing-bench is
// installed beside this module (CMakeLists.txt). routing-compare finds this
// file and calls RegisterArms_beeadhoc() at start-up; the arm's name is
// `beeadhoc`, the token AntHocNet's `adaptive` arm used, so the v270a command
// lines run the new module unchanged. The two never share a build: a
// duplicate name aborts.
#include "ns3/beeadhoc-helper.h"
#include "ns3/routing-bench-arm.h"

namespace ns3 {
namespace beeadhoc {
namespace {

class BeeAdHocArm : public bench::Arm {
public:
    std::string Name() const override { return "beeadhoc"; }
    void InstallRouting(InternetStackHelper& internet, bench::RunContext&) override {
        internet.SetRoutingHelper(m_helper);
    }
    int64_t AssignStreams(bench::RunContext& run, int64_t stream) override {
        return m_helper.AssignStreams(run.nodes, stream);
    }
    // InstallRouting only names the helper; the monitors BeeAdHocHelper::Install
    // would add (a WifiLinkMonitor per wifi node, an EnergyMetricHook per node
    // with an energy source) are added here instead.
    void AfterStack(bench::RunContext& run) override { SwarmMonitorHelper().Install(run.nodes); }

private:
    BeeAdHocHelper m_helper;
};

Ptr<bench::Arm> MakeBeeAdHocArm() {
    return Create<BeeAdHocArm>();
}

}  // namespace
}  // namespace beeadhoc

namespace bench {

void RegisterArms_beeadhoc();

void RegisterArms_beeadhoc() {
    RegisterArm("beeadhoc", "BeeAdHoc, standalone on swarm-core (Wedde & Farooq 2005; PG 460)",
                &beeadhoc::MakeBeeAdHocArm);
}

}  // namespace bench
}  // namespace ns3
