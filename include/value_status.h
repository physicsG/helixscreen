// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One classification for a value a widget judges, shared by every widget that
// colours a reading. Pure functions, no LVGL: each returns a ValueStatus and the
// caller maps it to its own appearance with bind_style_if_eq.

#include <cstdint>

namespace helix {

/// The ints match the ams_env_ind_<n>_humidity_status encoding (0 ok, 1 warn, 2 danger).
enum class ValueStatus : int { Normal = 0, Warn = 1, Danger = 2 };

} // namespace helix

namespace helix::status {

/// A speed or flow override is Warn whenever it is off 100%.
constexpr ValueStatus override_status(int pct) {
    return pct != 100 ? ValueStatus::Warn : ValueStatus::Normal;
}

/// A Z-offset is Warn while there is an unsaved one to save.
constexpr ValueStatus z_offset_status(bool save_available) {
    return save_available ? ValueStatus::Warn : ValueStatus::Normal;
}

/// How flat a bed mesh is, from its Z range in millimetres: Normal below 0.15, Warn below
/// 0.30, Danger from there.
constexpr ValueStatus mesh_flatness_status(float range_mm) {
    return range_mm < 0.15f   ? ValueStatus::Normal
           : range_mm < 0.30f ? ValueStatus::Warn
                              : ValueStatus::Danger;
}

/// A maintenance meter from its used share of the service interval: Normal below three
/// quarters, Warn from there, Danger once the interval is reached.
constexpr ValueStatus maintenance_status(double used_fraction) {
    return used_fraction >= 1.0    ? ValueStatus::Danger
           : used_fraction >= 0.75 ? ValueStatus::Warn
                                   : ValueStatus::Normal;
}

/// Free space on the gcodes disk below which the host is Warn.
constexpr int kDiskLowFreeMb = 500;

/// What the host-health judgement reads. A reading the printer does not report stays at
/// its default, and a metric at its default is never judged.
struct HostHealthInput {
    int cpu_pct = -1;      ///< host CPU load, -1 = not reported
    int cpu_temp_c10 = -1; ///< host CPU temperature in tenths of a degree, -1 = not reported
    /// Raspberry Pi throttle register: bits 0-3 are "now", bits 16-19 "occurred since boot".
    uint32_t throttle_bits = 0;
    int mcu_max_load_pct = -1; ///< the busiest MCU's load, -1 = none reported
    /// Some MCU's cumulative retransmit count is higher than at the previous sample.
    bool mcu_retransmits_rose = false;
    int disk_free_mb = -1; ///< free space on the gcodes disk, -1 = not reported
};

/// The metric that put the host at its status; None while everything is Normal.
/// The ints are the host_health_cause subject.
enum class HealthCause : int {
    None = 0,
    HostCpu = 1,
    HostTemp = 2,
    Throttle = 3,
    McuLoad = 4,
    McuRetransmit = 5,
    DiskFree = 6,
};

struct HostHealthStatus {
    ValueStatus status = ValueStatus::Normal;
    HealthCause cause = HealthCause::None;
};

constexpr uint32_t kThrottleNowMask = 0x0000000F;
constexpr uint32_t kThrottleSinceBootMask = 0x000F0000;

/// Worst of the host and MCU rules: load >= 70% Warn and >= 90% Danger for the host CPU
/// and for each MCU, CPU temperature >= 70 C Warn and >= 80 C Danger, a throttle "now"
/// bit Danger and an "occurred since boot" bit Warn, any MCU retransmit rise Warn, disk
/// free below kDiskLowFreeMb Warn. When metrics tie at the worst status the one earlier
/// in HealthCause order wins, so the answer does not flicker between them.
constexpr HostHealthStatus host_health_status(const HostHealthInput& in) {
    HostHealthStatus worst;
    const auto consider = [&worst](ValueStatus status, HealthCause cause) {
        if (static_cast<int>(status) > static_cast<int>(worst.status))
            worst = {status, cause};
    };
    const auto by_load = [](int pct) {
        if (pct < 0)
            return ValueStatus::Normal;
        return pct >= 90   ? ValueStatus::Danger
               : pct >= 70 ? ValueStatus::Warn
                           : ValueStatus::Normal;
    };
    consider(by_load(in.cpu_pct), HealthCause::HostCpu);
    if (in.cpu_temp_c10 >= 0) {
        consider(in.cpu_temp_c10 >= 800   ? ValueStatus::Danger
                 : in.cpu_temp_c10 >= 700 ? ValueStatus::Warn
                                          : ValueStatus::Normal,
                 HealthCause::HostTemp);
    }
    consider((in.throttle_bits & kThrottleNowMask)         ? ValueStatus::Danger
             : (in.throttle_bits & kThrottleSinceBootMask) ? ValueStatus::Warn
                                                           : ValueStatus::Normal,
             HealthCause::Throttle);
    consider(by_load(in.mcu_max_load_pct), HealthCause::McuLoad);
    consider(in.mcu_retransmits_rose ? ValueStatus::Warn : ValueStatus::Normal,
             HealthCause::McuRetransmit);
    consider(in.disk_free_mb >= 0 && in.disk_free_mb < kDiskLowFreeMb ? ValueStatus::Warn
                                                                      : ValueStatus::Normal,
             HealthCause::DiskFree);
    return worst;
}

} // namespace helix::status
