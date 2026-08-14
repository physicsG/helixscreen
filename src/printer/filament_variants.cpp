// SPDX-License-Identifier: GPL-3.0-or-later

#include "filament_variants.h"

#include "filament_database.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>

#include "hv/json.hpp"

namespace filament {

namespace {

/// Known variant affixes, matched case-insensitively and ONLY when delimited by
/// a '-', '_' or ' ' separator. Derived from the type strings actually present
/// in assets/filaments.json (CF, GF, AERO), the variant rows in
/// filament_database.h MATERIALS[] (Silk/Matte/Wood/Marble/Metal/Glow), and the
/// prefixed product names the catalog carries (HT-PLA-GF, PLA-HS, PETG+HS,
/// PLA-LW, Bambu PETG HF).
///
/// Deliberately NOT listed: "ABS" (so "PC-ABS" keeps its own identity and its
/// ABS_ASA compat group), "Soft"/"95A"/"85A" (TPU shore grades), "Change"
/// (Color-Change). Adding a polymer name here would merge two real materials.
constexpr const char* VARIANT_AFFIXES[] = {
    // Fiber fills
    "CF",
    "GF",
    // Foaming / lightweight grades
    "AERO",
    "LW",
    // Speed / temperature grades
    "HS",
    "HF",
    "HT",
    // Cosmetic finishes
    "Silk",
    "Matte",
    "Wood",
    "Marble",
    "Metal",
    "Glow",
};

/// Family for PAHT-branded products. "PAHT" is not a standardized polymer
/// designation — it is a marketing category, and the underlying resin varies by
/// vendor. Every PAHT product in assets/filaments.json is PA12/PA612-class
/// (Bambu and Creality both name PA12 as the base resin) and prints in the
/// ordinary PA envelope, which is why it maps to PA. Other vendors ship
/// PPA-based PAHT-CF under the same type string, at PPA temperatures and
/// needing a hardened nozzle and sealed enclosure: verify the base resin before
/// adding a PAHT product from a new vendor rather than assuming this mapping.
constexpr const char* PAHT_FAMILY = "PA";

/// Explicit family mapping for names that affix-stripping alone cannot reduce,
/// because the modifier is fused to the polymer name with no separator.
///
/// Numbered nylon grades collapse into PA: the catalog does not carry enough of
/// each to justify separate headings (PA-CF 14 products, PA 8, PA-GF 3,
/// PA6-CF 3, PA12 a single Generic product), and it files PA6-CF products under
/// BOTH type=PA-CF and type=PA6-CF — one "PA" heading papers over that split.
///
/// Self-mapping rows are documented STOPS, not no-ops: they assert that a name
/// which superficially looks reducible must be left alone.
struct FamilyOverride {
    const char* name;
    const char* family;
};
constexpr FamilyOverride FAMILY_OVERRIDES[] = {
    {"PA6", "PA"},
    {"PA12", "PA"},
    {"PA66", "PA"},
    {"PA612", "PA"},
    {"PAHT", PAHT_FAMILY},
    // STOP: PPA (polyphthalamide) must NEVER be normalized or aliased to PA.
    // The names are one letter apart, but it is a semi-aromatic polyamide in a
    // different processing regime — 280-310C nozzle, 100-120C bed, sealed
    // enclosure and hardened nozzle, against PA's 260-290C/90-110C. It keeps
    // its own heading, with PPA-CF and PPA-GF filed under it.
    {"PPA", "PPA"},
    // STOP: copolyester elastomer ends in "PE" but is unrelated to polyethylene.
    {"CoPE", "CoPE"},
    // STOP: polyethylene is a polyolefin, NOT polyethylene terephthalate.
    // "PE-CF" must reduce to PE and "PET-CF" to PET — never across.
    {"PE", "PE"},
    {"PET", "PET"},
};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char ca, char cb) {
               return std::tolower(static_cast<unsigned char>(ca)) ==
                      std::tolower(static_cast<unsigned char>(cb));
           });
}

bool is_separator(char c) {
    return c == '-' || c == '_' || c == ' ';
}

bool is_variant_affix(std::string_view token) {
    for (const char* affix : VARIANT_AFFIXES) {
        if (iequals(token, affix)) {
            return true;
        }
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

/// One pass of affix removal. Returns true if @p s was shortened.
bool strip_one_affix(std::string_view& s) {
    // Leading affix: "HT-PLA-GF" -> "PLA-GF", "Silk PLA" -> "PLA"
    for (size_t i = 0; i < s.size(); ++i) {
        if (!is_separator(s[i]))
            continue;
        if (is_variant_affix(s.substr(0, i))) {
            std::string_view rest = s.substr(i + 1);
            if (!rest.empty()) {
                s = rest;
                return true;
            }
        }
        break; // only the first token can be a leading affix
    }
    // Trailing affix: "PLA-CF" -> "PLA", "PLA Silk" -> "PLA"
    for (size_t i = s.size(); i > 0; --i) {
        if (!is_separator(s[i - 1]))
            continue;
        if (is_variant_affix(s.substr(i))) {
            std::string_view rest = s.substr(0, i - 1);
            if (!rest.empty()) {
                s = rest;
                return true;
            }
        }
        break; // only the last token can be a trailing affix
    }
    // Trailing "+": a vendor grade marker ("PLA+", "ASA+"), not a separate
    // polymer. "+" is not in is_separator() because it never delimits an affix
    // TOKEN — it is always fused to the polymer name, so it needs its own rule.
    // Returning true re-enters the caller's loop, letting "PLA+-CF" reduce fully.
    if (s.size() > 1 && s.back() == '+') {
        s.remove_suffix(1);
        return true;
    }
    return false;
}

/// Explicit family override lookup, or empty if the name has no entry.
std::string_view family_override(std::string_view name) {
    for (const auto& row : FAMILY_OVERRIDES) {
        if (iequals(name, row.name)) {
            return row.family;
        }
    }
    return {};
}

/// Lazily-loaded Orca tables. Guarded because backends resolve from their own
/// threads; the tables are immutable after load.
std::mutex g_orca_mutex;
bool g_orca_loaded = false;
std::set<std::string> g_orca_library_types;
std::map<std::string, std::string> g_orca_overrides;

// Same search order as FilamentCatalog::BUILTIN_PATHS (filament_catalog.cpp:19).
const char* ORCA_TABLE_PATHS[] = {"assets/filaments.json", "../assets/filaments.json",
                                  "/opt/helixscreen/assets/filaments.json"};

/// Load the tables from the first readable asset. Caller holds g_orca_mutex.
void load_orca_tables_locked() {
    if (g_orca_loaded)
        return;
    g_orca_loaded = true; // one attempt; a missing asset must not retry per call
    for (const char* path : ORCA_TABLE_PATHS) {
        std::ifstream f(path);
        if (!f.is_open())
            continue;
        try {
            auto doc = nlohmann::json::parse(f);
            if (!doc.is_object())
                continue;
            if (auto it = doc.find("orca_library_types"); it != doc.end() && it->is_array()) {
                for (const auto& t : *it) {
                    if (t.is_string())
                        g_orca_library_types.insert(t.get<std::string>());
                }
            }
            if (auto it = doc.find("orca_type_overrides"); it != doc.end() && it->is_object()) {
                for (const auto& [k, v] : it->items()) {
                    if (v.is_string())
                        g_orca_overrides[k] = v.get<std::string>();
                }
            }
            spdlog::debug("[filament] loaded {} Orca library types, {} overrides from {}",
                          g_orca_library_types.size(), g_orca_overrides.size(), path);
            return;
        } catch (const std::exception& e) {
            spdlog::warn("[filament] Orca table parse failed {}: {}", path, e.what());
        }
    }
    // No asset: every lookup misses, so orca_match_type returns "" and the
    // caller omits `material`. Orca then shows the lane empty — visibly wrong
    // rather than confidently wrong, which is the safe failure direction.
    spdlog::warn("[filament] no Orca library tables found; lane_data will omit material");
}

/// Case-insensitive lookup against the library set. Orca's own match is
/// case-sensitive, so we return the CANONICAL spelling from the table, never
/// the caller's casing.
const std::string* find_library_type(const std::string& candidate) {
    auto exact = g_orca_library_types.find(candidate);
    if (exact != g_orca_library_types.end())
        return &*exact;
    for (const auto& t : g_orca_library_types) {
        if (iequals(t, candidate))
            return &t;
    }
    return nullptr;
}

} // namespace

std::string extract_base_material(std::string_view name) {
    std::string_view work = trim(name);
    if (work.empty()) {
        return std::string(name);
    }

    // Resolve aliases first so decoration on the CANONICAL spelling is visible:
    // "SILK" -> "Silk PLA" -> (leading affix) -> "PLA".
    work = resolve_alias(work);

    // Strip known affixes from both ends until nothing more is recognised. The
    // override table is consulted each round so a fused grade name exposed by
    // stripping is caught ("PA6-CF" -> "PA6" -> "PA").
    for (int guard = 0; guard < 8; ++guard) {
        if (auto fam = family_override(work); !fam.empty()) {
            return std::string(fam);
        }
        if (!strip_one_affix(work)) {
            break;
        }
    }

    if (find_material(work).has_value()) {
        return std::string(work);
    }

    // Unrecognised compound name ("PLA SnapSpeed"): walk progressively shorter
    // prefixes at separator boundaries against the database.
    for (size_t i = work.size(); i > 0; --i) {
        if (!is_separator(work[i - 1]))
            continue;
        auto prefix = work.substr(0, i - 1);
        if (!prefix.empty() && find_material(prefix).has_value()) {
            return std::string(prefix);
        }
    }

    return std::string(work);
}

std::string display_family(std::string_view type) {
    std::string base = extract_base_material(type);
    // A type we cannot reduce is its own family — one heading, one entry — so
    // user-overlay and firmware-only types stay reachable instead of vanishing.
    return base.empty() ? std::string(type) : base;
}

void FilamentVariantsTestAccess::set_orca_tables(std::set<std::string> library_types,
                                                 std::map<std::string, std::string> overrides) {
    std::lock_guard<std::mutex> lock(g_orca_mutex);
    g_orca_library_types = std::move(library_types);
    g_orca_overrides = std::move(overrides);
    // Empty tables mean "restore lazy load"; non-empty means "tests own these".
    g_orca_loaded = !(g_orca_library_types.empty() && g_orca_overrides.empty());
}

bool orca_tables_available() {
    std::lock_guard<std::mutex> lock(g_orca_mutex);
    load_orca_tables_locked();
    return !g_orca_library_types.empty();
}

void warm_orca_tables() {
    std::lock_guard<std::mutex> lock(g_orca_mutex);
    load_orca_tables_locked();
}

void merge_user_orca_overrides(const std::map<std::string, std::string>& overrides) {
    if (overrides.empty())
        return;
    std::lock_guard<std::mutex> lock(g_orca_mutex);
    // Ensure the shipped tables are present before merging on top — otherwise
    // warm_orca_tables() would race to populate them after the merge and (being
    // a no-op once g_orca_loaded latches) leave the user entries stranded in a
    // set that never got loaded. Belt-and-suspenders: callers pair this with a
    // warm_orca_tables() call, but the merge must be correct standalone too.
    load_orca_tables_locked();
    size_t added = 0, updated = 0;
    for (const auto& [k, v] : overrides) {
        // Case-insensitive replace: erase any existing entry whose key matches
        // case-insensitively before inserting the user's. Shipped override keys
        // are mixed-case (SILK, rPLA, Color-Change, ...); without this a
        // case-variant user key would coexist with the shipped one, and
        // orca_match_type()'s sorted iteration — not user precedence — would
        // pick the winner, silently ignoring the user's override.
        bool replaced = false;
        for (auto it = g_orca_overrides.begin(); it != g_orca_overrides.end();) {
            if (iequals(it->first, k)) {
                it = g_orca_overrides.erase(it);
                replaced = true;
            } else {
                ++it;
            }
        }
        g_orca_overrides[k] = v;
        replaced ? ++updated : ++added;
    }
    spdlog::debug("[filament] merged user orca_type_map: {} new, {} updated", added, updated);
}

std::string orca_match_type(std::string_view display_type) {
    std::string work(trim(display_type));
    if (work.empty())
        return "";

    std::lock_guard<std::mutex> lock(g_orca_mutex);
    load_orca_tables_locked();

    // 1. Explicit override wins outright, including an intentional "" that
    //    means "this type must never be emitted".
    for (const auto& [k, v] : g_orca_overrides) {
        if (iequals(k, work))
            return v;
    }
    // 2. The type itself, if Orca's library carries it.
    if (const std::string* hit = find_library_type(work))
        return *hit;
    // 3. Base polymer, if the library carries that.
    std::string base = extract_base_material(work);
    if (const std::string* hit = find_library_type(base))
        return *hit;
    // 4. Nothing safe to say. Caller omits the field.
    return "";
}

} // namespace filament
