// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_error.h"

#include <any>
#include <cmath>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

/**
 * @file ams_device_action_value.h
 * @brief Reading the value a DeviceAction control hands to execute_device_action()
 *
 * One reader for every backend and for the UI that builds the controls, so the
 * two ends agree on what a value is. Numbers are read numerically, not by their
 * C++ type: a slider sends a float, a test or a backend's own current_value may
 * hold a double or an int, and all of them are the same setting. Pointer-form
 * any_cast only: the firmware builds without exceptions or RTTI.
 */

namespace helix::printer {

/// The number @p value holds, whatever arithmetic type carries it; nullopt when it
/// holds no value, a non-number, or a NaN/infinity a setting cannot take.
[[nodiscard]] inline std::optional<double> device_action_number(const std::any& value) {
    double out = 0.0;
    if (const auto* f = std::any_cast<float>(&value)) {
        out = *f;
    } else if (const auto* d = std::any_cast<double>(&value)) {
        out = *d;
    } else if (const auto* i = std::any_cast<int>(&value)) {
        out = *i;
    } else if (const auto* l = std::any_cast<long>(&value)) {
        out = static_cast<double>(*l);
    } else {
        return std::nullopt;
    }
    if (!std::isfinite(out)) {
        return std::nullopt;
    }
    return out;
}

/// Whether two action values say the same thing: both empty, the same bool, the
/// same string, or the same number whatever type carries it.
[[nodiscard]] inline bool device_action_values_equal(const std::any& a, const std::any& b) {
    if (!a.has_value() || !b.has_value()) {
        return a.has_value() == b.has_value();
    }
    if (const auto* x = std::any_cast<bool>(&a)) {
        const auto* y = std::any_cast<bool>(&b);
        return y != nullptr && *x == *y;
    }
    if (const auto* x = std::any_cast<std::string>(&a)) {
        const auto* y = std::any_cast<std::string>(&b);
        return y != nullptr && *x == *y;
    }
    const auto x = device_action_number(a);
    const auto y = device_action_number(b);
    return x && y && *x == *y;
}

/**
 * @brief The value of a toggle (bool), slider (float/double/int) or dropdown
 *        (std::string) action, or the refusal to return instead.
 *
 * A missing value is refused, the same as a value of the wrong kind: an action
 * that takes a value never guesses one. A backend whose command genuinely has a
 * meaning without a value tests `value.has_value()` before calling this and
 * says why at that call.
 *
 * @param label What the value is, in English, for the messages ("speed",
 *              "bowden length"). Translated here.
 */
template <typename T>
[[nodiscard]] std::pair<T, AmsError> device_action_value(const std::any& value, const char* label) {
    static_assert(std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
                      std::is_arithmetic_v<T>,
                  "device_action_value reads bool, std::string or a number");
    if (!value.has_value()) {
        return {T{},
                AmsError(AmsResult::WRONG_STATE, fmt::format("{} value required", label),
                         lv_tr("Missing value"), fmt::format(lv_tr("Provide a {}"), lv_tr(label)))};
    }
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
        if (const auto* v = std::any_cast<T>(&value)) {
            return {*v, AmsErrorHelper::success()};
        }
    } else {
        if (const auto n = device_action_number(value)) {
            return {static_cast<T>(*n), AmsErrorHelper::success()};
        }
    }
    return {T{}, AmsError(AmsResult::WRONG_STATE, fmt::format("Invalid {} type", label),
                          lv_tr("Invalid value type"),
                          fmt::format(lv_tr("Provide a {}"), lv_tr(label)))};
}

/// The macro a macro-picker dropdown chose (kAutoMacro or a macro name), refused
/// when missing, not a string, or empty.
[[nodiscard]] inline std::pair<std::string, AmsError>
device_action_macro_choice(const std::any& value) {
    auto [choice, err] = device_action_value<std::string>(value, "macro");
    if (err.success() && choice.empty()) {
        err = AmsErrorHelper::invalid_parameter("No macro selected");
    }
    return {std::move(choice), std::move(err)};
}

} // namespace helix::printer
