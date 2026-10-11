// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string_view>

namespace helix::sensors {

/// One row of an enum's persisted-id / display-name table.
template <typename E> struct EnumName {
    E value;
    const char* id;
    const char* display;
};

/// Persisted id of `value`; the first row's id if `value` has no row.
template <typename E, size_t N> const char* enum_id(const EnumName<E> (&table)[N], E value) {
    for (const auto& row : table) {
        if (row.value == value) {
            return row.id;
        }
    }
    return table[0].id;
}

/// Display name of `value`; the first row's if `value` has no row.
template <typename E, size_t N> const char* enum_display(const EnumName<E> (&table)[N], E value) {
    for (const auto& row : table) {
        if (row.value == value) {
            return row.display;
        }
    }
    return table[0].display;
}

/// Value whose id is `id`; the first row's value if none matches.
template <typename E, size_t N> E enum_from_id(const EnumName<E> (&table)[N], std::string_view id) {
    for (const auto& row : table) {
        if (id == row.id) {
            return row.value;
        }
    }
    return table[0].value;
}

} // namespace helix::sensors
