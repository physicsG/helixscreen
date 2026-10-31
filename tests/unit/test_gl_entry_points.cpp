// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gl_entry_points.h"

#include <cstring>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::gl::EntryPoint;
using helix::gl::resolve_entry_points;

namespace {

int g_fake_target = 0;

/// Resolves every name except the one in g_missing.
const char* g_missing = nullptr;
void* fake_get_proc(const char* name) {
    if (g_missing && std::strcmp(name, g_missing) == 0) {
        return nullptr;
    }
    return &g_fake_target;
}

void* null_get_proc(const char*) {
    return nullptr;
}

} // namespace

TEST_CASE("resolve_entry_points fills every slot when all names resolve", "[gl_entry_points]") {
    void* a = nullptr;
    void* b = nullptr;
    EntryPoint points[] = {{"glClear", &a}, {"glViewport", &b}};
    g_missing = nullptr;

    const auto missing = resolve_entry_points(points, 2, fake_get_proc);

    CHECK(missing.empty());
    CHECK(a == &g_fake_target);
    CHECK(b == &g_fake_target);
}

TEST_CASE("resolve_entry_points is all-or-nothing", "[gl_entry_points]") {
    void* a = nullptr;
    void* b = nullptr;
    void* c = nullptr;
    EntryPoint points[] = {{"glClear", &a}, {"glReadPixels", &b}, {"glViewport", &c}};

    SECTION("one unresolved name clears every slot and is reported") {
        g_missing = "glReadPixels";
        const auto missing = resolve_entry_points(points, 3, fake_get_proc);
        REQUIRE(missing == std::vector<std::string>{"glReadPixels"});
        CHECK(a == nullptr);
        CHECK(b == nullptr);
        CHECK(c == nullptr);
    }

    SECTION("no GL library at all reports every name") {
        const auto missing = resolve_entry_points(points, 3, null_get_proc);
        CHECK(missing == std::vector<std::string>{"glClear", "glReadPixels", "glViewport"});
        CHECK(a == nullptr);
    }

    SECTION("a missing loader reports every name") {
        const auto missing = resolve_entry_points(points, 3, nullptr);
        CHECK(missing.size() == 3);
        CHECK(b == nullptr);
    }
    g_missing = nullptr;
}
