// SPDX-License-Identifier: GPL-3.0-or-later

#include "power_loss_sensor.h"

#include "printer_discovery.h"

#include <algorithm>

namespace helix::power_loss {
namespace {

/// One firmware that publishes an incoming-mains monitor.
struct Provider {
    const char* name;
    /// Printer object whose presence identifies the firmware. Matched exactly:
    /// the firmware also publishes `power_loss_check e0`..`e3`, one per
    /// extruder, all uninitialised, and none of them may extend the match.
    const char* detect_object;
    /// Object carrying the monitor's fields: initialized, high_level_tick,
    /// low_level_tick, voltage_type, power_loss_flag, duty_percent.
    const char* status_object;
};

const std::vector<Provider>& providers() {
    static const std::vector<Provider> kProviders = {
        {"Snapmaker U1", "power_loss_check", "power_loss_check"},
    };
    return kProviders;
}

const Provider* provider_for(const PrinterDiscovery& hw) {
    const auto& objects = hw.printer_objects();
    for (const auto& p : providers()) {
        if (std::find(objects.begin(), objects.end(), p.detect_object) != objects.end()) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

bool firmware_reports_power_loss(const PrinterDiscovery& hw) {
    return provider_for(hw) != nullptr;
}

std::vector<std::string> required_status_objects(const PrinterDiscovery& hw) {
    const Provider* p = provider_for(hw);
    return p ? std::vector<std::string>{p->status_object} : std::vector<std::string>{};
}

std::optional<bool> power_loss_asserted(const PrinterDiscovery& hw, const nlohmann::json& status) {
    const Provider* p = provider_for(hw);
    if (!p || !status.is_object()) {
        return std::nullopt;
    }
    auto obj = status.find(p->status_object);
    if (obj == status.end() || !obj->is_object()) {
        return std::nullopt;
    }
    // The shape of a printer's payload is data, not a contract violation to
    // throw on: every field is type-guarded and anything else reads as silence.
    auto initialized = obj->find("initialized");
    if (initialized == obj->end() || !initialized->is_number_integer() ||
        initialized->get<int>() == 0) {
        return std::nullopt;
    }
    auto flag = obj->find("power_loss_flag");
    if (flag == obj->end() || !flag->is_number_integer()) {
        return std::nullopt;
    }
    return flag->get<int>() != 0;
}

} // namespace helix::power_loss
