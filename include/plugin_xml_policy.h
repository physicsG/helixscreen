// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helix::plugin {

/// One attribute of a plugin's XML. Nullopt when `name`/`value` is allowed for plugin
/// `id`; otherwise the rejection reason, as a sentence. `check_plugin_xml` applies this
/// to every attribute it walks, and the runtime paths that hand Lua-supplied attributes
/// to `lv_xml_create` apply it to those, so the file check and the runtime check are one
/// rule. The rules:
///  - an attribute named `callback` or `event_cb`, or ending `_callback` or `_cb`, has
///    the value `plugin_event`;
///  - `user_data` is an event target owned by `id`, optionally followed by `:<arg>`;
///  - an attribute named `subject`, ending `_subject`, or starting `bind_` names a
///    subject owned by `id`;
///  - an attribute named `cond` or ending `_cond` is an expression whose every
///    identifier is owned by `id`; numbers, operators, whitespace and parentheses are
///    the only other tokens allowed;
///  - `name` names an object owned by `id` (`check_plugin_xml` skips it on `prop`,
///    `style`, `remove_style*` and `bind_style*`, where it names no object);
///  - none of those attributes takes a `$prop` value, since a prop default cannot be
///    checked against the attribute it feeds.
std::optional<std::string> check_plugin_attr(std::string_view id, std::string_view name,
                                             std::string_view value);

/// Empty when `xml`, a component file of plugin `id`, references only what a plugin may;
/// otherwise the first violation, as a sentence. `components` are the component names
/// this plugin is registering (its `ui/*.xml` stems): the only components its XML may
/// place or extend, beyond the app allowlist. The rules:
///  - an element name, after stripping a leading `lv_obj-`, must be an `lv_*` builtin
///    widget, one of helix-xml's `lv_obj` children (`event_cb`, `bind_*`, `style`,
///    `remove_style*`, `subject_*_event`, `play_timeline_event`), one of `components`,
///    or an allowlisted app component (`overlay_panel`); screen load and create events
///    are never available, nor are the `subjects`, `images` and `fonts` sections;
///  - a `<view extends>` value obeys the same element rule;
///  - every attribute obeys `check_plugin_attr`.
std::string check_plugin_xml(std::string_view id, const std::vector<std::string>& components,
                             const std::string& xml);

} // namespace helix::plugin
