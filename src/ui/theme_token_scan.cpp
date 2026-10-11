// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Design-token discovery: expat passes over ui_xml/ that find the <color>, <px>
// and <string> constants, plus the validator for responsive constant sets.

#include "data_root_resolver.h"
#include "helix-xml/src/libs/expat/expat.h"
#include "text_io.h"
#include "theme_manager.h"
#include "theme_manager_internal.h"
#include "theme_token_table.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace helix::theme_detail {

const char* ui_xml_dir() {
    static const std::string dir = helix::asset_path("ui_xml");
    return dir.c_str();
}

} // namespace helix::theme_detail

// Expat callback data for extracting name→value pairs with a specific suffix
struct SuffixValueParserData {
    const char* element_type;                              // "color", "px", or "string"
    const char* suffix;                                    // "_light", "_small", etc.
    std::unordered_map<std::string, std::string>* results; // Output: base_name → value
};

// Helper: check if string ends with suffix
static bool ends_with_suffix(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (str_len < suffix_len)
        return false;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Parser callback for ALL elements of a given type (no suffix matching)
struct AllElementParserData {
    const char* element_type;
    std::unordered_map<std::string, std::string>* token_values;
};

static void XMLCALL all_element_start(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* data = static_cast<AllElementParserData*>(userData);
    if (strcmp(name, data->element_type) != 0)
        return;

    const char* elem_name = nullptr;
    const char* elem_value = nullptr;
    for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "name") == 0)
            elem_name = atts[i + 1];
        else if (strcmp(atts[i], "value") == 0)
            elem_value = atts[i + 1];
    }
    if (elem_name && elem_value) {
        (*data->token_values)[elem_name] = elem_value;
    }
}

// Expat element start handler - extracts name and value for matching elements
static void XMLCALL suffix_value_element_start(void* user_data, const XML_Char* name,
                                               const XML_Char** attrs) {
    SuffixValueParserData* data = static_cast<SuffixValueParserData*>(user_data);

    if (strcmp(name, data->element_type) != 0)
        return;

    // Extract both name and value attributes
    const char* const_name = nullptr;
    const char* const_value = nullptr;
    for (int i = 0; attrs[i]; i += 2) {
        if (strcmp(attrs[i], "name") == 0)
            const_name = attrs[i + 1];
        if (strcmp(attrs[i], "value") == 0)
            const_value = attrs[i + 1];
    }

    // Skip if either attribute is missing
    if (!const_name || !const_value)
        return;

    // Check if name ends with the target suffix
    if (ends_with_suffix(const_name, data->suffix)) {
        // Extract base name (without suffix)
        size_t base_len = strlen(const_name) - strlen(data->suffix);
        std::string base_name(const_name, base_len);

        // Store in results (overwrites any existing value - last-wins)
        (*data->results)[base_name] = const_value;
    }
}

/// Read a top-level XML file whole, for the token-discovery passes.
///
/// Discovery makes roughly 25 passes and each one re-reads every file. That is
/// free on a desktop filesystem; on SPI-flash LittleFS it costs minutes of boot
/// and trips the watchdog, so the bytes are cached there. Making discovery
/// single-pass would retire the cache.
static std::string tm_read_xml_file(const char* filepath) {
#if defined(HELIX_PLATFORM_ESP32)
    static std::unordered_map<std::string, std::string> cache;
    auto cached = cache.find(filepath);
    if (cached != cache.end()) {
        return cached->second;
    }
#endif
    std::string content = helix::text_io::read_file(filepath).value_or("");
#if defined(HELIX_PLATFORM_ESP32)
    cache.emplace(filepath, content);
#endif
    return content;
}

void theme_manager_parse_xml_file_for_all(
    const char* filepath, const char* element_type,
    std::unordered_map<std::string, std::string>& token_values) {
    if (!filepath)
        return;

    const std::string xml_content = tm_read_xml_file(filepath);
    if (xml_content.empty())
        return;

    AllElementParserData parser_data = {element_type, &token_values};
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser)
        return;

    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, all_element_start, nullptr);
    XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE);
    XML_ParserFree(parser);
}

void theme_manager_parse_xml_file_for_suffix(
    const char* filepath, const char* element_type, const char* suffix,
    std::unordered_map<std::string, std::string>& token_values) {
    // Handle NULL filepath gracefully
    if (!filepath) {
        spdlog::trace("[Theme] parse_xml_file_for_suffix: NULL filepath");
        return;
    }

    const std::string xml_content = tm_read_xml_file(filepath);
    if (xml_content.empty()) {
        spdlog::trace("[Theme] Could not open {} for suffix parsing", filepath);
        return;
    }

    // Handle empty file
    if (xml_content.empty()) {
        return;
    }

    SuffixValueParserData parser_data = {element_type, suffix, &token_values};
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
        spdlog::error("[Theme] Failed to create XML parser for {}", filepath);
        return;
    }
    XML_SetUserData(parser, &parser_data);
    XML_SetElementHandler(parser, suffix_value_element_start, nullptr);

    if (XML_Parse(parser, xml_content.c_str(), static_cast<int>(xml_content.size()), XML_TRUE) ==
        XML_STATUS_ERROR) {
        spdlog::trace("[Theme] XML parse error in {} line {}: {}", filepath,
                      XML_GetCurrentLineNumber(parser), XML_ErrorString(XML_GetErrorCode(parser)));
        // Continue with partial results (don't clear token_values)
    }
    XML_ParserFree(parser);
}

std::vector<std::string> theme_manager_find_xml_files(const char* directory, bool recursive) {
    std::vector<std::string> result;

    // Handle NULL directory gracefully
    if (!directory) {
        spdlog::trace("[Theme] find_xml_files: NULL directory");
        return result;
    }

    DIR* dir = opendir(directory);
    if (!dir) {
        spdlog::trace("[Theme] Could not open directory: {}", directory);
        return result;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        if (entry->d_type == DT_DIR) {
            if (recursive && filename != "." && filename != "..") {
                const std::string sub = std::string(directory) + "/" + filename;
                auto nested = theme_manager_find_xml_files(sub.c_str(), true);
                result.insert(result.end(), nested.begin(), nested.end());
            }
            continue;
        }

        // Skip suspicious filenames (path traversal defense)
        if (filename.find('/') != std::string::npos || filename.find("..") != std::string::npos) {
            continue;
        }

        // Check if file ends with .xml (case-sensitive, lowercase only)
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".xml") {
            std::string full_path = std::string(directory) + "/" + filename;
            result.push_back(full_path);
        }
    }
    closedir(dir);

    // Sort alphabetically for deterministic ordering (needed for last-wins)
    std::sort(result.begin(), result.end());

    return result;
}

std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_element(const char* directory, const char* element_type) {
    // Build-time token table: skip the ~28-scan boot storm when the table is
    // enabled, carries this element type, and the caller wants the canonical
    // ui_xml dir (tests, alternate dirs and uncovered types always scan live).
    if (helix::theme_tokens::answers_from_table(helix::theme_tokens::enabled(), element_type,
                                                directory, helix::theme_detail::ui_xml_dir())) {
        return helix::theme_tokens::for_element(element_type);
    }
    std::unordered_map<std::string, std::string> token_values;
    std::vector<std::string> files = theme_manager_find_xml_files(directory);
    for (const auto& filepath : files) {
        theme_manager_parse_xml_file_for_all(filepath.c_str(), element_type, token_values);
    }
    return token_values;
}

std::unordered_map<std::string, std::string>
theme_manager_parse_all_xml_for_suffix(const char* directory, const char* element_type,
                                       const char* suffix) {
    // Build-time token table: same fast-path guard as _for_element above.
    if (helix::theme_tokens::answers_from_table(helix::theme_tokens::enabled(), element_type,
                                                directory, helix::theme_detail::ui_xml_dir())) {
        return helix::theme_tokens::for_suffix(element_type, suffix);
    }

    std::unordered_map<std::string, std::string> token_values;

    // Get sorted list of all XML files
    std::vector<std::string> files = theme_manager_find_xml_files(directory);

    // Parse each file in alphabetical order (last-wins via map overwrite)
    for (const auto& filepath : files) {
        theme_manager_parse_xml_file_for_suffix(filepath.c_str(), element_type, suffix,
                                                token_values);
    }

    return token_values;
}

std::vector<std::string> theme_manager_validate_constant_sets(const char* directory) {
    std::vector<std::string> warnings;

    if (!directory) {
        return warnings;
    }

    // Every layout directory (components/, portrait/, micro/, ...), read from the
    // files themselves: a set split across files there is as broken as one at
    // the top level.
    const std::vector<std::string> files = theme_manager_find_xml_files(directory, true);
    auto suffix_tokens = [&files](const char* element_type, const char* suffix) {
        std::unordered_map<std::string, std::string> tokens;
        for (const auto& filepath : files) {
            theme_manager_parse_xml_file_for_suffix(filepath.c_str(), element_type, suffix, tokens);
        }
        return tokens;
    };

    // Validate responsive px sets (_small/_medium/_large required, _tiny optional)
    {
        auto tiny_tokens = suffix_tokens("px", "_tiny");
        auto small_tokens = suffix_tokens("px", "_small");
        auto medium_tokens = suffix_tokens("px", "_medium");
        auto large_tokens = suffix_tokens("px", "_large");

        // Collect all base names that have at least one responsive suffix
        // _tiny is optional — only _small/_medium/_large are required for a complete set
        std::unordered_map<std::string, int> base_names;
        for (const auto& [name, _] : small_tokens) {
            base_names[name] |= 1; // bit 0 = _small
        }
        for (const auto& [name, _] : medium_tokens) {
            base_names[name] |= 2; // bit 1 = _medium
        }
        for (const auto& [name, _] : large_tokens) {
            base_names[name] |= 4; // bit 2 = _large
        }

        // border_radius_small is a fixed 4px token that only looks like a
        // responsive variant; plugin XML references it by name, so it stays.
        // Exempt only while it is the lone tier: a second tier makes it a set.
        auto border_radius = base_names.find("border_radius");
        if (border_radius != base_names.end() && border_radius->second == 1) {
            base_names.erase(border_radius);
        }

        // Check for incomplete sets (_small/_medium/_large must be complete)
        for (const auto& [base_name, flags] : base_names) {
            if (flags != 7) { // Not all three present (111 in binary)
                std::vector<std::string> found;
                std::vector<std::string> missing;

                if (flags & 1)
                    found.push_back("_small");
                else
                    missing.push_back("_small");

                if (flags & 2)
                    found.push_back("_medium");
                else
                    missing.push_back("_medium");

                if (flags & 4)
                    found.push_back("_large");
                else
                    missing.push_back("_large");

                std::string found_str;
                for (size_t i = 0; i < found.size(); ++i) {
                    if (i > 0)
                        found_str += ", ";
                    found_str += found[i];
                }

                std::string missing_str;
                for (size_t i = 0; i < missing.size(); ++i) {
                    if (i > 0)
                        missing_str += ", ";
                    missing_str += missing[i];
                }

                warnings.push_back("Incomplete responsive set for '" + base_name + "': found " +
                                   found_str + " but missing " + missing_str);
            }
        }

        // Warn about _tiny tokens without corresponding _small (likely a typo)
        for (const auto& [name, _] : tiny_tokens) {
            if (small_tokens.find(name) == small_tokens.end()) {
                warnings.push_back("Token '" + name +
                                   "' has _tiny but no _small (tiny falls back to small)");
            }
        }
    }

    // Validate themed color pairs (_light/_dark)
    {
        auto light_tokens = suffix_tokens("color", "_light");
        auto dark_tokens = suffix_tokens("color", "_dark");

        // Collect all base names that have at least one theme suffix
        std::unordered_map<std::string, int> base_names;
        for (const auto& [name, _] : light_tokens) {
            base_names[name] |= 1; // bit 0 = _light
        }
        for (const auto& [name, _] : dark_tokens) {
            base_names[name] |= 2; // bit 1 = _dark
        }

        // Check for incomplete pairs
        for (const auto& [base_name, flags] : base_names) {
            if (flags != 3) { // Not both present (11 in binary)
                if (flags == 1) {
                    warnings.push_back("Incomplete theme pair for '" + base_name +
                                       "': found _light but missing _dark");
                } else if (flags == 2) {
                    warnings.push_back("Incomplete theme pair for '" + base_name +
                                       "': found _dark but missing _light");
                }
            }
        }
    }

    return warnings;
}
