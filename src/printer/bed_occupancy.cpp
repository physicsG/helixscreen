// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_occupancy.h"

#include "json_utils.h"

#include <algorithm>
#include <cmath>

#include "hv/json.hpp"

namespace helix {

namespace {

constexpr const char* kFilenameField = "filename";
constexpr const char* kObjectsField = "objects";
constexpr const char* kJobIdField = "job_id";
constexpr const char* kStartTimeField = "start_time";
constexpr const char* kObjectHeightField = "object_height";
constexpr double kSameStartToleranceS = 1e-3;

bool status_leaves_part(PrintJobStatus status) {
    switch (status) {
    case PrintJobStatus::COMPLETED:
    case PrintJobStatus::CANCELLED:
    case PrintJobStatus::ERROR:
        return true;
    default:
        return false;
    }
}

/// @p mm on the kSnapshotResolutionMm grid, as the shortest decimal that names it (10.1,
/// not 10.100000381469727), so the persisted text stays small.
double round_to_resolution(double mm) {
    const double steps_per_mm = std::round(1.0 / kSnapshotResolutionMm);
    return std::round(mm * steps_per_mm) / steps_per_mm;
}

/// The points to persist for one outline: the polygon, or its bbox's corners when it has
/// more points than the cap.
std::vector<glm::vec2> persisted_points(const std::vector<glm::vec2>& points) {
    std::vector<glm::vec2> out;
    if (points.size() > kMaxSnapshotPointsPerObject) {
        glm::vec2 lo = points.front();
        glm::vec2 hi = points.front();
        for (const auto& p : points) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
        out = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
    } else {
        out = points;
    }
    return out;
}

} // namespace

bool same_bed_job(const BedJobKey& key, const BedJobSummary& job) {
    return !key.job_id.empty() && key.job_id == job.job_id &&
           std::abs(key.start_time - job.start_time) < kSameStartToleranceS;
}

BedOccupancy decide_bed_occupancy(const BedOccupancyInput& in) {
    BedOccupancy out;
    if (in.print_active || !in.newest || !status_leaves_part(in.newest->status)) {
        return out;
    }
    if (same_bed_job(in.cleared, *in.newest)) {
        return out;
    }
    out.occupied = true;
    out.job_id = in.newest->job_id;
    out.start_time = in.newest->start_time;
    if (in.newest->object_height_mm > 0.0) {
        out.part_height_mm = in.newest->object_height_mm;
    }
    return out;
}

std::string bed_outline_snapshot_to_json(const BedOutlineSnapshot& snapshot) {
    nlohmann::json objects = nlohmann::json::array();
    const std::size_t object_count = std::min(snapshot.objects.size(), kMaxSnapshotObjects);
    for (std::size_t i = 0; i < object_count; ++i) {
        nlohmann::json pts = nlohmann::json::array();
        for (const auto& point : persisted_points(snapshot.objects[i].points)) {
            pts.push_back(nlohmann::json::array(
                {round_to_resolution(point.x), round_to_resolution(point.y)}));
        }
        objects.push_back(std::move(pts));
    }
    nlohmann::json root = {{kFilenameField, snapshot.filename},
                           {kObjectsField, std::move(objects)}};
    return json_util::safe_dump(root);
}

std::optional<BedOutlineSnapshot> bed_outline_snapshot_from_json(const std::string& text) {
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }
    const nlohmann::json* objects = json_util::find_member(root, kObjectsField);
    if (objects == nullptr || !objects->is_array()) {
        return std::nullopt;
    }
    BedOutlineSnapshot snapshot;
    snapshot.filename = json_util::safe_string(root, kFilenameField);
    for (const auto& object : *objects) {
        if (snapshot.objects.size() >= kMaxSnapshotObjects) {
            break;
        }
        if (!object.is_array()) {
            return std::nullopt;
        }
        BedObjectOutline outline;
        for (const auto& point : object) {
            if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
                !point[1].is_number()) {
                return std::nullopt;
            }
            outline.points.emplace_back(json_util::as_float(point[0]),
                                        json_util::as_float(point[1]));
        }
        outline.points = persisted_points(outline.points);
        snapshot.objects.push_back(std::move(outline));
    }
    return snapshot;
}

std::string bed_job_to_json(const BedJobSummary& job) {
    const nlohmann::json root = {{kJobIdField, job.job_id},
                                 {kStartTimeField, job.start_time},
                                 {kFilenameField, job.filename},
                                 {kObjectHeightField, job.object_height_mm}};
    return json_util::safe_dump(root);
}

std::optional<BedJobSummary> bed_job_from_json(const std::string& text) {
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }
    BedJobSummary job;
    job.job_id = json_util::safe_string(root, kJobIdField);
    if (job.job_id.empty()) {
        return std::nullopt;
    }
    job.start_time = json_util::safe_double(root, kStartTimeField);
    job.filename = json_util::safe_string(root, kFilenameField);
    job.object_height_mm = json_util::safe_double(root, kObjectHeightField);
    job.status = PrintJobStatus::COMPLETED;
    return job;
}

} // namespace helix
