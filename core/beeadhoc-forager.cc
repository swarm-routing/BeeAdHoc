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

ForagerStore::ForagerStore(const Config& config, swarm::core::IRouterObserver* observer)
    : config_(config), observer_(observer) {}

ForagerStore::Footprint ForagerStore::footprint() const {
    Footprint f;
    for (const auto& [dest, entry] : entries_) {
        f.bytes += 12;
        for (const auto& array : entry.arrays) {
            for (const Forager& fg : array) {
                ++f.entries;
                f.bytes += 17 + 4 * static_cast<std::uint32_t>(fg.route.size());
            }
        }
    }
    return f;
}

void ForagerStore::added(NodeAddress dest, const Forager& f) {
    if (observer_)
        observer_->onRouteChanged(
            dest, f.route.empty() ? swarm::core::kInvalidAddress : f.route.front(), true);
}

void ForagerStore::removed(NodeAddress dest, const Forager& f) {
    if (observer_)
        observer_->onRouteChanged(
            dest, f.route.empty() ? swarm::core::kInvalidAddress : f.route.front(), false);
}

void ForagerStore::reportState() {
    if (!observer_) return;
    const Footprint now = footprint();
    if (reportedAny_ && now.entries == reported_.entries && now.bytes == reported_.bytes) return;
    reported_ = now;
    reportedAny_ = true;
    observer_->onStateSize(now.entries, now.bytes);
}

void ForagerStore::put(NodeAddress dest, ForagerType type, Forager f, Time now,
                       swarm::core::IRng& rng) {
    auto it = entries_.find(dest);
    if (it == entries_.end()) {
        if (entries_.size() >= std::max<std::size_t>(config_.maxDestinations, 1)) {
            auto lru = std::min_element(
                entries_.begin(), entries_.end(),
                [](const auto& a, const auto& b) { return a.second.lastUsed < b.second.lastUsed; });
            for (const auto& array : lru->second.arrays)
                for (const Forager& old : array)
                    removed(lru->first, old);
            entries_.erase(lru);
        }
        it = entries_.emplace(dest, Entry{}).first;
    }
    auto& array = it->second.arrays[static_cast<std::size_t>(type)];
    if (array.size() >= std::max<std::size_t>(config_.foragerArraySize, 1)) {
        removed(dest, array.front());
        array.erase(array.begin());
    }
    added(dest, f);
    array.push_back(std::move(f));
    it->second.lastUsed = now;

    if (config_.garbageProbability >= 1.0 ||
        (config_.garbageProbability > 0.0 && rng.uniform() < config_.garbageProbability))
        collect(now);
    reportState();
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
        removed(dest, used);
        if (age > config_.foragerLifetime && !array.empty()) continue;
        reportState();
        return used;
    }
    reportState();
    return std::nullopt;
}

std::optional<Forager> ForagerStore::takeForeign(NodeAddress dest, ForagerType type, Time now) {
    auto it = entries_.find(dest);
    if (it == entries_.end()) return std::nullopt;
    auto& array = it->second.arrays[static_cast<std::size_t>(type)];
    // The array is in arrival order (put appends): the first foreign is the oldest.
    const auto at =
        std::find_if(array.begin(), array.end(), [](const Forager& f) { return !f.own; });
    if (at == array.end()) return std::nullopt;
    it->second.lastUsed = now;
    Forager home = std::move(*at);
    array.erase(at);
    removed(dest, home);
    reportState();
    return home;
}

std::size_t ForagerStore::eraseFirstHop(NodeAddress neighbor) {
    std::size_t n = 0;
    for (auto& [dest, entry] : entries_) {
        for (auto& array : entry.arrays) {
            for (auto it = array.begin(); it != array.end();) {
                if (!it->route.empty() && it->route.front() == neighbor) {
                    removed(dest, *it);
                    it = array.erase(it);
                    ++n;
                } else {
                    ++it;
                }
            }
        }
    }
    // A destination left with no forager stays until MAXAGE collects it.
    if (n > 0) reportState();
    return n;
}

void ForagerStore::collect(Time now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.lastUsed > config_.maxAge) {
            for (const auto& array : it->second.arrays)
                for (const Forager& old : array)
                    removed(it->first, old);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    reportState();
}

std::size_t ForagerStore::size(NodeAddress dest, ForagerType type) const {
    const auto it = entries_.find(dest);
    return it == entries_.end() ? 0 : it->second.arrays[static_cast<std::size_t>(type)].size();
}

std::size_t ForagerStore::count(NodeAddress dest, ForagerType type, bool own) const {
    const auto it = entries_.find(dest);
    if (it == entries_.end()) return 0;
    const auto& array = it->second.arrays[static_cast<std::size_t>(type)];
    return static_cast<std::size_t>(std::count_if(
        array.begin(), array.end(), [own](const Forager& f) { return f.own == own; }));
}

}  // namespace beeadhoc::core
