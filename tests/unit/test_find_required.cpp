// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../lvgl_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "ui/ui_widget_helpers.h"

#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

#include "../catch_amalgamated.hpp"

using helix::ui::find_optional;
using helix::ui::find_required;

namespace {

/// Strict checks off for the scope; the fixture turns them on for every test.
struct LenientChecks {
    LenientChecks() {
        helix::ui::set_strict_ui_checks(false);
    }
    ~LenientChecks() {
        helix::ui::set_strict_ui_checks(true);
    }
};

lv_obj_t* named_child(lv_obj_t* parent, const char* name) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_name(obj, name);
    return obj;
}

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "find_required returns a present widget, nested or not",
                 "[find_required]") {
    lv_obj_t* row = named_child(test_screen(), "row");
    lv_obj_t* label = named_child(row, "value_label");
    CHECK(find_required(test_screen(), "row", "Test") == row);
    CHECK(find_required(find_required(test_screen(), "row", "Test"), "value_label", "Test") ==
          label);
    CHECK(find_optional(test_screen(), "value_label") == label);
}

TEST_CASE_METHOD(LVGLTestFixture, "find_required logs a missing name once per owner and name",
                 "[find_required]") {
    LenientChecks lenient;
    helix::LogCapture log;

    CHECK(find_required(test_screen(), "fr_missing_a", "OwnerA") == nullptr);
    CHECK(find_required(test_screen(), "fr_missing_a", "OwnerA") == nullptr);
    CHECK(find_required(test_screen(), "fr_missing_a", "OwnerB") == nullptr);

    CHECK(log.count_containing("[OwnerA] required widget 'fr_missing_a'") == 1);
    CHECK(log.count_containing("[OwnerB] required widget 'fr_missing_a'") == 1);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "find_required and find_optional pass a null root through silently",
                 "[find_required]") {
    helix::LogCapture log;
    // Strict is on here: a null root must not abort, or nested lookups would
    // report the same missing parent twice.
    CHECK(find_required(nullptr, "anything", "Owner") == nullptr);
    CHECK(find_optional(nullptr, "anything") == nullptr);
    CHECK(find_optional(test_screen(), "fr_never_there") == nullptr);
    CHECK(log.count_containing("required widget") == 0);
}

TEST_CASE_METHOD(LVGLTestFixture, "find_required aborts on a missing name under strict checks",
                 "[find_required][subprocess]") {
    std::fflush(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        std::signal(SIGABRT, SIG_DFL); // Catch2's handler would report instead of dying
        helix::ui::set_strict_ui_checks(true);
        find_required(test_screen(), "fr_strict_missing", "Owner");
        _exit(0);
    }
    REQUIRE(pid > 0);
    int status = 0;
    REQUIRE(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
}
