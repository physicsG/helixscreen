// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_types.h"

#include <optional>

namespace helix {

/// Everything the "open the slot editor for a freshly inserted spool" decision
/// reads. Presence is tri-state because the machine may state nothing: a slot
/// whose status is UNKNOWN, or a sensor that has not reported yet, has no
/// reading, and no reading is never an "empty" half of a transition.
struct FilamentInsertContext {
    std::optional<bool> was_present; ///< Last stated presence (nullopt = no reading yet)
    std::optional<bool> is_present;  ///< Presence now (nullopt = no reading)
    bool print_active = false;       ///< PRINTING or PAUSED
    bool operation_busy = false;     ///< A load/unload/tool change is running or just finished
    bool setting_enabled = false;    ///< The user opted in
    bool editor_open = false;        ///< The slot editor is already on screen
};

/// Presence the ENTRY and TOOLHEAD sensors state together, from their detected
/// subjects (1 = filament, 0 = none, -1 = no sensor). Any sensor seeing filament
/// wins, otherwise any sensor stating none; with no sensor there is no reading.
[[nodiscard]] constexpr std::optional<bool> manual_load_presence(int entry, int toolhead) {
    if (entry == 1 || toolhead == 1)
        return true;
    if (entry == 0 || toolhead == 0)
        return false;
    return std::nullopt;
}

/// True only for a stated-empty to stated-present edge while the user opted in
/// and nothing else owns the filament: the first reading after startup or a
/// reconnect has no prior reading, so a snapshot of an occupied slot is never
/// an insertion.
[[nodiscard]] constexpr bool should_open_editor_on_insert(const FilamentInsertContext& c) {
    return c.setting_enabled && c.was_present == false && c.is_present == true && !c.print_active &&
           !c.operation_busy && !c.editor_open;
}

/// What a lane's insert does to the slot editor once its presence edge passed
/// should_open_editor_on_insert().
enum class InsertEditorAction { Ignore, Open, Wait };

/// A lane with a tag reader settles who identifies an inserted spool: a tag
/// names it, so the editor opens only for a spool the reader found no tag on.
/// @p waiting is true for an edge already held for the reader's verdict; a
/// held insert the backend dropped (NotRead) is not asked about.
[[nodiscard]] constexpr InsertEditorAction lane_insert_action(InsertIdentity identity,
                                                              bool waiting) {
    switch (identity) {
    case InsertIdentity::Pending:
        return InsertEditorAction::Wait;
    case InsertIdentity::FromTag:
        return InsertEditorAction::Ignore;
    case InsertIdentity::NoTag:
        return InsertEditorAction::Open;
    case InsertIdentity::NotRead:
        break;
    }
    return waiting ? InsertEditorAction::Ignore : InsertEditorAction::Open;
}

} // namespace helix
