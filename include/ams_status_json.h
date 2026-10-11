// include/ams_status_json.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "hv/json.hpp"

namespace helix::ams {

/// The value of @p v as a T, or nullopt when it is not one. Numbers read as any
/// arithmetic T except bool, which only a JSON boolean satisfies.
template <typename T> [[nodiscard]] std::optional<T> read_scalar(const nlohmann::json& v) {
    if constexpr (std::is_same_v<T, bool>) {
        if (v.is_boolean()) {
            return v.get<bool>();
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (v.is_string()) {
            return v.get<std::string>();
        }
    } else if constexpr (std::is_arithmetic_v<T>) {
        if (v.is_number()) {
            return v.get<T>();
        }
    }
    return std::nullopt;
}

/// A JSON integer (not a float) as an int; nullopt for anything else.
[[nodiscard]] inline std::optional<int> read_integer(const nlohmann::json& v) {
    if (v.is_number_integer()) {
        return v.get<int>();
    }
    return std::nullopt;
}

/// The member @p key of @p obj as a T, or nullopt when it is absent, null or of
/// another type. A delta frame omits what did not change, so absent is "no
/// change" and never a default.
template <typename T>
[[nodiscard]] std::optional<T> read_field(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    return it == obj.end() ? std::nullopt : read_scalar<T>(*it);
}

/// The member @p key of @p obj as an integer (floats refused), or nullopt.
[[nodiscard]] inline std::optional<int> read_integer_field(const nlohmann::json& obj,
                                                           const char* key) {
    const auto it = obj.find(key);
    return it == obj.end() ? std::nullopt : read_integer(*it);
}

/// Every entry of the array @p obj[key], each read by @p reader, for a lane
/// array whose length is the firmware's to decide. nullopt when the key is
/// missing or not an array; otherwise one entry per array element, nullopt where
/// @p reader refused it, so the size stays what the frame sent.
template <typename T, typename Reader = std::optional<T> (*)(const nlohmann::json&)>
[[nodiscard]] std::optional<std::vector<std::optional<T>>>
read_array(const nlohmann::json& obj, const char* key, Reader reader = read_scalar<T>) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        return std::nullopt;
    }
    std::vector<std::optional<T>> out;
    out.reserve(it->size());
    for (const auto& entry : *it) {
        out.push_back(reader(entry));
    }
    return out;
}

/// The first N entries of the array @p obj[key], each read by @p reader. An
/// entry is nullopt when the array is missing, shorter than its index, or holds
/// something @p reader refuses, so a delta frame that omits or mistypes a lane
/// says nothing about it.
template <typename T, std::size_t N, typename Reader = std::optional<T> (*)(const nlohmann::json&)>
[[nodiscard]] std::array<std::optional<T>, N>
read_indexed(const nlohmann::json& obj, const char* key, Reader reader = read_scalar<T>) {
    std::array<std::optional<T>, N> out;
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        return out;
    }
    for (std::size_t i = 0; i < N && i < it->size(); ++i) {
        out[i] = reader((*it)[i]);
    }
    return out;
}

} // namespace helix::ams
