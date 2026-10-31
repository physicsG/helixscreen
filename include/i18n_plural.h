// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace helix::ui {

/**
 * @brief A CLDR plural category for an integer count.
 *
 * Only the categories a shipped language's integer rules can produce are
 * listed: no shipped language uses `zero` or `two`.
 * https://www.unicode.org/cldr/charts/latest/supplemental/language_plural_rules.html
 */
enum class PluralCategory : uint8_t {
    One,
    Few,
    Many,
    Other,
};

/**
 * @brief The CLDR plural category of integer @p n in language @p lang.
 *
 * @p lang is a language code ("ru", "pt", "zh"); a region suffix ("pt_BR",
 * "zh-CN") is ignored. An unknown language gets the English rule.
 */
PluralCategory plural_category(std::string_view lang, uint64_t n);

/** @brief The CLDR keyword for @p c: "one", "few", "many" or "other". */
const char* plural_category_name(PluralCategory c);

/**
 * @brief The translation tag that carries @p other_key's form for @p c.
 *
 * A YAML entry written as a mapping of categories (`one:`/`few:`/`many:`/
 * `other:`) is emitted by scripts/generate_translations.py as the plain tag
 * holding `other`, plus `<other_key>_<category>` for each other category.
 * For Other this is @p other_key itself.
 */
std::string plural_variant_tag(std::string_view other_key, PluralCategory c);

/**
 * @brief Translate a count string, choosing the form for @p n.
 *
 * - n == 1 with a @p one_key: the translation of @p one_key, so a singular
 *   written without a placeholder ("1 hour ago") keeps working.
 * - otherwise the `<other_key>_<category>` variant for @p n's category in
 *   @p lang, when the language's pack carries one;
 * - otherwise the translation of @p other_key.
 *
 * Callers format the result with the same arguments whichever form comes
 * back; printf and {fmt} both ignore arguments a format does not use. Both
 * keys must be string literals: scripts/translations/extractor.py and the
 * format-specifier gate read them from the call.
 *
 * @param one_key  English singular, or nullptr when there is none
 * @param other_key English plural; the key the plural forms hang off
 * @return a string that lives until lv_translation_deinit(), or @p one_key /
 *         @p other_key themselves
 */
const char* tr_plural_in(std::string_view lang, const char* one_key, const char* other_key,
                         uint64_t n);

/**
 * @brief tr_plural_in() for the language LVGL is translating into.
 *
 * Main thread only. It reads lv_translation_get_language() without the
 * translation lock (LVGL exposes no locked accessor), and
 * lv_translation_set_language() frees that string on the main thread, so a
 * call from another thread can read freed memory during a language switch.
 * lv_tr() itself is safe from any thread; off the main thread, pass the
 * language to tr_plural_in() instead.
 */
const char* tr_plural(const char* one_key, const char* other_key, uint64_t n);

} // namespace helix::ui
