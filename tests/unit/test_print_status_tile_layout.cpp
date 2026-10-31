// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_status_tile_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
PrintStatusTileLayout L(int w, int h) {
    return decide_print_status_tile_layout(w, h, 8, 48, 96);
}
} // namespace

TEST_CASE("tile layout 724x414 is side by side with a row strip", "[print_status_tile_layout]") {
    auto l = L(724, 414);
    CHECK(l.arrangement == PrintStatusArrangement::SideBySide);
    CHECK_FALSE(l.strip_stacked);
    CHECK_FALSE(l.checklist_compact);
    CHECK(l.job_w + l.printer_w + 8 == 724);
}

TEST_CASE("tile layout 426x279 is side by side and full", "[print_status_tile_layout]") {
    auto l = L(426, 279);
    CHECK(l.arrangement == PrintStatusArrangement::SideBySide);
    CHECK_FALSE(l.strip_stacked);
    CHECK_FALSE(l.checklist_compact);
}

TEST_CASE("tile layout 438x224 is compact", "[print_status_tile_layout]") {
    auto l = L(438, 224);
    CHECK(l.arrangement == PrintStatusArrangement::SideBySide);
    CHECK_FALSE(l.strip_stacked);
    CHECK(l.checklist_compact);
}

TEST_CASE("tile layout 480x610 stacks", "[print_status_tile_layout]") {
    auto l = L(480, 610);
    CHECK(l.arrangement == PrintStatusArrangement::Stacked);
    CHECK(l.strip_stacked);
    CHECK(l.strip_h == 96);
    CHECK(l.job_w == 480);
    CHECK(l.job_h + l.printer_h + 8 == 610 - 96 - 8);
    CHECK(l.job_h > l.printer_h);
}

TEST_CASE("tile layout narrow panels stack the strip and go compact",
          "[print_status_tile_layout]") {
    for (int w : {304, 256}) {
        auto l = L(w, 346);
        CHECK(l.arrangement == PrintStatusArrangement::SideBySide);
        CHECK(l.strip_stacked);
        CHECK(l.checklist_compact);
    }
}

TEST_CASE("tile layout 920x528 is side by side and full", "[print_status_tile_layout]") {
    auto l = L(920, 528);
    CHECK(l.arrangement == PrintStatusArrangement::SideBySide);
    CHECK_FALSE(l.checklist_compact);
}

TEST_CASE("tile layout stacks only when tall enough", "[print_status_tile_layout]") {
    CHECK(L(400, 499).arrangement == PrintStatusArrangement::SideBySide);
    CHECK(L(400, 500).arrangement == PrintStatusArrangement::Stacked);
    CHECK(L(500, 500).arrangement == PrintStatusArrangement::SideBySide);
}
