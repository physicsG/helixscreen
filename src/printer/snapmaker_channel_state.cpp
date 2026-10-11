// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapmaker_channel_state.h"

#include <spdlog/spdlog.h>

#include <string_view>
#include <unordered_map>

namespace helix::snapmaker {

[[nodiscard]] ChannelStateInfo classify_channel_state(const std::string& state) {
    // One row per firmware state. Exact-match lookup: unambiguous and reads
    // directly off the reference table. Unknown/future states fall through to
    // the prefix/suffix heuristic below so we degrade gracefully rather than
    // silently mis-classify.
    static const std::unordered_map<std::string, ChannelStateInfo> TABLE = [] {
        std::unordered_map<std::string, ChannelStateInfo> m;
        auto add = [&](const char* s, ChannelStateInfo info) { m.emplace(s, info); };
        constexpr auto LOAD = AmsAction::LOADING;
        constexpr auto UNLOAD = AmsAction::UNLOADING;
        constexpr auto IDLE = AmsAction::IDLE;
        constexpr auto ERR = AmsAction::ERROR;
        // {action, phase, is_terminal, is_fail, sets_loaded, clears_loaded, ignore}
        // --- idle / init ---
        add("none", {IDLE, -1, false, false, false, false, false});
        add("inited", {IDLE, -1, false, false, false, false, false});
        add("wait_insert", {IDLE, -1, false, false, false, /*clear=*/true, false});
        add("test", {IDLE, -1, false, false, false, false, /*ignore=*/true});
        // --- preload (stage insert -> gear, NOT to nozzle) ---
        add("preload_prepare", {LOAD, 0, false, false, false, false, false});
        add("preload_feeding", {LOAD, 2, false, false, false, false, false});
        add("preload_finish", {IDLE, -1, false, false, false, /*clear=*/true, false});
        add("preload_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        // --- load (feed to nozzle) ---
        add("load_prepare", {LOAD, 0, false, false, false, false, false});
        add("load_homing", {LOAD, 0, false, false, false, false, false});
        add("load_picking", {LOAD, 1, false, false, false, false, false});
        add("load_feeding", {LOAD, 2, false, false, false, false, false});
        add("load_heating", {LOAD, 3, false, false, false, false, false});
        add("load_extruding", {LOAD, 4, false, false, false, false, false});
        add("load_flushing", {LOAD, 5, false, false, false, false, false});
        add("load_finish", {IDLE, -1, /*terminal=*/true, false, /*set=*/true, false, false});
        add("load_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        // --- unload (retract from nozzle) ---
        add("unload_prepare", {UNLOAD, 0, false, false, false, false, false});
        add("unload_homing", {UNLOAD, 0, false, false, false, false, false});
        add("unload_picking", {UNLOAD, 1, false, false, false, false, false});
        add("unload_heating", {UNLOAD, 2, false, false, false, false, false});
        add("unload_heat_finish", {UNLOAD, 2, false, false, false, false, false});
        add("unload_doing", {UNLOAD, 3, false, false, false, false, false});
        add("unload_finish", {IDLE, -1, /*terminal=*/true, false, false, /*clear=*/true, false});
        add("unload_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        // --- manual feed (MANUAL_FEEDING) ---
        add("manual_sta_prepare", {LOAD, 0, false, false, false, false, false});
        add("manual_sta_homing", {LOAD, 0, false, false, false, false, false});
        add("manual_sta_picking", {LOAD, 1, false, false, false, false, false});
        add("manual_sta_prepare_finish", {LOAD, 1, false, false, false, false, false});
        add("manual_sta_prepare_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        add("manual_sta_heating", {LOAD, 3, false, false, false, false, false});
        add("manual_sta_extruding", {LOAD, 4, false, false, false, false, false});
        add("manual_sta_extrude_finish", {LOAD, 4, false, false, false, false, false});
        add("manual_sta_extrude_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        add("manual_sta_flushing", {LOAD, 5, false, false, false, false, false});
        add("manual_sta_flush_finish", {LOAD, 5, false, false, false, false, false});
        add("manual_sta_flush_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        // manual_sta_finish is a completed manual EXTRUDE, not a load; it ends
        // the op (IDLE) but does NOT set the loaded latch.
        add("manual_sta_finish", {IDLE, -1, /*terminal=*/true, false, false, false, false});
        add("manual_sta_fail", {ERR, -1, false, /*fail=*/true, false, false, false});
        return m;
    }();

    auto it = TABLE.find(state);
    if (it != TABLE.end()) {
        return it->second;
    }

    // Fallback for an unrecognized state (firmware drift). Never emitted by
    // firmware 20260608, but classify conservatively so a future state can't
    // wedge the action machine. Prefix chooses the family; suffix the phase.
    ChannelStateInfo info;
    auto ends_with = [&](std::string_view suffix) {
        return state.size() > suffix.size() &&
               state.compare(state.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const bool is_unload = state.rfind("unload_", 0) == 0;
    const bool is_load =
        !is_unload && (state.rfind("load_", 0) == 0 || state.rfind("preload_", 0) == 0 ||
                       state.rfind("manual_sta_", 0) == 0);
    if (ends_with("_fail")) {
        info.action = AmsAction::ERROR;
        info.is_fail = true;
    } else if (ends_with("_finish")) {
        info.action = AmsAction::IDLE;
        info.is_terminal = true;
    } else if (is_unload) {
        info.action = AmsAction::UNLOADING;
    } else if (is_load) {
        info.action = AmsAction::LOADING;
    } else {
        info.action = AmsAction::IDLE;
    }
    if (info.action == AmsAction::LOADING || info.action == AmsAction::UNLOADING) {
        // Mirrors the per-direction step models. Load, manual and preload:
        // Home 0, Select 1, Feed 2, Heat 3, Extrude 4, Purge 5. Unload: Home 0,
        // Select 1, Heat 2, Retract 3.
        if (ends_with("_homing") || ends_with("_prepare"))
            info.phase = 0;
        else if (ends_with("_picking"))
            info.phase = 1;
        else if (is_unload)
            info.phase = ends_with("_heating") ? 2 : 3;
        else if (ends_with("_feeding"))
            info.phase = 2;
        else if (ends_with("_heating"))
            info.phase = 3;
        else if (ends_with("_extruding"))
            info.phase = 4;
        else if (ends_with("_flushing"))
            info.phase = 5;
    }
    spdlog::debug("[AmsBackendSnapmaker] unrecognized channel_state '{}' -> fallback action={} "
                  "phase={}",
                  state, ams_action_to_string(info.action), info.phase);
    return info;
}

bool channel_state_in_progress(const std::string& state) {
    const AmsAction action = classify_channel_state(state).action;
    return action == AmsAction::LOADING || action == AmsAction::UNLOADING;
}

std::optional<std::string> settled_op_outcome(const std::string& channel_state,
                                              const std::string& prev_action_state,
                                              const std::string& action_state) {
    if (channel_state.empty() || !observed_change(prev_action_state, action_state)) {
        return std::nullopt;
    }
    const ChannelStateInfo resting = classify_channel_state(channel_state);
    if (resting.action != AmsAction::IDLE || resting.is_terminal || resting.ignore) {
        return std::nullopt;
    }
    const ChannelStateInfo outcome = classify_channel_state(action_state);
    if (!outcome.is_terminal && !outcome.is_fail) {
        return std::nullopt;
    }
    return action_state;
}

} // namespace helix::snapmaker
