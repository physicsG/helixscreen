// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef ENABLE_GLES_3D

#include "gles2_loader.h"

#include "../catch_amalgamated.hpp"

namespace {

int g_lookups = 0;

void* count_and_miss(const char*) {
    ++g_lookups;
    return nullptr;
}

} // namespace

TEST_CASE("The GLES2 table is resolved once per process", "[gl_entry_points]") {
    g_lookups = 0;
    const bool first = helix::gles2::load(count_and_miss);
    const int after_first = g_lookups;
    const bool second = helix::gles2::load(count_and_miss);

    // Nothing resolves, so the first call fails having asked for the names...
    CHECK_FALSE(first);
    CHECK(after_first > 0);
    // ...and the second returns that answer without asking again.
    CHECK_FALSE(second);
    CHECK(g_lookups == after_first);
}

#endif
