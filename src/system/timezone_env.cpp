// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timezone_env.h"

#include "spdlog/spdlog.h"

#include <climits>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace helix::timezone_env {

namespace {

constexpr const char* DEFAULT_SYSTEM_PROBE = "/usr/share/zoneinfo/UTC";
constexpr const char* DEFAULT_BUNDLED_SUBDIR = "assets/zoneinfo";

bool probed_ = false;
std::string active_tzdir_;

bool path_exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}

} // namespace

const char* configure_tzdir(const char* system_probe, const char* bundled_subdir) {
    if (probed_) {
        return active_tzdir_.c_str();
    }
    probed_ = true;

    const char* probe = system_probe ? system_probe : DEFAULT_SYSTEM_PROBE;
    const char* subdir = bundled_subdir ? bundled_subdir : DEFAULT_BUNDLED_SUBDIR;

    if (path_exists(probe)) {
        // System tzdata present — let glibc use its default TZDIR.
        return active_tzdir_.c_str();
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        spdlog::warn("[timezone_env] System zoneinfo missing and getcwd failed; "
                     "times may display as UTC");
        return active_tzdir_.c_str();
    }

    std::string bundled = std::string(cwd) + "/" + subdir;
    if (!path_exists(bundled + "/UTC")) {
        spdlog::warn("[timezone_env] System zoneinfo missing and no bundled fallback at '{}'; "
                     "times may display as UTC",
                     bundled);
        return active_tzdir_.c_str();
    }

    ::setenv("TZDIR", bundled.c_str(), 1);
    active_tzdir_ = std::move(bundled);
    spdlog::info("[timezone_env] System zoneinfo missing; using bundled TZDIR={}", active_tzdir_);
    return active_tzdir_.c_str();
}

void apply(const char* iana_id, const char* posix_tz) {
#ifdef ESP_PLATFORM
    // newlib reads no zoneinfo: an IANA name parses as UTC.
    if (posix_tz) {
        ::setenv("TZ", posix_tz, 1);
        ::tzset();
        return;
    }
#else
    (void)posix_tz;
#endif
    configure_tzdir();
    ::setenv("TZ", iana_id, 1);
    ::tzset();
}

void reset_probe_state() {
    probed_ = false;
    active_tzdir_.clear();
    ::unsetenv("TZDIR");
    ::unsetenv("TZ");
    ::tzset();
}

} // namespace helix::timezone_env
