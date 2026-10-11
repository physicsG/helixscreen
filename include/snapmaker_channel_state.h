// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The Snapmaker U1 filament_feed channel_state vocabulary: one table that the
// AMS backend's status parse and the connect-time batch reconcile both read.
// Compiled on every target, so capability code that only asks "is a feed under
// way" does not depend on the gated backend class.

#include "ams_types.h"

#include <optional>
#include <string>

namespace helix::snapmaker {

// Classification of a single filament_feed channel_state. The firmware exposes
// 39 distinct states (filament_feed.py:34-72, captured live from firmware
// 20260608); this maps each to everything the backend needs so the parse reads
// off ONE table instead of scattered string compares. See
// .claude/scratchpad/u1_channel_state_reference.md for the authoritative table.
//
// Fields:
//  - action:        the AmsAction the operation collapses to (drives the coarse
//                   LOAD/UNLOAD/ERROR/IDLE status). LOADING covers preload/load/
//                   manual feed; UNLOADING covers unload; ERROR covers *_fail;
//                   IDLE covers none/inited/wait_insert/test and every *_finish.
//  - phase:         step-bar step index into get_operation_step_model(op).
//                   Per-direction (a state is unambiguously load/unload/manual by
//                   prefix, so indices never collide across directions):
//                     LOAD/manual/preload model (6 steps, filament_feed.py's order):
//                       0=Home 1=Select 2=Feed 3=Heat 4=Extrude 5=Purge
//                     UNLOAD model (4 steps):
//                       0=Home 1=Select 2=Heat 3=Retract
//                   -1 = "no active step" (idle / *_finish / *_fail).
//  - is_terminal:   a *_finish that ENDS the operation (resolves action -> IDLE).
//                   preload_finish is not one: it is a resting state that also
//                   shows up while the nozzle heats for an unload, so it clears
//                   the latch but leaves the action alone.
//  - is_fail:       a *_fail state, surfaced as ERROR.
//  - sets_loaded:   SET the "loaded at toolhead" latch true (load_finish only).
//  - clears_loaded: CLEAR the latch false (unload_finish/wait_insert/preload_finish).
//  - ignore:        the factory 'test' state; touch nothing.
struct ChannelStateInfo {
    AmsAction action = AmsAction::IDLE;
    int phase = -1;
    bool is_terminal = false;
    bool is_fail = false;
    bool sets_loaded = false;
    bool clears_loaded = false;
    bool ignore = false;
};

[[nodiscard]] ChannelStateInfo classify_channel_state(const std::string& state);

/// True when a channel_state names a load or unload under way.
[[nodiscard]] bool channel_state_in_progress(const std::string& state);

/// Whether a channel field moved from a value already observed. The first value
/// a channel reports belongs to whatever ran before anyone was watching, and a
/// repeat is the same fact again, so neither is an event.
[[nodiscard]] inline bool observed_change(const std::string& prev, const std::string& cur) {
    return !prev.empty() && !cur.empty() && cur != prev;
}

/// The op outcome a resting channel reports only through channel_action_state.
///
/// The firmware sets an op's terminal and its resting state (wait_insert,
/// preload_finish) in one reactor tick, and a status frame carries only the
/// last channel_state. channel_action_state keeps the op's own last step at
/// rest, so it stands in when the channel rests and it moved to a *_finish or
/// *_fail since the previous frame. An unchanged value is the previous op's
/// outcome and says nothing about the current one, and the first value a
/// channel reports is an op that ended before anyone was watching.
///
/// @param channel_state          The channel's current (held) channel_state.
/// @param prev_action_state      channel_action_state before this frame.
/// @param action_state           channel_action_state after this frame.
/// @return The state to drive the op lifecycle with, or nullopt to use channel_state.
[[nodiscard]] std::optional<std::string> settled_op_outcome(const std::string& channel_state,
                                                            const std::string& prev_action_state,
                                                            const std::string& action_state);

} // namespace helix::snapmaker
