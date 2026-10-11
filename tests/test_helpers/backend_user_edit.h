// tests/test_helpers/backend_user_edit.h
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A person's edit applied to a backend alone, with nothing filed on the lane.
#pragma once

#include "ams_backend.h"
#include "ams_types.h"
#include "lane_translation.h"

namespace helix::test {

/// Apply @p info to @p slot_index as a person's edit, through the backend alone.
///
/// The declaration is the slot as the backend reports it now, diffed against
/// @p info: what an editor opened on the slot this moment would declare.
/// Nothing reaches the lane. The declaration is not filed, the previous spool's
/// records are not dropped and the slot is not repainted, so a test that reads
/// the lane back wants edit_slot_as_user() (seeded_override.h) instead.
inline AmsError apply_edit(AmsBackend& backend, int slot_index, const SlotInfo& info) {
    return backend.apply_user_edit(
        slot_index, info,
        helix::ams::user_edit_observation(backend.get_slot_info(slot_index), info));
}

} // namespace helix::test
