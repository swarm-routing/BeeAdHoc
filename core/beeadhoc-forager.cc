// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "beeadhoc-forager.h"

#include "beeadhoc-scout.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace beeadhoc::core {

std::uint8_t batteryLevel(double residualFraction) {
    if (std::isnan(residualFraction)) return 7;
    const double pct = residualFraction * 100.0;
    // Upper bounds of levels 0..6; above the last is 7.
    constexpr std::array<double, 7> kUpper{9.0, 14.0, 24.2, 34.4, 44.6, 54.8, 65.0};
    for (std::size_t level = 0; level < kUpper.size(); ++level) {
        if (pct <= kUpper[level]) return static_cast<std::uint8_t>(level);
    }
    return 7;
}

int rateForager(std::size_t routeLength, std::uint32_t info, const Config& config) {
    if (routeLength == 0) return 0;
    if (routeLength == 1) return config.maxDances;
    const auto length = static_cast<int>(routeLength);
    int lowest = 7;
    int sum = 0;
    for (int i = 0; i < length; ++i) {
        const int value =
            i < kInfoValues ? static_cast<int>((info >> (kInfoBitsPerHop * i)) & 0x7u) : 0;
        lowest = std::min(lowest, value);
        sum += value;
    }
    const int average = sum / length;
    const int rating = ((config.maxHops - (length - 1)) * lowest * average + 1) / 44;
    return std::max(rating, 0);
}

ForagerStore::ForagerStore(const Config& config) : config_(config) {}

void ForagerStore::put(NodeAddress dest, ForagerType type, Forager f, Time now,
                       swarm::core::IRng& rng) {
    auto it = entries_.find(dest);
    if (it == entries_.end()) {
        if (entries_.size() >= std::max<std::size_t>(config_.maxDestinations, 1)) {
            auto lru = std::min_element(
                entries_.begin(), entries_.end(),
                [](const auto& a, const auto& b) { return a.second.lastUsed < b.second.lastUsed; });
            entries_.erase(lru);
        }
        it = entries_.emplace(dest, Entry{}).first;
    }
    auto& array = it->second.arrays[static_cast<std::size_t>(type)];
    if (array.size() >= std::max<std::size_t>(config_.foragerArraySize, 1))
        array.erase(array.begin());
    array.push_back(std::move(f));
    it->second.lastUsed = now;

    if (config_.garbageProbability >= 1.0 ||
        (config_.garbageProbability > 0.0 && rng.uniform() < config_.garbageProbability))
        collect(now);
}

std::optional<Forager> ForagerStore::take(NodeAddress dest, ForagerType type, Time now,
                                          swarm::core::IRng& rng) {
    auto it = entries_.find(dest);
    if (it == entries_.end()) return std::nullopt;
    it->second.lastUsed = now;
    auto& array = it->second.arrays[static_cast<std::size_t>(type)];
    while (!array.empty()) {
        const bool own =
            std::any_of(array.begin(), array.end(), [](const Forager& f) { return f.own; });
        std::vector<std::size_t> candidates;
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (array[i].own == own) candidates.push_back(i);
        }
        const int pick = rng.uniformInt(static_cast<int>(candidates.size()));
        const auto at = array.begin() +
                        static_cast<std::ptrdiff_t>(candidates.at(static_cast<std::size_t>(pick)));
        const Time age = now - at->created;
        if (at->dances > 0 && age < config_.foragerDanceTime) {
            --at->dances;
            return *at;
        }
        Forager used = std::move(*at);
        array.erase(at);
        if (age > config_.foragerLifetime && !array.empty()) continue;
        return used;
    }
    return std::nullopt;
}

void ForagerStore::collect(Time now) {
    std::erase_if(entries_,
                  [&](const auto& e) { return now - e.second.lastUsed > config_.maxAge; });
}

std::size_t ForagerStore::size(NodeAddress dest, ForagerType type) const {
    const auto it = entries_.find(dest);
    return it == entries_.end() ? 0 : it->second.arrays[static_cast<std::size_t>(type)].size();
}

}  // namespace beeadhoc::core
