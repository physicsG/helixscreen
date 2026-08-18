# Override Merge Authority + Spool Retention Setting — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give spec §5 (override-wins merge) exactly one implementation shared by every AMS backend, add the re-bind rule (firmware reports a different positive spool id → our override clears) fixing #1281 step 7, and add an AMS-level "Keep Spool Info on Eject" setting governing the eject signal.

**Architecture:** A pure `helix::ams::merge_override()` in the override-store module becomes the single read-path authority; all six per-backend `apply_overrides()` if-chains shrink to lock + lookup + call (+ persist clear when the merge says so). A new `AmsBackend::firmware_reports_spool_ids()` capability virtual (true only on AFC and Happy Hare) makes "no firmware id" mean *eject* only where firmware actually reports ids — everywhere else the eject rule is inert and existing §6 fingerprint detectors keep their role. The write path stays `AmsState::commit_slot_edit()` (merged in `4fc40061e`).

**Tech Stack:** C++17, LVGL subjects, Catch2, pure Makefile.

**Spec:** `docs/superpowers/specs/2026-08-17-override-merge-authority-design.md`

## Global Constraints

- Work in `.worktrees/override-merge-authority` (branch `fix/override-merge-authority`, off main `4fc40061e` or later). NEVER edit from another tree — Bash cwd resets to the main repo every call; use the `workdir` parameter.
- Run tests from the WORKTREE ROOT, never another cwd; never pipe `make` output through `tail`/`head` (masks exit codes) — redirect to a file.
- `make -j` builds only the app; `make test` builds only tests; `make test-run` builds AND runs. Rebuild tests before claiming anything.
- Before building: `pgrep -x make; pgrep -x cc1plus` — a sibling session may hold the machine; check `free -h` (≥20Gi available is fine).
- SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on every new source file. spdlog only, no printf/cout. No RTTI.
- UI: XML + design tokens only; new user-facing strings need `label_tag`/`description_tag` + the translation workflow in Task 5.
- Commit subjects match repo style (`fix(ams): …`, `feat(ams): …`), cite `prestonbrown/helixscreen#1281` where the fix lands.
- The merge function is PURE: no IO, no locks, no subjects, no SettingsManager calls inside it.

---

### Task 1: `merge_override` — shared spec-§5 implementation + rule matrix

**Files:**
- Modify: `include/filament_slot_override_store.h` (after `mirror_firmware_to_lane_data`, ~line 238)
- Modify: `src/printer/filament_slot_override_store.cpp` (end of `helix::ams` section)
- Test: `tests/unit/test_filament_slot_override_store.cpp`

**Interfaces (produced — later tasks depend on these exact names):**

```cpp
namespace helix::ams {

struct MergeOptions {
    /// From SettingsManager::get_ams_keep_spool_info_on_eject() (Task 2).
    /// Default true = today's designed retention across eject.
    bool keep_spool_info_on_eject = true;
    /// True only on backends whose firmware reports a spool id while a spool
    /// is loaded (AFC, Happy Hare). There — and only there — a firmware id of
    /// 0/null means "ejected". Elsewhere firmware never reports ids, so 0 is
    /// the everyday reading and MUST NOT be treated as eject.
    bool firmware_reports_spool_ids = false;
};

struct MergeResult {
    bool cleared_rebind = false;  ///< firmware re-bound to a different spool; record dropped
    bool cleared_eject = false;   ///< eject signal + setting OFF; record dropped
};

/// Single implementation of filament_slots.md §5 plus the two cross-field
/// rules. `slot` carries FIRMWARE-reported values on entry; on return it
/// carries the values the UI should paint. When either cleared_* is true the
/// caller must drop its in-memory override and persist the clear.
MergeResult merge_override(SlotInfo& slot, const FilamentSlotOverride& o,
                           const MergeOptions& options);

} // namespace helix::ams
```

- [ ] **Step 1: Write the failing tests** — append to `tests/unit/test_filament_slot_override_store.cpp` (it already includes the store header; add `#include "ams_types.h"` if not present):

```cpp
TEST_CASE("merge_override rule matrix", "[ams][override-merge]") {
    using helix::ams::merge_override;
    SlotInfo slot;
    const auto ovr_with = [](int id) {
        helix::ams::FilamentSlotOverride o;
        o.brand = "Polymaker";
        o.spool_name = "PLA Black";
        o.spoolman_id = id;
        o.material = "PLA";
        o.color_set = true; o.color_rgb = 0x000000;
        o.catalog_id = "sku-1";
        return o;
    };

    SECTION("no rules fire when firmware agrees with the override") {
        slot.spoolman_id = 42;
        auto o = ovr_with(42);
        auto r = merge_override(slot, o, {});
        CHECK_FALSE(r.cleared_rebind);
        CHECK_FALSE(r.cleared_eject);
        CHECK(slot.spoolman_id == 42);
        CHECK(slot.brand == "Polymaker");
    }

    SECTION("re-bind: firmware reports a different positive id — whole record drops, firmware truth paints") {
        slot.spoolman_id = 7;
        auto o = ovr_with(42);
        auto r = merge_override(slot, o, {});
        CHECK(r.cleared_rebind);
        CHECK(slot.spoolman_id == 7);       // firmware truth
        CHECK(slot.brand.empty());          // no override field painted
        CHECK(slot.material.empty());
    }

    SECTION("re-bind ignores the setting — an explicit external write is not a preference") {
        slot.spoolman_id = 7;
        helix::ams::MergeOptions opts; opts.keep_spool_info_on_eject = true;
        auto r = merge_override(slot, ovr_with(42), opts);
        CHECK(r.cleared_rebind);
    }

    SECTION("re-bind never fires on eject zero") {
        slot.spoolman_id = 0;
        auto r = merge_override(slot, ovr_with(42), {});
        CHECK_FALSE(r.cleared_rebind);
    }

    SECTION("eject: firmware 0 on an id-reporting backend — retention ON keeps the record") {
        slot.spoolman_id = 0;
        helix::ams::MergeOptions opts; opts.firmware_reports_spool_ids = true;
        opts.keep_spool_info_on_eject = true;
        auto r = merge_override(slot, ovr_with(42), opts);
        CHECK_FALSE(r.cleared_eject);
        CHECK(slot.spoolman_id == 42);      // override wins, designed retention
        CHECK(slot.brand == "Polymaker");
    }

    SECTION("eject: firmware 0 on an id-reporting backend — setting OFF clears") {
        slot.spoolman_id = 0;
        helix::ams::MergeOptions opts; opts.firmware_reports_spool_ids = true;
        opts.keep_spool_info_on_eject = false;
        auto r = merge_override(slot, ovr_with(42), opts);
        CHECK(r.cleared_eject);
        CHECK_FALSE(r.cleared_rebind);
        CHECK(slot.spoolman_id == 0);
        CHECK(slot.brand.empty());
    }

    SECTION("eject rule inert on backends that never report ids (default options)") {
        // IFS/ACE/CFS/Snapmaker: firmware id is 0 every poll; a setting-OFF
        // user must not have every override nuked.
        slot.spoolman_id = 0;
        helix::ams::MergeOptions opts; opts.keep_spool_info_on_eject = false;
        auto r = merge_override(slot, ovr_with(42), opts);
        CHECK_FALSE(r.cleared_eject);
        CHECK(slot.spoolman_id == 42);
    }

    SECTION("eject rule needs a linked override — an unlinked record is never ejected") {
        slot.spoolman_id = 0;
        helix::ams::MergeOptions opts; opts.firmware_reports_spool_ids = true;
        opts.keep_spool_info_on_eject = false;
        auto r = merge_override(slot, ovr_with(0), opts);  // override holds color only
        CHECK_FALSE(r.cleared_eject);
        CHECK(slot.color_rgb == 0x000000);  // its fields still merge
    }

    SECTION("spec §5 field merge: sentinels fall through to firmware") {
        slot.spoolman_id = 9; slot.remaining_weight_g = 111.f; slot.brand = "Elegoo";
        helix::ams::FilamentSlotOverride o;  // all sentinels
        auto r = merge_override(slot, o, {});
        CHECK_FALSE(r.cleared_rebind);
        CHECK(slot.brand == "Elegoo");
        CHECK(slot.remaining_weight_g == 111.f);
        CHECK(slot.spoolman_id == 9);
    }

    SECTION("spec §5 field merge: override values win field-by-field") {
        slot.brand = "Elegoo"; slot.spool_name = "firmware-name"; slot.material = "PETG";
        auto o = ovr_with(0); o.spool_name = "user-name"; o.remaining_weight_g = 50.f;
        auto r = merge_override(slot, o, {});
        CHECK(slot.brand == "Polymaker");
        CHECK(slot.spool_name == "user-name");
        CHECK(slot.material == "PLA");
        CHECK(slot.remaining_weight_g == 50.f);
        CHECK(slot.catalog_id == "sku-1");
    }

    SECTION("weight zero is a real value, not a sentinel") {
        slot.remaining_weight_g = -1.f;
        auto o = ovr_with(0); o.remaining_weight_g = 0.f;
        merge_override(slot, o, {});
        CHECK(slot.remaining_weight_g == 0.f);
    }
}
```

- [ ] **Step 2: Run to verify it fails** — `make test > /tmp/t.log 2>&1; echo $? >> /tmp/t.log` then run the binary: `./build/bin/helix-tests "[override-merge]"`. Expected: compile error, `merge_override` not declared.

- [ ] **Step 3: Implement.** Declaration in `include/filament_slot_override_store.h` (after `mirror_firmware_to_lane_data`, inside `namespace helix::ams`), definition in `src/printer/filament_slot_override_store.cpp`:

```cpp
MergeResult merge_override(SlotInfo& slot, const FilamentSlotOverride& o,
                           const MergeOptions& options) {
    // Rule 1 — external re-bind. Another well-behaved writer (Mainsail, the
    // AFC plugin) explicitly set a DIFFERENT spool on this lane. That is a
    // statement, not a guess: the whole record drops, firmware truth paints.
    // Never gated by the setting; never fires on eject's 0/null (#1281 step 7).
    if (slot.spoolman_id > 0 && o.spoolman_id > 0 && slot.spoolman_id != o.spoolman_id) {
        MergeResult r;
        r.cleared_rebind = true;
        return r;
    }
    // Rule 2 — eject signal, setting-gated. Only meaningful where firmware
    // reports ids while loaded (AFC, Happy Hare): there, 0/null is the eject
    // the plugin itself writes. Elsewhere firmware never reports ids and 0 is
    // the everyday reading — the rule must stay inert.
    if (options.firmware_reports_spool_ids && slot.spoolman_id <= 0 && o.spoolman_id > 0 &&
        !options.keep_spool_info_on_eject) {
        MergeResult r;
        r.cleared_eject = true;
        return r;
    }
    // Spec §5 — override wins field-by-field; sentinels fall through.
    if (!o.brand.empty()) slot.brand = o.brand;
    if (!o.spool_name.empty()) slot.spool_name = o.spool_name;
    if (o.spoolman_id > 0) slot.spoolman_id = o.spoolman_id;
    if (o.spoolman_vendor_id > 0) slot.spoolman_vendor_id = o.spoolman_vendor_id;
    if (o.remaining_weight_g >= 0.0f) slot.remaining_weight_g = o.remaining_weight_g;
    if (o.total_weight_g >= 0.0f) slot.total_weight_g = o.total_weight_g;
    if (o.color_set) slot.color_rgb = o.color_rgb;
    if (!o.color_name.empty()) slot.color_name = o.color_name;
    if (!o.material.empty()) slot.material = o.material;
    if (!o.catalog_id.empty()) slot.catalog_id = o.catalog_id;
    if (!o.product_name.empty()) slot.product_name = o.product_name;
    return {};
}
```

- [ ] **Step 4: Run the matrix green** — `make test-run > /tmp/t.log 2>&1; echo $? >> /tmp/t.log`; `./build/bin/helix-tests "[override-merge]"` — all pass, exit 0.

- [ ] **Step 5: Commit** — `feat(ams): merge_override — single spec §5 implementation + re-bind/eject rules (#1281)` (stage the two source files + the test file; the pre-commit hook runs the quality gates).

---

### Task 2: capability virtual + SettingsManager setting

**Files:**
- Modify: `include/ams_backend.h` (near `has_firmware_spool_persistence()`)
- Modify: `include/ams_backend_afc.h` / `include/ams_backend_happy_hare.h` (public override)
- Modify: `src/printer/ams_backend_afc.cpp` / `src/printer/ams_backend_happy_hare.cpp` (override body)
- Modify: `include/settings_manager.h` (~line 329, beside the bypass accessors) + `src/system/settings_manager.cpp` (~line 174 init, ~line 540 accessors)
- Test: `tests/unit/test_ams_firmware_persistence.cpp` (capability) — create `tests/unit/test_settings_keep_spool_info.cpp`

**Interfaces:**
- Consumes: Task 1's `MergeOptions` field names.
- Produces: `virtual bool AmsBackend::firmware_reports_spool_ids() const` (default `false`); `bool SettingsManager::get_ams_keep_spool_info_on_eject() const`; `void SettingsManager::set_ams_keep_spool_info_on_eject(bool)`; `lv_subject_t* subject_ams_keep_spool_info_on_eject()`; subject name `"ams_keep_spool_info_on_eject"`; config key `"ams/keep_spool_info_on_eject"` (default **true**).

- [ ] **Step 1: Failing capability test** — append to `tests/unit/test_ams_firmware_persistence.cpp`:

```cpp
TEST_CASE("firmware_reports_spool_ids capability", "[ams][capabilities]") {
    CHECK_FALSE(AmsBackendAfc(nullptr, nullptr).AmsBackend::firmware_reports_spool_ids()); // default
    // Direct construction like the sibling tests in this file:
    AmsBackendAfc afc(nullptr, nullptr);
    CHECK(afc.firmware_reports_spool_ids());
    AmsBackendHappyHare hh(nullptr, nullptr);
    CHECK(hh.firmware_reports_spool_ids());
}
```

Match this file's existing construction pattern exactly (some backends take extra args); if `AmsBackend::firmware_reports_spool_ids()` cannot be called non-virtually on an instance, drop that first CHECK and keep the two backend checks. Expected: compile error, no such virtual.

- [ ] **Step 2: Failing settings test** — create `tests/unit/test_settings_keep_spool_info.cpp` following an existing SettingsManager test's fixture (e.g. grep `get_ams_always_show_bypass_spool` in `tests/unit/`):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../helix_test_fixture.h"
#include "settings_manager.h"

TEST_CASE("ams keep_spool_info_on_eject setting", "[settings][ams]") {
    HelixTestFixture fx;  // resets settings state per its contract
    auto& sm = SettingsManager::instance();
    CHECK(sm.get_ams_keep_spool_info_on_eject());          // default ON = designed retention
    sm.set_ams_keep_spool_info_on_eject(false);
    CHECK_FALSE(sm.get_ams_keep_spool_info_on_eject());
    sm.set_ams_keep_spool_info_on_eject(true);
    CHECK(sm.get_ams_keep_spool_info_on_eject());
}
```

- [ ] **Step 3: Run both, verify they fail** (compile errors).

- [ ] **Step 4: Implement.** In `include/ams_backend.h` beside `has_firmware_spool_persistence()`:

```cpp
    /// Whether this backend's firmware reports a Spoolman spool id per slot
    /// while a spool is loaded (AFC and Happy Hare publish spool_id in their
    /// status). Only there does a firmware id of 0/null mean "ejected"; on
    /// every other backend firmware never reports ids and 0 is the everyday
    /// reading. merge_override() uses this to arm the eject rule.
    virtual bool firmware_reports_spool_ids() const { return false; }
```

In `include/ams_backend_afc.h` public section: `bool firmware_reports_spool_ids() const override { return true; }` (same in `include/ams_backend_happy_hare.h`; no .cpp body needed if inline — prefer inline to match sibling capability overrides; check how `has_firmware_spool_persistence` is declared there and mirror it).

SettingsManager — mirror the bypass triple exactly:
- header (beside line 329): `bool get_ams_keep_spool_info_on_eject() const;` / `void set_ams_keep_spool_info_on_eject(bool enabled);` / accessor returning `&ams_keep_spool_info_on_eject_subject_`; private member `lv_subject_t ams_keep_spool_info_on_eject_subject_;`
- init (~line 174): `bool ams_keep_spool_info = config->get<bool>(config->df() + "ams/keep_spool_info_on_eject", true);` then `UI_MANAGED_SUBJECT_INT(ams_keep_spool_info_on_eject_subject_, ams_keep_spool_info ? 1 : 0, "ams_keep_spool_info_on_eject", ...)`
- accessors (~line 540): same shape as `get/set_ams_always_show_bypass_spool` — note the setter logs `set_ams_keep_spool_info_on_eject({})`, sets the subject, writes `config->set<bool>(config->df() + "ams/keep_spool_info_on_eject", enabled)`, `config->save()`.
- deinit: wherever `ams_always_show_bypass_spool_subject_` is torn down, add the twin.

- [ ] **Step 5: Run green** — `./build/bin/helix-tests "[capabilities]" "[settings][ams]"` via `make test-run`.
- [ ] **Step 6: Commit** — `feat(ams): firmware_reports_spool_ids capability + Keep Spool Info on Eject setting`.

---

### Task 3: AFC + Happy Hare delegate to the merge (behavior change)

**Files:**
- Modify: `src/printer/ams_backend_afc.cpp:4622` (`apply_overrides`)
- Modify: `src/printer/ams_backend_happy_hare.cpp:2396` (`apply_overrides`)
- Test: `tests/unit/test_afc_lane_data_clears.cpp` (pattern source) — create `tests/unit/test_override_rebind.cpp`

**Interfaces:**
- Consumes: `helix::ams::merge_override` (Task 1), `firmware_reports_spool_ids()` + `SettingsManager::get_ams_keep_spool_info_on_eject()` (Task 2), `override_store_->clear_async(int, SaveCallback)` (existing).
- Produces: the shared delegation shape every later backend copies verbatim:

```cpp
void AmsBackendAfc::apply_overrides(SlotInfo& slot, int slot_index) {
    // Callers hold mutex_. The whole spec §5 policy + the re-bind/eject rules
    // live in helix::ams::merge_override — the single implementation every
    // backend shares.
    auto it = overrides_.find(slot_index);
    if (it == overrides_.end())
        return;
    helix::ams::MergeOptions opts;
    opts.firmware_reports_spool_ids = firmware_reports_spool_ids();
    opts.keep_spool_info_on_eject =
        SettingsManager::instance().get_ams_keep_spool_info_on_eject();
    const auto result = helix::ams::merge_override(slot, it->second, opts);
    if (result.cleared_rebind || result.cleared_eject) {
        overrides_.erase(it);
        if (override_store_) {
            override_store_->clear_async(
                slot_index, [slot_index](bool ok, const std::string& err) {
                    if (!ok)
                        spdlog::warn("[AMS AFC] override clear persist failed for slot {}: {}",
                                     slot_index, err);
                });
        }
    }
}
```

(Happy Hare: same body, log tag `[AMS HH]`.)

- [ ] **Step 1: Write the failing behavior test** — create `tests/unit/test_override_rebind.cpp`, using the `AfcLaneDataClearHelper` fixture pattern from `test_afc_lane_data_clears.cpp` (subclass `AmsBackendAfc(nullptr, nullptr)`, `initialize_slots({"lane1","lane2"})`, feed status JSON under `mutex_`):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_override_rebind.cpp
 * @brief External re-bind clears our override; eject honors the retention
 * setting (#1281). Firmware truth must win back a lane another writer re-bound.
 */
#include "ams_backend_afc.h"
#include "ams_types.h"
#include "settings_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
class AfcRebindHelper : public AmsBackendAfc {
  public:
    AfcRebindHelper() : AmsBackendAfc(nullptr, nullptr) {
        std::vector<std::string> names{"lane1", "lane2"};
        initialize_slots(names);
    }
    void set_override(int slot_index, int spoolman_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        SlotInfo info;
        info.spoolman_id = spoolman_id;
        info.brand = "Polymaker";
        info.material = "PLA";
        persist_override(slot_index, info);
    }
    void feed_stepper(int lane_idx, std::nullptr_t) { feed_lane_json(lane_idx, nullptr); }
    void feed_stepper(int lane_idx, int spool_id) { feed_lane_json(lane_idx, spool_id); }
    [[nodiscard]] int visible_spool_id(int slot_index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const SlotInfo* s = slots_.get_slot(slot_index);
        return s ? s->spoolman_id : -1;
    }
    [[nodiscard]] std::string visible_brand(int slot_index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const SlotInfo* s = slots_.get_slot(slot_index);
        return s ? s->brand : "<none>";
    }
    [[nodiscard]] bool has_override(int slot_index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return overrides_.count(slot_index) > 0;
    }

  private:
    // Drive the live status path: AFC_stepper laneN records, mirroring
    // test_afc_lane_data_clears.cpp's feed_stepper.
    void feed_lane_json(int lane_idx, nlohmann::json spool_id) {
        nlohmann::json data;
        data["spool_id"] = spool_id;
        nlohmann::json params;
        params["AFC_stepper lane" + std::to_string(lane_idx + 1)] = data;
        nlohmann::json notification;
        notification["params"] = params;
        std::lock_guard<std::mutex> lock(mutex_);
        handle_status_update(notification);
    }
};
} // namespace

TEST_CASE("AFC external re-bind clears our override (#1281 step 7)", "[ams][afc][override-merge]") {
    AfcRebindHelper afc;
    afc.set_override(0, 42);
    // Firmware (via Mainsail/AFC macro) now reports a DIFFERENT spool:
    afc.feed_stepper(0, 169);
    CHECK(afc.visible_spool_id(0) == 169);   // firmware truth paints
    CHECK(afc.visible_brand(0).empty());     // our stale brand no longer shadows
    CHECK_FALSE(afc.has_override(0));        // record dropped
}

TEST_CASE("AFC eject retains by default, clears with setting off (#1281)", "[ams][afc][override-merge]") {
    HelixTestFixture fx;
    SettingsManager::instance().set_ams_keep_spool_info_on_eject(true);
    AfcRebindHelper afc;
    afc.set_override(0, 42);
    afc.feed_stepper(0, 42);   // firmware echoes our id — normal loaded state
    afc.feed_stepper(0, nullptr); // eject: AFC writes spool_id=None
    CHECK(afc.visible_spool_id(0) == 42);    // designed retention
    CHECK(afc.has_override(0));

    SettingsManager::instance().set_ams_keep_spool_info_on_eject(false);
    afc.set_override(1, 7);
    afc.feed_stepper(1, 7);
    afc.feed_stepper(1, nullptr);
    CHECK(afc.visible_spool_id(1) == 0);     // start fresh
    CHECK_FALSE(afc.has_override(1));
    SettingsManager::instance().set_ams_keep_spool_info_on_eject(true);
}
```

Notes for the implementer: `persist_override`, `handle_status_update`, `slots_`, `overrides_`, `mutex_` are protected members of the backend — the sibling fixture in `test_afc_lane_data_clears.cpp` accesses exactly these, copy its visibility pattern (`#define protected public` is NOT used there; it subclasses and adds helpers). If the status-path JSON shape differs (check how `test_afc_lane_data_clears.cpp` builds `AFC_stepper` notifications around line 60), mirror that builder exactly. `SettingsManager` may need the LVGL fixture instead of `HelixTestFixture` if subjects complain — mirror whichever fixture the file's siblings use for settings (`grep -l "SettingsManager::instance" tests/unit/ | head -3` and copy).

- [ ] **Step 2: Run, verify failure** — the re-bind test fails with `visible_spool_id(0) == 42` (old override still shadowing).
- [ ] **Step 3: Implement** both `apply_overrides` bodies using the delegation shape above. Also delete the now-dead `#include` of nothing — leave includes untouched.
- [ ] **Step 4: Run the new tests + the full AFC suite green**: `./build/bin/helix-tests "[afc]" "[override-merge]"`. Also `"[happy_hare]"` (behavior there is identical; covered by the unit matrix in Task 1 + capability test — no HH-specific status fixture is needed unless one already exists: if `test_ams_backend_happy_hare.cpp` has a stepper-feed fixture, add the twin re-bind test there instead of skipping).
- [ ] **Step 5: Commit** — `fix(ams): AFC/HH overrides yield to an external re-bind; eject honors retention setting (prestonbrown/helixscreen#1281)`.

---

### Task 4: ACE, AD5X IFS, Snapmaker, CFS delegate (pure refactor, rules inert)

**Files:**
- Modify: `src/printer/ams_backend_ace.cpp:1631`, `src/printer/ams_backend_ad5x_ifs.cpp:964`, `src/printer/ams_backend_snapmaker.cpp:1709`, `src/printer/ams_backend_cfs.cpp:3363`

**Interfaces:**
- Consumes: same as Task 3 minus the capability (these backends keep the default `firmware_reports_spool_ids() == false`, so `merge_override` never clears on their behalf — their §6 fingerprint detectors keep their role unchanged).

- [ ] **Step 1: Guard test first** — add to `tests/unit/test_override_rebind.cpp` (CFS-shaped; reuse the same helper idea against `AmsBackendCfs` if a CFS fixture exists in `test_ams_backend_cfs.cpp` — mirror it; otherwise construct via the file's existing helper):

```cpp
TEST_CASE("Non-id backends: eject rule inert, field merge intact", "[ams][cfs][override-merge]") {
    // CFS keeps firmware spoolman_id at 0 (it never reports one) yet carries a
    // real override. Even with retention OFF nothing clears — and the CFS
    // EMPTY->AVAILABLE promotion tail must keep working.
    SettingsManager::instance().set_ams_keep_spool_info_on_eject(false);
    // (build a CfsHelper per test_ams_backend_cfs.cpp's fixture; install an
    //  override with brand+material; feed a status where the bay reads EMPTY)
    CHECK(cfs.visible_brand(0) == "Polymaker");   // override still paints
    CHECK(cfs.slot_status(0) == SlotStatus::AVAILABLE);  // CFS tail promoted
    SettingsManager::instance().set_ams_keep_spool_info_on_eject(true);
}
```

If the CFS fixture cannot drive status cheaply, an acceptable weaker guard: construct the backend, call the (now-shared) `apply_overrides` via a subclass helper with a synthetic `SlotInfo{spoolman_id=0, status=EMPTY}` and assert brand paints + status promoted + override map entry still present. The point being pinned: **setting OFF + firmware id 0 does not clear**.

- [ ] **Step 2: Run, verify pass-or-fail honestly** — this is characterization: it may already pass. If it passes before the refactor, that is correct; the refactor must keep it passing.
- [ ] **Step 3: Refactor the four `apply_overrides`** to the Task-3 delegation shape. **CFS exception** — keep its presence tail after the merge call (it is CFS-specific presence policy, not §5 merge policy):

```cpp
void AmsBackendCfs::apply_overrides(SlotInfo& slot, int slot_index) {
    // (delegation preamble identical to Task 3's shape, log tag "[AMS CFS]")
    ...
    const auto& o = /* the override, BEFORE any erase — CFS tail reads it */;
    const auto result = helix::ams::merge_override(slot, o, opts);
    // Trust the user's assignment for presence (unchanged CFS tail):
    const bool real_assignment = o.spoolman_id > 0 || !o.material.empty() ||
                                 !o.brand.empty() || !o.spool_name.empty() || o.color_set;
    if (real_assignment && slot.status == SlotStatus::EMPTY) {
        slot.status = SlotStatus::AVAILABLE;
    }
    if (result.cleared_rebind || result.cleared_eject) { /* erase + clear_async */ }
}
```

Since rules are inert here (capability false → rules can only fire via re-bind, which needs firmware id > 0, impossible on these backends), the erase branch is dead code today but correct tomorrow — keep it anyway (one shape everywhere; a future firmware that starts reporting ids inherits it).

- [ ] **Step 4: Full AMS suite green**: `make test-run > /tmp/t.log 2>&1; echo $? >> /tmp/t.log`; `./build/bin/helix-tests "[ams]"`.
- [ ] **Step 5: Commit** — `refactor(ams): all backends share merge_override; CFS keeps its presence tail`.

---

### Task 5: Settings UI — AMS Management overlay toggle + translations

**Files:**
- Modify: `ui_xml/ams_device_operations.xml` (after the `row_ams_always_show_bypass_spool` block, ~line 136)
- Modify: `src/ui/ui_ams_device_operations_overlay.cpp` (event cb registration ~line 153, handler ~line 611, and the subject that gates visibility — find how `ams_device_ops_is_afc` is published and mirror it)

**Interfaces:**
- Consumes: Task 2's subject `ams_keep_spool_info_on_eject`.
- Produces: XML row name `row_ams_keep_spool_info_on_eject`, callback `on_ams_keep_spool_info_toggled`, visibility subject `ams_device_ops_reports_spool_ids` (int 1/0).

- [ ] **Step 1: XML row** (copy the bypass row's shape exactly — tokens, `label_tag`, `description_tag`):

```xml
        <!-- Retention across eject: keep the previously assigned spool details
             so reloading the same spool after maintenance needs no re-selection
             (default on). Turn off to start fresh whenever a lane empties.
             Meaningful on systems whose firmware reports spool ids per lane
             (AFC, Happy Hare); other systems clear on a detected spool swap
             regardless of this toggle. -->
        <setting_toggle_row name="row_ams_keep_spool_info_on_eject"
                            label="Keep Spool Info on Eject" label_tag="Keep Spool Info on Eject" icon="filament"
                            description="Remember lane spool details across an eject"
                            description_tag="Remember lane spool details across an eject"
                            subject="ams_keep_spool_info_on_eject" callback="on_ams_keep_spool_info_toggled">
          <bind_flag_if_eq subject="ams_device_ops_reports_spool_ids" flag="hidden" ref_value="0"/>
        </setting_toggle_row>
```

- [ ] **Step 2: Wire the overlay** — in `ui_ams_device_operations_overlay.cpp`: register `on_ams_keep_spool_info_toggled` (mirror `on_ams_always_show_bypass_spool_toggled` at line 611: `LVGL_SAFE_EVENT_CB_BEGIN`, read checked state, `SettingsManager::instance().set_ams_keep_spool_info_on_eject(is_checked)`); publish `ams_device_ops_reports_spool_ids` as an int subject where `ams_device_ops_is_afc` is published, value = backend's `firmware_reports_spool_ids()` (guard: no backend → 0, row hidden).
- [ ] **Step 3: Translations (L064 workflow)** — `make translation-sync && make translations`, then stage the YAMLs AND `ui_xml/translations/*.xml`.
- [ ] **Step 4: Verify in the running app** — launch the pinned-socket instance from the worktree (`HELIX_SOCK`/`HELIX_CONFIG_DIR` per AGENTS.md, `SDL_VIDEODRIVER=dummy`), `ctl navigate` to the AMS Management overlay, `ctl ls` + `ctl text row_ams_keep_spool_info_on_eject` to confirm the row exists and is hidden on the mock backend unless mock reports ids; `ctl set_value` the toggle and confirm `ctl text` reflects it. Kill the instance by pinned socket afterward.
- [ ] **Step 5: Commit** — `feat(ams): Keep Spool Info on Eject toggle in AMS Management overlay`.

---

### Task 6: Docs

**Files:**
- Modify: `docs/specs/filament_slots.md` §5 (re-bind rule amendment), §6 (setting note + AFC/HH row via merge rule)
- Modify: `docs/devel/FILAMENT_SLOT_METADATA.md` (§ merge policy points at `merge_override` as the single implementation; note the eject rule arming condition)
- Modify: `docs/devel/FILAMENT_MANAGEMENT.md` (retention subsection: setting-gated eject, re-bind always clears — replaces the "residual case … same tradeoff #1071 accepts" paragraph's final claim where it says re-bind keeps a stale binding)
- Modify: `docs/user/guide/filament.md` (AMS Management overlay section: new bullet after "Always Show Bypass Spool", user terms, no source refs) and `docs/user/CONFIGURATION.md` (`ams/keep_spool_info_on_eject`, default, what it does)

**Content sketches:**

§5 amendment (spec, third-party voice):
> Amendment (v1.6): when firmware itself reports a per-lane `spool_id` that differs from a reader's stored override, the firmware value is authoritative — HelixScreen drops its whole override record for that lane rather than shadowing the external write. An override survives only an absent/zero firmware id (ejection), which HelixScreen makes user-configurable per system.

FILAMENT_MANAGEMENT replacement paragraph (keep the #1071 gating rationale, end with):
> A lane whose firmware later reports a *different* spool id drops the binding
> entirely (`merge_override` re-bind rule, #1281) — the residual stale-binding
> case is now only the no-signal backends. On AFC and Happy Hare the eject
> signal itself is user-configurable ("Keep Spool Info on Eject", AMS
> Management overlay; default on).

User guide bullet:
> - **Keep Spool Info on Eject** — When a lane is emptied, keep its spool details so reloading the same spool after maintenance needs no re-selection (on by default). Turn it off to start fresh when a lane empties. Shown on systems whose firmware tracks spool ids per lane (such as AFC and Happy Hare); systems that detect spool swaps by tag always refresh on a swap regardless of this setting.

- [ ] **Step 1: Apply the five edits** above (match each file's tone/structure; the spec has a changelog — add a v1.6 line).
- [ ] **Step 2: Doc gates** — commit; the pre-commit doc-reference check must pass.
- [ ] **Step 3: Commit** — `docs(ams): override merge authority, re-bind rule, Keep Spool Info on Eject (#1281)`.

---

### Task 7: Issue #1281 reply + close

**Files:** none (GitHub).

- [ ] **Step 1: Draft the reply** (user's voice: educated peer, no em-dash, plain hyphens, no "Noted", concise). Draft:

> You're right that the spool info sticking was intentional - HelixScreen deliberately remembers what was on a lane so pulling a spool for maintenance and reloading it doesn't make you re-select everything. What was NOT intentional: once you picked the correct spool in Mainsail, HelixScreen should have followed it. That was a real gap - our stored assignment kept shadowing the server-side selection. Fixed: when the firmware reports a different spool id than the one we stored, we now drop our record and show what the printer actually says.
>
> There's also a new toggle in the AMS Management overlay, "Keep Spool Info on Eject" (default on), if you'd rather lanes start fresh when emptied.
>
> Both land in the next release. Closing as fixed; the retention behavior is documented in the filament guide.

- [ ] **Step 2: Show the draft to Preston and get his OK before posting** — he posts under his name; do not post unreviewed.
- [ ] **Step 3: Post + close** — `gh issue comment 1281 --body-file <draft> && gh issue close 1281` (only after approval).
- [ ] **Step 4: No commit needed** (or, if house style wants a record, none - the issue is the record).

---

## Self-Review (done at plan time)

- **Spec coverage**: shared merge fn (T1), re-bind rule (T1/T3), eject rule + setting (T1/T2/T3), all-backends generic (T3/T4 + capability flag), UI toggle (T5), docs incl. user manual (T6), issue reply + close (T7). The spec's "#1281 replay integration test" is realized as T3's two status-path tests against the real AFC backend (stronger than a mock replay). The spec's `MergeOptions` gained `firmware_reports_spool_ids` — a correctness necessity discovered during plan-context gathering (without it, setting-OFF would nuke every override on the five backends whose firmware never reports ids, since their id is 0 on every poll); the spec doc gets its §architecture wording updated in T6. ✔
- **Placeholders**: every step carries exact code or an exact sibling to mirror; the two "mirror the sibling fixture" instructions point at named files and line ranges. ✔
- **Type consistency**: `MergeOptions`/`MergeResult` field names identical in T1/T3/T4; `firmware_reports_spool_ids()` same name in T2 virtual, T3 opts wiring, T5 visibility subject (subject name is `ams_device_ops_reports_spool_ids`, distinct from the method - intentional); setting accessor names consistent T2/T3/T5; config key `ams/keep_spool_info_on_eject` consistent T2/T6. ✔
