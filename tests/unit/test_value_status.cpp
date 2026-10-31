// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_value_status.cpp
 * @brief helix::status classifiers at their thresholds.
 *
 * Run with: ./build/bin/helix-tests "[value_status]"
 */

#include "value_status.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("override_status: Warn whenever the override is off 100", "[value_status]") {
    struct Row {
        int pct;
        ValueStatus expected;
    };
    const Row rows[] = {
        {100, ValueStatus::Normal}, {99, ValueStatus::Warn},  {101, ValueStatus::Warn},
        {50, ValueStatus::Warn},    {200, ValueStatus::Warn}, {0, ValueStatus::Warn},
    };
    for (const auto& r : rows) {
        INFO(r.pct);
        CHECK(status::override_status(r.pct) == r.expected);
    }
}

TEST_CASE("z_offset_status: Warn exactly while a save is available", "[value_status]") {
    CHECK(status::z_offset_status(true) == ValueStatus::Warn);
    CHECK(status::z_offset_status(false) == ValueStatus::Normal);
}

TEST_CASE("mesh_flatness_status: bands at 0.15 and 0.30 mm", "[value_status]") {
    struct Row {
        float range_mm;
        ValueStatus expected;
    };
    const Row rows[] = {
        {0.0f, ValueStatus::Normal}, {0.149f, ValueStatus::Normal}, {0.15f, ValueStatus::Warn},
        {0.299f, ValueStatus::Warn}, {0.30f, ValueStatus::Danger},  {1.2f, ValueStatus::Danger},
    };
    for (const auto& r : rows) {
        INFO(r.range_mm);
        CHECK(status::mesh_flatness_status(r.range_mm) == r.expected);
    }
}

TEST_CASE("ValueStatus ints match the humidity status encoding", "[value_status]") {
    CHECK(static_cast<int>(ValueStatus::Normal) == 0);
    CHECK(static_cast<int>(ValueStatus::Warn) == 1);
    CHECK(static_cast<int>(ValueStatus::Danger) == 2);
}

TEST_CASE("maintenance_status: Warn from three quarters, Danger once due", "[value_status]") {
    CHECK(status::maintenance_status(0.0) == ValueStatus::Normal);
    CHECK(status::maintenance_status(0.7499) == ValueStatus::Normal);
    CHECK(status::maintenance_status(0.75) == ValueStatus::Warn);
    CHECK(status::maintenance_status(0.9999) == ValueStatus::Warn);
    CHECK(status::maintenance_status(1.0) == ValueStatus::Danger);
}

namespace {

using status::HealthCause;
using status::HostHealthInput;

status::HostHealthStatus judge(HostHealthInput in) {
    return status::host_health_status(in);
}

} // namespace

TEST_CASE("host_health_status: a host that reports nothing is Normal", "[value_status]") {
    const auto r = judge({});
    CHECK(r.status == ValueStatus::Normal);
    CHECK(r.cause == HealthCause::None);
}

TEST_CASE("host_health_status: CPU load and MCU load thresholds are 70 and 90", "[value_status]") {
    struct Row {
        int pct;
        ValueStatus expected;
    };
    const Row rows[] = {
        {0, ValueStatus::Normal},  {69, ValueStatus::Normal}, {70, ValueStatus::Warn},
        {89, ValueStatus::Warn},   {90, ValueStatus::Danger}, {100, ValueStatus::Danger},
        {-1, ValueStatus::Normal},
    };
    for (const auto& r : rows) {
        INFO(r.pct);
        HostHealthInput cpu;
        cpu.cpu_pct = r.pct;
        CHECK(judge(cpu).status == r.expected);
        CHECK(judge(cpu).cause ==
              (r.expected == ValueStatus::Normal ? HealthCause::None : HealthCause::HostCpu));
        HostHealthInput mcu;
        mcu.mcu_max_load_pct = r.pct;
        CHECK(judge(mcu).status == r.expected);
        CHECK(judge(mcu).cause ==
              (r.expected == ValueStatus::Normal ? HealthCause::None : HealthCause::McuLoad));
    }
}

TEST_CASE("host_health_status: CPU temperature thresholds are 70 and 80 C", "[value_status]") {
    struct Row {
        int c10;
        ValueStatus expected;
    };
    const Row rows[] = {
        {0, ValueStatus::Normal}, {699, ValueStatus::Normal}, {700, ValueStatus::Warn},
        {799, ValueStatus::Warn}, {800, ValueStatus::Danger}, {-1, ValueStatus::Normal},
    };
    for (const auto& r : rows) {
        INFO(r.c10);
        HostHealthInput in;
        in.cpu_temp_c10 = r.c10;
        CHECK(judge(in).status == r.expected);
    }
}

TEST_CASE("host_health_status: a throttle 'now' bit is Danger, 'since boot' alone is Warn",
          "[value_status]") {
    struct Row {
        uint32_t bits;
        ValueStatus expected;
    };
    const Row rows[] = {
        {0x0, ValueStatus::Normal},      {0x1, ValueStatus::Danger},
        {0x8, ValueStatus::Danger},      {0x10, ValueStatus::Normal},
        {0x10000, ValueStatus::Warn},    {0x80000, ValueStatus::Warn},
        {0x100000, ValueStatus::Normal}, {0x50000, ValueStatus::Warn},
        {0x50005, ValueStatus::Danger},
    };
    for (const auto& r : rows) {
        INFO(r.bits);
        HostHealthInput in;
        in.throttle_bits = r.bits;
        const auto result = judge(in);
        CHECK(result.status == r.expected);
        CHECK(result.cause ==
              (r.expected == ValueStatus::Normal ? HealthCause::None : HealthCause::Throttle));
    }
}

TEST_CASE("host_health_status: a retransmit rise is Warn", "[value_status]") {
    HostHealthInput in;
    in.mcu_retransmits_rose = true;
    CHECK(judge(in).status == ValueStatus::Warn);
    CHECK(judge(in).cause == HealthCause::McuRetransmit);
}

TEST_CASE("host_health_status: disk free below 500 MB is Warn", "[value_status]") {
    struct Row {
        int free_mb;
        ValueStatus expected;
    };
    const Row rows[] = {
        {0, ValueStatus::Warn},       {499, ValueStatus::Warn},  {500, ValueStatus::Normal},
        {20000, ValueStatus::Normal}, {-1, ValueStatus::Normal},
    };
    for (const auto& r : rows) {
        INFO(r.free_mb);
        HostHealthInput in;
        in.disk_free_mb = r.free_mb;
        CHECK(judge(in).status == r.expected);
    }
}

TEST_CASE("host_health_status: the worst metric wins, and ties keep the earlier cause",
          "[value_status]") {
    HostHealthInput in;
    in.cpu_pct = 75;        // Warn
    in.disk_free_mb = 100;  // Warn
    in.throttle_bits = 0x2; // Danger
    auto r = judge(in);
    CHECK(r.status == ValueStatus::Danger);
    CHECK(r.cause == HealthCause::Throttle);

    in.throttle_bits = 0;
    r = judge(in);
    CHECK(r.status == ValueStatus::Warn);
    CHECK(r.cause == HealthCause::HostCpu);

    in.cpu_pct = 95;
    in.mcu_max_load_pct = 95;
    r = judge(in);
    CHECK(r.status == ValueStatus::Danger);
    CHECK(r.cause == HealthCause::HostCpu);
}
