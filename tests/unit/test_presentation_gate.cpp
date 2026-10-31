// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Which app surfaces hold back a plugin surface, and how the modal stack
// tells tagged (plugin) entries from app ones.

#include "ui_modal.h"
#include "ui_presentation_gate.h"

#include "../lvgl_ui_test_fixture.h"

#include "../catch_amalgamated.hpp"

using helix::ui::app_blocked;
using helix::ui::AppCovers;

TEST_CASE("app_blocked: any cover blocks, none clears", "[modal][presentation]") {
    CHECK_FALSE(app_blocked(AppCovers{}));

    AppCovers c;
    c.app_modal = true;
    CHECK(app_blocked(c));
    c = {};
    c.external_cover = true;
    CHECK(app_blocked(c));
    c = {};
    c.lock_screen = true;
    CHECK(app_blocked(c));
    c = {};
    c.aborting = true;
    CHECK(app_blocked(c));
    c = {};
    c.busy = true;
    CHECK(app_blocked(c));
}

TEST_CASE_METHOD(LVGLUITestFixture, "has_visible_untagged ignores tagged and exiting entries",
                 "[modal][presentation]") {
    auto& stack = ModalStack::instance();
    CHECK_FALSE(stack.has_visible_untagged());

    lv_obj_t* tagged = Modal::show("print_cancel_confirm_modal");
    REQUIRE(tagged != nullptr);
    stack.set_tag(stack.backdrop_for(tagged), "demo-plugin");
    CHECK_FALSE(stack.has_visible_untagged());

    lv_obj_t* app = Modal::show("print_cancel_confirm_modal");
    REQUIRE(app != nullptr);
    CHECK(stack.has_visible_untagged());

    stack.mark_exiting(stack.backdrop_for(app));
    CHECK_FALSE(stack.has_visible_untagged());
}

TEST_CASE_METHOD(LVGLUITestFixture, "top_visible_tag names the top tagged entry",
                 "[modal][presentation]") {
    auto& stack = ModalStack::instance();
    CHECK(stack.top_visible_tag().empty());

    lv_obj_t* first = Modal::show("print_cancel_confirm_modal");
    stack.set_tag(stack.backdrop_for(first), "first-plugin");
    lv_obj_t* app = Modal::show("print_cancel_confirm_modal");
    REQUIRE(app != nullptr);
    CHECK(stack.top_visible_tag() == "first-plugin");

    lv_obj_t* second = Modal::show("print_cancel_confirm_modal");
    stack.set_tag(stack.backdrop_for(second), "second-plugin");
    CHECK(stack.top_visible_tag() == "second-plugin");

    stack.mark_exiting(stack.backdrop_for(second));
    CHECK(stack.top_visible_tag() == "first-plugin");
}

TEST_CASE_METHOD(LVGLUITestFixture, "current_app_covers reads the modal stack and external covers",
                 "[modal][presentation]") {
    auto& stack = ModalStack::instance();
    CHECK_FALSE(app_blocked(helix::ui::current_app_covers()));

    stack.add_external_cover();
    CHECK(helix::ui::current_app_covers().external_cover);
    CHECK(stack.has_external_cover());
    stack.remove_external_cover();
    CHECK_FALSE(stack.has_external_cover());

    lv_obj_t* app = Modal::show("print_cancel_confirm_modal");
    REQUIRE(app != nullptr);
    CHECK(helix::ui::current_app_covers().app_modal);
}
