// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometry_budget_manager.h"

#include "memory_utils.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>

namespace helix {
namespace gcode {

size_t GeometryBudgetManager::calculate_budget(size_t available_kb) const {
    if (available_kb == 0)
        return 0;
    size_t budget = (available_kb * 1024) / (100 / BUDGET_PERCENT);
    return std::min(budget, MAX_BUDGET_BYTES);
}

size_t GeometryBudgetManager::read_system_available_kb() const {
    return get_system_memory_info().available_kb;
}

bool GeometryBudgetManager::is_system_memory_critical() const {
    return read_system_available_kb() < CRITICAL_MEMORY_KB;
}

std::string GeometryBudgetManager::render_driver_name_at(const std::string& drm_class_dir) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(drm_class_dir, ec), end; it != end && !ec;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("renderD", 0) != 0) {
            continue;
        }
        auto driver = std::filesystem::read_symlink(it->path() / "device" / "driver", ec);
        if (ec) {
            return "";
        }
        return driver.filename().string();
    }
    return "";
}

std::string GeometryBudgetManager::read_render_driver_name() const {
    return render_driver_name_at("/sys/class/drm");
}

GeometryBudgetManager::BudgetConfig GeometryBudgetManager::select_tier(size_t segment_count,
                                                                       size_t budget_bytes,
                                                                       size_t max_triangles) const {
    if (budget_bytes == 0) {
        spdlog::info("[GeometryBudget] Zero budget — thumbnail only (tier 5)");
        return {.tier = 5,
                .tube_sides = 0,
                .simplification_tolerance = 0.0f,
                .include_travels = false,
                .budget_bytes = 0};
    }
    if (segment_count == 0) {
        return {.tier = 1,
                .tube_sides = 16,
                .simplification_tolerance = 0.01f,
                .include_travels = true,
                .budget_bytes = budget_bytes};
    }

    size_t est_n16 = segment_count * BYTES_PER_SEG_N16;
    size_t est_n8 = segment_count * BYTES_PER_SEG_N8;
    size_t est_n4 = segment_count * BYTES_PER_SEG_N4;
    size_t tris_n16 = segment_count * TRIS_PER_SEG_N16;
    size_t tris_n8 = segment_count * TRIS_PER_SEG_N8;
    size_t tris_n4 = segment_count * TRIS_PER_SEG_N4;

    // A tier qualifies only when both the byte estimate and (when a GPU
    // triangle budget is set) the triangle estimate fit; a tier the triangle
    // cap excluded is noted once in the tier that ends up chosen. The flag is
    // only ever set by a tier FINER than the one chosen (coarser tiers are
    // never tested past the chosen one, and a tier failing its own triangle
    // test is not chosen), so at a return site it means exactly "bytes alone
    // would have allowed a finer tier". The coarse tolerances exist to save
    // bytes; the cap is what bound the build, so they are spending detail
    // nothing asked them to save. The triangle estimates were calibrated at
    // 0.01 mm, so that is the tolerance a capped build uses.
    bool tris_excluded_a_tier = false;
    auto tris_fit = [&](size_t est_tris) {
        if (max_triangles == 0 || est_tris <= max_triangles) {
            return true;
        }
        tris_excluded_a_tier = true;
        return false;
    };
    auto tris_note = [&](size_t est_tris) -> std::string {
        if (!tris_excluded_a_tier) {
            return "";
        }
        return fmt::format(", est {}k tris / {}k cap", est_tris / 1000, max_triangles / 1000);
    };

    if (est_n16 < budget_bytes && tris_fit(tris_n16)) {
        spdlog::info("[GeometryBudget] Tier 1 (full): est {}MB / {}MB budget{}",
                     est_n16 / (1024 * 1024), budget_bytes / (1024 * 1024), tris_note(tris_n16));
        return {.tier = 1,
                .tube_sides = 16,
                .simplification_tolerance = 0.01f,
                .include_travels = true,
                .budget_bytes = budget_bytes};
    }
    if (est_n8 < budget_bytes && tris_fit(tris_n8)) {
        spdlog::info("[GeometryBudget] Tier 2 (medium): est {}MB / {}MB budget{}",
                     est_n8 / (1024 * 1024), budget_bytes / (1024 * 1024), tris_note(tris_n8));
        return {.tier = 2,
                .tube_sides = 8,
                .simplification_tolerance = tris_excluded_a_tier ? 0.01f : 0.2f,
                .include_travels = true,
                .budget_bytes = budget_bytes,
                .triangle_capped = tris_excluded_a_tier};
    }
    if (est_n4 < budget_bytes && tris_fit(tris_n4)) {
        spdlog::info("[GeometryBudget] Tier 3 (low): est {}MB / {}MB budget{}",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024), tris_note(tris_n4));
        return {.tier = 3,
                .tube_sides = 4,
                .simplification_tolerance = tris_excluded_a_tier ? 0.01f : 1.0f,
                .include_travels = false,
                .budget_bytes = budget_bytes,
                .triangle_capped = tris_excluded_a_tier};
    }
    if (est_n4 < budget_bytes * 2 && tris_fit(tris_n4)) {
        spdlog::info("[GeometryBudget] Tier 3 (aggressive): est {}MB / {}MB budget{}",
                     est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024), tris_note(tris_n4));
        return {.tier = 3,
                .tube_sides = 4,
                .simplification_tolerance = 2.0f,
                .include_travels = false,
                .budget_bytes = budget_bytes};
    }

    spdlog::info("[GeometryBudget] Tier 4 (2D fallback): est {}MB exceeds {}MB budget{}",
                 est_n4 / (1024 * 1024), budget_bytes / (1024 * 1024), tris_note(tris_n4));
    // Capped here means the bytes fit tier 3 and only the triangle cap refused
    // it, so a coarser 3D build (a banded still) is still affordable.
    return {.tier = 4,
            .tube_sides = 0,
            .simplification_tolerance = 0.0f,
            .include_travels = false,
            .budget_bytes = budget_bytes,
            .triangle_capped = tris_excluded_a_tier && est_n4 < budget_bytes};
}

GeometryBudgetManager::BudgetAction GeometryBudgetManager::check_budget(size_t current_usage_bytes,
                                                                        size_t budget_bytes,
                                                                        int current_tier) const {
    if (budget_bytes == 0)
        return BudgetAction::ABORT;

    float usage_ratio = static_cast<float>(current_usage_bytes) / budget_bytes;
    if (usage_ratio < BUDGET_THRESHOLD)
        return BudgetAction::CONTINUE;

    if (current_tier < 3) {
        spdlog::warn("[GeometryBudget] {}MB / {}MB ({:.0f}%) — degrading from tier {}",
                     current_usage_bytes / (1024 * 1024), budget_bytes / (1024 * 1024),
                     usage_ratio * 100, current_tier);
        return BudgetAction::DEGRADE;
    }

    spdlog::warn("[GeometryBudget] {}MB / {}MB ({:.0f}%) — aborting (already at tier 3)",
                 current_usage_bytes / (1024 * 1024), budget_bytes / (1024 * 1024),
                 usage_ratio * 100);
    return BudgetAction::ABORT;
}

} // namespace gcode
} // namespace helix
