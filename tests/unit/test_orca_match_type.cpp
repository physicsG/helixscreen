// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resolution from HelixScreen's display type to a string OrcaSlicer can match.
//
// Why this matters: Orca resolves an unmatched filament_type to the first
// library preset whose name contains "PLA" (Preset.cpp:3300), and that bogus id
// then resolves successfully in sync_ams_list, short-circuiting the smarter
// similarity search. So an unmatchable string does not degrade gracefully —
// it becomes PLA temperatures on an ASA-GF spool.

#include "filament_catalog.h"
#include "filament_variants.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>

#include "../catch_amalgamated.hpp"

namespace {

// Mirrors the shipped orca_library_types for deterministic tests.
const std::set<std::string> LIB = {
    "ABS",    "ABS-GF", "ASA",     "ASA-AERO", "ASA-CF", "BVOH",     "CoPE",   "EVA",
    "HIPS",   "PA",     "PA-CF",   "PA-GF",    "PA6-CF", "PC",       "PCTG",   "PE",
    "PET-CF", "PETG",   "PETG-CF", "PHA",      "PLA",    "PLA-AERO", "PLA-CF", "PP",
    "PP-CF",  "PP-GF",  "PPA-CF",  "PPA-GF",   "PVA",    "SBS",      "TPU"};

const std::map<std::string, std::string> OVERRIDES = {
    {"rPLA", "PLA"},    {"rPETG", "PETG"}, {"TPE", "TPU"},          {"TPU-95A", "TPU"},
    {"TPU-85A", "TPU"}, {"SILK", "PLA"},   {"Color-Change", "PLA"}, {"PLA+", "PLA"},
    {"ASA+", "ASA"},    {"ABS+", "ABS"}};

struct TableFixture {
    TableFixture() {
        filament::FilamentVariantsTestAccess::set_orca_tables(LIB, OVERRIDES);
    }
    ~TableFixture() {
        filament::FilamentVariantsTestAccess::set_orca_tables({}, {});
    }
};

} // namespace

TEST_CASE_METHOD(TableFixture, "orca_match_type passes library types through", "[orca_match]") {
    CHECK(filament::orca_match_type("PLA") == "PLA");
    CHECK(filament::orca_match_type("PLA-CF") == "PLA-CF");
    CHECK(filament::orca_match_type("ASA-CF") == "ASA-CF");
    CHECK(filament::orca_match_type("TPU") == "TPU");
    CHECK(filament::orca_match_type("ABS") == "ABS");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type degrades unmatchable variants to base",
                 "[orca_match]") {
    // The reported bug: K2 CFS slot loaded with ASA-GF synced to Orca as PLA.
    CHECK(filament::orca_match_type("ASA-GF") == "ASA");
    CHECK(filament::orca_match_type("PLA-GF") == "PLA");
    CHECK(filament::orca_match_type("PETG-GF") == "PETG");
    CHECK(filament::orca_match_type("PC-GF") == "PC");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type applies explicit overrides", "[orca_match]") {
    CHECK(filament::orca_match_type("rPLA") == "PLA");
    CHECK(filament::orca_match_type("TPE") == "TPU");
    CHECK(filament::orca_match_type("TPU-95A") == "TPU");
    CHECK(filament::orca_match_type("PLA+") == "PLA");
    CHECK(filament::orca_match_type("ASA+") == "ASA");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type emits nothing rather than a wrong match",
                 "[orca_match][safety]") {
    // PET is NOT PETG — different polymer, different temperatures. A future
    // "helpful" PET->PETG mapping must fail this test loudly.
    CHECK(filament::orca_match_type("PET") == "");
    CHECK(filament::orca_match_type("PET-GF") == "");
    // High-temp engineering materials with no library equivalent. Blank makes
    // Orca show the lane as empty; a guess would give it PLA temps.
    CHECK(filament::orca_match_type("PPS") == "");
    CHECK(filament::orca_match_type("PPS-CF") == "");
    CHECK(filament::orca_match_type("PPA") == "");
    // Garbage in, nothing out — never a PLA fallback.
    CHECK(filament::orca_match_type("") == "");
    CHECK(filament::orca_match_type("NotAMaterial") == "");
}

TEST_CASE_METHOD(TableFixture, "orca_match_type handles non-catalog input", "[orca_match]") {
    // Material reaching the write path is not always catalog-sourced: firmware
    // reports strings, the whitelist dropdown has its own spellings, and users
    // type free text. [L093] — test the inputs the code actually receives.
    CHECK(filament::orca_match_type("  PLA  ") == "PLA");  // whitespace
    CHECK(filament::orca_match_type("pla") == "PLA");      // case
    CHECK(filament::orca_match_type("Silk PLA") == "PLA"); // decorated
}

// Every catalog type must resolve to a library type, or be on the explicit
// must-not-guess list. A new catalog type that silently resolves to "" would
// otherwise ship as a lane Orca shows empty, with nobody noticing. This case
// deliberately skips TableFixture — it exercises the real shipped tables
// loaded from assets/filaments.json, not the hand-mirrored copy above.
TEST_CASE("every catalog type resolves or is deliberately blank", "[orca_match][catalog]") {
    // Deliberately unmatchable — see the spec's safety rationale. PET is not
    // PETG; PPS/PPA have no library equivalent and a wrong guess is unsafe.
    const std::set<std::string> MUST_NOT_GUESS = {"PET", "PET-GF", "PPS", "PPS-CF", "PPA"};

    auto catalog = helix::printer::FilamentCatalog::load_full();
    auto products = catalog.all_products();
    REQUIRE(products.size() > 300); // sanity: the asset actually loaded

    std::set<std::string> unresolved;
    for (const auto* p : products) {
        if (p->type.empty())
            continue;
        if (filament::orca_match_type(p->type).empty() && !MUST_NOT_GUESS.count(p->type))
            unresolved.insert(p->type);
    }

    INFO("Types resolving to nothing. Either add an entry to ORCA_TYPE_OVERRIDES "
         "in scripts/import_orca_filaments.py and regenerate, or add it to "
         "MUST_NOT_GUESS above with a comment explaining why guessing is unsafe.");
    for (const auto& t : unresolved) {
        UNSCOPED_INFO("  unresolved type: " << t);
    }
    CHECK(unresolved.empty());
}

// warm_orca_tables() exists to load assets/filaments.json on the main thread
// at startup, so the first orca_match_type() call from a WebSocket background
// thread (the lane_data heal) finds the tables already populated instead of
// parsing JSON while holding g_orca_mutex on that thread.
//
// orca_tables_available() and orca_match_type() are BOTH lazy-loading
// themselves (by design, as a safety net), so asserting
// orca_tables_available() right after warm_orca_tables() would pass even if
// warm_orca_tables() were a no-op — the assertion call would just trigger the
// lazy load itself. To actually discriminate, this test breaks the lazy path
// (relative ORCA_TABLE_PATHS resolve from cwd) AFTER warm_orca_tables() has had
// its chance to run from the real cwd: if warm already populated the tables,
// the later orca_tables_available() call sees g_orca_loaded already true and
// returns the cached result; if warm did nothing, that call performs the
// FIRST load attempt from the broken cwd, fails to find the asset, and
// (because load_orca_tables_locked only ever attempts once) latches in empty.
TEST_CASE("warm_orca_tables loads the real asset without a prior match call",
          "[orca_match][warm]") {
    // Some earlier TEST_CASE in this binary may have already loaded or
    // injected tables; force back to fresh lazy mode first.
    filament::FilamentVariantsTestAccess::set_orca_tables({}, {});

    filament::warm_orca_tables();

    const std::filesystem::path original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    bool available = false;
    try {
        available = filament::orca_tables_available();
    } catch (...) {
        std::filesystem::current_path(original_cwd);
        throw;
    }
    std::filesystem::current_path(original_cwd);

    CHECK(available);

    // Restore lazy mode so later TEST_CASEs aren't left with the (possibly
    // now-latched-empty) state from this test.
    filament::FilamentVariantsTestAccess::set_orca_tables({}, {});
}

// merge_user_orca_overrides() injects user-contributed entries from
// config/user_filaments.json's `orca_type_map` into the live override table.
// It runs once at startup, AFTER warm_orca_tables(), and merges under
// g_orca_mutex. User entries must win over shipped entries in resolution
// (step 1 of orca_match_type beats step 2/3), and the empty-string "suppress"
// case must round-trip verbatim.
TEST_CASE_METHOD(TableFixture, "merge_user_orca_overrides adds and supersedes",
                 "[orca_match][user_override]") {
    // Baseline: "Unobtainium-Plus" is not in the shipped library or overrides,
    // and its base material ("Unobtainium") isn't either, so it resolves to ""
    // today (Orca shows the lane empty — the safe failure direction).
    CHECK(filament::orca_match_type("Unobtainium-Plus") == "");

    // PLA IS in the shipped library, so PLA-Silk would normally degrade to PLA
    // via extract_base_material. Pretend a user wants to suppress that and emit
    // nothing instead — that's the documented "value of \"\" means suppress"
    // case from FILAMENT_MANAGEMENT.md.
    CHECK(filament::orca_match_type("PLA-Silk") == "PLA");

    std::map<std::string, std::string> user_map;
    user_map["Unobtainium-Plus"] = "PLA"; // adds a new match where there was none
    user_map["PLA-Silk"] = "";            // suppresses a previously-working match
    user_map["rPLA"] = "PETG";            // supersedes a shipped override (was "PLA")

    filament::merge_user_orca_overrides(user_map);

    CHECK(filament::orca_match_type("Unobtainium-Plus") == "PLA"); // new entry applied
    CHECK(filament::orca_match_type("PLA-Silk") == "");            // suppress honored
    CHECK(filament::orca_match_type("rPLA") == "PETG");            // user wins over shipped
    // Unrelated shipped entries are untouched.
    CHECK(filament::orca_match_type("TPU-95A") == "TPU");
    CHECK(filament::orca_match_type("ASA-CF") == "ASA-CF");
}

TEST_CASE_METHOD(TableFixture, "merge_user_orca_overrides empty map is a no-op",
                 "[orca_match][user_override]") {
    // Capture the resolution of a few representative inputs before/after an
    // empty merge. Empty must not perturb the table — SubjectInitializer calls
    // merge unconditionally with whatever load_user_orca_type_map() returned,
    // and that returns empty when there's no user overlay on the device.
    const std::string before1 = filament::orca_match_type("PLA");
    const std::string before2 = filament::orca_match_type("rPLA");
    const std::string before3 = filament::orca_match_type("ASA-GF");

    filament::merge_user_orca_overrides({});

    CHECK(filament::orca_match_type("PLA") == before1);
    CHECK(filament::orca_match_type("rPLA") == before2);
    CHECK(filament::orca_match_type("ASA-GF") == before3);
}

TEST_CASE_METHOD(TableFixture, "merge_user_orca_overrides is idempotent",
                 "[orca_match][user_override]") {
    // SubjectInitializer runs once at startup, but double-merging the same map
    // (e.g. after a future hot-reload) must not duplicate or corrupt state —
    // std::map::operator[] overwrites, so the second merge is a no-op.
    std::map<std::string, std::string> user_map;
    user_map["MyCustomPLA"] = "PLA";

    filament::merge_user_orca_overrides(user_map);
    CHECK(filament::orca_match_type("MyCustomPLA") == "PLA");

    filament::merge_user_orca_overrides(user_map);
    CHECK(filament::orca_match_type("MyCustomPLA") == "PLA");

    // Change the value and merge again — last write wins.
    user_map["MyCustomPLA"] = "PETG";
    filament::merge_user_orca_overrides(user_map);
    CHECK(filament::orca_match_type("MyCustomPLA") == "PETG");
}

TEST_CASE_METHOD(TableFixture, "merge_user_orca_overrides supersedes a case-variant shipped key",
                 "[orca_match][user_override]") {
    // Shipped overrides are mixed-case (OVERRIDES carries "SILK" -> "PLA"). A
    // user hand-editing orca_type_map may not match that exact case. The merge
    // must still let the user win: without case-insensitive replacement, "SILK"
    // and "Silk" coexist and std::map's sorted iteration (upper before lower)
    // returns the shipped "PLA", silently dropping the user's suppression.
    CHECK(filament::orca_match_type("Silk") == "PLA"); // baseline: shipped SILK, case-insensitive

    std::map<std::string, std::string> user_map;
    user_map["Silk"] = ""; // suppress, using a different case than shipped "SILK"
    filament::merge_user_orca_overrides(user_map);

    // User wins outright regardless of the case in the query or the shipped key.
    CHECK(filament::orca_match_type("Silk") == "");
    CHECK(filament::orca_match_type("SILK") == "");
    CHECK(filament::orca_match_type("silk") == "");
}
