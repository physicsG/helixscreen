// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_occupancy.h"

#include <string>

#include "../catch_amalgamated.hpp"

using ::PrintJobStatus;
using helix::BedJobSummary;
using helix::BedOccupancyInput;
using helix::BedOutlineSnapshot;

namespace {

constexpr double kStart = 1700000000.5;

BedOccupancyInput finished(PrintJobStatus status, double height = 38.0, std::string cleared = "",
                           double cleared_start = kStart) {
    BedOccupancyInput in;
    BedJobSummary job;
    job.job_id = "a";
    job.start_time = kStart;
    job.filename = "part.gcode";
    job.status = status;
    job.object_height_mm = height;
    in.newest = job;
    in.cleared = {std::move(cleared), cleared_start};
    return in;
}

} // namespace

TEST_CASE("a finished job occupies the bed until it is cleared", "[bed_occupancy]") {
    const auto occ = helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED));
    CHECK(occ.occupied);
    REQUIRE(occ.part_height_mm.has_value());
    CHECK(*occ.part_height_mm == Catch::Approx(38.0));
    CHECK(occ.job_id == "a");
    CHECK(occ.start_time == kStart);

    const auto cleared =
        helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED, 38.0, "a"));
    CHECK_FALSE(cleared.occupied);
    CHECK_FALSE(cleared.part_height_mm.has_value());
    CHECK(cleared.job_id.empty());

    // Clearing an older job does not clear the newest one.
    CHECK(helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED, 38.0, "b")).occupied);
}

TEST_CASE("a cleared marker names the job by id and start time", "[bed_occupancy]") {
    // Moonraker reuses ids after its history is reset: the same id with another start time
    // is a different job, and its part is on the bed.
    CHECK(
        helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED, 38.0, "a", kStart + 3600.0))
            .occupied);
    CHECK_FALSE(helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED, 38.0, "a", kStart))
                    .occupied);
}

TEST_CASE("cancelled and failed jobs leave a part, unknown and running ones do not",
          "[bed_occupancy]") {
    CHECK(helix::decide_bed_occupancy(finished(PrintJobStatus::CANCELLED)).occupied);
    CHECK(helix::decide_bed_occupancy(finished(PrintJobStatus::ERROR)).occupied);
    CHECK_FALSE(helix::decide_bed_occupancy(finished(PrintJobStatus::UNKNOWN)).occupied);
    CHECK_FALSE(helix::decide_bed_occupancy(finished(PrintJobStatus::IN_PROGRESS)).occupied);
}

TEST_CASE("an active print or no history means no part on the bed", "[bed_occupancy]") {
    auto active = finished(PrintJobStatus::COMPLETED);
    active.print_active = true;
    CHECK_FALSE(helix::decide_bed_occupancy(active).occupied);

    BedOccupancyInput none;
    CHECK_FALSE(helix::decide_bed_occupancy(none).occupied);
}

TEST_CASE("a job without an object height occupies with no height", "[bed_occupancy]") {
    const auto occ = helix::decide_bed_occupancy(finished(PrintJobStatus::COMPLETED, 0.0));
    CHECK(occ.occupied);
    CHECK_FALSE(occ.part_height_mm.has_value());
}

TEST_CASE("outline snapshot survives a round trip", "[bed_occupancy]") {
    BedOutlineSnapshot snap;
    snap.filename = "dir/part.gcode";
    snap.objects.push_back({{{10.0f, 20.0f}, {30.5f, 20.0f}, {30.5f, 40.25f}}});
    snap.objects.push_back(
        {{{100.0f, 100.0f}, {150.0f, 100.0f}, {150.0f, 150.0f}, {100.0f, 150.0f}}});

    const auto back =
        helix::bed_outline_snapshot_from_json(helix::bed_outline_snapshot_to_json(snap));
    REQUIRE(back.has_value());
    CHECK(back->filename == "dir/part.gcode");
    REQUIRE(back->objects.size() == 2);
    REQUIRE(back->objects[0].points.size() == 3);
    CHECK(back->objects[0].points[1].x == Catch::Approx(30.5f));
    CHECK(back->objects[0].points[2].y == Catch::Approx(40.3f));
    REQUIRE(back->objects[1].points.size() == 4);
    CHECK(back->objects[1].points[3].x == Catch::Approx(100.0f));
    CHECK(back->objects[1].points[3].y == Catch::Approx(150.0f));
}

TEST_CASE("outline snapshot rounds to a tenth of a millimetre", "[bed_occupancy]") {
    BedOutlineSnapshot snap;
    snap.filename = "a.gcode";
    snap.objects.push_back({{{10.04f, 20.06f}, {123.456f, 7.0f}, {0.0f, 0.0f}}});

    const std::string text = helix::bed_outline_snapshot_to_json(snap);
    CHECK(text.find("123.5") != std::string::npos);
    CHECK(text.find("123.45") == std::string::npos);
    const auto back = helix::bed_outline_snapshot_from_json(text);
    REQUIRE(back.has_value());
    REQUIRE(back->objects.size() == 1);
    REQUIRE(back->objects[0].points.size() == 3);
    CHECK(back->objects[0].points[0].x == Catch::Approx(10.0f));
    CHECK(back->objects[0].points[0].y == Catch::Approx(20.1f));
}

TEST_CASE("outline snapshot caps objects and stores a long polygon as its bbox",
          "[bed_occupancy]") {
    BedOutlineSnapshot snap;
    snap.filename = "many.gcode";
    for (int o = 0; o < 70; ++o) {
        helix::BedObjectOutline outline;
        for (int p = 0; p < 40; ++p) {
            outline.points.emplace_back(static_cast<float>(o + p), static_cast<float>(2 * p));
        }
        snap.objects.push_back(outline);
    }
    snap.objects[1].points.resize(helix::kMaxSnapshotPointsPerObject);
    REQUIRE(snap.objects.size() > helix::kMaxSnapshotObjects);
    REQUIRE(snap.objects[0].points.size() > helix::kMaxSnapshotPointsPerObject);

    const auto back =
        helix::bed_outline_snapshot_from_json(helix::bed_outline_snapshot_to_json(snap));
    REQUIRE(back.has_value());
    CHECK(back->objects.size() == helix::kMaxSnapshotObjects);

    // Over the point cap: the whole object's bbox, corner by corner.
    const auto& box = back->objects[0].points;
    REQUIRE(box.size() == 4);
    CHECK(box[0].x == Catch::Approx(0.0f));
    CHECK(box[0].y == Catch::Approx(0.0f));
    CHECK(box[2].x == Catch::Approx(39.0f));
    CHECK(box[2].y == Catch::Approx(78.0f));

    // At the cap: the polygon itself.
    REQUIRE(back->objects[1].points.size() == helix::kMaxSnapshotPointsPerObject);
    CHECK(back->objects[1].points[31].x == Catch::Approx(32.0f));

    // The first objects are the ones kept.
    const auto& last = back->objects[helix::kMaxSnapshotObjects - 1].points;
    REQUIRE(last.size() == 4);
    CHECK(last[0].x == Catch::Approx(31.0f));
}

TEST_CASE("a job record survives a round trip and rejects garbage", "[bed_occupancy]") {
    BedJobSummary job;
    job.job_id = "00012A";
    job.start_time = kStart;
    job.filename = "dir/part.gcode";
    job.object_height_mm = 38.4;

    const auto back = helix::bed_job_from_json(helix::bed_job_to_json(job));
    REQUIRE(back.has_value());
    CHECK(back->job_id == "00012A");
    CHECK(back->start_time == kStart);
    CHECK(back->filename == "dir/part.gcode");
    CHECK(back->object_height_mm == Catch::Approx(38.4));
    CHECK(back->status == PrintJobStatus::COMPLETED);

    CHECK_FALSE(helix::bed_job_from_json("").has_value());
    CHECK_FALSE(helix::bed_job_from_json("[1,2]").has_value());
    CHECK_FALSE(helix::bed_job_from_json(R"({"start_time":5})").has_value());
}

TEST_CASE("a malformed outline snapshot parses to nothing", "[bed_occupancy]") {
    CHECK_FALSE(helix::bed_outline_snapshot_from_json("garbage").has_value());
    CHECK_FALSE(helix::bed_outline_snapshot_from_json("").has_value());
    CHECK_FALSE(helix::bed_outline_snapshot_from_json("{}").has_value());
    CHECK_FALSE(
        helix::bed_outline_snapshot_from_json(R"({"filename":"a","objects":{"x":1}})").has_value());
    CHECK_FALSE(helix::bed_outline_snapshot_from_json(R"({"filename":"a","objects":[[["x",1]]]})")
                    .has_value());
    CHECK(helix::bed_outline_snapshot_from_json(R"({"filename":"a","objects":[]})").has_value());
}
