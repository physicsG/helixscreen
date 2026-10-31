// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/release_page.h"

#include "../catch_amalgamated.hpp"

using helix::release_page_url;

namespace {
constexpr const char* kTagBase = "https://github.com/prestonbrown/helixscreen/releases/tag/";
constexpr const char* kLatest = "https://github.com/prestonbrown/helixscreen/releases/latest";
} // namespace

TEST_CASE("release_page_url: the offered release's tag page", "[release_page]") {
    CHECK(release_page_url({"v1.2.3", "1.2.3", true}) == std::string(kTagBase) + "v1.2.3");
    CHECK(release_page_url({"", "1.2.3", true}) == std::string(kTagBase) + "v1.2.3");
    CHECK(release_page_url({"", "v1.2.3", true}) == std::string(kTagBase) + "v1.2.3");
    CHECK(release_page_url({"", "1.1.0-beta.3", true}) == std::string(kTagBase) + "v1.1.0-beta.3");
}

TEST_CASE("release_page_url: falls back to the latest release", "[release_page]") {
    SECTION("a channel not published on GitHub") {
        CHECK(release_page_url({"v1.3.0-dev.42", "1.3.0-dev.42", false}) == kLatest);
    }
    SECTION("no usable version") {
        CHECK(release_page_url({"", "", true}) == kLatest);
        CHECK(release_page_url({"", "v", true}) == kLatest);
    }
    SECTION("a manifest version never carries a path or query into the URL") {
        CHECK(release_page_url({"", "1.2.3/../../evil", true}) == kLatest);
        CHECK(release_page_url({"v1.2.3?x=1", "1.2.3", true}) == kLatest);
    }
}
