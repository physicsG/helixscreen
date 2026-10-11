// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "mock_persona.h"
#include "scoped_env.h"

#include <string>
#include <string_view>
#include <vector>

namespace helix::test {

/// Selects persona `id` and pins every env var that changes the mock's objects
/// list, so a value another test in this shard leaked cannot add or remove
/// objects.
struct PersonaEnv {
    explicit PersonaEnv(std::string_view id)
        : printer("HELIX_MOCK_PRINTER", std::string(id).c_str()) {}
    helix::ScopedEnv printer;
    helix::ScopedEnv ams{"HELIX_MOCK_AMS", nullptr};
    helix::ScopedEnv objects{"HELIX_MOCK_OBJECTS", nullptr};
    helix::ScopedEnv probe{"HELIX_MOCK_PROBE_TYPE", nullptr};
    helix::ScopedEnv sensors{"HELIX_MOCK_FILAMENT_SENSORS", nullptr};
    helix::ScopedEnv kinematics{"HELIX_MOCK_KINEMATICS", nullptr};
};

/// The first persona of each distinct PrinterType, in persona-table order, so a
/// loop over "all printer types" covers a new persona without an edit.
inline std::vector<const helix::mock::PersonaEntry*> personas_one_per_type() {
    std::vector<const helix::mock::PersonaEntry*> out;
    for (const auto& p : helix::mock::PERSONAS) {
        bool seen = false;
        for (const auto* q : out) {
            seen = seen || q->type == p.type;
        }
        if (!seen) {
            out.push_back(&p);
        }
    }
    return out;
}

} // namespace helix::test
