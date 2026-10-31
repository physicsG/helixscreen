// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_mock_performance_disk.cpp
 * @brief The mock performance source reports the mock gcodes disk in every sample, so
 * the host-health tile has a disk figure to judge under --test.
 *
 * Run with: ./build/bin/helix-tests "[performance][mock_disk]"
 */

#include "../lvgl_test_fixture.h"
#include "mock_disk_usage.h"
#include "mock_performance_source.h"

#include <cstdlib>
#include <optional>

#include "../catch_amalgamated.hpp"

using helix::perf::MockPerformanceSource;
using helix::perf::PerfSample;

TEST_CASE_METHOD(LVGLTestFixture, "mock performance source samples carry the mock disk",
                 "[performance][mock_disk]") {
    setenv("HELIX_MOCK_DISK_FREE_MB", "1234", 1);
    std::optional<PerfSample> got;
    {
        MockPerformanceSource source;
        source.set_callback([&](const PerfSample& s) { got = s; });
        source.start();
        lv_tick_inc(1100);
        lv_timer_handler();
        source.stop();
    }
    unsetenv("HELIX_MOCK_DISK_FREE_MB");

    REQUIRE(got.has_value());
    REQUIRE(got->host_disk_total_mb.has_value());
    REQUIRE(got->host_disk_free_mb.has_value());
    CHECK(*got->host_disk_total_mb == helix::mock::DiskMb{}.total);
    CHECK(*got->host_disk_free_mb == 1234);
}
