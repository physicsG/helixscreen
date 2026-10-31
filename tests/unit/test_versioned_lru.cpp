// SPDX-License-Identifier: GPL-3.0-or-later

#include "versioned_lru.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::FileVersion;
using helix::VersionedLru;

TEST_CASE("VersionedLru answers only for the version a value was kept for", "[versioned_lru]") {
    VersionedLru<int> lru;
    lru.put("a.gcode", FileVersion{10, 1}, 7);

    REQUIRE(lru.lookup("a.gcode", FileVersion{10, 1}) != nullptr);
    CHECK(*lru.lookup("a.gcode", FileVersion{10, 1}) == 7);
    CHECK(lru.lookup("a.gcode", FileVersion{11, 1}) == nullptr);
    CHECK(lru.lookup("a.gcode", FileVersion{10, 2}) == nullptr);
    CHECK(lru.lookup("b.gcode", FileVersion{10, 1}) == nullptr);
    // find() sees the entry whatever its version.
    REQUIRE(lru.find("a.gcode") != nullptr);
    CHECK(lru.find("a.gcode")->version == FileVersion{10, 1});
}

TEST_CASE("VersionedLru evicts the least recently used, never a pinned key", "[versioned_lru]") {
    VersionedLru<int> lru;
    lru.put("a", {}, 1);
    lru.put("b", {}, 2);
    lru.put("c", {}, 3);

    SECTION("a lookup keeps an entry") {
        REQUIRE(lru.lookup("a", {}) != nullptr);
        lru.evict(2);
        CHECK(lru.find("a") != nullptr);
        CHECK(lru.find("b") == nullptr);
        CHECK(lru.find("c") != nullptr);
    }
    SECTION("mark_used keeps an entry") {
        lru.mark_used("a");
        lru.evict(2);
        CHECK(lru.find("a") != nullptr);
        CHECK(lru.find("b") == nullptr);
    }
    SECTION("a pinned key survives even as the oldest") {
        lru.evict(1, [](const std::string& k) { return k == "a"; });
        CHECK(lru.size() == 1);
        CHECK(lru.find("a") != nullptr);
    }
    SECTION("only pinned keys left: the bound gives way") {
        lru.evict(0, [](const std::string& k) { return k == "a" || k == "c"; });
        CHECK(lru.size() == 2);
        CHECK(lru.find("b") == nullptr);
    }
}
