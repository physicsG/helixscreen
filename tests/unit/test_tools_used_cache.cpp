// SPDX-License-Identifier: GPL-3.0-or-later

#include "../test_helpers/scoped_cache_dir.h"
#include "tools_used_cache.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "../catch_amalgamated.hpp"

namespace {
using CacheDirGuard = helix::test::ScopedCacheDir;
} // namespace

TEST_CASE("ToolsUsedCache round trip + staleness", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;

    SECTION("miss then hit") {
        REQUIRE(a.lookup("file.gcode", {100, 1000}) == std::nullopt);
        const std::set<int> tools{0, 2};
        a.store("file.gcode", {100, 1000}, tools);
        REQUIRE(a.lookup("file.gcode", {100, 1000}).value_or(std::set<int>{}) == tools);
    }
    SECTION("empty set is a hit, not a miss") {
        const std::set<int> empty;
        a.store("single.gcode", {50, 5}, empty);
        auto got = a.lookup("single.gcode", {50, 5});
        REQUIRE(got.has_value());
        REQUIRE(got->empty());
    }
    SECTION("size or mtime change invalidates") {
        a.store("file.gcode", {100, 1000}, {0});
        REQUIRE(a.lookup("file.gcode", {101, 1000}) == std::nullopt);
        REQUIRE(a.lookup("file.gcode", {100, 1001}) == std::nullopt);
    }
    SECTION("path difference is a different entry") {
        a.store("dir/a.gcode", {100, 1000}, {1});
        REQUIRE(a.lookup("dir/b.gcode", {100, 1000}) == std::nullopt);
    }
}

TEST_CASE("ToolsUsedCache persistence across instances", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;
    const std::set<int> tools{0, 1, 3};
    a.store("persist.gcode", {10, 20}, tools);
    helix::ToolsUsedCache b; // fresh instance reads the same disk file
    REQUIRE(b.lookup("persist.gcode", {10, 20}).value_or(std::set<int>{}) == tools);
}

TEST_CASE("ToolsUsedCache LRU bound", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;
    for (size_t i = 0; i < helix::ToolsUsedCache::MAX_ENTRIES + 10; ++i) {
        a.store("f" + std::to_string(i) + ".gcode", {i, static_cast<time_t>(i)}, {0});
    }
    // f0 was stored first and never looked up — evicted.
    REQUIRE(a.lookup("f0.gcode", {0, 0}) == std::nullopt);
    // The most recent stores survive.
    const size_t last = helix::ToolsUsedCache::MAX_ENTRIES + 9;
    REQUIRE(a.lookup("f" + std::to_string(last) + ".gcode", {last, static_cast<time_t>(last)})
                .has_value());
}

TEST_CASE("ToolsUsedCache evicts the least recently looked up, not the first by name",
          "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;
    const size_t n = helix::ToolsUsedCache::MAX_ENTRIES;
    // Stored in reverse name order, so name order and age disagree.
    auto name = [](size_t i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "f%03zu.gcode", i);
        return std::string(buf);
    };
    for (size_t i = n; i-- > 0;) {
        a.store(name(i), {i, static_cast<time_t>(i)}, {static_cast<int>(i % 4)});
    }
    // The oldest store, looked up, becomes the most recent.
    const size_t oldest = n - 1;
    REQUIRE(a.lookup(name(oldest), {oldest, static_cast<time_t>(oldest)}).has_value());

    a.store("overflow.gcode", {1, 1}, {0});

    CHECK(a.lookup(name(oldest), {oldest, static_cast<time_t>(oldest)}).has_value());
    CHECK_FALSE(a.lookup(name(n - 2), {n - 2, static_cast<time_t>(n - 2)}).has_value());
    CHECK(a.lookup(name(0), {0, 0}).has_value()); // first by name, stored last: kept
}

TEST_CASE("the shared tools-used cache reads the directory it is pointed at",
          "[tools_used_cache]") {
    {
        CacheDirGuard guard;
        helix::tools_used_cache().store("shared.gcode", {7, 7}, {1});
        REQUIRE(helix::tools_used_cache().lookup("shared.gcode", {7, 7}).has_value());
    }
    CacheDirGuard fresh_dir;
    // Nothing carries over from the last directory...
    CHECK_FALSE(helix::tools_used_cache().lookup("shared.gcode", {7, 7}).has_value());
    // ...and what lands in this one on disk is read, not a stale empty copy.
    helix::tools_used_cache().invalidate();
    helix::ToolsUsedCache writer;
    writer.store("written.gcode", {3, 3}, {2});
    CHECK(helix::tools_used_cache().lookup("written.gcode", {3, 3}).has_value());
}
