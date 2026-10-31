// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_xml_policy.h"

#include "helix-xml/src/libs/expat/expat.h"
#include "plugin_manifest.h"
#include "theme_manager.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace helix::plugin {

namespace {

struct Walk {
    std::string id;
    const std::vector<std::string>* components;
    std::string error;
    int consts_depth = 0; ///< inside a <consts> block
};

/// The value elements a <consts> block holds.
bool is_const_value_element(std::string_view el) {
    return el == "px" || el == "int" || el == "percentage" || el == "color" || el == "string";
}

/// App subjects a plugin may read in a conditional bind: the display tier and
/// orientation, so a plugin component can lay itself out per screen size.
bool is_readonly_layout_subject(std::string_view s) {
    return s == "ui_breakpoint" || s == "ui_breakpoint_v" || s == "ui_is_portrait";
}

// helix-xml resolves a child tag both as written and with an "lv_obj-" prefix
// (lv_xml_widget_get_processor), so every element comparison happens on the
// stripped name.
constexpr std::string_view kLvObjPrefix = "lv_obj-";

std::string_view strip_lv_obj_prefix(std::string_view el) {
    if (el.size() > kLvObjPrefix.size() && el.substr(0, kLvObjPrefix.size()) == kLvObjPrefix)
        el.remove_prefix(kLvObjPrefix.size());
    return el;
}

bool starts_with(std::string_view s, std::string_view head) {
    return s.size() >= head.size() && s.substr(0, head.size()) == head;
}

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size(), tail.size()) == tail;
}

// App widgets a plugin's own components may build on: chrome and presentational
// widgets whose only name-taking attributes are the generic bind_* and name= ones
// checked below. plugin_canvas is the plugin drawing surface and plugin_table the
// plugin data table; the name= of each is the registry key its data publishes under.
bool is_allowlisted_app_component(std::string_view name) {
    return name == "overlay_panel" || name == "ui_card" || name == "icon" ||
           name == "text_heading" || name == "text_body" || name == "text_muted" ||
           name == "text_small" || name == "text_xs" || name == "text_tiny" ||
           name == "plugin_canvas" || name == "plugin_table" || name == "ui_dialog" ||
           name == "modal_header" || name == "modal_button_row";
}

/// The props through which an allowlisted app component names its own objects.
const std::vector<const char*>* required_object_names(std::string_view el) {
    static const std::vector<const char*> kButtonRow = {
        "primary_name",    "secondary_name",        "tertiary_name",
        "quaternary_name", "tertiary_divider_name", "quaternary_divider_name"};
    static const std::vector<const char*> kHeader = {"icon_name", "title_name", "close_name"};
    if (el == "modal_button_row")
        return &kButtonRow;
    if (el == "modal_header")
        return &kHeader;
    return nullptr;
}

bool is_allowed_element(const Walk& w, std::string_view el) {
    // The closed allowlist below rejects the screen load and create events on its
    // own; the explicit check keeps that true if the allowlist ever grows.
    if (el == "screen_load_event" || el == "screen_create_event")
        return false;
    if (el == "component" || el == "view" || el == "api" || el == "prop")
        return true;
    if (starts_with(el, "lv_"))
        return true;
    // No subject_*_event here: it stores a raw subject pointer as event user_data with
    // no observer, so a plugin object could write into a subject its runtime already
    // freed. plugin_event plus s:set in Lua reaches the same effect by name.
    if (el == "event_cb" || el == "style" || el == "play_timeline_event" ||
        starts_with(el, "bind_") || starts_with(el, "remove_style"))
        return true;
    if (is_allowlisted_app_component(el))
        return true;
    return std::find(w.components->begin(), w.components->end(), el) != w.components->end();
}

bool is_word_operator(std::string_view w) {
    return w == "and" || w == "or" || w == "not" || w == "eq" || w == "ne" || w == "lt" ||
           w == "le" || w == "gt" || w == "ge";
}

// helix-xml's expression grammar (lv_xml_expr.c) knows identifiers, integers, word
// operators and the symbol operators below; any other character is a lex error there,
// so here it is a rejection rather than a guess. Every identifier left after the
// operators must be a subject the plugin owns.
bool cond_identifiers_owned(std::string_view id, std::string_view expr, std::string& why) {
    auto alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    size_t i = 0;
    while (i < expr.size()) {
        char c = expr[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
        } else if (c >= '0' && c <= '9') {
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9')
                ++i;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            size_t start = i;
            while (i < expr.size() && (alnum(expr[i]) || expr[i] == '_'))
                ++i;
            std::string_view word = expr.substr(start, i - start);
            if (!is_word_operator(word) && !is_owned_name(id, word) &&
                !is_readonly_layout_subject(word)) {
                why = "cond may name only subjects owned by the plugin";
                return false;
            }
        } else if (c == '(' || c == ')' || c == '+' || c == '-' || c == '*' || c == '/' ||
                   c == '%' || c == '&' || c == '|' || c == '!' || c == '=' || c == '<' ||
                   c == '>') {
            ++i;
        } else {
            why = "cond is not a valid expression";
            return false;
        }
    }
    return true;
}

void fail(Walk& w, std::string msg) {
    if (w.error.empty())
        w.error = std::move(msg);
}

void on_start(void* ud, const XML_Char* name, const XML_Char** attrs) {
    auto& w = *static_cast<Walk*>(ud);
    std::string_view el = strip_lv_obj_prefix(name);
    if (el == "consts") {
        ++w.consts_depth;
        return;
    }
    if (is_const_value_element(el)) {
        if (w.consts_depth == 0) {
            fail(w, "<" + std::string(name) + "> belongs inside <consts>");
            return;
        }
        for (int i = 0; attrs[i]; i += 2) {
            if (std::string_view(attrs[i]) == "name" && !is_owned_name(w.id, attrs[i + 1]))
                fail(w, "constant '" + std::string(attrs[i + 1]) + "' must be named " + w.id +
                            "__<name>");
        }
        return;
    }
    if (!is_allowed_element(w, el)) {
        fail(w, "<" + std::string(name) + "> is not available to plugins");
        return;
    }
    if (el == "view") {
        for (int i = 0; attrs[i]; i += 2) {
            if (std::string_view(attrs[i]) != "extends")
                continue;
            std::string_view val = attrs[i + 1];
            if (!is_allowed_element(w, strip_lv_obj_prefix(val))) {
                fail(w, "extends=\"" + std::string(val) + "\": <" + std::string(val) +
                            "> is not available to plugins");
            }
        }
    }
    // App chrome that names its own objects by default must be given owned names,
    // or a default (btn_primary, header_title) would land on screen.
    if (const auto* required = required_object_names(el)) {
        for (const char* prop : *required) {
            bool given = false;
            for (int i = 0; attrs[i] && !given; i += 2)
                given = std::string_view(attrs[i]) == prop;
            if (!given)
                fail(w, "<" + std::string(el) + "> needs " + prop + "=\"" + w.id +
                            "__<name>\": its default name is the app's");
        }
    }
    // An icon source carrying the ownership separator is a plugin image icon, and a
    // plugin draws only its own.
    if (el == "icon") {
        for (int i = 0; attrs[i]; i += 2) {
            std::string_view val = attrs[i + 1];
            if (std::string_view(attrs[i]) == "src" &&
                val.find(kPluginNameSeparator) != std::string_view::npos &&
                !is_owned_name(w.id, val))
                fail(w, "src=\"" + std::string(val) + "\": an image icon must be named " + w.id +
                            "__<name>");
        }
    }
    // On these elements name= declares a prop or refers to a style (style,
    // remove_style*, bind_style*); it names no object.
    bool names_object = el != "prop" && el != "style" && !starts_with(el, "remove_style") &&
                        !starts_with(el, "bind_style");
    const bool conditional_bind =
        starts_with(el, "bind_flag_if") || starts_with(el, "bind_style_if");
    for (int i = 0; attrs[i]; i += 2) {
        if (!names_object && std::string_view(attrs[i]) == "name")
            continue;
        if (conditional_bind && std::string_view(attrs[i]) == "subject" &&
            is_readonly_layout_subject(attrs[i + 1]))
            continue;
        if (auto why = check_plugin_attr(w.id, attrs[i], attrs[i + 1],
                                         required_object_names(el) != nullptr))
            fail(w, std::move(*why));
    }
}

void on_end(void* ud, const XML_Char* name) {
    auto& w = *static_cast<Walk*>(ud);
    if (strip_lv_obj_prefix(name) == "consts" && w.consts_depth > 0)
        --w.consts_depth;
}

} // namespace

std::optional<std::string> check_plugin_attr(std::string_view id, std::string_view name,
                                             std::string_view value, bool chrome_props) {
    // Base font tokens only: a size-suffixed variant names a face AssetManager
    // registers from its tier up, so on a smaller display the XML engine
    // silently substitutes the default font.
    if (name == "style_text_font" && starts_with(value, "#font_") &&
        !theme_manager_font_token_is_base(value.data() + 1))
        return "style_text_font=\"" + std::string(value) +
               "\": plugins may use only base font tokens; a size-suffixed variant renders as "
               "the default font below its tier";
    bool is_callback = name == "callback" || name == "event_cb" || ends_with(name, "_callback") ||
                       ends_with(name, "_cb");
    bool is_subject =
        name == "subject" || ends_with(name, "_subject") || starts_with(name, "bind_");
    bool is_cond = name == "cond" || ends_with(name, "_cond");
    // The app's dialog chrome takes its objects' names and its buttons' targets
    // through props (modal_button_row's primary_name, primary_user_data); on a
    // plugin's own components those suffixes are ordinary props.
    bool is_target = name == "user_data" || (chrome_props && ends_with(name, "_user_data"));
    // Screen-wide lookups by name (lv_obj_find_by_name from lv_layer_top or the
    // screen) must never resolve to a plugin object.
    bool is_object_name = name == "name" || (chrome_props && ends_with(name, "_name"));
    if (!is_callback && !is_subject && !is_cond && !is_target && !is_object_name)
        return std::nullopt;
    if (!value.empty() && value.front() == '$')
        return std::string(name) + "=\"" + std::string(value) + "\": plugins cannot pass " +
               std::string(name) + " through a prop";
    if (is_callback && value != "plugin_event")
        return std::string(name) + "=\"" + std::string(value) +
               "\": plugins may only use the plugin_event callback";
    if (is_subject && !is_owned_name(id, value))
        return std::string(name) + "=\"" + std::string(value) + "\": subject must be named " +
               std::string(id) + "__<name>";
    if (is_object_name && !is_owned_name(id, value))
        return std::string(name) + "=\"" + std::string(value) + "\": object names must be " +
               std::string(id) + "__<name>";
    if (is_target && !is_owned_name(id, value.substr(0, value.find(':'))))
        return std::string(name) + "=\"" + std::string(value) + "\": handler must be named " +
               std::string(id) + "__<name>";
    if (is_cond) {
        std::string why;
        if (!cond_identifiers_owned(id, value, why))
            return std::string(name) + "=\"" + std::string(value) + "\": " + why;
    }
    return std::nullopt;
}

std::string check_plugin_xml(std::string_view id, const std::vector<std::string>& components,
                             const std::string& xml) {
    Walk w{std::string(id), &components, {}};
    XML_Parser p = XML_ParserCreate(nullptr);
    if (!p)
        return "cannot check XML: out of memory";
    XML_SetUserData(p, &w);
    XML_SetElementHandler(p, &on_start, &on_end);
    if (XML_Parse(p, xml.data(), static_cast<int>(xml.size()), 1) == XML_STATUS_ERROR)
        fail(w, std::string("not valid XML: ") + XML_ErrorString(XML_GetErrorCode(p)));
    XML_ParserFree(p);
    return w.error;
}

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
