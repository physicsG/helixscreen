// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Whether a finished print may still be on the bed, decided from the newest history job.
//
// Free of LVGL and of the network layer so it is unit-testable. BedOccupancyState feeds it
// the newest job and the persisted acknowledgement; the plate, the lift rule and the
// Cleared chip read the verdict.

#include "print_history_data.h"

#include <glm/vec2.hpp>
#include <optional>
#include <string>
#include <vector>

namespace helix {

/// The newest history job, reduced to what the decision reads.
struct BedJobSummary {
    std::string job_id;
    double start_time = 0.0; ///< Unix seconds; with job_id, names the job
    std::string filename;
    PrintJobStatus status = PrintJobStatus::UNKNOWN;
    double object_height_mm = 0.0; ///< file metadata; 0 when the slicer did not record it
};

/// Names one history job. Moonraker numbers job ids up from the highest one in its database,
/// so after its history is reset an id names a different job: the start time tells them apart.
struct BedJobKey {
    std::string job_id; ///< empty: no job
    double start_time = 0.0;
};

/// Whether @p key names @p job: same id and the same start time to the millisecond.
[[nodiscard]] bool same_bed_job(const BedJobKey& key, const BedJobSummary& job);

struct BedOccupancyInput {
    std::optional<BedJobSummary> newest; ///< nullopt: no history (fresh install, disabled)
    BedJobKey cleared;                   ///< the job the user tapped Cleared for
    bool print_active = false;           ///< a job is printing or paused right now
};

struct BedOccupancy {
    bool occupied = false;                ///< a part may be on the bed
    std::optional<double> part_height_mm; ///< nullopt when occupied but the height is unknown
    std::string job_id;                   ///< the job the part came from; empty when not occupied
    double start_time = 0.0;              ///< that job's start time; 0 when not occupied
};

/**
 * @brief Decide whether the newest job may have left a part on the bed
 *
 * Occupied when the newest job ended COMPLETED, CANCELLED or ERROR, it is not the
 * @p cleared job (same_bed_job()), and no print is active. A cancelled or failed job leaves a
 * partial part, and its file's full object height is the conservative bound for it. UNKNOWN and
 * IN_PROGRESS never occupy. part_height_mm is the object height when it is above 0.
 */
[[nodiscard]] BedOccupancy decide_bed_occupancy(const BedOccupancyInput& in);

/// One object's outline in printer millimetres: the slicer's polygon, or its bbox as four
/// corners when the polygon is missing.
struct BedObjectOutline {
    std::vector<glm::vec2> points;
};

/// Outlines captured when a print ended, kept so a Klipper restart does not forget them.
struct BedOutlineSnapshot {
    std::string filename; ///< matched against the newest job's filename
    std::vector<BedObjectOutline> objects;
};

/// At most this many objects are persisted; objects past it are dropped.
inline constexpr std::size_t kMaxSnapshotObjects = 32;
/// A polygon with more points than this is persisted as its bbox's four corners, so a
/// restart draws a coarser outline of the whole object rather than part of it.
inline constexpr std::size_t kMaxSnapshotPointsPerObject = 32;
/// Persisted coordinates are rounded to a multiple of this, in millimetres.
inline constexpr double kSnapshotResolutionMm = 0.1;

/// Serialise and parse a snapshot for the per-printer config. Parsing is non-throwing: a
/// malformed value yields nullopt. Both directions apply the caps; writing also rounds.
[[nodiscard]] std::string bed_outline_snapshot_to_json(const BedOutlineSnapshot& snapshot);
[[nodiscard]] std::optional<BedOutlineSnapshot>
bed_outline_snapshot_from_json(const std::string& text);

/// Serialise and parse a job record for the per-printer config (the last occupying job, the
/// cleared job). The status is not stored: a parsed record reads COMPLETED. Parsing is
/// non-throwing: a malformed value or one without a job id yields nullopt.
[[nodiscard]] std::string bed_job_to_json(const BedJobSummary& job);
[[nodiscard]] std::optional<BedJobSummary> bed_job_from_json(const std::string& text);

} // namespace helix
