// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_types.h"

namespace helix::ui {

/// After a slot edit commits, ask the question the backend's
/// edit_follow_up() poses, if any, and run its follow-up on yes. Main thread.
void offer_edit_follow_up(int slot_index, const SlotInfo& before, const SlotInfo& after);

} // namespace helix::ui
