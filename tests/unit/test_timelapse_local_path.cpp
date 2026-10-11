// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_timelapse_local_path.cpp
 * @brief Same-host playback opens videos under the root Moonraker reports.
 *
 * Run with: ./build/bin/helix-tests "[timelapse][roots]"
 *
 * The stock printer_data/timelapse layout was the only path the player ever
 * got, so a Moonraker configured with another data_path played nothing
 * (prestonbrown/helixscreen#1373). server.files.roots names the directory;
 * the stock layout is the fallback until it answers.
 */

#include "ui_overlay_timelapse_videos.h"

#include "moonraker_types.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

struct TimelapseVideosOverlayTestAccess {
    static void apply_roots(TimelapseVideosOverlay& o, const std::vector<FileRoot>& roots) {
        o.apply_timelapse_root(roots);
    }
    static std::string local_path(const TimelapseVideosOverlay& o, const std::string& filename) {
        return o.local_video_path(filename);
    }
};

namespace {

FileRoot root(const std::string& name, const std::string& path, const std::string& perms) {
    FileRoot r;
    r.name = name;
    r.path = path;
    r.permissions = perms;
    return r;
}

} // namespace

TEST_CASE("timelapse local path follows Moonraker's reported root", "[timelapse][roots]") {
    TimelapseVideosOverlay overlay(nullptr);

    SECTION("before roots arrive: the stock data_path layout") {
        std::string p = TimelapseVideosOverlayTestAccess::local_path(overlay, "a.mp4");
        CHECK(p.find("printer_data/timelapse/a.mp4") != std::string::npos);
    }

    SECTION("a reported root wins, with or without its trailing slash") {
        TimelapseVideosOverlayTestAccess::apply_roots(
            overlay, {root("gcodes", "/data/gcodes", "rw"),
                      root("timelapse", "/data/moonraker/timelapse", "rw")});
        CHECK(TimelapseVideosOverlayTestAccess::local_path(overlay, "a.mp4") ==
              "/data/moonraker/timelapse/a.mp4");

        TimelapseVideosOverlayTestAccess::apply_roots(overlay,
                                                      {root("timelapse", "/srv/tl/", "r")});
        CHECK(TimelapseVideosOverlayTestAccess::local_path(overlay, "b.mp4") == "/srv/tl/b.mp4");
    }

    SECTION("an answer without a timelapse root keeps the fallback") {
        TimelapseVideosOverlayTestAccess::apply_roots(overlay, {root("gcodes", "/data/g", "rw")});
        std::string p = TimelapseVideosOverlayTestAccess::local_path(overlay, "a.mp4");
        CHECK(p.find("printer_data/timelapse/a.mp4") != std::string::npos);
    }
}
