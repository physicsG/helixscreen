// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "macro_params.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "hv/json.hpp"

namespace helix {

enum class MacroParamKnowledge { KNOWN_PARAMS, KNOWN_NO_PARAMS, UNKNOWN };

/// A gcode_macro template's call to action_emergency_stop, when it makes one.
struct PrinterStopCall {
    bool calls = false; ///< The template calls action_emergency_stop
    /// The call's argument when it is a plain quoted string literal; empty for no
    /// argument or anything Klipper would have to evaluate.
    std::string message;
};

/// Find the first action_emergency_stop(...) call in a gcode_macro template.
[[nodiscard]] PrinterStopCall find_printer_stop_call(std::string_view gcode_template);

struct CachedMacroInfo {
    MacroParamKnowledge knowledge = MacroParamKnowledge::UNKNOWN;
    std::vector<MacroParam> params;
    std::string description;    ///< From Klipper gcode_macro description field
    bool stops_printer = false; ///< Running the macro shuts the printer down
    std::string stop_message;   ///< PrinterStopCall::message, when stops_printer
};

/// Cache for macro parameter information, populated once during printer discovery.
/// Avoids per-click configfile.config queries by pre-parsing all gcode_macro templates.
class MacroParamCache {
  public:
    static MacroParamCache& instance();

    /// Populate cache from configfile.config response.
    /// @param config The configfile.config JSON object (keys like "gcode_macro clean_nozzle")
    /// @param known_macros Set of known macro names (uppercase, from printer's object list)
    void populate_from_configfile(const nlohmann::json& config,
                                  const std::unordered_set<std::string>& known_macros);

    /// Lookup cached info for a macro (case-insensitive).
    [[nodiscard]] CachedMacroInfo get(const std::string& macro_name) const;

    /// True if the macro is registered with Klipper (case-insensitive). Used to
    /// gate features that require a specific gcode_macro to be defined in the
    /// firmware (e.g. K2 AI detect needs LOAD_AI_RUN — not all variants ship it).
    [[nodiscard]] bool has_macro(const std::string& macro_name) const;

    /// Upper-case names of the macros whose template stops the printer, each with
    /// its stop message (empty when not a literal).
    [[nodiscard]] std::map<std::string, std::string> printer_stop_commands() const;

    /// Whether populate_from_configfile() has read a configfile since the last clear().
    [[nodiscard]] bool is_populated() const;

    /// Changes whenever the cached macro set may have changed (any populate or
    /// clear). An answer computed against an older value describes a macro set
    /// this printer may no longer have, so it is not reusable.
    [[nodiscard]] uint64_t generation() const;

    /// Clear all cached state (call on disconnect/reconnect).
    void clear();

  private:
    MacroParamCache() = default;

    mutable std::mutex mutex_;
    // Key: lowercase macro name (e.g., "clean_nozzle")
    std::unordered_map<std::string, CachedMacroInfo> cache_;
    bool populated_ = false;
    uint64_t generation_ = 0;
};

} // namespace helix
