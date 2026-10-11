// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "klipper_config_editor.h"

#include <string>
#include <vector>

/**
 * @brief Parser for Klipper's INI-like config format.
 *
 * A read/modify view over one file's text. Structure scanning and in-place
 * edits come from KlipperConfigEditor, so both follow Klipper's configparser
 * rules: first `:` or `=` separates, option names are case-insensitive, `#`
 * and `;` start comments, a repeated key takes its last value, and
 * `[include ...]` is a directive, not a section. Comments, blank lines and
 * separator style survive serialize().
 */
class KlipperConfigParser {
  public:
    /// Parse config from string content. Returns true on success.
    bool parse(const std::string& content);

    /// Get a string value from section/key, or default_val if not found.
    std::string get(const std::string& section, const std::string& key,
                    const std::string& default_val = "") const;

    /// Get a boolean value. Recognizes True/False, true/false, yes/no, 1/0.
    bool get_bool(const std::string& section, const std::string& key,
                  bool default_val = false) const;

    /// Get a float value, or default_val if not found or not parseable.
    float get_float(const std::string& section, const std::string& key,
                    float default_val = 0.0f) const;

    /// Get an integer value, or default_val if not found or not parseable.
    int get_int(const std::string& section, const std::string& key, int default_val = 0) const;

    /// Set a value in memory. Preserves original separator style for existing keys.
    void set(const std::string& section, const std::string& key, const std::string& value);

    /// Check if a section exists.
    bool has_section(const std::string& section) const;

    /// Get all section names in order of appearance.
    std::vector<std::string> get_sections() const;

    /// Get all sections whose name starts with prefix (e.g. "AFC_stepper").
    /// Matches "prefix" exactly or "prefix " followed by anything.
    std::vector<std::string> get_sections_matching(const std::string& prefix) const;

    /// Get all keys in a section (lowercased, as Klipper reads them), in order of appearance.
    std::vector<std::string> get_keys(const std::string& section) const;

    /// Serialize back to string, preserving comments, blank lines, and formatting.
    std::string serialize() const;

    /// Returns true if any set() call has been made since parse().
    bool is_modified() const;

  private:
    const helix::system::ConfigKey* find_key(const std::string& section,
                                             const std::string& key) const;
    /// Re-run the structure scan after content_ changed.
    void reindex();

    std::string content_;
    helix::system::ConfigStructure structure_;
    bool modified_ = false;
};
