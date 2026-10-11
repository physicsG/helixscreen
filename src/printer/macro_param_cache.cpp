// SPDX-License-Identifier: GPL-3.0-or-later

#include "macro_param_cache.h"

#include "text_io.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <string_view>

namespace helix {

namespace {
/// Convert a string to lowercase in-place and return a reference.
std::string& to_lower_inplace(std::string& s) {
    s = helix::text_io::to_lower(s);
    return s;
}

bool is_identifier_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

size_t skip_space(std::string_view s, size_t pos) {
    while (pos < s.size() && is_space(s[pos])) {
        ++pos;
    }
    return pos;
}
} // namespace

PrinterStopCall find_printer_stop_call(std::string_view gcode_template) {
    static constexpr std::string_view NAME = "action_emergency_stop";
    PrinterStopCall call;
    for (size_t at = gcode_template.find(NAME); at != std::string_view::npos;
         at = gcode_template.find(NAME, at + NAME.size())) {
        if (at > 0 && is_identifier_char(gcode_template[at - 1])) {
            continue; // part of a longer name
        }
        size_t pos = skip_space(gcode_template, at + NAME.size());
        if (pos >= gcode_template.size() || gcode_template[pos] != '(') {
            continue; // named, not called
        }
        call.calls = true;

        pos = skip_space(gcode_template, pos + 1);
        if (pos >= gcode_template.size()) {
            return call;
        }
        const char quote = gcode_template[pos];
        if (quote != '"' && quote != '\'') {
            return call;
        }
        const size_t close = gcode_template.find(quote, pos + 1);
        if (close == std::string_view::npos) {
            return call;
        }
        const std::string_view literal = gcode_template.substr(pos + 1, close - pos - 1);
        if (literal.find('\\') != std::string_view::npos) {
            return call; // an escape is not a plain literal
        }
        const size_t after = skip_space(gcode_template, close + 1);
        if (after < gcode_template.size() && gcode_template[after] == ')') {
            call.message = std::string(literal);
        }
        return call;
    }
    return call;
}

MacroParamCache& MacroParamCache::instance() {
    static MacroParamCache cache;
    return cache;
}

void MacroParamCache::populate_from_configfile(
    const nlohmann::json& config, const std::unordered_set<std::string>& known_macros) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    populated_ = false;
    ++generation_;

    if (!config.is_object()) {
        spdlog::warn("[MacroParamCache] configfile.config is not an object");
        return;
    }

    static constexpr std::string_view PREFIX = "gcode_macro ";

    size_t config_count = 0;
    for (auto it = config.begin(); it != config.end(); ++it) {
        const auto& key = it.key();

        if (key.size() <= PREFIX.size() || key.compare(0, PREFIX.size(), PREFIX) != 0) {
            continue;
        }

        std::string macro_name_lower = key.substr(PREFIX.size());
        to_lower_inplace(macro_name_lower);

        // Extract gcode template for param parsing
        std::string gcode_template;
        if (it.value().is_object() && it.value().contains("gcode") &&
            it.value()["gcode"].is_string()) {
            gcode_template = it.value()["gcode"].get<std::string>();
        }

        // Extract description if present
        std::string description;
        if (it.value().is_object() && it.value().contains("description") &&
            it.value()["description"].is_string()) {
            description = it.value()["description"].get<std::string>();
        }

        CachedMacroInfo info;
        info.description = std::move(description);
        PrinterStopCall stop = find_printer_stop_call(gcode_template);
        info.stops_printer = stop.calls;
        info.stop_message = std::move(stop.message);
        auto params = parse_macro_params(gcode_template);

        // Note: variable_* keys (Klipper SET_GCODE_VARIABLE state) are
        // intentionally NOT included. They are internal macro state, not
        // user-facing parameters. User params are already extracted from
        // the gcode template (params.NAME references) above.

        if (params.empty()) {
            info.knowledge = MacroParamKnowledge::KNOWN_NO_PARAMS;
        } else {
            info.knowledge = MacroParamKnowledge::KNOWN_PARAMS;
            info.params = std::move(params);
        }

        cache_[macro_name_lower] = std::move(info);
        ++config_count;
    }

    // Mark known macros not found in configfile as UNKNOWN
    size_t unknown_count = 0;
    for (const auto& macro : known_macros) {
        std::string lower = macro;
        to_lower_inplace(lower);
        if (cache_.find(lower) == cache_.end()) {
            cache_[lower] = CachedMacroInfo{MacroParamKnowledge::UNKNOWN, {}};
            ++unknown_count;
        }
    }

    populated_ = true;
    spdlog::info("[MacroParamCache] Populated: {} entries from configfile, {} unknown macros",
                 config_count, unknown_count);
}

std::map<std::string, std::string> MacroParamCache::printer_stop_commands() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, std::string> commands;
    for (const auto& [name, info] : cache_) {
        if (!info.stops_printer) {
            continue;
        }
        std::string upper = name;
        upper = helix::text_io::to_upper(upper);
        commands.emplace(std::move(upper), info.stop_message);
    }
    return commands;
}

bool MacroParamCache::is_populated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return populated_;
}

uint64_t MacroParamCache::generation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation_;
}

CachedMacroInfo MacroParamCache::get(const std::string& macro_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lower = macro_name;
    to_lower_inplace(lower);

    auto it = cache_.find(lower);
    if (it != cache_.end()) {
        return it->second;
    }
    return CachedMacroInfo{MacroParamKnowledge::UNKNOWN, {}};
}

bool MacroParamCache::has_macro(const std::string& macro_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string lower = macro_name;
    to_lower_inplace(lower);
    // Every entry in cache_ comes from either configfile or known_macros —
    // both signal the macro is registered with Klipper. The UNKNOWN tag means
    // "no params parsed"; it still indicates the macro exists.
    return cache_.find(lower) != cache_.end();
}

void MacroParamCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
    populated_ = false;
    ++generation_;
    spdlog::debug("[MacroParamCache] Cache cleared");
}

} // namespace helix
