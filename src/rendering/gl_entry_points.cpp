// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gl_entry_points.h"

namespace helix::gl {

std::vector<std::string> resolve_entry_points(EntryPoint* points, size_t count,
                                              GetProcFn get_proc) {
    std::vector<std::string> missing;
    for (size_t i = 0; i < count; ++i) {
        void* fn = get_proc ? get_proc(points[i].name) : nullptr;
        *points[i].slot = fn;
        if (!fn) {
            missing.emplace_back(points[i].name);
        }
    }
    if (!missing.empty()) {
        for (size_t i = 0; i < count; ++i) {
            *points[i].slot = nullptr;
        }
    }
    return missing;
}

} // namespace helix::gl
