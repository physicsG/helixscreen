// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

namespace helix {
namespace gcode {

class GeometryBudgetManager {
  public:
    static constexpr size_t MAX_BUDGET_BYTES = 256 * 1024 * 1024;
    static constexpr int BUDGET_PERCENT = 25;
    static constexpr size_t CRITICAL_MEMORY_KB = 100 * 1024;

    struct BudgetConfig {
        int tier;
        int tube_sides;
        float simplification_tolerance;
        bool include_travels;
        size_t budget_bytes;
        /// True when the GPU triangle cap (not the byte budget) is what chose
        /// this tier: bytes alone allowed a finer one. Consumers may then spend
        /// the memory headroom on detail (finer tolerance, tighter merge angle).
        bool triangle_capped = false;
    };

    // Empirically measured bytes per raw gcode segment (includes simplification,
    // strip overhead, normal/color palettes). Calibrated from Pi 5 builds.
    static constexpr size_t BYTES_PER_SEG_N16 = 1300;
    static constexpr size_t BYTES_PER_SEG_N8 = 600;
    static constexpr size_t BYTES_PER_SEG_N4 = 300;

    // Upper-ish triangles per raw gcode segment, calibrated on a Pi 3B with a
    // 3DBenchy (88,096 drawable segments, travels included at tier 1). Coarser
    // tiers' simplification only lowers the real count, so these estimates
    // safely gate the GPU triangle budget.
    static constexpr size_t TRIS_PER_SEG_N16 = 53;
    static constexpr size_t TRIS_PER_SEG_N8 = 25;
    static constexpr size_t TRIS_PER_SEG_N4 = 11;

    size_t calculate_budget(size_t available_kb) const;
    size_t read_system_available_kb() const;
    bool is_system_memory_critical() const;

    // Kernel driver name backing the first DRM render node in @p drm_class_dir
    // (basename of its device/driver symlink, e.g. "vc4-drm"); "" on any
    // failure. Static so tests can point it at a scratch sysfs tree.
    static std::string render_driver_name_at(const std::string& drm_class_dir);
    std::string read_render_driver_name() const;

    // Tier selection gates each tier's byte estimate against @p budget_bytes
    // and, when @p max_triangles is nonzero, its triangle estimate against it
    // too. A tier skipped for triangles falls through to the next; 0 disables
    // the triangle check entirely.
    BudgetConfig select_tier(size_t segment_count, size_t budget_bytes,
                             size_t max_triangles = 0) const;

    enum class BudgetAction { CONTINUE, DEGRADE, ABORT };

    static constexpr float BUDGET_THRESHOLD = 0.9f;
    static constexpr size_t CHECK_INTERVAL_SEGMENTS = 5000;
    static constexpr size_t SYSTEM_CHECK_INTERVAL_SEGMENTS = 20000;

    BudgetAction check_budget(size_t current_usage_bytes, size_t budget_bytes,
                              int current_tier) const;
};

} // namespace gcode
} // namespace helix
