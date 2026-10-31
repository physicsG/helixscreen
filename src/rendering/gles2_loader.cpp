// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef ENABLE_GLES_3D

#include "gles2_loader.h"

#include "gl_entry_points.h"

#include <spdlog/spdlog.h>

#include <iterator>
#include <mutex>

namespace helix::gles2 {

#define HELIX_GLES2_DEFINE(type, name) type name = nullptr;
HELIX_GLES2_ENTRY_POINTS(HELIX_GLES2_DEFINE)
#undef HELIX_GLES2_DEFINE

bool load(void* (*get_proc)(const char*)) {
    // One table for the process, filled once: the renderer, the blur and the
    // probe each ask, possibly from different threads, and a second fill would
    // write slots another thread is calling through.
    static std::once_flag once;
    static bool loaded = false;
    std::call_once(once, [get_proc] {
#define HELIX_GLES2_ENTRY(type, name) {#name, reinterpret_cast<void**>(&name)},
        helix::gl::EntryPoint points[] = {HELIX_GLES2_ENTRY_POINTS(HELIX_GLES2_ENTRY)};
#undef HELIX_GLES2_ENTRY

        const auto missing = helix::gl::resolve_entry_points(points, std::size(points), get_proc);
        if (!missing.empty()) {
            spdlog::warn("[GLES2] {} of {} entry points unavailable (first: {})", missing.size(),
                         std::size(points), missing.front());
            return;
        }
        loaded = true;
    });
    return loaded;
}

} // namespace helix::gles2

#endif
