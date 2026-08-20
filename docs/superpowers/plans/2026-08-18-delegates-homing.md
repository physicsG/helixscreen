# Delegates Homing to Printer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip HelixScreen's "home printer first?" prompt AND its synthesized G28 when the printer-side system arranges its own homing (AFC's `[AFC] auto_home`).

**Architecture:** One capability virtual `AmsBackend::delegates_homing_to_printer()` (default false), overridden only by `AmsBackendAfc` from its async-loaded `afc_config_` (false-until-loaded). Three consumption sites: `AmsSubscriptionBackend::ensure_homed_then()`'s first guard, and the two UI pre-prompt checks (AMS sidebar, filament panel). No subjects, no policy structs.

**Tech Stack:** C++20, LVGL subjects (read-only here), Catch2, existing `AfcConfigManager`/`KlipperConfigParser`.

**Spec:** `docs/superpowers/specs/2026-08-18-delegates-homing-design.md` (read it first).

## Global Constraints

- Vendor rule: `auto_home`/AFC.cfg knowledge lives ONLY in `AmsBackendAfc`. Generic code (subscription backend, sidebar, filament panel) asks `delegates_homing_to_printer()` and never names AFC.
- False-until-loaded: before `afc_config_` lands, the capability answers false — never skip a needed home, at worst one redundant prompt.
- `filament_ops_self_home()` (paused-print refusal) is a DIFFERENT question; do not modify or unify.
- Doc comments on both virtuals cross-reference each other.
- Default false on the base class; only `AmsBackendAfc` overrides in this change.
- Comments: none beyond the doc comments specified (house style).
- Build/test from repo root: `make test` builds tests; `./build/bin/helix-tests "[tag]"` runs them.

---

### Task 1: Capability virtual + AFC override

**Files:**
- Modify: `include/ams_backend.h` (beside `filament_ops_self_home()`, ~line 677)
- Modify: `include/ams_backend_afc.h` (beside `printer_reports_spool_ids()`, ~line 355)
- Create: `tests/unit/test_afc_delegates_homing.cpp`

**Interfaces:**
- Produces: `bool AmsBackend::delegates_homing_to_printer() const` (virtual, default false) — Tasks 2 and 3 consult it.
- Consumes: `AmsBackendAfc::afc_config_` (`std::unique_ptr<AfcConfigManager>`, private, friend `AmsBackendAfcConfigHelper` already declared at `include/ams_backend_afc.h:520`), `AfcConfigManager::is_loaded()` and `parser().get_bool(section, key, default)`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/unit/test_afc_delegates_homing.cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_afc_delegates_homing.cpp
 * @brief delegates_homing_to_printer(): true only when AFC.cfg's [AFC]
 * auto_home is loaded and set (#1265). False-until-loaded is the safety
 * posture — never skip a needed home, at worst one redundant prompt.
 */

#include "../lvgl_test_fixture.h"
#include "afc_config_manager.h"
#include "ams_backend_afc.h"
#include "ams_types.h"

#include <memory>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {
const char* CFG_WITH_AUTO_HOME = R"(
[AFC]
auto_home: True
)";

const char* CFG_WITHOUT_AUTO_HOME = R"(
[AFC]
tool_start: direct
)";

// Same friend-based access shape as test_afc_device_actions_config.cpp's
// AmsBackendAfcConfigHelper (declared friend at include/ams_backend_afc.h:520).
class AfcDelegatesHomingHelper : public AmsBackendAfc {
  public:
    AfcDelegatesHomingHelper() : AmsBackendAfc(nullptr, nullptr) {}

    void load_config(const char* content) {
        afc_config_ = std::make_unique<AfcConfigManager>(nullptr);
        afc_config_->load_from_string(content, "AFC/AFC.cfg");
    }
};
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "AFC delegates_homing_to_printer reads [AFC] auto_home",
                 "[afc][homing][1265]") {
    AfcDelegatesHomingHelper afc;

    // Config never loaded (first ~1-2s after connect, or fetch failed):
    // conservatively false — the prompt still fires.
    CHECK_FALSE(afc.delegates_homing_to_printer());

    afc.load_config(CFG_WITHOUT_AUTO_HOME);
    CHECK_FALSE(afc.delegates_homing_to_printer());

    afc.load_config(CFG_WITH_AUTO_HOME);
    CHECK(afc.delegates_homing_to_printer());
}

TEST_CASE_METHOD(LVGLTestFixture, "base default: no backend delegates homing",
                 "[capabilities][homing][1265]") {
    // Qualified call pins the BASE default, matching the
    // printer_reports_spool_ids pattern in test_ams_firmware_persistence.cpp.
    auto afc = std::make_unique<AmsBackendAfc>(nullptr, nullptr);
    CHECK_FALSE(afc->AmsBackend::delegates_homing_to_printer());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test 2>&1 | grep -E "error:" | head -5` then `./build/bin/helix-tests "[homing][1265]" -c`
Expected: compile error — `delegates_homing_to_printer` not a member of `AmsBackendAfc`/`AmsBackend`.

- [ ] **Step 3: Write minimal implementation**

In `include/ams_backend.h`, immediately AFTER the closing brace of `filament_ops_self_home()`'s definition (~line 681):

```cpp
    /**
     * @brief Does the printer-side system arrange its own homing for filament
     *        load/unload ops, so HelixScreen should neither prompt nor send G28?
     *
     * AFC answers true when [AFC] auto_home is set in AFC.cfg: its macros
     * home-if-needed themselves, so both our confirmation prompt and the G28
     * that ensure_homed_then() would synthesize are redundant.
     *
     * False-until-config-loaded by construction: the AFC override reads
     * afc_config_, which lands asynchronously, so an early or failed load
     * answers false — at worst one redundant prompt, never a skipped home.
     *
     * NOT the same question as filament_ops_self_home() (which gates
     * PAUSED-print refusal); the two must stay separate.
     */
    [[nodiscard]] virtual bool delegates_homing_to_printer() const {
        return false;
    }
```

In `include/ams_backend_afc.h`, immediately after the `printer_retains_spool_info()` override (~line 360):

```cpp
    /// [AFC] auto_home from AFC.cfg — true only once afc_config_ has landed
    /// (see AmsBackend::delegates_homing_to_printer()).
    [[nodiscard]] bool delegates_homing_to_printer() const override {
        return afc_config_ && afc_config_->is_loaded() &&
               afc_config_->parser().get_bool("AFC", "auto_home", false);
    }
```

Also append one sentence to `filament_ops_self_home()`'s existing doc comment in `include/ams_backend.h`, right before its closing `*/`:
`Distinct from delegates_homing_to_printer() (load/unload homing delegation); the two must stay separate.`

- [ ] **Step 4: Run test to verify it passes**

Run: `make test && ./build/bin/helix-tests "[homing][1265]"`
Expected: `All tests passed (5 assertions in 2 test cases)` (approx — exact count may differ, both cases green).

- [ ] **Step 5: Mutation check**

Change the AFC override body to `return true;` (unconditionally), rebuild, run. Expected: `base default` case still passes, `reads [AFC] auto_home` FAILS (false branches). Revert the mutation, rebuild, re-run green.

- [ ] **Step 6: Commit**

```bash
git add include/ams_backend.h include/ams_backend_afc.h tests/unit/test_afc_delegates_homing.cpp
git commit -m "feat(ams): delegates_homing_to_printer capability, AFC auto_home (#1265)"
```

---

### Task 2: Backend dispatch honors the capability

**Files:**
- Modify: `src/printer/ams_subscription_backend.cpp:370`
- Test: `tests/unit/test_afc_delegates_homing.cpp` (extend)

**Interfaces:**
- Consumes: `delegates_homing_to_printer()` from Task 1 (reachable unqualified — `AmsSubscriptionBackend` derives from `AmsBackend`).
- Produces: `ensure_homed_then()` short-circuit semantics later tasks rely on: when delegating, payload dispatches with NO prompt and NO G28, and a previously-armed `home_preconfirmed_` stays armed for a later non-delegating dispatch.

- [ ] **Step 1: Write the failing test (append to test_afc_delegates_homing.cpp)**

```cpp
namespace {

// Drives the REAL ensure_homed_then() path with a captured-gcode API, the
// same shape AfcReassertHelper uses in test_afc_spool_reassert.cpp.
class AfcDispatchHelper : public AmsBackendAfc {
  public:
    AfcDispatchHelper() : AmsBackendAfc(nullptr, nullptr) {
        std::vector<std::string> names{"lane1"};
        initialize_slots(names);
    }

    void load_config(const char* content) {
        afc_config_ = std::make_unique<AfcConfigManager>(nullptr);
        afc_config_->load_from_string(content, "AFC/AFC.cfg");
    }

    AmsError execute_gcode(const std::string& gcode) override {
        captured.push_back(gcode);
        return AmsErrorHelper::success();
    }

    bool prompted = false;
    std::vector<std::string> captured;
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "ensure_homed_then dispatches without G28 or prompt when delegating (#1265)",
                 "[afc][homing][1265]") {
    SettingsManager::instance().init_subjects();
    AfcDispatchHelper afc;
    afc.load_config(CFG_WITH_AUTO_HOME);

    // Force the unhomed branch: toolhead_homed() reads the live subject.
    lv_subject_set_int(get_printer_state().get_homed_axes_subject(), 0);

    helix::ui::set_home_confirm_prompter(
        [&afc](std::function<void()> on_confirm, std::function<void()>) {
            afc.prompted = true;
            on_confirm();
        });

    bool dispatched = false;
    afc.ensure_homed_then("AFF_LOAD LANE=lane1", [&dispatched]() { dispatched = true; });

    CHECK(dispatched);
    CHECK_FALSE(afc.prompted);
    // The payload left, and no G28 was synthesized ahead of it.
    REQUIRE(std::find(afc.captured.begin(), afc.captured.end(), "AFF_LOAD LANE=lane1") !=
            afc.captured.end());
    CHECK(std::none_of(afc.captured.begin(), afc.captured.end(),
                       [](const std::string& g) { return g == "G28"; }));

    helix::ui::set_home_confirm_prompter(nullptr);
}
```

Note: check the real subject accessor name in `include/printer_state.h` — if `get_homed_axes_subject()` does not exist, grep for how `toolhead_is_homed()` reads axes (`src/printer/toolhead_homing.cpp:17`) and set that subject instead. If the unhomed branch proves unreachable through subjects in a fixture, use `#if`-free seam alternative: assert only the delegating short-circuit via a subclass that overrides `toolhead_homed()` — that method is protected virtual on `AmsSubscriptionBackend`? Verify; if not virtual, drive unhomed state via the subject (preferred).

- [ ] **Step 2: Run test to verify it fails**

Run: `make test && ./build/bin/helix-tests "[afc][homing][1265]"`
Expected: the new case FAILS — `prompted` is true (prompt fired) and/or `G28` present in captured gcode.

- [ ] **Step 3: Implement the guard**

`src/printer/ams_subscription_backend.cpp:370`, change:

```cpp
    if (skip_homing || toolhead_homed()) {
```

to:

```cpp
    if (skip_homing || delegates_homing_to_printer() || toolhead_homed()) {
```

Update the comment block above it (lines ~359-369) by appending after the `skip_homing` sentence:
`delegates_homing_to_printer() short-circuits the same way when the printer-side system homes (AFC auto_home) — neither prompt nor G28.`

- [ ] **Step 4: Run tests**

Run: `make test && ./build/bin/helix-tests "[homing][1265],[ams]"`
Expected: all green, including the pre-existing ensure_homed_then prompt tests (they use non-delegating fixtures).

- [ ] **Step 5: Mutation check**

Temporarily change the guard to `if (skip_homing || toolhead_homed())` (drop the new term), rebuild, run the Task 2 case. Expected: FAIL (`prompted` true / G28 present). Restore, rebuild, green.

- [ ] **Step 6: Commit**

```bash
git add src/printer/ams_subscription_backend.cpp tests/unit/test_afc_delegates_homing.cpp
git commit -m "feat(ams): ensure_homed_then skips prompt and G28 when delegating (#1265)"
```

---

### Task 3: UI pre-prompt sites honor the capability

**Files:**
- Modify: `src/ui/ui_ams_sidebar.cpp:1417`
- Modify: `src/ui/ui_panel_filament.cpp:1257`
- Test: extend `tests/unit/test_afc_delegates_homing.cpp`

**Interfaces:**
- Consumes: `AmsState::instance().get_backend()` (both files already include/derive what they need) and `delegates_homing_to_printer()` from Task 1.
- Produces: user-visible behavior — no prompt at the two pre-prompt sites when the active backend delegates; no `arm_home_preconfirmed()` armed in that case.

- [ ] **Step 1: Write the failing test (append)**

```cpp
TEST_CASE_METHOD(LVGLTestFixture,
                 "sidebar and filament-panel pre-prompt predicate skips when delegating",
                 "[ui][homing][1265]") {
    // The predicate shape both UI sites use after this task. A stub backend
    // makes the capability the only variable.
    class DelegatingStub : public AmsBackendAfc {
      public:
        DelegatingStub() : AmsBackendAfc(nullptr, nullptr) {}
        [[nodiscard]] bool delegates_homing_to_printer() const override { return true; }
    };
    class NotDelegatingStub : public AmsBackendAfc {
      public:
        NotDelegatingStub() : AmsBackendAfc(nullptr, nullptr) {}
    };

    auto should_prompt = [](AmsBackendAfc& b) {
        return !b.delegates_homing_to_printer();
    };
    // (predicate exercised with unhomed state implied: the UI guards ask
    //  !toolhead_is_homed && !delegates — the delegates term alone is pinned
    //  here; the homed term is pre-existing behavior.)

    DelegatingStub d;
    NotDelegatingStub n;
    CHECK_FALSE(should_prompt(d));
    CHECK(should_prompt(n));
}
```

(If the executor finds an existing harness that drives the sidebar/filament-panel prompt end-to-end — grep `tests/unit/` for `request_home_confirmation` — prefer adding the delegating-backend case there instead; the point is to pin that the prompt does not fire when delegating. The pure predicate above is the minimum acceptable pin.)

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `delegates_homing_to_printer` is not yet consulted anywhere in UI (the predicate test fails only if the virtual is missing; if the pure test passes vacuously at this stage, that is acceptable — Task 3's real red/green is Step 3's build + the Step 4 grep below).

- [ ] **Step 3: Implement both site guards**

`src/ui/ui_ams_sidebar.cpp:1417`, change:

```cpp
    if (!helix::toolhead_is_homed(printer_state_)) {
```

to:

```cpp
    AmsBackend* delegating_backend = AmsState::instance().get_backend();
    if (!helix::toolhead_is_homed(printer_state_) &&
        !(delegating_backend && delegating_backend->delegates_homing_to_printer())) {
```

`src/ui/ui_panel_filament.cpp:1257`, same shape (it already has `is_extrusion_allowed()` context above):

```cpp
        AmsBackend* delegating_backend = AmsState::instance().get_backend();
        if (!helix::toolhead_is_homed(printer_state_) &&
            !(delegating_backend && delegating_backend->delegates_homing_to_printer())) {
```

If `AmsBackend`/`AmsState` includes are missing in either file, add the existing headers (`ams_backend.h`, `ams_state.h`) — both are already compiled into the UI layer.

- [ ] **Step 4: Verify no-backend path unchanged + wiring compiles**

Run: `make -j && grep -n "delegates_homing_to_printer" src/ui/ui_ams_sidebar.cpp src/ui/ui_panel_filament.cpp`
Expected: build succeeds; both files show the call. The no-backend path needs no test change: `delegating_backend == nullptr` → `!(nullptr && ...)` → `!(false)` → prompt branch reachable exactly as before (pinned by existing filament-panel tests, if any, and by Task 4's live check).

- [ ] **Step 5: Run the suite**

Run: `make test && ./build/bin/helix-tests "[homing][1265],[ams],[filament]"`
Expected: green.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_ams_sidebar.cpp src/ui/ui_panel_filament.cpp tests/unit/test_afc_delegates_homing.cpp
git commit -m "feat(ui): skip pre-preheat home prompt when the backend delegates homing (#1265)"
```

---

### Task 4: Live verification + docs

**Files:**
- Modify: `docs/devel/FILAMENT_MANAGEMENT.md` (AFC section)
- Modify: `docs/user/guide/filament.md` (AFC load flow)

**Interfaces:** none (verification + documentation).

- [ ] **Step 1: Live check via ctl**

```bash
TREE=delegates-homing && mkdir -p /tmp/helix-config-$TREE
HELIX_CONFIG_DIR=/tmp/helix-config-$TREE SDL_VIDEODRIVER=dummy HELIX_MOCK_AMS=afc \
  ./build/bin/helix-screen --test -vv --remote-socket /tmp/helix-$TREE.sock \
  > /tmp/helix-$TREE.log 2>&1 &
# navigate: settings > hardware > ams; confirm normal behavior (mock doesn't
# delegate — the prompt path stays reachable, which is itself the check).
grep -c "delegates_homing" /tmp/helix-$TREE.log || true
```

Then kill the instance by its pinned socket. (The mock backend not overriding the capability is by design; this run proves no regression in the default path. The delegating true-path is covered by Tasks 1-3 tests.)

- [ ] **Step 2: Docs**

In `docs/devel/FILAMENT_MANAGEMENT.md`, find the AFC section that documents config-backed behavior (grep `auto_home` — if absent, grep `update_tip_method_from_config` for the neighboring paragraph) and add:

```markdown
**Homing delegation.** With `[AFC] auto_home: True` in AFC.cfg, AFC's
macros home-if-needed themselves. `AmsBackendAfc` surfaces this via
`AmsBackend::delegates_homing_to_printer()` (false until AFC.cfg has
loaded), and all three home-first prompt sites — the AMS sidebar, the
filament panel, and `ensure_homed_then()` — skip both the prompt and the
synthesized G28. Distinct from `filament_ops_self_home()`, which governs
paused-print refusal.
```

In `docs/user/guide/filament.md`, in the AFC load-flow prose (the paragraphs around lane loading), add one sentence:

```markdown
If AFC's `auto_home` is enabled in AFC.cfg, HelixScreen skips its
home-first prompt — AFC homes the printer itself when needed.
```

- [ ] **Step 3: Full gate**

Run: `make -j && make test && ./build/bin/helix-tests "[homing],[ams],[filament],[ui]" 2>&1 | tail -2`
Expected: all green.

- [ ] **Step 4: Commit**

```bash
git add docs/devel/FILAMENT_MANAGEMENT.md docs/user/guide/filament.md
git commit -m "docs(ams): auto_home delegation skips the home-first prompt (#1265)"
```

---

## Self-Review

- **Spec coverage:** capability virtual + AFC override (Task 1), `ensure_homed_then` guard (Task 2), two UI sites + no-backend path (Task 3), docs + edge-case posture verified live (Task 4). False-until-loaded pinned by Task 1's first CHECK. Paused-print untouched — no task touches `filament_ops_self_home` bodies. ✓
- **Placeholder scan:** no TBDs; every code step carries real code. Task 3 Step 2's vacuous-pass caveat is honest about it and puts the real verification in Steps 3-4. ✓
- **Type consistency:** `delegates_homing_to_printer() const` consistently; helper class names unique per task (`AfcDelegatesHomingHelper`, `AfcDispatchHelper`) matching the friend-declaration convention needs Task 1's helper friended — executor must add `friend class AfcDelegatesHomingHelper;` and `friend class AfcDispatchHelper;` to `include/ams_backend_afc.h` alongside `AfcReassertHelper` (line 536). Noted here since Tasks 2/3 reuse the same file. ✓
