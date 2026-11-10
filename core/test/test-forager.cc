// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The battery levels, the rating and the forager store: its caps (golden rule
// 7), the choice rule of Listing 13.17 and the MAXAGE collector.
#include "../beeadhoc-forager.h"
#include "../beeadhoc-scout.h"
#include "beeadhoc-core-tests.h"

#include <array>
#include <cmath>
#include <set>

namespace beeadhoc::core::test {

namespace {

std::uint32_t levels(std::initializer_list<std::uint8_t> written) {
    std::uint32_t info = 0;
    for (auto l : written)
        info = pushInfo(info, l);
    return info;
}

Forager forager(NodeAddress first, int dances, Time created, bool own = true) {
    return Forager{{first, 50}, 0, dances, created, own};
}

}  // namespace

void TestBatteryLevel(Checker& c) {
    c.check(batteryLevel(std::nan("")) == 7, "no battery data reads full");
    c.check(batteryLevel(0.0) == 0 && batteryLevel(0.05) == 0, "<= 9 % -> 0");
    c.check(batteryLevel(0.10) == 1 && batteryLevel(0.139) == 1, "(9, 14] -> 1");
    const std::array<double, 5> mids{0.20, 0.30, 0.40, 0.50, 0.60};
    for (std::size_t i = 0; i < mids.size(); ++i)
        c.check(batteryLevel(mids[i]) == i + 2, "levels 2-6 in equal 10.2 % bins");
    c.check(batteryLevel(0.66) == 7 && batteryLevel(1.0) == 7, "> 65 % -> 7");
}

void TestRating(Checker& c) {
    const Config config;
    // Every level 7: direct neighbour 8, then 10, 8, 7, 6, 5, 4, 3 for 2 to 8
    // links, 0 for 9 and 10 (fidelity "The rating is capped at MAX_DANCES").
    const std::array<int, 10> expected{8, 10, 8, 7, 6, 5, 4, 3, 0, 0};
    for (std::size_t l = 1; l <= 10; ++l) {
        std::uint32_t info = 0;
        for (std::size_t i = 0; i < l; ++i)
            info = pushInfo(info, 7);
        c.check(rateForager(l, info, config) == expected[l - 1], "all-7 rating by length");
    }
    c.check(rateForager(2, levels({7, 7}), config) > config.maxDances, "no cap at MAX_DANCES");
    // [7, 7, 3]: min 3, avg 17 / 3 = 5, (8 * 3 * 5 + 1) / 44 = 2.
    c.check(rateForager(3, levels({7, 7, 3}), config) == 2, "min and integer average");
    c.check(rateForager(3, levels({7, 7, 0}), config) == 0, "one level 0 rates 0");
    c.check(rateForager(0, 0, config) == 0, "an empty route rates 0");
    c.check(rateForager(12, levels({7, 7, 7}), config) == 0, "never negative");
    Config shortHops;
    shortHops.maxHops = 2;  // (2 - 3) * 49 + 1 = -48: negative before the clamp
    c.check(rateForager(4, levels({7, 7, 7, 7}), shortHops) == 0, "a negative result clamps to 0");
}

void TestForagerArrays(Checker& c) {
    Config config;
    config.foragerArraySize = 3;
    ForagerStore store(config);
    SeededRng rng(1);
    for (int i = 0; i < 5; ++i)
        store.put(7, ForagerType::Energy, forager(i, 0, 0.0), 0.0, rng);
    c.check(store.size(7, ForagerType::Energy) == 3, "the array stops at ARRAYSIZE");
    c.check(store.size(7, ForagerType::Delay) == 0 && store.size(8, ForagerType::Energy) == 0,
            "types and destinations are separate arrays");
    std::set<NodeAddress> firstHops;
    while (auto f = store.take(7, ForagerType::Energy, 0.5, rng))
        firstHops.insert(f->route[0]);
    c.check(firstHops == std::set<NodeAddress>{2, 3, 4}, "a full array drops its oldest");
    c.check(store.size(7, ForagerType::Energy) == 0, "non-dancing foragers are used once");
}

void TestForagerChoice(Checker& c) {
    const Config config;
    SeededRng rng(2);
    {
        ForagerStore store(config);
        store.put(1, ForagerType::Energy, forager(9, 0, 0.0, false), 0.0, rng);
        store.put(1, ForagerType::Energy, forager(4, 0, 0.0, true), 0.0, rng);
        auto a = store.take(1, ForagerType::Energy, 0.1, rng);
        auto b = store.take(1, ForagerType::Energy, 0.1, rng);
        c.check(a && a->route[0] == 4 && b && b->route[0] == 9, "own before foreign");
        c.check(!store.take(1, ForagerType::Energy, 0.1, rng), "empty after both are used");
    }
    {
        ForagerStore store(config);
        store.put(1, ForagerType::Delay, forager(4, 2, 0.0), 0.0, rng);
        auto a = store.take(1, ForagerType::Delay, 0.5, rng);
        c.check(a && a->dances == 1 && store.size(1, ForagerType::Delay) == 1,
                "a dancing forager is copied and loses a dance");
        store.take(1, ForagerType::Delay, 0.6, rng);
        auto b = store.take(1, ForagerType::Delay, 0.7, rng);
        c.check(b && store.size(1, ForagerType::Delay) == 0, "out of dances: removed and used");
    }
    {
        ForagerStore store(config);
        store.put(1, ForagerType::Energy, forager(4, 5, 0.0), 0.0, rng);
        auto a = store.take(1, ForagerType::Energy, config.foragerDanceTime + 0.1, rng);
        c.check(a && store.size(1, ForagerType::Energy) == 0,
                "past DANCETIME it stops dancing; the last one is used though expired");
    }
    {
        ForagerStore store(config);
        store.put(1, ForagerType::Energy, forager(4, 0, 0.0), 0.0, rng);
        store.put(1, ForagerType::Energy, forager(6, 0, 2.5), 2.5, rng);
        auto a = store.take(1, ForagerType::Energy, 3.0, rng);
        c.check(a && a->route[0] == 6, "an expired forager is discarded while others remain");
    }
}

void TestForagerCollection(Checker& c) {
    Config config;
    {
        ForagerStore store(config);
        SeededRng rng(3);
        store.put(1, ForagerType::Energy, forager(4, 0, 0.0), 0.0, rng);
        store.put(2, ForagerType::Energy, forager(4, 0, 20.0), 20.0, rng);
        store.put(3, ForagerType::Energy, forager(4, 0, 31.0), 31.0, rng);
        c.check(store.destinations() == 2 && store.size(1, ForagerType::Energy) == 0,
                "a put collects destinations idle for more than MAXAGE");
        c.check(rng.draws() == 0, "GARBAGE_PROBAB 1 draws nothing");
        store.collect(60.0);
        c.check(store.destinations() == 1, "collect() on its own");
    }
    {
        config.maxDestinations = 2;
        config.garbageProbability = 0.0;
        ForagerStore store(config);
        SeededRng rng(4);
        store.put(1, ForagerType::Energy, forager(4, 9, 0.0), 0.0, rng);
        store.put(2, ForagerType::Energy, forager(4, 9, 1.0), 1.0, rng);
        store.take(1, ForagerType::Energy, 1.5, rng);  // 1 is now the most recent
        store.put(3, ForagerType::Energy, forager(4, 9, 2.0), 2.0, rng);
        c.check(store.destinations() == 2 && store.size(2, ForagerType::Energy) == 0 &&
                    store.size(1, ForagerType::Energy) == 1,
                "the least recently used destination is evicted at the cap");
    }
    {
        config.garbageProbability = 0.5;
        ForagerStore store(config);
        SeededRng rng(5);
        store.put(1, ForagerType::Energy, forager(4, 0, 0.0), 0.0, rng);
        c.check(rng.draws() == 1, "a fractional GARBAGE_PROBAB draws once per put");
    }
}

}  // namespace beeadhoc::core::test
