// SPDX-License-Identifier: GPL-3.0-or-later
#include "layer_pause.h"

#include "json_utils.h"
#include "printer_discovery.h"

#include <algorithm>

namespace helix::layer_pause {

namespace {

constexpr const char* kNextLayerMacro = "SET_PAUSE_NEXT_LAYER";
constexpr const char* kAtLayerMacro = "SET_PAUSE_AT_LAYER";
constexpr const char* kStateMacro = "SET_PRINT_STATS_INFO";
constexpr const char* kNextLayerVar = "pause_next_layer";
constexpr const char* kAtLayerVar = "pause_at_layer";

} // namespace

bool supported(const PrinterDiscovery& hw) {
    return hw.has_macro(kNextLayerMacro) && hw.has_macro(kAtLayerMacro);
}

std::string state_object(const PrinterDiscovery& hw) {
    if (!supported(hw)) {
        return {};
    }
    const std::string name = hw.macro_config_name(kStateMacro);
    return name.empty() ? std::string{} : "gcode_macro " + name;
}

std::vector<std::string> required_status_objects(const PrinterDiscovery& hw) {
    std::string object = state_object(hw);
    if (object.empty()) {
        return {};
    }
    return {std::move(object)};
}

std::string schedule_next_layer_command() {
    return std::string(kNextLayerMacro) + " ENABLE=1";
}

std::string cancel_next_layer_command() {
    return std::string(kNextLayerMacro) + " ENABLE=0";
}

std::string schedule_at_layer_command(int layer) {
    return std::string(kAtLayerMacro) + " ENABLE=1 LAYER=" + std::to_string(layer);
}

std::string cancel_at_layer_command() {
    return std::string(kAtLayerMacro) + " ENABLE=0";
}

std::optional<Schedule> parse_state(const nlohmann::json& object, const Schedule& previous) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto* next = json_util::find_member(object, kNextLayerVar);
    const auto* at = json_util::find_member(object, kAtLayerVar);
    const bool next_ok = next && next->is_object();
    const bool at_ok = at && at->is_object();
    if (!next_ok && !at_ok) {
        return std::nullopt;
    }
    Schedule out = previous;
    if (next_ok) {
        out.next_layer = json_util::safe_bool(*next, "enable", false);
    }
    if (at_ok) {
        const bool enabled = json_util::safe_bool(*at, "enable", false);
        const int layer = json_util::safe_int(*at, "layer", 0);
        out.at_layer = (enabled && layer > 0) ? layer : 0;
    }
    return out;
}

int pending_layer(const Schedule& schedule, int current_layer) {
    if (schedule.next_layer) {
        return current_layer + 1;
    }
    if (schedule.at_layer > current_layer) {
        return schedule.at_layer;
    }
    return 0;
}

void Tracker::reset_for_new_print() {
    sent_ = Schedule{};
    next_layer_sent_at_ = 0;
}

void Tracker::note_next_layer(bool enable, int current_layer) {
    sent_.next_layer = enable;
    next_layer_sent_at_ = current_layer;
    if (readback_) {
        readback_->next_layer = enable;
    }
}

void Tracker::note_at_layer(int layer) {
    sent_.at_layer = std::max(layer, 0);
    if (readback_) {
        readback_->at_layer = sent_.at_layer;
    }
}

bool Tracker::apply_status(const nlohmann::json& object) {
    const Schedule before = effective();
    auto parsed = parse_state(object, readback_ ? *readback_ : Schedule{});
    if (!parsed) {
        return false;
    }
    readback_ = *parsed;
    return effective() != before;
}

bool Tracker::on_layer(int current_layer) {
    // Only what was sent expires here; read-back, when present, wins in
    // effective() and the macros report their own firing.
    const Schedule before = effective();
    // SET_PAUSE_NEXT_LAYER fires on the next layer report after it was sent.
    if (sent_.next_layer && current_layer > next_layer_sent_at_) {
        sent_.next_layer = false;
    }
    // SET_PAUSE_AT_LAYER fires on the report EQUAL to its layer; once the
    // print is there or past it, it has fired or never will.
    if (sent_.at_layer > 0 && current_layer >= sent_.at_layer) {
        sent_.at_layer = 0;
    }
    return effective() != before;
}

bool Tracker::on_paused(int current_layer) {
    const Schedule before = effective();
    sent_.next_layer = false;
    if (sent_.at_layer == current_layer + 1) {
        sent_.at_layer = 0;
    }
    return effective() != before;
}

Availability classify(bool print_active, bool slicer_reports_layers, int current_layer,
                      int total_layers) {
    if (!print_active) {
        return Availability::Hidden;
    }
    if (!slicer_reports_layers || total_layers <= 0) {
        return Availability::NoLayerData;
    }
    if (current_layer >= total_layers) {
        return Availability::LastLayer;
    }
    return Availability::Ready;
}

LayerBounds layer_bounds(int current_layer, int total_layers) {
    const int min_layer = std::max(current_layer, 0) + 1;
    return {min_layer, std::max(total_layers, min_layer)};
}

} // namespace helix::layer_pause
