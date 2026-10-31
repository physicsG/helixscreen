// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
// TEST_MIRROR_OK: the code under test is a test helper
// (tests/test_helpers/scoped_xml_component_stub.h) against the real ui_xml component.

#include "../test_fixtures.h"
#include "../test_helpers/scoped_xml_component_stub.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"

#include "../catch_amalgamated.hpp"

namespace {

constexpr const char* kComponent = "panel_widget_firmware_restart";
constexpr const char* kRealChild = "firmware_restart_button_home";

/// Whether a fresh instance of the component carries the real tile's button.
bool builds_real_tile(lv_obj_t* parent) {
    auto* obj = static_cast<lv_obj_t*>(lv_xml_create(parent, kComponent, nullptr));
    REQUIRE(obj != nullptr);
    const bool real = lv_obj_find_by_name(obj, kRealChild) != nullptr;
    lv_obj_delete(obj);
    return real;
}

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "a scoped component stub gives the real component back",
                 "[xml][scoped_stub]") {
    REQUIRE(builds_real_tile(test_screen()));
    {
        helix::test::ScopedXmlComponentStub stub(kComponent);
        CHECK_FALSE(builds_real_tile(test_screen()));
    }
    CHECK(builds_real_tile(test_screen()));
}
