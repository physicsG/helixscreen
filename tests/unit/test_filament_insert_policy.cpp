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
