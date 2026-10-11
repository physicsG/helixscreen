// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Points the trusted update-URL lookup (AppConstants::Update::state_dir(), the
// root-owned update_urls.json) at a scratch directory, optionally seeding the
// file, and restores the previous directory even when a REQUIRE throws: a
// leaked redirect would point every later test's URL lookup at this scratch
// path instead of the suite's ConfigSandbox.

#include "app_constants.h"
#include "test_helpers/unique_temp_dir.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace helix::test {

class ScopedUpdateUrls {
  public:
    /// @param body  JSON for update_urls.json; empty leaves the file absent.
    explicit ScopedUpdateUrls(const std::string& prefix, const std::string& body = "")
        : prev_(AppConstants::Update::detail::state_dir_ref()), dir_(unique_temp_dir(prefix)) {
        std::filesystem::create_directories(dir_);
        // The trust gate refuses update_urls.json in a group/world-writable
        // directory, so pin the mode rather than trusting the umask.
        ::chmod(dir_.c_str(), 0755);
        if (!body.empty()) {
            const std::string file = dir_ + "/update_urls.json";
            std::ofstream f(file, std::ios::trunc);
            f << body;
            f.close();
            ::chmod(file.c_str(), 0644);
        }
        AppConstants::Update::detail::state_dir_ref() = dir_;
    }

    ~ScopedUpdateUrls() {
        AppConstants::Update::detail::state_dir_ref() = prev_;
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    ScopedUpdateUrls(const ScopedUpdateUrls&) = delete;
    ScopedUpdateUrls& operator=(const ScopedUpdateUrls&) = delete;

  private:
    std::string prev_;
    std::string dir_;
};

} // namespace helix::test
