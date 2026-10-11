// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_preview_setup.h"

#include "../catch_amalgamated.hpp"

using helix::ui::preview_cache_is_current;

TEST_CASE("preview cache is current only for a non-empty copy of the expected size",
          "[gcode][preview-cache]") {
    struct Row {
        size_t on_disk;
        uint64_t expected;
        bool current;
    };
    const Row rows[] = {
        {1000, 1000, true},  // same bytes
        {1000, 2000, false}, // same-name file re-sliced larger
        {2000, 1000, false}, // re-sliced smaller
        {512, 1000, false},  // truncated by a crash mid-transfer
        {0, 1000, false},    // nothing cached
        {0, 0, false},       // empty is never usable, known size or not
        {1000, 0, true},     // size unknown: any non-empty copy
    };
    for (const auto& r : rows) {
        INFO(r.on_disk << " vs " << r.expected);
        CHECK(preview_cache_is_current(r.on_disk, r.expected) == r.current);
    }
}
