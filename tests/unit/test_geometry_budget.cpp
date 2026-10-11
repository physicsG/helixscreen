// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 356C LLC

#include "geometry_budget_manager.h"
#include "platform_capabilities.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

// Memory parsing tests
TEST_CASE("Budget: parse MemAvailable from /proc/meminfo", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB
MemAvailable:    3768880 kB
Buffers:          104872 kB
Cached:          2091048 kB)";
    REQUIRE(helix::parse_meminfo_kb(meminfo, "MemAvailable") == 3768880);
}

TEST_CASE("Budget: parse MemAvailable from 1GB system", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         999936 kB
MemFree:          102400 kB
MemAvailable:     307200 kB)";
    REQUIRE(helix::parse_meminfo_kb(meminfo, "MemAvailable") == 307200);
}

TEST_CASE("Budget: parse MemAvailable returns 0 on missing field", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:        3884136 kB
MemFree:         1363424 kB)";
    REQUIRE(helix::parse_meminfo_kb(meminfo, "MemAvailable") == 0);
}

TEST_CASE("Budget: parse MemAvailable from AD5M (256MB)", "[gcode][budget]") {
    const std::string meminfo = R"(MemTotal:         253440 kB
MemFree:           12288 kB
MemAvailable:      38912 kB)";
    REQUIRE(helix::parse_meminfo_kb(meminfo, "MemAvailable") == 38912);
}

// Budget calculation tests
TEST_CASE("Budget: 25% of available memory", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(3768880);
    REQUIRE(budget == 256 * 1024 * 1024); // Capped at 256MB
}

TEST_CASE("Budget: 1GB Pi with 300MB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(307200);
    REQUIRE(budget == 307200 * 1024 / 4);
}

TEST_CASE("Budget: AD5M with 38MB available", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(38912);
    REQUIRE(budget == 38912 * 1024 / 4);
}

TEST_CASE("Budget: hard cap at 256MB even with 8GB free", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(6144000);
    REQUIRE(budget == 256 * 1024 * 1024);
}

TEST_CASE("Budget: 0 available memory returns 0", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    size_t budget = mgr.calculate_budget(0);
    REQUIRE(budget == 0);
}

// Tier selection tests
TEST_CASE("Budget: tier selection - small file gets Tier 1", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto config = mgr.select_tier(50000, 256 * 1024 * 1024);
    REQUIRE(config.tier == 1);
    REQUIRE(config.tube_sides == 16);
    REQUIRE(config.include_travels == true);
}

TEST_CASE("Budget: tier selection - medium file gets Tier 2", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    // 150K segs × 1300 = 195MB > 150MB, × 600 = 90MB < 150MB → Tier 2
    auto config = mgr.select_tier(150000, 150 * 1024 * 1024);
    REQUIRE(config.tier == 2);
    REQUIRE(config.tube_sides == 8);
}

TEST_CASE("Budget: dropped auxiliary mass must not cost a tier", "[gcode][budget]") {
    // #1425: the builder drops prime-tower/purge segments, but the tier gate was
    // estimating from total_segments, which still counted them. A 4-color file
    // that is half tower was charged for the half that never becomes geometry.
    GeometryBudgetManager mgr;
    const size_t budget = 100 * 1024 * 1024;

    // 300K total segments, half of them prime tower.
    // Charged in full:   300K x 600 = 171.7MB > 100MB, x 300 = 85.8MB < 100MB -> tier 3
    // Charged drawable:  150K x 600 =  85.8MB < 100MB                         -> tier 2
    auto inflated = mgr.select_tier(300000, budget);
    auto drawable = mgr.select_tier(150000, budget);

    REQUIRE(inflated.tier == 3);
    REQUIRE(drawable.tier == 2);
    REQUIRE(drawable.tube_sides > inflated.tube_sides);
    REQUIRE(drawable.include_travels);
    REQUIRE_FALSE(inflated.include_travels);
}

TEST_CASE("Budget: dropped auxiliary mass must not cost 3D entirely", "[gcode][budget]") {
    // The worst case the issue names: the inflated count crosses out of tier 3
    // into tier 4, where ui_gcode_viewer bails (`if (tier > 3) return nullptr`)
    // and the file gets no 3D preview at all.
    GeometryBudgetManager mgr;
    const size_t budget = 100 * 1024 * 1024;

    // Charged in full:  800K x 300 = 240MB, above 2x budget -> tier 4, no 3D
    // Charged drawable: 300K x 300 =  90MB < 100MB          -> tier 3, still 3D
    REQUIRE(mgr.select_tier(800000, budget).tier == 4);
    REQUIRE(mgr.select_tier(300000, budget).tier == 3);
}

TEST_CASE("Budget: tier selection - large file gets Tier 3", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    // 300K segs × 600 = 180MB > 100MB, × 300 = 90MB < 100MB → Tier 3
    auto config = mgr.select_tier(300000, 100 * 1024 * 1024);
    REQUIRE(config.tier == 3);
    REQUIRE(config.tube_sides == 4);
    REQUIRE(config.include_travels == false);
    REQUIRE(config.simplification_tolerance > 0.1f);
}

TEST_CASE("Budget: tier selection - massive file gets Tier 4", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    // 2M segs × 300 = 600MB >> 75MB even at N=4 → Tier 4
    auto config = mgr.select_tier(2000000, 75 * 1024 * 1024);
    REQUIRE(config.tier == 4);
    REQUIRE(config.tube_sides == 0);
}

TEST_CASE("Budget: tier selection - tiny budget forces high tier", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    // 50K segs × 300 = 15MB > 10MB → aggressive Tier 3 or Tier 4
    auto config = mgr.select_tier(50000, 10 * 1024 * 1024);
    REQUIRE(config.tier >= 3);
}

TEST_CASE("Budget: tier selection - 0 segments gets Tier 1", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto config = mgr.select_tier(0, 256 * 1024 * 1024);
    REQUIRE(config.tier == 1);
}

TEST_CASE("Budget: tier 5 for zero budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto config = mgr.select_tier(100000, 0);
    REQUIRE(config.tier == 5);
}

// Triangle-cap tier selection: memory alone approves a tier that a slow GPU
// cannot draw. VC4 (Pi 0-3) draws ~4M tris/s; a multi-second frame wedges the
// GPU until reboot, so a triangle cap must demote the tier (or refuse 3D)
// wherever the byte budget alone would not.
TEST_CASE("Budget: triangle cap demotes tiers that exceed it", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    const size_t big_bytes = 256 * 1024 * 1024; // bytes never the binding limit here
    const size_t cap = 1'000'000;

    // 3DBenchy on a Pi 3B: 88,096 segments est 4.67M/2.20M/0.97M tris per tier.
    auto benchy = mgr.select_tier(88096, big_bytes, cap);
    REQUIRE(benchy.tier == 3);
    REQUIRE(benchy.tube_sides == 4);

    auto small = mgr.select_tier(10000, big_bytes, cap);
    REQUIRE(small.tier == 1); // 530k tris at N16, under the cap

    auto medium = mgr.select_tier(20000, big_bytes, cap);
    REQUIRE(medium.tier == 2); // 1.06M at N16 exceeds, 500k at N8 fits

    auto huge = mgr.select_tier(200000, big_bytes, cap);
    REQUIRE(huge.tier == 4); // 2.2M tris even at N4: 2D fallback
}

// The coarse-tier tolerances (0.2 / 1.0 / 2.0 mm) exist to fit a byte budget.
// When the GPU triangle cap is what demoted the tier, bytes were never the
// binding constraint, so the coarse tolerance spends detail nothing asked it
// to save; the TRIS_PER_SEG_* estimates were calibrated at 0.01 mm, the
// tolerance the build then uses.
TEST_CASE("Budget: triangle-capped tiers keep the fine simplification tolerance",
          "[gcode][budget]") {
    GeometryBudgetManager mgr;
    const size_t big_bytes = 256 * 1024 * 1024; // bytes never the binding limit here
    const size_t cap = 1'000'000;

    // 3DBenchy: bytes alone allow tier 1, the cap demotes to tier 3.
    auto benchy = mgr.select_tier(88096, big_bytes, cap);
    REQUIRE(benchy.tier == 3);
    REQUIRE(benchy.tube_sides == 4);
    REQUIRE(benchy.simplification_tolerance == 0.01f);
    REQUIRE(benchy.triangle_capped);

    // The demotion can land on tier 2; same rule.
    auto medium = mgr.select_tier(20000, big_bytes, cap);
    REQUIRE(medium.tier == 2);
    REQUIRE(medium.simplification_tolerance == 0.01f);
    REQUIRE(medium.triangle_capped);

    // No cap: the flag never sets and the tier's own tolerance stands.
    auto uncapped = mgr.select_tier(88096, big_bytes, 0);
    REQUIRE(uncapped.tier == 1);
    REQUIRE(uncapped.simplification_tolerance == 0.01f);
    REQUIRE_FALSE(uncapped.triangle_capped);

    // Byte-limited tier 3 (small budget, no cap) keeps its coarse tolerance.
    auto byte_limited = mgr.select_tier(300000, 100 * 1024 * 1024, 0);
    REQUIRE(byte_limited.tier == 3);
    REQUIRE(byte_limited.simplification_tolerance == 1.0f);
    REQUIRE_FALSE(byte_limited.triangle_capped);
}

// Tier 4 says whether the cap alone refused 3D: the viewer can then still
// afford a banded 3D still, which a memory-bound file cannot.
TEST_CASE("Budget: tier 4 marks a cap-bound fallback, not a memory-bound one", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    const size_t big_bytes = 256 * 1024 * 1024;
    const size_t cap = 1'000'000;

    // Eiffel on a Pi 3B: 325k segments are 98MB at N4 (fits), 3.58M tris (does not).
    auto eiffel = mgr.select_tier(325589, big_bytes, cap);
    REQUIRE(eiffel.tier == 4);
    REQUIRE(eiffel.triangle_capped);

    // 1M segments are 300MB at N4: over the byte budget too, so no 3D at all.
    auto too_big = mgr.select_tier(1'000'000, big_bytes, cap);
    REQUIRE(too_big.tier == 4);
    REQUIRE_FALSE(too_big.triangle_capped);

    // Memory-bound with no cap.
    REQUIRE_FALSE(mgr.select_tier(2'000'000, big_bytes, 0).triangle_capped);
}

TEST_CASE("Budget: explicit zero triangle cap matches the two-argument form", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    const size_t budget = 100 * 1024 * 1024;

    REQUIRE(mgr.select_tier(150000, budget, 0).tier == mgr.select_tier(150000, budget).tier);
    REQUIRE(mgr.select_tier(300000, budget, 0).tier == mgr.select_tier(300000, budget).tier);
}

// Render-driver resolution: the triangle cap keys off the kernel driver behind
// the first DRM render node, resolved through the device/driver symlink.
namespace {

// RAII scratch directory under $TMPDIR (or /tmp), removed on destruction.
class ScratchDir {
  public:
    ScratchDir() {
        const char* base = std::getenv("TMPDIR");
        std::string templ = std::string(base ? base : "/tmp") + "/gcode_budget_XXXXXX";
        std::vector<char> buf(templ.begin(), templ.end());
        buf.push_back('\0');
        if (::mkdtemp(buf.data()) == nullptr) {
            FAIL("mkdtemp failed: " << std::strerror(errno));
        }
        path_ = buf.data();
    }
    ~ScratchDir() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
    const std::string& path() const {
        return path_;
    }

  private:
    std::string path_;
};

} // namespace

TEST_CASE("Budget: render_driver_name_at resolves the render node driver symlink",
          "[gcode][budget]") {
    ScratchDir scratch;
    std::error_code ec;

    // Shape of /sys/class/drm on a Pi 3B: renderD128/device/driver -> .../vc4-drm.
    std::filesystem::create_directories(scratch.path() + "/renderD128/device", ec);
    REQUIRE_FALSE(ec);
    std::filesystem::create_directories(scratch.path() + "/drivers/vc4-drm", ec);
    REQUIRE_FALSE(ec);
    std::filesystem::create_symlink(scratch.path() + "/drivers/vc4-drm",
                                    scratch.path() + "/renderD128/device/driver", ec);
    REQUIRE_FALSE(ec);

    REQUIRE(GeometryBudgetManager::render_driver_name_at(scratch.path()) == "vc4-drm");
}

TEST_CASE("Budget: render_driver_name_at returns empty without a render node", "[gcode][budget]") {
    ScratchDir scratch;
    std::error_code ec;

    // card0 alone has no renderD entry to resolve.
    std::filesystem::create_directories(scratch.path() + "/card0", ec);
    REQUIRE_FALSE(ec);
    REQUIRE(GeometryBudgetManager::render_driver_name_at(scratch.path()) == "");

    // A missing drm class dir is a query failure, not a driver name.
    REQUIRE(GeometryBudgetManager::render_driver_name_at(scratch.path() + "/nope") == "");
}

// Progressive budget checking tests
TEST_CASE("Budget: check_budget returns CONTINUE when under budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(50 * 1024 * 1024, 256 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::CONTINUE);
}

TEST_CASE("Budget: check_budget returns DEGRADE at 90% for tier 1", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::DEGRADE);
}

TEST_CASE("Budget: check_budget returns DEGRADE at 90% for tier 2", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 2);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::DEGRADE);
}

TEST_CASE("Budget: check_budget returns ABORT at 90% for tier 3", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(91 * 1024 * 1024, 100 * 1024 * 1024, 3);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::ABORT);
}

TEST_CASE("Budget: check_budget CONTINUE at 89%", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(89 * 1024 * 1024, 100 * 1024 * 1024, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::CONTINUE);
}

TEST_CASE("Budget: check_budget handles 0 budget", "[gcode][budget]") {
    GeometryBudgetManager mgr;
    auto action = mgr.check_budget(1024, 0, 1);
    REQUIRE(action == GeometryBudgetManager::BudgetAction::ABORT);
}
