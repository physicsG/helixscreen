// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_preview_cost.h"

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

namespace {
constexpr uint64_t kMb = 1024ull * 1024ull;
/// Index throughput measured on a CC1 (dual Cortex-A7).
constexpr double kCc1Rate = 10.0;
/// A desktop-class host.
constexpr double kDesktopRate = 280.0;
} // namespace

TEST_CASE("projected_preview_cpu_s: index scan plus a ghost of comparable cost",
          "[gcode][preview_cost]") {
    CHECK(projected_preview_cpu_s(10 * kMb, kCc1Rate) == Catch::Approx(2.0));
    CHECK(projected_preview_cpu_s(41 * kMb, kCc1Rate) == Catch::Approx(8.2));
    CHECK(projected_preview_cpu_s(41 * kMb, 0.0) == 0.0);
    CHECK(projected_preview_cpu_s(41 * kMb, -1.0) == 0.0);
}

TEST_CASE("judge_preview: a CC1 shows legitsu's 41MB file idle, but not mid-print",
          "[gcode][preview_cost]") {
    CHECK(judge_preview(41 * kMb, kCc1Rate, /*printing=*/false) == PreviewVerdict::Affordable);
    CHECK(judge_preview(41 * kMb, kCc1Rate, /*printing=*/true) == PreviewVerdict::TooExpensive);
}

TEST_CASE("judge_preview: the budget boundary, both sides", "[gcode][preview_cost]") {
    // At 10 MB/cpu-s a preview costs 0.2 cpu-s per MB: 25MB is exactly 5s.
    CHECK(judge_preview(25 * kMb, kCc1Rate, true) == PreviewVerdict::Affordable);
    CHECK(judge_preview(26 * kMb, kCc1Rate, true) == PreviewVerdict::TooExpensive);
    CHECK(judge_preview(75 * kMb, kCc1Rate, false) == PreviewVerdict::Affordable);
    CHECK(judge_preview(76 * kMb, kCc1Rate, false) == PreviewVerdict::TooExpensive);
}

TEST_CASE("judge_preview: a fast board previews anything", "[gcode][preview_cost]") {
    CHECK(judge_preview(500 * kMb, kDesktopRate, true) == PreviewVerdict::Affordable);
}

TEST_CASE("judge_preview: no learned rate decides nothing", "[gcode][preview_cost]") {
    CHECK(judge_preview(500 * kMb, 0.0, true) == PreviewVerdict::Unknown);
}

TEST_CASE("judge_index_progress: waits for a sample worth trusting", "[gcode][preview_cost]") {
    // Wildly expensive-looking, but too early to act on.
    CHECK(judge_index_progress(kProbeMinFraction / 2, 5.0, true) == PreviewVerdict::Unknown);
    CHECK(judge_index_progress(0.5, kProbeMinCpuS / 2, true) == PreviewVerdict::Unknown);
}

TEST_CASE("judge_index_progress: projects the whole preview from the scan so far",
          "[gcode][preview_cost]") {
    // 10% scanned in 0.4s: 4s to scan, 8s for the preview.
    CHECK(judge_index_progress(0.10, 0.4, /*printing=*/true) == PreviewVerdict::TooExpensive);
    CHECK(judge_index_progress(0.10, 0.4, /*printing=*/false) == PreviewVerdict::Affordable);
    // 10% in 0.25s: 5s for the preview, exactly the printing budget.
    CHECK(judge_index_progress(0.10, 0.25, /*printing=*/true) == PreviewVerdict::Affordable);
}
