// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_manifest.h"

#include <algorithm>
#include <set>

namespace helix::plugin {

namespace {

bool is_lower_or_digit(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool is_valid_setting_key(std::string_view k) {
    if (k.empty() || k.size() > 64)
        return false;
    return std::all_of(k.begin(), k.end(), [](char c) { return is_lower_or_digit(c) || c == '_'; });
}

std::optional<SettingType> setting_type_from(const std::string& s) {
    static const std::pair<const char*, SettingType> kTypes[] = {
        {"bool", SettingType::Bool},     {"int", SettingType::Int},
        {"float", SettingType::Float},   {"enum", SettingType::Enum},
        {"string", SettingType::String}, {"action", SettingType::Action},
        {"info", SettingType::Info},
    };
    for (const auto& [n, t] : kTypes) {
        if (s == n)
            return t;
    }
    return std::nullopt;
}

void require_string(const json& j, const char* key, std::string& out,
                    std::vector<std::string>& errors) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        errors.push_back(std::string("'") + key + "' must be a non-empty string");
        return;
    }
    out = it->get<std::string>();
}

void optional_string(const json& j, const char* key, std::string& out,
                     std::vector<std::string>& errors) {
    auto it = j.find(key);
    if (it == j.end())
        return;
    if (!it->is_string()) {
        errors.push_back(std::string("'") + key + "' must be a string");
        return;
    }
    out = it->get<std::string>();
}

// The consent dialog's header line and the plugin rows print these fields
// verbatim, so a control character could forge dialog text and a runaway
// length could push the permission lines off the visible body.
constexpr size_t kMaxNameBytes = 48;
constexpr size_t kMaxVersionBytes = 32;
constexpr size_t kMaxAuthorBytes = 64;

void check_display_field(const char* key, const std::string& v, size_t max_bytes,
                         std::vector<std::string>& errors) {
    bool single_line = std::none_of(v.begin(), v.end(), [](char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7f;
    });
    if (v.size() > max_bytes || !single_line) {
        errors.push_back(std::string("'") + key + "' must be a single line of at most " +
                         std::to_string(max_bytes) + " bytes");
    }
}

void check_range(SettingDecl& d, const json& s, std::vector<std::string>& local) {
    auto mn = s.find("min");
    auto mx = s.find("max");
    if (mn == s.end() || mx == s.end() || !mn->is_number() || !mx->is_number() ||
        mn->get<double>() >= mx->get<double>()) {
        local.push_back("'min' and 'max' must be numbers with min < max");
        return;
    }
    d.min = mn->get<double>();
    d.max = mx->get<double>();
    if (d.default_value.is_null())
        return;
    bool ok = d.default_value.is_number() && d.default_value.get<double>() >= d.min &&
              d.default_value.get<double>() <= d.max &&
              (d.type != SettingType::Int || d.default_value.is_number_integer());
    if (!ok)
        local.push_back("'default' must be a number within [min, max]");
}

void check_enum(SettingDecl& d, const json& s, std::vector<std::string>& local) {
    auto opts = s.find("options");
    if (opts == s.end() || !opts->is_array() || opts->empty()) {
        local.push_back("'options' must be a non-empty array of strings");
        return;
    }
    for (const auto& o : *opts) {
        if (!o.is_string()) {
            local.push_back("'options' must be a non-empty array of strings");
            return;
        }
        const std::string& opt = o.get_ref<const std::string&>();
        // The settings screen joins options with '\n' into one dropdown, so an
        // option containing a newline would split into wrong entries.
        if (opt.find('\n') != std::string::npos) {
            local.push_back("'options' entries must not contain newlines");
            return;
        }
        d.options.push_back(opt);
    }
    if (!d.default_value.is_null() &&
        (!d.default_value.is_string() ||
         std::find(d.options.begin(), d.options.end(), d.default_value.get<std::string>()) ==
             d.options.end()))
        local.push_back("'default' must be one of 'options'");
}

void parse_setting(const std::string& id, const json& s, size_t index, Manifest& m,
                   std::set<std::string>& seen, std::vector<std::string>& errors) {
    std::string where = "settings[" + std::to_string(index) + "]";
    if (!s.is_object()) {
        errors.push_back(where + " must be an object");
        return;
    }
    auto key_it = s.find("key");
    if (key_it == s.end() || !key_it->is_string() ||
        !is_valid_setting_key(key_it->get_ref<const std::string&>())) {
        errors.push_back(where + ": 'key' must match [a-z0-9_]{1,64}");
        return;
    }
    SettingDecl d;
    d.key = key_it->get<std::string>();
    if (!seen.insert(d.key).second) {
        errors.push_back(where + ": duplicate key '" + d.key + "'");
        return;
    }

    std::vector<std::string> local;
    require_string(s, "label", d.label, local);
    std::string type_name;
    require_string(s, "type", type_name, local);
    std::optional<SettingType> type =
        type_name.empty() ? std::nullopt : setting_type_from(type_name);
    if (!type_name.empty() && !type)
        local.push_back("unknown type '" + type_name + "'");
    if (type)
        d.type = *type;
    if (auto it = s.find("default"); it != s.end())
        d.default_value = *it;

    if (type == SettingType::Int || type == SettingType::Float) {
        check_range(d, s, local);
    } else if (type == SettingType::Enum) {
        check_enum(d, s, local);
    } else if (type == SettingType::Bool) {
        if (!d.default_value.is_null() && !d.default_value.is_boolean())
            local.push_back("'default' must be true or false");
    } else if (type == SettingType::String) {
        if (!d.default_value.is_null() && !d.default_value.is_string())
            local.push_back("'default' must be a string");
    } else if (type == SettingType::Action) {
        optional_string(s, "callback", d.callback, local);
        if (!is_owned_name(id, d.callback))
            local.push_back("'callback' must be named " + id + "__<name>");
    } else if (type == SettingType::Info) {
        optional_string(s, "subject", d.subject, local);
        if (!is_owned_name(id, d.subject))
            local.push_back("'subject' must be named " + id + "__<name>");
    }

    for (auto& e : local)
        errors.push_back(where + " ('" + d.key + "'): " + e);
    if (local.empty())
        m.settings.push_back(std::move(d));
}

/// Reads a span written in cells into `out` in tracks. A widget with
/// `half_cells` may write halves (1.5); any other must write whole cells.
void span_field(const json& w, const char* key, int& out, int lo, bool half,
                std::vector<std::string>& local) {
    auto it = w.find(key);
    if (it == w.end())
        return;
    const double cells = it->is_number() ? it->get<double>() : -1;
    const double tracks = cells * 2;
    const bool on_step = half ? tracks == static_cast<int>(tracks) : it->is_number_integer();
    if (!on_step || cells < lo || cells > kMaxWidgetCells) {
        local.push_back(std::string("'") + key + "' must be " +
                        (half ? "a whole or half number" : "an integer") + " from " +
                        std::to_string(lo) + " to " + std::to_string(kMaxWidgetCells));
        return;
    }
    out = static_cast<int>(tracks);
}

void parse_widgets(const json& arr, Manifest& m, std::vector<std::string>& errors) {
    if (!arr.is_array()) {
        errors.push_back("'widgets' must be an array");
        return;
    }
    if (arr.size() > kMaxWidgetsPerPlugin) {
        errors.push_back("'widgets' may list at most " + std::to_string(kMaxWidgetsPerPlugin));
        return;
    }
    std::set<std::string> seen;
    for (size_t i = 0; i < arr.size(); ++i) {
        const std::string where = "widgets[" + std::to_string(i) + "]";
        const json& w = arr[i];
        if (!w.is_object()) {
            errors.push_back(where + " must be an object");
            continue;
        }
        WidgetDecl d;
        std::vector<std::string> local;
        require_string(w, "id", d.id, local);
        require_string(w, "name", d.name, local);
        require_string(w, "component", d.component, local);
        optional_string(w, "icon", d.icon, local);
        optional_string(w, "description", d.description, local);
        if (!d.id.empty() && !is_owned_name(m.id, d.id))
            local.push_back("'id' must be named " + m.id + "__<name>");
        if (!d.component.empty() && !is_owned_name(m.id, d.component))
            local.push_back("'component' must be named " + m.id + "__<name>");
        if (auto h = w.find("half_cells"); h != w.end()) {
            if (h->is_boolean())
                d.half_cells = h->get<bool>();
            else
                local.push_back("'half_cells' must be true or false");
        }
        span_field(w, "colspan", d.colspan, 1, d.half_cells, local);
        span_field(w, "rowspan", d.rowspan, 1, d.half_cells, local);
        span_field(w, "max_colspan", d.max_colspan, 0, d.half_cells, local);
        span_field(w, "max_rowspan", d.max_rowspan, 0, d.half_cells, local);
        if (d.max_colspan != 0 && d.max_colspan < d.colspan)
            local.push_back("'max_colspan' must be 0 or at least 'colspan'");
        if (d.max_rowspan != 0 && d.max_rowspan < d.rowspan)
            local.push_back("'max_rowspan' must be 0 or at least 'rowspan'");
        if (!d.id.empty() && !seen.insert(d.id).second)
            local.push_back("duplicate widget id '" + d.id + "'");
        for (const auto& e : local)
            errors.push_back(where + ": " + e);
        if (local.empty())
            m.widgets.push_back(std::move(d));
    }
}

} // namespace

bool is_valid_plugin_id(std::string_view id) {
    if (id.size() < 2 || id.size() > 32 || !(id[0] >= 'a' && id[0] <= 'z'))
        return false;
    return std::all_of(id.begin(), id.end(),
                       [](char c) { return is_lower_or_digit(c) || c == '-'; });
}

bool is_owned_name(std::string_view id, std::string_view name) {
    return name.size() > id.size() + kPluginNameSeparator.size() &&
           name.substr(0, id.size()) == id &&
           name.substr(id.size(), kPluginNameSeparator.size()) == kPluginNameSeparator;
}

std::string plugin_owned_name(std::string_view id, std::string_view rest) {
    std::string name(id);
    name += kPluginNameSeparator;
    name += rest;
    return name;
}

std::string_view owner_of(std::string_view name) {
    auto pos = name.find(kPluginNameSeparator);
    return pos == std::string_view::npos ? std::string_view{} : name.substr(0, pos);
}

ManifestParse parse_manifest(const std::string& text) {
    ManifestParse r;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        r.errors.push_back("manifest.json is not valid JSON");
        return r;
    }
    if (!j.is_object()) {
        r.errors.push_back("manifest.json must be an object");
        return r;
    }

    Manifest m;
    require_string(j, "id", m.id, r.errors);
    if (!m.id.empty() && !is_valid_plugin_id(m.id))
        r.errors.push_back("'id' must match ^[a-z][a-z0-9-]{1,31}$");
    require_string(j, "name", m.name, r.errors);
    check_display_field("name", m.name, kMaxNameBytes, r.errors);
    require_string(j, "version", m.version, r.errors);
    check_display_field("version", m.version, kMaxVersionBytes, r.errors);
    optional_string(j, "author", m.author, r.errors);
    check_display_field("author", m.author, kMaxAuthorBytes, r.errors);
    optional_string(j, "description", m.description, r.errors);
    optional_string(j, "helix_version", m.helix_version, r.errors);

    if (auto it = j.find("permissions"); it != j.end()) {
        if (!it->is_array()) {
            r.errors.push_back("'permissions' must be an array of strings");
        } else {
            for (const auto& p : *it) {
                auto perm =
                    p.is_string() ? permission_from_string(p.get<std::string>()) : std::nullopt;
                if (perm)
                    m.permissions.insert(*perm);
                else
                    r.errors.push_back("unknown permission '" +
                                       (p.is_string() ? p.get<std::string>() : p.dump()) + "'");
            }
        }
    }

    if (auto it = j.find("memory_mb"); it != j.end()) {
        if (!it->is_number_integer() || it->get<int>() < 1 || it->get<int>() > 64)
            r.errors.push_back("'memory_mb' must be an integer from 1 to 64");
        else
            m.memory_mb = it->get<int>();
    }

    if (auto it = j.find("settings"); it != j.end()) {
        if (!it->is_array()) {
            r.errors.push_back("'settings' must be an array");
        } else {
            std::set<std::string> seen;
            for (size_t i = 0; i < it->size(); ++i)
                parse_setting(m.id, (*it)[i], i, m, seen, r.errors);
        }
    }

    if (auto it = j.find("settings_overlay"); it != j.end() && !it->is_null()) {
        if (!it->is_string() || !is_owned_name(m.id, it->get<std::string>()))
            r.errors.push_back("'settings_overlay' must be a component named " + m.id + "__<name>");
        else
            m.settings_overlay = it->get<std::string>();
    }

    if (auto it = j.find("widgets"); it != j.end())
        parse_widgets(*it, m, r.errors);

    if (r.errors.empty())
        r.manifest = std::move(m);
    return r;
}

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
