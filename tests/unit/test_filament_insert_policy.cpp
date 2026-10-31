// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_insert_policy.h"

#include "../catch_amalgamated.hpp"

using helix::FilamentInsertContext;
using helix::should_open_editor_on_insert;

namespace {
FilamentInsertContext insertion() {
    FilamentInsertContext c;
    c.was_present = false;
    c.is_present = true;
    c.setting_enabled = true;
    return c;
}
} // namespace

TEST_CASE("insert policy: empty to present with the setting on opens the editor",
          "[filament][insert_policy]") {
    REQUIRE(should_open_editor_on_insert(insertion()));
}

TEST_CASE("insert policy: every guard suppresses", "[filament][insert_policy]") {
    auto c = insertion();
    SECTION("setting off") {
        c.setting_enabled = false;
    }
    SECTION("print active or paused") {
        c.print_active = true;
    }
    SECTION("operation in progress") {
        c.operation_busy = true;
    }
    SECTION("editor already open") {
        c.editor_open = true;
    }
    REQUIRE_FALSE(should_open_editor_on_insert(c));
}

TEST_CASE("insert policy: only a stated-empty to stated-present edge counts",
          "[filament][insert_policy]") {
    auto c = insertion();
    SECTION("first reading after startup or reconnect has no prior state") {
        c.was_present = std::nullopt;
    }
    SECTION("already present") {
        c.was_present = true;
    }
    SECTION("removal") {
        c.was_present = true;
        c.is_present = false;
    }
    SECTION("no reading now") {
        c.is_present = std::nullopt;
    }
    SECTION("still empty") {
        c.is_present = false;
    }
    REQUIRE_FALSE(should_open_editor_on_insert(c));
}

TEST_CASE("insert policy: manual load presence combines the entry and toolhead readings",
          "[filament][insert_policy]") {
    using helix::manual_load_presence;
    REQUIRE(manual_load_presence(1, 0) == true);
    REQUIRE(manual_load_presence(0, 1) == true);
    REQUIRE(manual_load_presence(-1, 1) == true);
    REQUIRE(manual_load_presence(0, -1) == false);
    REQUIRE(manual_load_presence(0, 0) == false);
    REQUIRE_FALSE(manual_load_presence(-1, -1).has_value());
}

TEST_CASE("insert policy: a lane's tag reader settles who identifies the spool",
          "[filament][insert_policy][entry_sensor]") {
    using helix::InsertEditorAction;
    using helix::InsertIdentity;
    using helix::lane_insert_action;
    SECTION("at the insert edge") {
        // No reader on the lane: the edge alone decides.
        CHECK(lane_insert_action(InsertIdentity::NotRead, false) == InsertEditorAction::Open);
        // The reader has not answered yet: hold the edge for its verdict.
        CHECK(lane_insert_action(InsertIdentity::Pending, false) == InsertEditorAction::Wait);
        // Answered in the same frame.
        CHECK(lane_insert_action(InsertIdentity::FromTag, false) == InsertEditorAction::Ignore);
        CHECK(lane_insert_action(InsertIdentity::NoTag, false) == InsertEditorAction::Open);
    }
    SECTION("while an edge waits on the reader") {
        CHECK(lane_insert_action(InsertIdentity::Pending, true) == InsertEditorAction::Wait);
        // The tag named the spool: nothing to ask.
        CHECK(lane_insert_action(InsertIdentity::FromTag, true) == InsertEditorAction::Ignore);
        // The reader found no tag: the person identifies it.
        CHECK(lane_insert_action(InsertIdentity::NoTag, true) == InsertEditorAction::Open);
        // The backend dropped the insert (a firmware feed, the spool left).
        CHECK(lane_insert_action(InsertIdentity::NotRead, true) == InsertEditorAction::Ignore);
    }
}
