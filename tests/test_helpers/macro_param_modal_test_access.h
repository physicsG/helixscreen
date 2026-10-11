// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "macro_param_modal.h"

#include <cstddef>

namespace helix {

/// Test-only access to MacroParamModal's field bookkeeping.
struct MacroParamModalTestAccess {
    /// How many field slots the modal holds. collect_values() pairs slot i with
    /// parameter i, so this must equal the parameter count even when a field
    /// could not be built.
    static std::size_t field_slot_count(const MacroParamModal& modal) {
        return modal.textareas_.size();
    }
};

} // namespace helix
