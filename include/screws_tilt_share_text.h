// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "calibration_types.h"

#include <string>
#include <vector>

/**
 * @file screws_tilt_share_text.h
 * @brief Plain-text serialization of a SCREWS_TILT_CALCULATE result set
 *
 * HelixScreen has no OS clipboard on a printer (SDL2 is not linked on any
 * shipping target, X11/Wayland are off), so the way results leave the screen
 * is a QR code the user scans with a phone. These helpers produce the payload
 * that QR encodes, and the per-column strings the share modal renders, from
 * one place so the two can never disagree.
 *
 * Pure logic — no LVGL, no translation lookups. The caller supplies the label
 * used for the reference ("base") screw so the on-screen rows can be
 * translated while the QR payload stays plain ASCII.
 */

namespace helix {

/// Label used for the reference screw inside the QR payload (untranslated).
inline constexpr const char* kScrewsTiltShareBaseLabel = "base";

/// Placeholder for a non-reference screw that reported no adjustment string.
inline constexpr const char* kScrewsTiltShareNoAdjustment = "--";

/// First line of the QR payload — identifies what the scanned text is.
inline constexpr const char* kScrewsTiltShareHeader = "HelixScreen bed screw results";

/**
 * @brief Format one screw's probed Z height for display/serialization
 * @return Fixed 3-decimal representation, e.g. "2.075"
 */
[[nodiscard]] std::string format_screw_share_z(const ScrewTiltResult& screw);

/**
 * @brief Format one screw's adjustment column
 *
 * @param screw     The result row
 * @param base_label Text used when the screw is the reference/base screw.
 *                   Pass a translated string for UI, the default for the QR
 *                   payload.
 * @return `base_label` for the reference screw, the raw adjustment string
 *         (e.g. "CW 00:05") otherwise, or "--" when it is empty.
 */
[[nodiscard]] std::string
format_screw_share_adjustment(const ScrewTiltResult& screw,
                              const char* base_label = kScrewsTiltShareBaseLabel);

/**
 * @brief Build the full plain-text payload encoded into the QR code
 *
 * Layout (LF-separated, no trailing newline):
 * @code
 * HelixScreen bed screw results
 * Front Left: z=2.075 CW 00:05
 * Rear Left: z=2.100 base
 * @endcode
 *
 * An empty result set yields the header followed by "(no results)".
 */
[[nodiscard]] std::string build_screws_tilt_share_text(const std::vector<ScrewTiltResult>& results);

} // namespace helix
