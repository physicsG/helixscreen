// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The numbers a bed mesh is judged by, computed once from a parsed profile.
//
// Pure, no LVGL: BedMeshState publishes them as subjects, BedMeshPanel normalizes the
// matrix it hands the renderer with the same mean, and the home tile reads the range.

#include "bed_mesh_presence.h"
#include "moonraker_types.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace helix {

struct MeshStats {
    bool loaded = false; ///< the profile holds at least one probed point
    int cols = 0;        ///< points per row of the matrix
    int rows = 0;        ///< rows of the matrix
    float min_z = 0.0f;
    float max_z = 0.0f;
    float mean_z = 0.0f;
    float range = 0.0f; ///< max_z - min_z, the figure bed flatness is judged by
    /// Where the extremes sit, as printer coordinates (the probe grid spans
    /// mesh_min..mesh_max, row 0 at mesh_min Y).
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
};

[[nodiscard]] inline MeshStats compute_mesh_stats(const BedMeshProfile& mesh) {
    MeshStats s;
    if (!probed_matrix_has_mesh(mesh.probed_matrix)) {
        return s;
    }
    s.loaded = true;
    s.rows = static_cast<int>(mesh.probed_matrix.size());
    s.cols = static_cast<int>(mesh.probed_matrix.front().size());

    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    double z_sum = 0.0;
    int z_count = 0;
    int min_row = 0, min_col = 0, max_row = 0, max_col = 0;
    for (size_t row = 0; row < mesh.probed_matrix.size(); row++) {
        for (size_t col = 0; col < mesh.probed_matrix[row].size(); col++) {
            const float z = mesh.probed_matrix[row][col];
            z_sum += z;
            z_count++;
            if (z < min_z) {
                min_z = z;
                min_row = static_cast<int>(row);
                min_col = static_cast<int>(col);
            }
            if (z > max_z) {
                max_z = z;
                max_row = static_cast<int>(row);
                max_col = static_cast<int>(col);
            }
        }
    }
    s.min_z = min_z;
    s.max_z = max_z;
    s.mean_z = static_cast<float>(z_sum / z_count);
    s.range = max_z - min_z;

    const float x_step =
        mesh.x_count > 1 ? (mesh.mesh_max[0] - mesh.mesh_min[0]) / (mesh.x_count - 1) : 0.0f;
    const float y_step =
        mesh.y_count > 1 ? (mesh.mesh_max[1] - mesh.mesh_min[1]) / (mesh.y_count - 1) : 0.0f;
    s.min_x = mesh.mesh_min[0] + min_col * x_step;
    s.min_y = mesh.mesh_min[1] + min_row * y_step;
    s.max_x = mesh.mesh_min[0] + max_col * x_step;
    s.max_y = mesh.mesh_min[1] + max_row * y_step;
    return s;
}

/// The matrix with its mean subtracted, so the renderer shows flatness rather than the
/// probe's absolute heights (which depend on Z calibration).
[[nodiscard]] inline std::vector<std::vector<float>>
mean_normalized_matrix(const BedMeshProfile& mesh, const MeshStats& stats) {
    std::vector<std::vector<float>> out;
    out.reserve(mesh.probed_matrix.size());
    for (const auto& row : mesh.probed_matrix) {
        std::vector<float> norm_row;
        norm_row.reserve(row.size());
        for (float z : row) {
            norm_row.push_back(z - stats.mean_z);
        }
        out.push_back(std::move(norm_row));
    }
    return out;
}

} // namespace helix
