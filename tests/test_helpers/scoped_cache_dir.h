// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scoped_env.h"
#include "tools_used_cache.h"
#include "unique_temp_dir.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace helix::test {

/// A cache directory of the test's own, through HELIX_CACHE_DIR (the
/// documented override for get_helix_cache_dir()), removed and the variable
/// restored on destruction. The shared tools-used cache drops its in-memory
/// copy at both ends, so it reads only this directory and leaves nothing for
/// the next test.
struct ScopedCacheDir {
    ScopedEnv restore{"HELIX_CACHE_DIR"};
    std::filesystem::path dir = unique_temp_dir("helix_cache_test");

    ScopedCacheDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        ::setenv("HELIX_CACHE_DIR", dir.c_str(), 1);
        tools_used_cache().invalidate();
    }
    ~ScopedCacheDir() {
        tools_used_cache().invalidate();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    ScopedCacheDir(const ScopedCacheDir&) = delete;
    ScopedCacheDir& operator=(const ScopedCacheDir&) = delete;
};

} // namespace helix::test
