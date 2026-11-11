// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// BeeAdHoc's core test functions. Each records its failures in the Checker;
// the runners (run-core-tests.cc here; the ns-3 suite later) report them.
#ifndef BEEADHOC_CORE_TESTS_H
#define BEEADHOC_CORE_TESTS_H

#include "beeadhoc-test-support.h"

#include <span>

namespace beeadhoc::core::test {

void TestScoutRoundtrip(Checker& c);
void TestScoutFailsClosed(Checker& c);
void TestScoutFuzz(Checker& c);
void TestBatteryLevel(Checker& c);
void TestRating(Checker& c);
void TestForagerArrays(Checker& c);
void TestForagerChoice(Checker& c);
void TestForagerCollection(Checker& c);
void TestDiscovery(Checker& c);
void TestDestinationLevel(Checker& c);
void TestExpandingRing(Checker& c);
void TestScoutBounds(Checker& c);
void TestRouterDeterminism(Checker& c);
void TestObserverEvents(Checker& c);
void TestObserverReadOnly(Checker& c);
void TestObserverStore(Checker& c);

struct NamedTest {
    const char* name;
    void (*run)(Checker&);
};

/// Every core test, in a fixed order.
std::span<const NamedTest> AllTests();

}  // namespace beeadhoc::core::test

#endif  // BEEADHOC_CORE_TESTS_H
