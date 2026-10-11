// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display_numbering.h"

#include "ams_state.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/fmt/fmt.h>

#include <cstdio>

namespace helix::ui {

namespace {
/// U+00B7 MIDDLE DOT, the separator between a unit name and its position.
constexpr const char* kUnitSeparator = "\xc2\xb7";

/**
 * @brief The translated plural for @p noun, as a range header spells it.
 *
 * One fixed nominative plural per noun per locale, never derived from how many
 * positions the range covers: Russian would need three forms for a count and
 * agrees with neither end of a range, and a header is not a count anyway.
 */
std::string noun_text_plural(LaneNoun noun) {
    switch (noun) {
    case LaneNoun::Lane:
        return lv_tr("Lanes");
    case LaneNoun::Gate:
        return lv_tr("Gates");
    case LaneNoun::Tool:
        return lv_tr("Tools");
    case LaneNoun::Feeder:
        return lv_tr("Feeders");
    case LaneNoun::Toolhead:
        return lv_tr("Toolheads");
    case LaneNoun::Slot:
        break;
    }
    return lv_tr("Slots");
}
} // namespace

std::string tool_label(int gcode_tool) {
    if (gcode_tool < 0)
        return {};
    return "T" + std::to_string(gcode_tool);
}

bool is_generated_tool_name(std::string_view name) {
    if (name.size() < 2 || name[0] != 'T')
        return false;
    for (size_t i = 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9')
            return false;
    }
    return true;
}

std::string tool_short_label(std::string_view name, int index) {
    if (index < 0)
        return {};
    if (is_generated_tool_name(name))
        return lane_number_text(index);
    return std::string(name);
}

int lane_number(int index) {
    if (index < 0)
        return -1;
    return index + 1;
}

std::string lane_number_text(int index) {
    const int n = lane_number(index);
    if (n < 0)
        return {};
    return std::to_string(n);
}

std::string noun_text(LaneNoun noun) {
    switch (noun) {
    case LaneNoun::Lane:
        return lv_tr("Lane");
    case LaneNoun::Gate:
        return lv_tr("Gate");
    case LaneNoun::Tool:
        return lv_tr("Tool");
    case LaneNoun::Feeder:
        return lv_tr("Feeder");
    case LaneNoun::Toolhead:
        return lv_tr("Toolhead");
    case LaneNoun::Slot:
        break;
    }
    return lv_tr("Slot");
}

std::string lane_label(LaneNoun noun, int index) {
    const int n = lane_number(index);
    if (n < 0)
        return {};
    return noun_text(noun) + " " + std::to_string(n);
}

std::string backend_lane_label(const helix::AmsBackend& backend, int slot_index) {
    if (slot_index < 0)
        return {};
    // spool_display_number() is already the 1-based number shown, so it is
    // spelled directly rather than stepped back through lane_number().
    return noun_text(backend.lane_noun()) + " " +
           std::to_string(backend.spool_display_number(slot_index));
}

std::string clear_spool_blocked_hint(LaneNoun noun, int index) {
    // lv_tr() returns the format string in the active locale; fmt::format
    // substitutes the label, the same composition NOTIFY_INFO uses for the
    // "{} spool cleared" toast.
    return fmt::format(lv_tr("{} is feeding the current print. Clear it after the print ends."),
                       lane_label(noun, index));
}

std::string lane_label(LaneNoun noun, std::string_view unit_display_name, int index) {
    if (unit_display_name.empty())
        return lane_label(noun, index);
    const std::string body = lane_label(noun, index);
    if (body.empty())
        return {};
    return std::string(unit_display_name) + " " + kUnitSeparator + " " + body;
}

std::string lane_range_label(LaneNoun noun, int first_index, int last_index) {
    const int first = lane_number(first_index);
    const int last = lane_number(last_index);
    if (first < 0 || last < 0 || last < first)
        return {};
    // "Slots 3-3" is not how anyone says it, and a plural header over one
    // position is wrong in every locale that inflects.
    if (first == last)
        return lane_label(noun, first_index);
    return noun_text_plural(noun) + " " + std::to_string(first) + "-" + std::to_string(last);
}

std::string lane_count_label(LaneNoun noun, int count) {
    // Wide enough for the longest form: ru "Печатающие головки" is 35 bytes.
    // Each lv_tr() sits inside its snprintf so the format-specifier gate sees
    // the pair - a format held in a variable between the two is invisible to
    // it. No default: a new enumerator must fail to compile here, not pass a
    // null format to snprintf.
    char buf[64];
    switch (noun) {
    case LaneNoun::Lane:
        snprintf(buf, sizeof(buf), lv_tr("%d lanes"), count);
        return buf;
    case LaneNoun::Gate:
        snprintf(buf, sizeof(buf), lv_tr("%d gates"), count);
        return buf;
    case LaneNoun::Tool:
        snprintf(buf, sizeof(buf), lv_tr("%d tools"), count);
        return buf;
    case LaneNoun::Feeder:
        snprintf(buf, sizeof(buf), lv_tr("%d feeders"), count);
        return buf;
    case LaneNoun::Toolhead:
        snprintf(buf, sizeof(buf), lv_tr("%d toolheads"), count);
        return buf;
    case LaneNoun::Slot:
        snprintf(buf, sizeof(buf), lv_tr("%d slots"), count);
        return buf;
    }
    return {};
}

LaneNoun active_lane_noun() {
    auto& ams = AmsState::instance();
    const auto* backend = ams.get_backend(ams.active_backend_index());
    return backend ? backend->lane_noun() : LaneNoun::Slot;
}

LaneNoun active_tool_noun() {
    auto& ams = AmsState::instance();
    const auto* backend = ams.get_backend(ams.active_backend_index());
    return backend ? backend->tool_noun() : LaneNoun::Tool;
}

} // namespace helix::ui
