// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace helix {
namespace ui {

/**
 * @brief Ask whether a freshly inserted spool is the one the lane held
 *
 * For an insert the hardware read nothing about (InsertVerdict::NoEvidence,
 * prestonbrown/helixscreen#1710). Non-blocking: the lane keeps its details
 * unless the user taps Clear, which runs Clear Spool through
 * ams_dispatch_backend_action(). Offers nothing on a lane with no details to
 * clear, nor on the lane feeding the print, where that clear would be refused.
 * A lane still showing the details it was asked about is not asked again for
 * five minutes, so a flapping gate sensor raises one notice, not one per edge.
 * Main thread only.
 *
 * Declared here, apart from ui_ams_detail.h, so a printer backend can post the
 * notice from its parse thread without pulling the LVGL panel world in.
 *
 * @param slot Slot index the spool went into
 */
void offer_clear_after_unverified_insert(int slot);

/// Forget every lane already asked about, so a test starts with no notice pending.
void reset_insert_offers_for_test();

} // namespace ui
} // namespace helix
