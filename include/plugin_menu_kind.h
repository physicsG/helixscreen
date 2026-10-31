// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string_view>

namespace helix::plugin {

/// The app menus a plugin can add items to.
enum class MenuKind { PrintFile, FilamentSlot, Spool, Tile };

inline constexpr MenuKind kMenuKinds[] = {MenuKind::PrintFile, MenuKind::FilamentSlot,
                                          MenuKind::Spool, MenuKind::Tile};

/// The manifest spelling: "print_file", "filament_slot", "spool", "tile".
constexpr const char* menu_kind_name(MenuKind k) {
    switch (k) {
    case MenuKind::PrintFile:
        return "print_file";
    case MenuKind::FilamentSlot:
        return "filament_slot";
    case MenuKind::Spool:
        return "spool";
    case MenuKind::Tile:
        return "tile";
    }
    return "";
}

constexpr std::optional<MenuKind> parse_menu_kind(std::string_view name) {
    for (MenuKind k : kMenuKinds) {
        if (name == menu_kind_name(k))
            return k;
    }
    return std::nullopt;
}

} // namespace helix::plugin
