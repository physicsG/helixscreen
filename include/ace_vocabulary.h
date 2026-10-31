// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

/**
 * @file ace_vocabulary.h
 * @brief Anycubic ACE Pro names that discovery and the backends share.
 *
 * Kept free of LVGL and the AMS types: printer discovery compiles into the
 * ESP32 firmware, which has neither.
 */
namespace helix::ace {

/// What the ACEPRO driver names each unit's Klipper object, before its
/// instance number (`ace_instance_0`, `ace_instance_1`, ...).
inline constexpr std::string_view kInstanceObjectPrefix = "ace_instance_";

/// The conventional bypass wrapper macros. The ACE's filament path has a master
/// switch but no bypass command, so these are what a stock config defines; an
/// owner whose macros are named differently picks theirs in the device operations.
inline constexpr const char* kBypassOnMacro = "ACE_BYPASS_ON";
inline constexpr const char* kBypassOffMacro = "ACE_BYPASS_OFF";

} // namespace helix::ace
