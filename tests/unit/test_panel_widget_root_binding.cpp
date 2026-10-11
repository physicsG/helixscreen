// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// The tile root's user_data back-pointer belongs to the base: it is bound before
// attach() and cleared after detach(), on every reuse, so a widget never sets it.

#include "../lvgl_test_fixture.h"
#include "panel_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

struct RootSpy : PanelWidget {
    void* user_data_in_attach = nullptr;
    lv_obj_t* root_in_attach = nullptr;
    void* user_data_in_detach = nullptr;
    int attaches = 0;
    int sizes = 0;
    int last_w = 0;

    void attach(lv_obj_t* obj, lv_obj_t*) override {
        ++attaches;
        root_in_attach = root();
        user_data_in_attach = lv_obj_get_user_data(obj);
    }
    void detach() override {
        user_data_in_detach = root() ? lv_obj_get_user_data(root()) : nullptr;
    }
    void on_size_changed(int, int, int w, int) override {
        ++sizes;
        last_w = w;
    }
    const char* id() const override {
        return "root_spy";
    }
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "PanelWidget binds the root around attach and detach",
                 "[panel_widget][root_binding]") {
    RootSpy w;
    lv_obj_t* a = lv_obj_create(test_screen());

    w.attach_tile(a, test_screen());
    CHECK(w.root() == a);
    CHECK(w.root_in_attach == a);
    CHECK(w.user_data_in_attach == &w);
    CHECK(lv_obj_get_user_data(a) == &w);

    w.detach_tile();
    CHECK(w.user_data_in_detach == &w);
    CHECK(w.root() == nullptr);
    CHECK(lv_obj_get_user_data(a) == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "PanelWidget rebinds a recycled instance to the new root",
                 "[panel_widget][root_binding]") {
    RootSpy w;
    lv_obj_t* a = lv_obj_create(test_screen());
    lv_obj_t* b = lv_obj_create(test_screen());

    w.attach_tile(a, test_screen());
    w.detach_tile();
    w.attach_tile(b, test_screen());

    CHECK(w.attaches == 2);
    CHECK(w.user_data_in_attach == &w);
    CHECK(lv_obj_get_user_data(b) == &w);
    CHECK(lv_obj_get_user_data(a) == nullptr);
    w.detach_tile();
}

TEST_CASE_METHOD(LVGLTestFixture, "PanelWidget root survives a raw tree delete before detach",
                 "[panel_widget][root_binding]") {
    RootSpy w;
    lv_obj_t* a = lv_obj_create(test_screen());
    w.attach_tile(a, test_screen());

    lv_obj_delete(a);
    CHECK(w.root() == nullptr);
    w.detach_tile(); // must not touch the freed object
    CHECK(w.root() == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "PanelWidget size announcements reach a bound widget",
                 "[panel_widget][root_binding]") {
    RootSpy w;
    lv_obj_t* a = lv_obj_create(test_screen());
    w.attach_tile(a, test_screen());
    w.notify_size_changed(2, 2, 123, 45);
    CHECK(w.sizes == 1);
    CHECK(w.last_w == 123);
    w.detach_tile();
}
