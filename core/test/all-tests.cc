// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The list both runners walk: run-core-tests.cc (plain CMake + ctest) and,
// later, the ns-3 suite in ns3/test/.
#include "beeadhoc-core-tests.h"

#include <array>

namespace beeadhoc::core::test {

namespace {

constexpr std::array<NamedTest, 22> kTests{{
    {"scout: wire roundtrip", TestScoutRoundtrip},
    {"scout: the decoder fails closed", TestScoutFailsClosed},
    {"scout: seeded fuzz (fails or round-trips to itself)", TestScoutFuzz},
    {"battery level bins", TestBatteryLevel},
    {"rating (Listing 13.15, #4)", TestRating},
    {"forager arrays: bounded, per type", TestForagerArrays},
    {"forager choice (Listing 13.17)", TestForagerChoice},
    {"forager store: MAXAGE collection, destination cap", TestForagerCollection},
    {"discovery: scout out, back, foragers home", TestDiscovery},
    {"discovery: the destination's level is the L-th value", TestDestinationLevel},
    {"discovery: expanding ring and give-up", TestExpandingRing},
    {"discovery: seen list, hop and search caps", TestScoutBounds},
    {"router: fixed seed, fixed trace", TestRouterDeterminism},
    {"trailer: wire roundtrip", TestTrailerRoundtrip},
    {"trailer: the decoder fails closed", TestTrailerFailsClosed},
    {"trailer: seeded fuzz (fails or round-trips to itself)", TestTrailerFuzz},
    {"data: FORWARD-chain levels, INPUT-chain foreign forager (#4)", TestForwardChain},
    {"data: one foreign forager piggybacked home (§13.7.1)", TestForeignHome},
    {"data: a packet from the destination ends its search", TestDataEndsSearch},
    {"observer: every callback, kinds, bytes, footprint", TestObserverEvents},
    {"observer: decisions and trace identical with and without", TestObserverReadOnly},
    {"observer: store removals (array cap, LRU, MAXAGE, use)", TestObserverStore},
}};

}  // namespace

std::span<const NamedTest> AllTests() {
    return kTests;
}

}  // namespace beeadhoc::core::test
