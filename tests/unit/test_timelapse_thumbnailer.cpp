// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timelapse_thumbnailer.h"

#include "../catch_amalgamated.hpp"

using namespace helix::timelapse;

TEST_CASE("Thumbnailer: companion filename", "[timelapse][thumbnailer]") {
    SECTION("mp4 gets .thumb.jpg companion") {
        REQUIRE(companion_filename("benchy.mp4") == "benchy.thumb.jpg");
    }

    SECTION("mkv gets .thumb.jpg companion") {
        REQUIRE(companion_filename("print.mkv") == "print.thumb.jpg");
    }
}

TEST_CASE("Thumbnailer: ffmpeg argument list construction", "[timelapse][thumbnailer]") {
    auto args = ffmpeg_extract_args("/home/pi/printer_data/timelapse/benchy.mp4",
                                    "/home/pi/printer_data/timelapse/benchy.thumb.jpg");

    REQUIRE(args.size() == 11);
    REQUIRE(args[0] == "ffmpeg");
    REQUIRE(args[1] == "-y");
    REQUIRE(args[2] == "-i");
    REQUIRE(args[3] == "/home/pi/printer_data/timelapse/benchy.mp4");
    REQUIRE(args[4] == "-vframes");
    REQUIRE(args[5] == "1");
    REQUIRE(args[6] == "-q:v");
    REQUIRE(args[7] == "3");
    // Small enough that pre-scaling it is a cheap decode.
    REQUIRE(args[8] == "-vf");
    REQUIRE(args[9] == "scale=480:-2");
    REQUIRE(args[10] == "/home/pi/printer_data/timelapse/benchy.thumb.jpg");
}

TEST_CASE("Thumbnailer: ffmpeg args are safe from shell injection", "[timelapse][thumbnailer]") {
    auto args = ffmpeg_extract_args("/tmp/$(rm -rf /).mp4", "/tmp/out.jpg");

    // The malicious filename is passed as a single argument element, not shell-interpreted
    REQUIRE(args[3] == "/tmp/$(rm -rf /).mp4");
}

TEST_CASE("Thumbnailer: video file filtering", "[timelapse][thumbnailer]") {
    REQUIRE(is_video_file("benchy.mp4") == true);
    REQUIRE(is_video_file("print.mkv") == true);
    REQUIRE(is_video_file("timelapse.avi") == true);
    REQUIRE(is_video_file("thumbnail.jpg") == false);
    REQUIRE(is_video_file("benchy.thumb.jpg") == false);
    REQUIRE(is_video_file("config.cfg") == false);
    REQUIRE(is_video_file("") == false);
}
