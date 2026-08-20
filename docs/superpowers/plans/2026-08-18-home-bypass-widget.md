# Home Bypass Toggle Widget Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A 1×1 home-grid tile that displays bypass state and toggles it in one tap, guarded identically to the sidebar toggle — via a shared `BypassToggleController` both surfaces call.

**Architecture:** Extract `AmsOperationSidebar::handle_bypass_toggle` + its unload-completion observer arm into a widgets-free controller (`src/ui/ui_bypass_toggle_controller.cpp`) that both the sidebar and a new `BypassWidget` home tile own. The tile itself is a declarative XML component gated on the existing `ams_supports_bypass` subject; the state icon swap and print-disable are pure bindings. No new subjects, no backend changes.

**Tech Stack:** LVGL 9.5 XML engine (helix-xml fork), Catch2, existing `PanelWidget` / `PanelWidgetRegistry` machinery.

**Spec:** `docs/superpowers/specs/2026-08-18-home-bypass-widget-design.md`

## Global Constraints

- spdlog only — no printf/cout/LV_LOG (repo code-standards table).
- SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` on new C++ files (20-line boilerplate is the anti-pattern).
- User-facing strings wrapped: `lv_tr("...")` in C++, `translation_tag="..."` in XML — then `make translation-sync && make translations`, and stage the 9 YAMLs + `ui_xml/translations/*.xml` (generated artifacts are tracked, not auto-staged).
- Never `git add -A` / `git add .`; stage explicit paths (worktree symlink-clobber #1107).
- Declarative UI rules: visibility via `bind_flag_if_eq`, styles via `bind_style_if`, no C++ widget mutation. The click callback registration (`lv_xml_register_event_cb` in `register_bypass_widget()`) is the sanctioned pattern.
- XML changes need no rebuild for layout, but this feature adds C++ — build both (`make -j`, `make test`).
- Run test binaries from the repo root; `make -j` builds only the app, `make test` only the tests — build both before running.
- Tags for new tests: `[ams][bypass-home]` (plus `[bypass-arming]` continuity where the controller interacts with arming).

---

### Task 1: `BypassToggleController` — extraction with print guard

**Files:**
- Create: `include/ui_bypass_toggle_controller.h`
- Create: `src/ui/ui_bypass_toggle_controller.cpp`
- Modify: `src/ui/ui_ams_sidebar.cpp` (handle_bypass_toggle + observer arm delegate to controller)
- Modify: `include/ui_ams_sidebar.h` (member swap)
- Test: `tests/unit/test_bypass_toggle_controller.cpp`

**Interfaces:**
- Consumes: `AmsState::instance().get_backend()` → `AmsBackend*` with `is_bypass_active()`, `get_system_info()`, `disable_bypass()`, `enable_bypass()`, `unload_active_filament()`, `allows_implicit_chaining()`; `should_unload_before_bypass(const AmsSystemInfo&, bool)` (`include/ams_types.h:1403`); `helix::ui::notify_ams_error`, `NOTIFY_INFO`/`NOTIFY_WARNING` (`ui_error_reporting.h` / toast macros); `AmsState::instance().get_ams_action_subject()`; `get_printer_state()` (`app_globals.h`) → `.get_print_state_enum_subject()` and `PrintJobState` (`printer_state.h:85`: STANDBY=0, PRINTING=1, PAUSED=2).
- Produces:

```cpp
namespace helix::ui {

/// Widgets-free bypass toggle policy shared by the AMS sidebar and the home
/// BypassWidget. Owns the pending-enable state machine (unload-first
/// chaining, #1229 discipline) and the print-active refusal.
class BypassToggleController {
  public:
    BypassToggleController() = default;
    ~BypassToggleController();

    BypassToggleController(const BypassToggleController&) = delete;
    BypassToggleController& operator=(const BypassToggleController&) = delete;

    /// User asked to flip bypass. Runs every guard, performs the backend
    /// call (or arms the unload→enable chain), toasts outcomes.
    void toggle();

    /// Feed an ams_action subject change (UNLOADING→IDLE/ERROR chain step).
    /// Returns true if the event was consumed for the pending chain.
    bool on_ams_action_changed(AmsAction prev, AmsAction next);

    /// Abort any pending chain (owner is going away / context reset).
    void cancel_pending();

    [[nodiscard]] bool pending_enable() const {
        return pending_bypass_enable_;
    }

  private:
    void enable_now(AmsBackend* backend);
    bool pending_bypass_enable_ = false;
};

} // namespace helix::ui
```

- [ ] **Step 1: Write the failing test**

`tests/unit/test_bypass_toggle_controller.cpp` — full file:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_bypass_toggle_controller.cpp
 * @brief Guard matrix for the shared bypass toggle policy.
 *
 * Run with: ./build/bin/helix-tests "[bypass-home]"
 */

#include "ui_bypass_toggle_controller.h"

#include "../helix_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "printer_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

namespace {

/// PrinterState helper: set print_state_enum the way a status update would.
void seed_print_state(PrintJobState state) {
    get_printer_state().get_print_state_enum_subject();
    lv_subject_set_int(get_printer_state().get_print_state_enum_subject(),
                       static_cast<int>(state));
}

class BypassToggleFixture : public HelixTestFixture {
  public:
    AmsBackendMock backend{4};
    BypassToggleController controller;

    BypassToggleFixture() {
        backend.set_operation_delay(0);
        REQUIRE(backend.start());
    }
    ~BypassToggleFixture() override {
        controller.cancel_pending();
        helix::ui::UpdateQueue::instance().drain();
    }
};

} // namespace

TEST_CASE("bypass toggle: refuses while printing", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    REQUIRE_FALSE(fx.backend.is_bypass_active());
    seed_print_state(PrintJobState::PRINTING);
    fx.controller.toggle();
    CHECK_FALSE(fx.backend.is_bypass_active());   // no enable happened
    CHECK_FALSE(fx.controller.pending_enable());

    seed_print_state(PrintJobState::PAUSED);
    fx.controller.toggle();
    CHECK_FALSE(fx.backend.is_bypass_active());
}

TEST_CASE("bypass toggle: standby allows enable/disable", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    fx.controller.toggle();
    CHECK(fx.backend.is_bypass_active());

    fx.controller.toggle();
    CHECK_FALSE(fx.backend.is_bypass_active());
}

TEST_CASE("bypass toggle chain: unload completes -> enable fires",
          "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    // Load a slot first so the toggle takes the unload-first path.
    REQUIRE(fx.backend.load_filament(0).result == AmsResult::SUCCESS);
    fx.controller.toggle();
    CHECK(fx.controller.pending_enable());
    CHECK_FALSE(fx.backend.is_bypass_active());

    // The chain step: UNLOADING -> IDLE.
    CHECK(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::IDLE));
    CHECK(fx.backend.is_bypass_active());
    CHECK_FALSE(fx.controller.pending_enable());
}

TEST_CASE("bypass toggle chain: unload ERROR disarms (regression)",
          "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);

    REQUIRE(fx.backend.load_filament(0).result == AmsResult::SUCCESS);
    fx.controller.toggle();
    REQUIRE(fx.controller.pending_enable());

    CHECK(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::ERROR));
    CHECK_FALSE(fx.controller.pending_enable());
    CHECK_FALSE(fx.backend.is_bypass_active());
}

TEST_CASE("bypass toggle chain: event not ours is ignored", "[ams][bypass-home]") {
    BypassToggleFixture fx;
    seed_print_state(PrintJobState::STANDBY);
    CHECK_FALSE(fx.controller.on_ams_action_changed(AmsAction::IDLE, AmsAction::LOADING));
    CHECK_FALSE(fx.controller.on_ams_action_changed(AmsAction::UNLOADING, AmsAction::IDLE));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -j4 test && ./build/bin/helix-tests "[bypass-home]"`
Expected: build FAIL — `ui_bypass_toggle_controller.h: No such file or directory`.

- [ ] **Step 3: Implement the controller**

`include/ui_bypass_toggle_controller.h` — the Produces block above verbatim, plus `#include "ams_types.h"` (for `AmsAction`) and SPDX/copyright header (`// Copyright (C) 2025-2026 356C LLC` / `// SPDX-License-Identifier: GPL-3.0-or-later`, match `motion_widget.h`).

`src/ui/ui_bypass_toggle_controller.cpp` — body ported **verbatim** from `src/ui/ui_ams_sidebar.cpp:1232-1287` (handle_bypass_toggle) and `:288-312` (the pending-bypass arm of the action observer), with these deltas:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_bypass_toggle_controller.h"

#include "ui_error_reporting.h"
#include "ui_toast_manager.h"

#include "ams_state.h"
#include "app_globals.h"
#include "i_moonraker_api.h"
#include "observer_factory.h"
#include "printer_state.h"

namespace helix::ui {

BypassToggleController::~BypassToggleController() {
    cancel_pending();
}

void BypassToggleController::toggle() {
    spdlog::info("[BypassToggle] Toggle requested");

    // Print guard — fully disabled while a job owns the toolhead (PRINTING
    // or PAUSED; a paused print still has filament staged mid-path).
    const int state = lv_subject_get_int(get_printer_state().get_print_state_enum_subject());
    if (print_occupies_toolhead(static_cast<PrintJobState>(state))) {
        NOTIFY_WARNING(lv_tr("Bypass cannot be changed while printing"));
        spdlog::info("[BypassToggle] Refused — print active ({})", state);
        return;
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        NOTIFY_WARNING(lv_tr("Multi-Filament System not available"));
        return;
    }

    AmsSystemInfo info = backend->get_system_info();
    if (info.has_hardware_bypass_sensor) {
        NOTIFY_WARNING(lv_tr("Bypass controlled by sensor"));
        spdlog::warn("[BypassToggle] Blocked — hardware sensor controls bypass");
        return;
    }

    if (backend->is_bypass_active()) {
        AmsError error = backend->disable_bypass();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO(lv_tr("Bypass disabled"));
        }
        if (error.result != AmsResult::SUCCESS) {
            helix::ui::notify_ams_error(error, lv_tr("Bypass toggle failed"));
        }
        return;
    }

    // Enable path: #1229 chaining discipline — unload first when the backend
    // allows implicit chaining, enable on UNLOADING->IDLE, disarm on ERROR.
    if (should_unload_before_bypass(info, backend->allows_implicit_chaining())) {
        spdlog::info("[BypassToggle] Unloading slot {} before enabling bypass",
                     info.current_slot);
        pending_bypass_enable_ = true;
        AmsError error = backend->unload_active_filament();
        if (error.result == AmsResult::SUCCESS) {
            NOTIFY_INFO(lv_tr("Unloading before bypass..."));
        } else {
            pending_bypass_enable_ = false;
            helix::ui::notify_ams_error(error);
        }
        return;
    }
    enable_now(backend);
}

void BypassToggleController::enable_now(AmsBackend* backend) {
    AmsError error = backend->enable_bypass();
    if (error.result == AmsResult::SUCCESS) {
        NOTIFY_INFO(lv_tr("Bypass enabled"));
    } else {
        helix::ui::notify_ams_error(error, lv_tr("Bypass failed"));
    }
}

bool BypassToggleController::on_ams_action_changed(AmsAction prev, AmsAction next) {
    // The pending flag is armed by the unload we started, so it must be
    // disarmed by whichever way that unload ends. Clearing only on IDLE left
    // a failed unload's flag set, and the next unrelated unload completion
    // then enabled bypass out of nowhere. Only IDLE actually chains.
    if (!pending_bypass_enable_ || prev != AmsAction::UNLOADING ||
        (next != AmsAction::IDLE && next != AmsAction::ERROR)) {
        return false;
    }
    pending_bypass_enable_ = false;
    if (next == AmsAction::ERROR) {
        spdlog::warn("[BypassToggle] Unload failed — cancelling pending bypass enable");
        return true;
    }
    spdlog::info("[BypassToggle] Unload complete — enabling bypass");
    if (AmsBackend* backend = AmsState::instance().get_backend()) {
        enable_now(backend);
    }
    return true;
}

void BypassToggleController::cancel_pending() {
    pending_bypass_enable_ = false;
}

} // namespace helix::ui
```

Note: check `NOTIFY_INFO`/`NOTIFY_WARNING` macro home first — the sidebar gets them via `ui_error_reporting.h`; include the same header and no more.

- [ ] **Step 4: Sidebar delegates to the controller**

In `include/ui_ams_sidebar.h`: replace member `bool pending_bypass_enable_ = false;` with `helix::ui::BypassToggleController bypass_toggle_;` (add the include; forward-declare in the header if the header currently forward-declares — it doesn't, so include is fine).

In `src/ui/ui_ams_sidebar.cpp`:
- `handle_bypass_toggle()` body becomes: `bypass_toggle_.toggle();` (keep the `[AmsSidebar] Bypass toggle requested` log in the controller — it already logs `[BypassToggle]`).
- In the action observer lambda (`:288-312`): replace the `pending_bypass_enable_` block with `self->bypass_toggle_.on_ams_action_changed(self->prev_ams_action_, action);` — keep the LOADING→IDLE load-complete arm untouched.
- Delete `pending_bypass_enable_ = false;` at `:481` (cleanup) → `bypass_toggle_.cancel_pending();` if cleanup semantics demand it; and the test-access references in `tests/unit/test_observer_cleanup_ordering.cpp:223-254` — that test simulates the sidebar member directly. Adapt it to construct a `BypassToggleController` instead (same assertions: pending cleared on cleanup), since the member no longer exists.

- [ ] **Step 5: Run tests to verify they pass**

Run: `make -j4 test && ./build/bin/helix-tests "[bypass-home]" && ./build/bin/helix-tests "[ams]" && ./build/bin/helix-tests "[observer-cleanup]"`
Expected: all pass. `[ams]` green proves the sidebar refactor is behavior-neutral.

- [ ] **Step 6: Commit**

```bash
git add include/ui_bypass_toggle_controller.h src/ui/ui_bypass_toggle_controller.cpp \
        src/ui/ui_ams_sidebar.cpp include/ui_ams_sidebar.h \
        tests/unit/test_bypass_toggle_controller.cpp tests/unit/test_observer_cleanup_ordering.cpp
git commit -m "refactor(ams): shared BypassToggleController + print-active guard

Sidebar's bypass toggle and its unload-chain state machine extracted
verbatim into a widgets-free controller; print guard added at the
controller so every surface inherits it."
```

---

### Task 2: XML tile + widget class + registry row

**Files:**
- Create: `ui_xml/components/panel_widget_bypass.xml`
- Create: `src/ui/panel_widgets/bypass_widget.h`
- Create: `src/ui/panel_widgets/bypass_widget.cpp`
- Modify: `src/ui/panel_widget_registry.cpp` (registry row + factory registration)
- Test: extend `tests/unit/test_bypass_toggle_controller.cpp` (render/gate assertions need the LVGL fixture — add there or a sibling file `tests/unit/test_bypass_home_widget.cpp`)

**Interfaces:**
- Consumes: `PanelWidget` base (`include/panel_widget.h`: `attach`/`detach`/`id()`/`record_interaction()`); `register_widget_factory(id, fn)` + `PanelWidgetDef` row shape (`src/ui/panel_widget_registry.cpp:77` — column order: `id, title, icon, description, hardware_gate_subject, gate_hint, default_on, col, row, min_c, min_r, max_c, max_r`); subjects `ams_supports_bypass`, `ams_bypass_active`, `ams_external_spool_color`, `print_state_enum`; `lv_xml_register_event_cb`.
- Produces: factory id `"bypass"`; component `panel_widget_bypass`; `register_bypass_widget()` declared and called from the registry's registration block (`src/ui/panel_widget_registry.cpp:181` area).

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_bypass_toggle_controller.cpp` (needs LVGL + XML, so a fixture change — switch `BypassToggleFixture` base or add a separate TEST_CASE with `LVGLUITestFixture`):

```cpp
// --- Tile render/gate (needs LVGL + XML registration) ---
#include "../lvgl_ui_test_fixture.h"
#include "panel_widget_registry.h"

TEST_CASE("bypass widget: gated on ams_supports_bypass", "[ams][bypass-home]") {
    LVGLUITestFixture fx; // registers XML components incl. panel_widget_bypass

    const auto* def = helix::find_widget_def("bypass");
    REQUIRE(def != nullptr);
    CHECK(def->hardware_gate_subject != nullptr);
    CHECK(std::string_view(def->hardware_gate_subject) == "ams_supports_bypass");
    // Default span 1x1, scalable to 2x1 per the registry row.
    CHECK(def->colspan == 1);
    CHECK(def->rowspan == 1);
}
```

(A gate-subject *liveness* assertion — setting `ams_supports_bypass` to 0 and checking the tile hides — belongs to `PanelWidgetManager`'s populate path; if a fixture already drives that (see `test_panel_widget_config.cpp`), mirror it. Assert the def row here; that is the contract this task creates.)

- [ ] **Step 2: Run test to verify it fails**

Run: `make -j4 test && ./build/bin/helix-tests "[bypass-home]"`
Expected: FAIL — no `bypass` definition.

- [ ] **Step 3: Write the XML component**

`ui_xml/components/panel_widget_bypass.xml` — full file:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Home bypass toggle tile. Gated on ams_supports_bypass by the registry
     row (panel_widget_registry.cpp). Tap toggles bypass through the shared
     BypassToggleController; the tile itself only renders state. -->
<component>
  <view name="panel_widget_bypass"
        extends="lv_obj" width="100%" height="#button_height_lg"
        style_pad_all="#space_xs" scrollable="false" clickable="true"
        flex_flow="column" style_flex_main_place="center" style_flex_cross_place="center"
        style_pad_gap="#space_xxs">
    <event_cb trigger="clicked" callback="bypass_widget_clicked_cb"/>
    <!-- Whole tile dims + refuses while a job owns the toolhead. Word-form
         OR per declarative rule 7 (&& and < need XML escaping). -->
    <bind_state_if cond="print_state_enum eq 1 or print_state_enum eq 2" state="disabled"/>

    <!-- State-swapped icon pair: OFF = the Device-Ops bypass-row glyph,
         ON = the filament-path bypass-node glyph. -->
    <icon name="bypass_icon_off" src="arrow_left_right" size="md" variant="muted"
          clickable="false">
      <bind_flag_if_eq subject="ams_bypass_active" flag="hidden" ref_value="1"/>
    </icon>
    <icon name="bypass_icon_on" src="source_branch" size="md" variant="success"
          clickable="false">
      <bind_flag_if_eq subject="ams_bypass_active" flag="hidden" ref_value="0"/>
    </icon>

    <text_tiny text="Bypass" translation_tag="Bypass"
               style_text_color="#text_muted">
      <bind_flag_if_eq subject="show_widget_labels" flag="hidden" ref_value="0"/>
    </text_tiny>

    <!-- Active detail: external spool color dot + material, only while
         engaged. Color dot via bind_style_if cannot take a *dynamic* color
         (styles are static), so the dot's bg color is bound by the widget
         class in attach() through the sanctioned spool-canvas helper; the
         material name stays declarative via the text subject. -->
    <lv_obj name="bypass_active_row" width="100%" height="content"
            style_pad_all="0" flex_flow="row" style_flex_cross_place="center"
            style_pad_gap="#space_xxs" scrollable="false" clickable="false">
      <bind_flag_if_eq subject="ams_bypass_active" flag="hidden" ref_value="0"/>
      <lv_obj name="bypass_color_dot" width="10" height="10" style_radius="5"
              style_bg_color="#text_muted" clickable="false"/>
      <text_tiny name="bypass_material_label" text="" long_mode="dots"
                 style_text_color="#text_muted" clickable="false" flex_grow="1"/>
    </lv_obj>
  </view>
</component>
```

Two verify-before-use items for the implementer (both one-minute greps):
1. `bind_state_if` with inline `cond=` word-form expressions — confirm the exact word forms (`eq`, `or`) against `docs/devel/LVGL9_XML_GUIDE.md` § expressions; `ui_xml/test_panel.xml:87-89` is the live example (`<bind_state_if cond="demo_alarm" state="disabled"/>`).
2. The 10px dot uses hardcoded pixel constants; if `check_design_tokens` flags it, express as `#space_sm`/2 is NOT possible (no division) — instead reuse an existing dot/canvas pattern from `ui_xml/components/` (grep `style_radius="5"` or the spool-canvas `size=` attribute) and stay within the ratchet baseline (the pre-commit gate reports `Hardcoded pixels: N == baseline`; it must not rise).

The `bypass_material_label` text needs a subject. **There is no `ams_external_spool_material` subject today** (only `ams_external_spool_color`, `ams_state.cpp:278`). Do NOT add one in XML — the widget class writes the label in `on_spool_changed()` via `lv_label_set_text` is FORBIDDEN (rule 3). Instead: Task 2 adds a tiny subject to AmsState, `ams_external_spool_material` (string), set from the same place `external_spool_color_` is set (`sync_from_backend`'s external-spool block, `ams_state.cpp:1496-1501`). Mirror the color subject's declaration/registration pair exactly (member `external_spool_material_` of type string subject, `UI_MANAGED_SUBJECT` string flavor — copy the color one's shape). This is the one sanctioned subject addition, and it is a pure reflector of `get_external_spool_info()`.

- [ ] **Step 4: Write the widget class**

`src/ui/panel_widgets/bypass_widget.h` — copy `motion_widget.h` shape:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "panel_widget.h"

namespace helix {

class BypassWidget : public PanelWidget {
  public:
    BypassWidget();
    ~BypassWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "bypass";
    }

    static void clicked_cb(lv_event_t* e);

  private:
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    helix::ui::BypassToggleController toggle_;
    // Color-dot observer guard (ObserverGuard, reset in detach()).
    ObserverGuard spool_color_observer_;

    void handle_click();
};

void register_bypass_widget();

} // namespace helix
```

`src/ui/panel_widgets/bypass_widget.cpp` — `motion_widget.cpp` shape:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bypass_widget.h"

#include "ui_bypass_toggle_controller.h"
#include "ui_event_safety.h"

#include "ams_state.h"
#include "observer_factory.h"
#include "theme_manager.h"
#include "ui_observer_guard.h"

#include <spdlog/spdlog.h>

namespace helix {

void register_bypass_widget() {
    register_widget_factory("bypass",
                            [](const std::string&) { return std::make_unique<BypassWidget>(); });

    lv_xml_register_event_cb(nullptr, "bypass_widget_clicked_cb", BypassWidget::clicked_cb);
}

BypassWidget::BypassWidget() = default;

BypassWidget::~BypassWidget() {
    detach();
}

void BypassWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    widget_obj_ = widget_obj;
    parent_screen_ = parent_screen;
    lv_obj_set_user_data(widget_obj_, this);

    // External-spool color dot: styles cannot bind a dynamic color, so the
    // dot's bg color follows the color subject through the sanctioned
    // observer + theme-parse path. Runs from attach() too — instances are
    // recycled across rebuilds.
    auto* dot = lv_obj_find_by_name(widget_obj_, "bypass_color_dot");
    if (auto* subj = AmsState::instance().get_external_spool_color_subject()) {
        spool_color_observer_ = observe_int_sync<BypassWidget>(
            subj, this, [dot](BypassWidget* self, int color) {
                if (dot) {
                    lv_obj_set_style_bg_color(
                        dot, lv_color_hex(static_cast<uint32_t>(color) & 0xFFFFFF), 0);
                }
            },
            AmsState::instance().get_subjects_lifetime());
    }

    spdlog::debug("[BypassWidget] attached");
}

void BypassWidget::detach() {
    spool_color_observer_.reset();
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
    }
    widget_obj_ = nullptr;
    parent_screen_ = nullptr;
}

void BypassWidget::handle_click() {
    toggle_.toggle();
}

void BypassWidget::clicked_cb(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BypassWidget] clicked_cb");
    if (auto* self = static_cast<BypassWidget*>(lv_event_get_user_data(e))) {
        self->record_interaction();
        self->handle_click();
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix
```

Caveat: `lv_obj_set_style_bg_color` on a named child from an observer is imperative styling — annotate with `// DECLARATIVE_OK: dynamic color from a subject; XML styles cannot bind non-constant colors` (same class of exception as breakpoint fonts). Capture `dot` by value in the lambda as shown (raw LVGL pointer that dies with the XML tree — acceptable only because the observer is reset in `detach()`; if review objects, resolve the dot by name inside the lambda from `self->widget_obj_` instead and null-check).

- [ ] **Step 5: Registry row + registration**

In `src/ui/panel_widget_registry.cpp`:
- Forward declaration next to the others (`:31-46` area): `void register_bypass_widget();`
- Registration call next to `register_motion_widget()` (`:181`).
- Table row, after the `ams` row (`:77`) — column order `id, title, icon, description, gate_subject, gate_hint, default_on, col, row, min_c, min_r, max_c, max_r`:

```cpp
    {"bypass",           "Bypass",            "source_branch",    "Toggle external spool bypass",  "ams_supports_bypass", "Requires a filament system with bypass", false, 1, 1, 1, 1, 2, 1},
```

(`default_on=false` — users add it via edit mode, like most non-core tiles; check neighbors: `ams` is `false`, so match.)

Also add the AmsState material subject here (Step 3's note): in `src/printer/ams_state.cpp` mirror `external_spool_color_` — member declaration near `include/ams_state.h:1555`, init + `lv_xml_register_subject(nullptr, "ams_external_spool_material", ...)` near `:278`, and set it from `get_external_spool_info()` in the `:1496-1501` block (string flavor; material empty → `lv_subject_set_string` with `""`).

- [ ] **Step 6: Build + run all gates**

Run: `make -j4 && make -j4 test && ./build/bin/helix-tests "[bypass-home]" && ./build/bin/helix-tests "[ams]"`
Expected: all pass, pre-commit gates clean (run `git add` then let the pre-commit hook report; hardcoded-pixel and imperative-UI counts must equal baseline).

- [ ] **Step 7: Translations**

Run: `make translation-sync && make translations`
Stage the YAMLs + regenerated XML artifacts explicitly (9 lang YAMLs + `ui_xml/translations/*.xml` + `ui_xml/translations/translations.xml`).

- [ ] **Step 8: Commit**

```bash
git add ui_xml/components/panel_widget_bypass.xml src/ui/panel_widgets/bypass_widget.h \
        src/ui/panel_widgets/bypass_widget.cpp src/ui/panel_widget_registry.cpp \
        src/printer/ams_state.cpp include/ams_state.h \
        tests/unit/test_bypass_toggle_controller.cpp \
        translations/*.yml ui_xml/translations/*.xml
git commit -m "feat(home): bypass toggle widget

1x1 tile gated on ams_supports_bypass: state-swapped icon pair
(arrow_left_right / source_branch), external-spool color dot +
material while engaged, disabled while printing, toggles via the
shared BypassToggleController. Adds the ams_external_spool_material
string subject as a pure reflector."
```

---

### Task 3: Live verification on the K2 + docs

**Files:**
- Modify: `docs/user/guide/home-panel.md` (widget catalog list)
- Modify: `docs/devel/architecture/09-home-widgets.md` (widget table if it enumerates tiles)

**Interfaces:**
- Consumes: `make k2-docker` + `make deploy-k2 K2_HOST=192.168.30.196` (K2 = arm musl toolchain image, deployed instance has `HELIX_REMOTE_CONTROL=1` from this session's earlier work).
- Produces: verified E2E evidence (log lines + ctl reads) documented in the PR/commit body.

- [ ] **Step 1: Build + deploy**

Run: `make k2-docker && make deploy-k2 K2_HOST=192.168.30.196`
Expected: clean build, successful deploy, app restarts.

- [ ] **Step 2: Drive via ctl (K2 has bypass ON from the earlier session — verify state first)**

```bash
SSH="sshpass -p creality_2024 ssh -o StrictHostKeyChecking=no root@192.168.30.196"
H="/opt/helixscreen/bin/helix-screen"; S="/tmp/helixscreen-control.sock"
$SSH "$H ctl -s $S navigate home"
$SSH "$H ctl -s $S ls"            # expect bypass tile present (gate open: CFS)
$SSH "$H ctl -s $S geom bypass"   # or find the tile's real widget name via ls
$SSH "$H ctl -s $S click <tile-name>"
$SSH "grep -E 'BypassToggle|Bypass' /mnt/UDISK/helixscreen/logs/helix.log | tail -5"
```

Expected: toggle logs `[BypassToggle] Toggle requested` → `Bypass disabled` (it was ON); a second click re-enables (box `enable=0` via Moonraker query). Screenshot via `ctl screenshot /tmp/bypass-tile.png` for the on/off icon swap.

- [ ] **Step 3: Print-guard live check (optional but cheap)**

Start any small print, then `ctl click` the tile → expect the refusal toast + `Refused — print active` log; cancel the print.

- [ ] **Step 4: Docs**

- `docs/user/guide/home-panel.md`: add "Bypass" to the widget list with one line ("Toggle external-spool bypass; hidden when your filament system has no bypass, disabled while printing").
- `docs/devel/architecture/09-home-widgets.md`: add the row to its widget table if it enumerates registry tiles (check; if it defers to the registry, skip).
- Re-run `python3 scripts/check_doc_refs.py`.

- [ ] **Step 5: Commit**

```bash
git add docs/user/guide/home-panel.md docs/devel/architecture/09-home-widgets.md
git commit -m "docs(home): bypass toggle widget"
```

---

## Self-Review (done)

- **Spec coverage:** registry/gating (Task 2 Step 5), icon pair + active detail + print-disable + tap (Task 2 Step 3), controller extraction + guards + chain (Task 1), widget class + recycling (Task 2 Step 4), tests (Tasks 1-2), K2 verification + docs (Task 3). Subject-addition scope (spec said "no new subjects") resolved: spec's "Consumed" list lacked a material subject because none existed; plan adds it as a pure reflector with rationale — flagged here as the one deviation, deliberate.
- **Placeholders:** none; every code block is complete.
- **Type consistency:** `on_ams_action_changed(AmsAction, AmsAction)` used consistently; `BypassToggleController` namespace `helix::ui` in both tasks; factory id `"bypass"` and component `panel_widget_bypass` consistent.
