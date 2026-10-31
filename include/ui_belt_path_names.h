// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "belt_tension_types.h"

#include <string>

namespace helix::ui {

/// What the Belt Tension screens call one path, translated. Until the user
/// says which belt is which, both forms are "Path A" / "Path B".
struct BeltPathName {
    std::string title;  ///< "Left belt": labels, headers, the start of a sentence
    std::string phrase; ///< "the left belt": inside a sentence
};

/// The one source of every user-visible path name on the Belt Tension screens.
[[nodiscard]] BeltPathName belt_path_name(helix::calibration::BeltPath path,
                                          helix::calibration::BeltLayout layout);

/// belt_path_name() under the active printer's saved layout.
[[nodiscard]] BeltPathName belt_path_name(helix::calibration::BeltPath path);

/// Which path RESULTS shows in header slot @p slot (0 = left, 1 = right). A
/// layout naming A the right belt puts B on the left, so the screen matches
/// the printer; every other layout, and none, keeps A on the left.
[[nodiscard]] helix::calibration::BeltPath belt_path_in_slot(int slot,
                                                             helix::calibration::BeltLayout layout);

/// belt_path_in_slot() under the active printer's saved layout.
[[nodiscard]] helix::calibration::BeltPath belt_path_in_slot(int slot);

} // namespace helix::ui
