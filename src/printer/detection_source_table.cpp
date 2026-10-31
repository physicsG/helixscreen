// SPDX-License-Identifier: GPL-3.0-or-later
#include "detection_source_table.h"

#include "k2_stock_detection_source.h"
#include "settings_manager.h"
#include "u1_stock_detection_source.h"

namespace helix::detection {

namespace {

std::unique_ptr<DetectionSource> build_u1(helix::PrinterState* state) {
    auto src = std::make_unique<U1StockSource>(state);
    src->start();
    return src;
}

std::optional<DetectionPolicy> u1_policy() {
    return static_cast<DetectionPolicy>(SettingsManager::instance().get_detection_policy_u1());
}

// Replaces the stock camera loop the installer disables. Probes capability
// itself (K2 + /usr/bin/detection) and stays inert elsewhere.
std::unique_ptr<DetectionSource> build_k2(helix::PrinterState* state) {
    auto src = std::make_unique<K2StockDetectionSource>(state);
    src->start();
    return src;
}

std::optional<DetectionPolicy> no_policy() {
    return std::nullopt;
}

} // namespace

const std::vector<SourceTableEntry>& builtin_sources() {
    static const std::vector<SourceTableEntry> kSources = {
        {build_u1, u1_policy},
        {build_k2, no_policy},
    };
    return kSources;
}

void register_builtin_sources(DetectionManager& dm, helix::PrinterState* state) {
    for (const auto& entry : builtin_sources()) {
        auto src = entry.build(state);
        if (!src) {
            continue;
        }
        const std::string id = src->id();
        dm.register_source(std::move(src));
        if (const auto p = entry.stored_policy()) {
            dm.set_policy(id, *p);
        }
    }
}

} // namespace helix::detection
