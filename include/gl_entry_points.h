// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace helix::gl {

/// One GL function to resolve at runtime: its name and the pointer to fill.
struct EntryPoint {
    const char* name;
    void** slot;
};

/// Looks a GL function up by name, as SDL_GL_GetProcAddress does.
using GetProcFn = void* (*)(const char*);

/// Resolve every entry point through @p get_proc.
///
/// All or nothing: when any name fails to resolve, every slot is cleared, so a
/// renderer can never run against a half-loaded function table. GL-header-free
/// so the rule is testable on hosts without GLES.
///
/// @return the names that did not resolve, in table order; empty on success
std::vector<std::string> resolve_entry_points(EntryPoint* points, size_t count, GetProcFn get_proc);

} // namespace helix::gl
