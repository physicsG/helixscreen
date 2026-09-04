// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_excluded_objects_state.h"

#include "../catch_amalgamated.hpp"

using Catch::Approx;
using namespace helix;

TEST_CASE("Object geometry storage and retrieval", "[exclude_object][geometry]") {
    PrinterExcludedObjectsState state;
    state.init_subjects();

    SECTION("set and get object geometry") {
        std::vector<PrinterExcludedObjectsState::ObjectInfo> objects = {
            {"part_1", {100.0f, 100.0f}, {80.0f, 80.0f}, {120.0f, 120.0f}, {}, true, true},
            {"part_2", {200.0f, 150.0f}, {180.0f, 130.0f}, {220.0f, 170.0f}, {}, true, true},
        };
        state.set_defined_objects_with_geometry(objects);

        auto defined = state.get_defined_objects();
        REQUIRE(defined.size() == 2);

        auto geom = state.get_object_geometry("part_1");
        REQUIRE(geom.has_value());
        REQUIRE(geom->center.x == Approx(100.0f));
        REQUIRE(geom->bbox_min.x == Approx(80.0f));
        REQUIRE(geom->bbox_max.x == Approx(120.0f));
    }

    SECTION("unknown object returns nullopt") {
        auto geom = state.get_object_geometry("nonexistent");
        REQUIRE_FALSE(geom.has_value());
    }

    SECTION("object without geometry flags") {
        std::vector<PrinterExcludedObjectsState::ObjectInfo> objects = {
            {"no_geom", {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {}, false, false},
        };
        state.set_defined_objects_with_geometry(objects);
        auto geom = state.get_object_geometry("no_geom");
        REQUIRE(geom.has_value());
        REQUIRE_FALSE(geom->has_center);
        REQUIRE_FALSE(geom->has_bbox);
    }

    SECTION("version bumps on geometry update") {
        int v1 = lv_subject_get_int(state.get_defined_objects_version_subject());
        std::vector<PrinterExcludedObjectsState::ObjectInfo> objects = {
            {"obj_a", {50.0f, 50.0f}, {10.0f, 10.0f}, {90.0f, 90.0f}, {}, true, true},
        };
        state.set_defined_objects_with_geometry(objects);
        int v2 = lv_subject_get_int(state.get_defined_objects_version_subject());
        REQUIRE(v2 > v1);
    }

    state.deinit_subjects();
}
