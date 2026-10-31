// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// The Printer Manager's capability chips: one spec per chip (its widget name,
/// its info popup copy, the screen a tap opens) and the gathering of the facts
/// its text is composed from. Main thread only.

#include "capability_chip_text.h"

#include <array>
#include <string_view>

namespace helix {

struct CapChipSpec {
    CapChip chip;
    const char* name;         ///< widget name, "pm_chip_probe"
    const char* text_subject; ///< string subject its label binds, "pm_chip_text_probe"
    const char* info_title;   ///< untranslated key, lv_tr at use
    const char* info_message; ///< what it is, what HelixScreen does with it, where
    void (*open)();           ///< nullptr = info popup
};

/// One entry per CapChip, in enum order.
const std::array<CapChipSpec, kCapChipCount>& cap_chip_specs();

/// The spec whose widget name is @p name, or nullptr.
const CapChipSpec* find_cap_chip_spec(std::string_view name);

/// What the chip texts read, from PrinterState, the active API's discovery,
/// AmsState and the sensor managers.
CapabilityChipFacts gather_capability_chip_facts();

/// A tapped chip's action: its screen, or its info popup when it has none.
void open_cap_chip(const CapChipSpec& spec);

} // namespace helix
