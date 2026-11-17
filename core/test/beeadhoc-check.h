// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Dependency-free test support: a failure collector and a seeded RNG, the two
// classes of swarm-core's core/test/swarm-core-check.h (v1.0.0) the core
// tests use, copied as they are. swarm-core's package (its vcpkg port,
// swarm-routing/.github#489) installs the library's headers but not its test
// sources, so the tests carry their own, as HOPNET's core/test/hopnet-check.h
// does. The same test functions run under ctest and ns-3's test.py.
#ifndef BEEADHOC_CORE_TEST_CHECK_H
#define BEEADHOC_CORE_TEST_CHECK_H

#include "swarm-ports.h"

#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace beeadhoc::core::test {

/// Collects failed checks, each with its file and line.
class Checker {
public:
    void check(bool ok, std::string_view what,
               std::source_location where = std::source_location::current()) {
        if (ok) return;
        failures_.push_back(std::string(where.file_name()) + ":" + std::to_string(where.line()) +
                            ": " + std::string(what));
    }
    const std::vector<std::string>& failures() const { return failures_; }

private:
    std::vector<std::string> failures_;
};

/// SplitMix64: a tiny, fully specified generator, so a seed gives the same
/// sequence on every compiler and platform (the tests compare sequences).
class SeededRng : public swarm::core::IRng {
public:
    explicit SeededRng(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() override {
        ++draws_;
        return static_cast<double>(next() >> 11) * 0x1.0p-53;
    }
    /// n <= 0 breaks the IRng contract: returns 0 without drawing, rather
    /// than dividing by zero.
    int uniformInt(int n) override {
        if (n <= 0) return 0;
        ++draws_;
        return static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }
    /// How many values were drawn through the IRng port.
    int draws() const { return draws_; }

private:
    std::uint64_t state_;
    int draws_ = 0;
};

}  // namespace beeadhoc::core::test

#endif  // BEEADHOC_CORE_TEST_CHECK_H
