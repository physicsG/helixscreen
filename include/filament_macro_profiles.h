// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file filament_macro_profiles.h
 * @brief What a printer's stock filament macros do for themselves.
 *
 * Most load/unload macros expect the hotend to already be at temperature, so
 * every dispatch surface heats first and waits. Some firmwares ship macros that
 * drive the hotend themselves, and preheating in front of one of those makes the
 * user wait twice and can settle on a different temperature than the macro then
 * commands.
 *
 * This module is the ONLY place that names which firmware's macros behave that
 * way. Generic code (the filament panel, the AMS sidebar, the shared dispatch
 * ladder) asks the capability question and never names a firmware.
 *
 * Adding a firmware means adding one row to the table in
 * filament_macro_profiles.cpp - no call site changes.
 *
 * The backend tier has its own answer to the same question,
 * AmsBackend::supports_auto_heat_on_load(). Both feed
 * helix::ui::preheat_skip_reason(), which is where a caller should ask.
 */

#pragma once

#include <string>

namespace helix::filament_macros {

/**
 * @brief Does @p macro_name bring the hotend to temperature on its own?
 *
 * Answered from the macro NAME alone, which is narrower than it looks: the
 * names in the table are firmware-specific spellings that carry no meaning in
 * stock Klipper, so a printer that defines one is running that firmware. A
 * generic name a user could plausibly write themselves (LOAD_FILAMENT,
 * UNLOAD_FILAMENT, M701, M702) is never in the table, because there the name
 * proves nothing about the body - those users are served by the
 * "Allow cold load/unload" setting instead.
 *
 * Klipper macro names are case-insensitive; @p macro_name may be in any case.
 *
 * @param macro_name Resolved macro name, e.g. StandardMacroInfo::get_macro()
 * @return true when a UI preheat in front of this macro would be redundant
 */
[[nodiscard]] bool macro_heats_hotend(const std::string& macro_name);

/**
 * @brief Does @p macro_name home the printer itself when the toolhead is unhomed?
 *
 * The macro-tier counterpart to AmsBackend::delegates_homing_to_printer(): when
 * true, any G28 a caller would synthesize is redundant, because the macro's
 * own guard runs first.
 *
 * Answered from the name for the same reason macro_heats_hotend() is, with the
 * same consequence for a wrong answer inverted: claiming a macro homes when it
 * does not moves an unreferenced toolhead. Only firmware-specific spellings whose
 * bodies are known belong here.
 *
 * @param macro_name Resolved macro name, e.g. StandardMacroInfo::get_macro()
 * @return true when the macro carries its own conditional home
 */
[[nodiscard]] bool macro_homes_if_needed(const std::string& macro_name);

} // namespace helix::filament_macros
