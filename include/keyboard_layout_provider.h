// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

/**
 * @file keyboard_layout_provider.h
 * @brief Keyboard layout data for on-screen keyboard
 *
 * Provides button maps and control arrays for different keyboard modes:
 * - Lowercase alphabet
 * - Uppercase alphabet (caps lock and one-shot)
 * - Numbers and symbols
 * - Alternative symbols
 *
 * Layouts are designed in Gboard style (no number row on alpha keyboard).
 */

/** @brief Keyboard mode enumeration */
enum keyboard_layout_mode_t {
    KEYBOARD_LAYOUT_ALPHA_LC,        ///< Lowercase alphabet
    KEYBOARD_LAYOUT_ALPHA_UC,        ///< Uppercase alphabet
    KEYBOARD_LAYOUT_NUMBERS_SYMBOLS, ///< Numbers and symbols (the ?123 page)
    KEYBOARD_LAYOUT_ALT_SYMBOLS,     ///< Alternative symbols (#+= mode)
    KEYBOARD_LAYOUT_NUMERIC          ///< Digits only — for keyboard_hint="numeric" fields
};

/**
 * @brief Get button map for a keyboard layout
 *
 * @param mode Keyboard mode
 * @param caps_lock_active true if caps lock is active (affects uppercase layout)
 * @return Button map array (null-terminated strings)
 */
const char* const* keyboard_layout_get_map(keyboard_layout_mode_t mode, bool caps_lock_active);

/**
 * @brief Get control map for a keyboard layout
 *
 * @param mode Keyboard mode
 * @return Control array (button widths and flags)
 */
const lv_buttonmatrix_ctrl_t* keyboard_layout_get_ctrl_map(keyboard_layout_mode_t mode);

/**
 * @brief Get spacebar text constant
 * @return Two-space string used for spacebar rendering
 */
const char* keyboard_layout_get_spacebar_text();
