# Ghost Scroll Buttons Implementation Plan

> ⚠️ **Historical record (verified 2026-08-09) - not instructions. Status: the FEATURE
> shipped; this DESIGN did not. Do not execute this plan - it would build a second,
> parallel implementation of something the tree already has.**
>
> Not one symbol this plan prescribes exists. `ui_scroll_buttons.{h,cpp}`,
> `helix::ui::attach_scroll_buttons()`, `ui_xml/scroll_buttons.xml`, the 3-state
> `scroll_buttons_mode` on `InputSettingsManager`, and `tests/unit/test_ui_scroll_buttons.cpp`
> are all zero-hit greps.
>
> What shipped instead:
>
> | This plan | Actual |
> |---|---|
> | `ui_xml/scroll_buttons.xml` | `ui_xml/components/page_scroll_gutter.xml` |
> | `helix::ui::attach_scroll_buttons()` (opt-in, per call site) | `src/ui/page_scroll_auto_inject.cpp` (auto-injected from `NavigationManager`) + `src/ui/page_scroll_controller.cpp` + `include/page_scroll_math.h` |
> | 3-state `scroll_buttons_mode` (Auto/On/Off) on `InputSettingsManager` | **boolean** `DisplaySettingsManager::get/set_page_scroll_buttons()` |
> | Touch & Input settings overlay | Display & Sound overlay (`ui_xml/settings_display_sound_overlay.xml`, `src/ui/ui_settings_display_sound.cpp`) |
> | `tests/unit/test_ui_scroll_buttons.cpp` | `test_page_scroll_auto_inject.cpp`, `test_page_scroll_controller.cpp`, `test_page_scroll_math.cpp`, `test_page_scroll_gutter_component.cpp`, `test_page_scroll_setting.cpp` |
>
> The two design decisions that were reversed are worth knowing: the setting became a boolean,
> not Auto/On/Off, and attachment became automatic rather than per-call-site. Read the plan for
> the LVGL floating-child and lifetime research it captured, which is still accurate. Do not
> read it as a build order.
>
> **Superseded by `docs/devel/PAGE_SCROLL_BUTTONS.md`** - that is the current description of
> what shipped, including where the gutter auto-attaches and why the walk stops at a home
> panel widget tile.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reusable semi-transparent up/down page-scroll buttons overlaid on the right edge of scrollable views, controlled by a 3-state setting (Auto/On/Off, default Auto), per Phase 1a of `docs/devel/specs/2026-06-10-esp32-display-device-design.md`.

**Architecture:** An XML component (`ui_xml/scroll_buttons.xml`, declarative appearance) plus a C++ companion `helix::ui::attach_scroll_buttons()` (logic: visibility, page-scroll, setting observation), mirroring the existing `attach_progress_arc` pattern (`include/ui_progress_arc.h` / `src/ui/ui_progress_arc.cpp`). Backed by a new `scroll_buttons_mode` setting in `InputSettingsManager` with a dropdown row in the Touch & Input settings overlay. Context lifetime is tied to the component root's `LV_EVENT_DELETE` so container `clean()`/rebuild and panel teardown are both safe.

**Tech Stack:** LVGL 9.5, helix-xml components, Catch2 (`tests/unit/`), Makefile (`make -j`, `make test-run`).

**Key facts an implementer needs (all verified against the tree):**
- Floating children: `LV_OBJ_FLAG_FLOATING` children ignore layout AND do not scroll with the parent — precedent: e-stop FAB `ui_xml/home_panel.xml:18` (`floating="true" align="top_right" style_translate_x/y`).
- `ui_button` accepts an `icon=` attribute directly (`ui_xml/ams_selector_menu.xml:44`) and a `ghost` variant (`src/ui/ui_button.cpp:314-320`).
- Icons `chevron_up` / `chevron_down` already exist in `include/ui_icon_codepoints.h:83,86` — **no font regen needed**.
- Components are registered in `src/xml_registration.cpp` via `register_xml("file.xml")` (see lines ~316, ~507).
- Settings pattern to copy: `scroll_limit` — subject init `src/system/input_settings_manager.cpp:40-42`, getter/setter `:110-124`, header `include/input_settings_manager.h:60-69,129-131`.
- Settings dropdown pattern: `setting_dropdown_row` component (`ui_xml/setting_dropdown_row.xml`); callbacks live in `src/ui/ui_panel_settings.cpp` (e.g. `on_bed_mesh_mode_changed:160`, registered in lists at `:374` and `:1579`); dropdown initial value is pushed on activate (`src/ui/ui_settings_touch.cpp:init_input_sliders`).
- Observer factory: `observe_int_sync(subject, panel, handler, lifetime = {})` (`include/observer_factory.h:331`). Callbacks are **deferred** via `queue_update` — tests must drain (`helix::ui::UpdateQueue::instance().drain()` as in `tests/unit/test_split_button.cpp:169-174`) [L048].
- The setting's subject is static (singleton `InputSettingsManager`) — `ObserverGuard` only, no `SubjectLifetime` needed. Cleanup uses `reset()` (never `release()`).
- Test fixtures: `XMLTestFixture` (registers globals.xml + widgets; see `tests/unit/test_ui_button.cpp:25` for a subclass example), `LVGLTestFixture` for non-XML tests (`test_screen()`, `process_lvgl(ms)`). Tests in `tests/unit/test_*.cpp` are auto-discovered by `mk/tests.mk`.
- i18n: after editing XML strings run `make translation-sync`; commit `translations/*.yml` **and** `ui_xml/translations/*.xml`; do **NOT** commit `src/generated/lv_i18n_translations.c` [L064].

---

### Task 1: `scroll_buttons_mode` setting in InputSettingsManager

**Files:**
- Modify: `include/input_settings_manager.h` (near `scroll_limit` members, lines ~60-69, ~129-131, ~155-156)
- Modify: `src/system/input_settings_manager.cpp` (init at ~line 45-52 block; setters near `set_scroll_limit` ~:114)
- Test: `tests/unit/test_input_settings_manager.cpp` (append)

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_input_settings_manager.cpp`:

```cpp
TEST_CASE_METHOD(LVGLTestFixture, "InputSettingsManager scroll_buttons_mode",
                 "[input_settings][scroll_buttons]") {
    Config::get_instance();
    InputSettingsManager::instance().init_subjects();

    SECTION("defaults to Auto (0)") {
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 0);
    }

    SECTION("set/get round trip") {
        InputSettingsManager::instance().set_scroll_buttons_mode(1);
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 1);
        InputSettingsManager::instance().set_scroll_buttons_mode(2);
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 2);
        InputSettingsManager::instance().set_scroll_buttons_mode(0);
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 0);
    }

    SECTION("clamps out-of-range values") {
        InputSettingsManager::instance().set_scroll_buttons_mode(7);
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 2);
        InputSettingsManager::instance().set_scroll_buttons_mode(-1);
        REQUIRE(InputSettingsManager::instance().get_scroll_buttons_mode() == 0);
    }

    SECTION("subject reflects value") {
        InputSettingsManager::instance().set_scroll_buttons_mode(1);
        REQUIRE(lv_subject_get_int(
                    InputSettingsManager::instance().subject_scroll_buttons_mode()) == 1);
    }

    // Restore default so later default-value assertions aren't poisoned by
    // the persisted config.
    InputSettingsManager::instance().set_scroll_buttons_mode(0);
    InputSettingsManager::instance().deinit_subjects();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"` (check no concurrent builds first: `pgrep -f 'make|c\+\+'`)
Expected: COMPILE FAILURE — `get_scroll_buttons_mode` is not a member of `InputSettingsManager`.

- [ ] **Step 3: Implement the setting**

In `include/input_settings_manager.h`, after the `scroll_limit` getter/setter block (~line 69):

```cpp
    /** @brief Get scroll buttons mode (0=Auto, 1=On, 2=Off) — live, no restart */
    int get_scroll_buttons_mode() const;

    /** @brief Set scroll buttons mode (updates subject + persists) */
    void set_scroll_buttons_mode(int mode);
```

After `subject_scroll_limit()` (~line 131):

```cpp
    /** @brief Scroll buttons mode subject (integer: 0=auto, 1=on, 2=off) */
    lv_subject_t* subject_scroll_buttons_mode() {
        return &scroll_buttons_mode_subject_;
    }
```

In the private members next to `scroll_limit_subject_` (~line 155):

```cpp
    lv_subject_t scroll_buttons_mode_subject_;
```

In `src/system/input_settings_manager.cpp`, in `init_subjects()` after the `scroll_limit` block (~line 42), matching the existing pattern exactly:

```cpp
    int scroll_buttons_mode = config->get<int>("/input/scroll_buttons_mode", 0);
    scroll_buttons_mode = std::clamp(scroll_buttons_mode, 0, 2);
    UI_MANAGED_SUBJECT_INT(scroll_buttons_mode_subject_, scroll_buttons_mode,
                           "settings_scroll_buttons_mode", subjects_);
```

After `set_scroll_limit` (~line 124), copying its shape:

```cpp
int InputSettingsManager::get_scroll_buttons_mode() const {
    return lv_subject_get_int(const_cast<lv_subject_t*>(&scroll_buttons_mode_subject_));
}

void InputSettingsManager::set_scroll_buttons_mode(int mode) {
    int clamped = std::clamp(mode, 0, 2);
    spdlog::info("[InputSettingsManager] set_scroll_buttons_mode({})", clamped);
    lv_subject_set_int(&scroll_buttons_mode_subject_, clamped);
    Config* config = Config::get_instance();
    config->set<int>("/input/scroll_buttons_mode", clamped);
    config->save();
}
```

(If the file uses `std::max(std::min(...))` instead of `std::clamp`, match the file's existing idiom.)

- [ ] **Step 4: Run test to verify it passes**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: all sections PASS.

- [ ] **Step 5: Commit** (explicit paths only [S002][S003])

```bash
git add include/input_settings_manager.h src/system/input_settings_manager.cpp tests/unit/test_input_settings_manager.cpp
git commit -m "feat(settings): add scroll_buttons_mode input setting (Auto/On/Off)"
```

---

### Task 2: Pure policy/math helpers + public header

**Files:**
- Create: `include/ui_scroll_buttons.h`
- Create: `src/ui/ui_scroll_buttons.cpp`
- Test: `tests/unit/test_ui_scroll_buttons.cpp` (new)

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_ui_scroll_buttons.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ui_scroll_buttons.cpp
 * @brief Unit tests for ghost scroll buttons (attach_scroll_buttons)
 */

#include "ui_scroll_buttons.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;

TEST_CASE("scroll_buttons_mode_enables policy", "[scroll_buttons][quick]") {
    // mode 1 = On: always enabled
    REQUIRE(scroll_buttons_mode_enables(1, false) == true);
    REQUIRE(scroll_buttons_mode_enables(1, true) == true);
    // mode 2 = Off: never enabled
    REQUIRE(scroll_buttons_mode_enables(2, false) == false);
    REQUIRE(scroll_buttons_mode_enables(2, true) == false);
    // mode 0 = Auto: follows remote backend presence
    REQUIRE(scroll_buttons_mode_enables(0, false) == false);
    REQUIRE(scroll_buttons_mode_enables(0, true) == true);
    // out-of-range treated as Auto
    REQUIRE(scroll_buttons_mode_enables(99, false) == false);
}

TEST_CASE("scroll_buttons_step_px is 80% of viewport, min 1", "[scroll_buttons][quick]") {
    REQUIRE(scroll_buttons_step_px(300) == 240);
    REQUIRE(scroll_buttons_step_px(100) == 80);
    REQUIRE(scroll_buttons_step_px(1) == 1);
    REQUIRE(scroll_buttons_step_px(0) == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test`
Expected: COMPILE FAILURE — `ui_scroll_buttons.h` not found.

- [ ] **Step 3: Create header and minimal implementation**

Create `include/ui_scroll_buttons.h`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

namespace helix::ui {

/// Attach ghost scroll buttons (semi-transparent up/down page-scroll overlay,
/// right edge) to a scrollable container. Creates the `scroll_buttons` XML
/// component as a floating child of `scrollable` and wires:
///   - tap up/down → page scroll by 80% of the viewport content height
///   - scroll/layout events → visibility recompute (hidden when content
///     fits; up hidden at top of range, down hidden at bottom)
///   - `settings_scroll_buttons_mode` subject → live enable/disable
///     (0=Auto → enabled only when a remote display backend is active,
///      1=On, 2=Off)
///
/// Self-cleaning: all state is freed on the component root's
/// LV_EVENT_DELETE, so both container clean()/rebuild and panel teardown
/// are safe. Idempotent: re-attaching to a container that already has the
/// overlay is a no-op (safe to call after every repopulate).
///
/// Requires InputSettingsManager subjects to be initialized (true at panel
/// creation time in production).
void attach_scroll_buttons(lv_obj_t* scrollable);

/// Pure policy: does `mode` (0=Auto, 1=On, 2=Off) enable the buttons given
/// whether a remote display backend is active? Exposed for tests.
bool scroll_buttons_mode_enables(int mode, bool remote_backend_active);

/// Pure math: page-scroll step in px for a viewport content height
/// (80%, minimum 1). Exposed for tests.
int scroll_buttons_step_px(int viewport_content_h);

} // namespace helix::ui
```

Create `src/ui/ui_scroll_buttons.cpp` (helpers only in this task; `attach_scroll_buttons` lands in Task 4 — provide a stub so the unit links):

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_scroll_buttons.h"

#include <algorithm>

namespace helix::ui {

bool scroll_buttons_mode_enables(int mode, bool remote_backend_active) {
    switch (mode) {
    case 1:
        return true; // On
    case 2:
        return false; // Off
    default:
        return remote_backend_active; // 0 (and anything unexpected) = Auto
    }
}

int scroll_buttons_step_px(int viewport_content_h) {
    return std::max(1, viewport_content_h * 8 / 10);
}

void attach_scroll_buttons(lv_obj_t* scrollable) {
    (void)scrollable; // implemented in the behavior task
}

} // namespace helix::ui
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: PASS (both new TEST_CASEs plus Task 1's).

- [ ] **Step 5: Commit**

```bash
git add include/ui_scroll_buttons.h src/ui/ui_scroll_buttons.cpp tests/unit/test_ui_scroll_buttons.cpp
git commit -m "feat(ui): scroll buttons policy + step helpers"
```

---

### Task 3: `scroll_buttons` XML component + registration

**Files:**
- Create: `ui_xml/scroll_buttons.xml`
- Modify: `src/xml_registration.cpp` (add `register_xml("scroll_buttons.xml");` after `register_xml("form_field.xml");` at ~line 316)
- Test: `tests/unit/test_ui_scroll_buttons.cpp` (extend)

- [ ] **Step 1: Write the failing test**

Add to `tests/unit/test_ui_scroll_buttons.cpp` (new includes at top of file):

```cpp
#include "config.h"
#include "input_settings_manager.h"
#include "ui_update_queue.h"

#include "../test_fixtures.h"

#include <cstring>
```

And the fixture + creation test:

```cpp
// ============================================================================
// Fixture: XMLTestFixture (globals + widgets) + scroll_buttons component +
// InputSettingsManager subjects (the attach helper reads the mode subject).
// ============================================================================
class ScrollButtonsTestFixture : public XMLTestFixture {
  public:
    ScrollButtonsTestFixture() : XMLTestFixture() {
        lv_xml_register_component_from_file("A:ui_xml/scroll_buttons.xml");
        Config::get_instance();
        helix::InputSettingsManager::instance().init_subjects();
    }

    ~ScrollButtonsTestFixture() override {
        helix::InputSettingsManager::instance().set_scroll_buttons_mode(0);
        helix::InputSettingsManager::instance().deinit_subjects();
    }

    // Scrollable 300px-tall container with `n` 50px children on the test screen.
    lv_obj_t* make_scrollable(int n_children) {
        lv_obj_t* cont = lv_obj_create(test_screen());
        lv_obj_set_size(cont, 400, 300);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        for (int i = 0; i < n_children; ++i) {
            lv_obj_t* item = lv_obj_create(cont);
            lv_obj_set_size(item, 360, 50);
        }
        lv_obj_update_layout(cont);
        return cont;
    }
};

TEST_CASE_METHOD(ScrollButtonsTestFixture, "scroll_buttons component instantiates",
                 "[scroll_buttons][xml]") {
    lv_obj_t* cont = make_scrollable(2);
    auto* root = static_cast<lv_obj_t*>(lv_xml_create(cont, "scroll_buttons", nullptr));
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_find_by_name(root, "scroll_btn_up") != nullptr);
    REQUIRE(lv_obj_find_by_name(root, "scroll_btn_down") != nullptr);
    REQUIRE(lv_obj_has_flag(root, LV_OBJ_FLAG_FLOATING));
    REQUIRE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)); // starts hidden until attach decides
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: FAIL — `lv_xml_create` returns null (component XML file doesn't exist).

- [ ] **Step 3: Create the component XML**

Create `ui_xml/scroll_buttons.xml`:

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Ghost scroll buttons: floating up/down page-scroll overlay for scrollable
     views. Created by helix::ui::attach_scroll_buttons() — do not instantiate
     directly in panel XML. Visibility is driven by the C++ companion. -->
<component>
  <view name="scroll_buttons"
        extends="lv_obj" width="content" height="content"
        floating="true" align="right_mid" style_translate_x="-8"
        style_bg_opa="0" style_border_width="0" style_pad_all="0"
        style_pad_gap="#space_md" flex_flow="column" scrollable="false"
        hidden="true">
    <ui_button name="scroll_btn_up" icon="chevron_up"
               width="#button_height" height="#button_height"
               style_radius="9999" variant="ghost" focusable="false"
               style_bg_color="#elevated_bg" style_bg_opa="140"/>
    <ui_button name="scroll_btn_down" icon="chevron_down"
               width="#button_height" height="#button_height"
               style_radius="9999" variant="ghost" focusable="false"
               style_bg_color="#elevated_bg" style_bg_opa="140"/>
  </view>
</component>
```

Notes: `floating="true"` keeps it out of flex layout AND pinned during parent scroll (e-stop FAB precedent). Press feedback comes from `ui_button` itself — do not add `transform_scale` (invisible on transparent containers, L078). XML-only changes need no rebuild [L031], but the registration below is C++.

In `src/xml_registration.cpp`, after `register_xml("form_field.xml");` (~line 316):

```cpp
    register_xml("scroll_buttons.xml"); // ghost scroll overlay (ui_scroll_buttons.cpp companion)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: PASS. (The test fixture registers the file directly, but the `xml_registration.cpp` line is what makes it work in production — verify it compiles via `make -j`.)

- [ ] **Step 5: Commit**

```bash
git add ui_xml/scroll_buttons.xml src/xml_registration.cpp tests/unit/test_ui_scroll_buttons.cpp
git commit -m "feat(ui): scroll_buttons XML component + registration"
```

---

### Task 4: attach_scroll_buttons behavior (visibility, paging, live setting)

**Files:**
- Modify: `src/ui/ui_scroll_buttons.cpp` (replace stub)
- Test: `tests/unit/test_ui_scroll_buttons.cpp` (extend)

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/test_ui_scroll_buttons.cpp`:

```cpp
TEST_CASE_METHOD(ScrollButtonsTestFixture, "attach: hidden when content fits",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1); // On
    lv_obj_t* cont = make_scrollable(2); // 100px content in 300px viewport
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* root = lv_obj_find_by_name(cont, "scroll_buttons");
    REQUIRE(root != nullptr);
    REQUIRE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "attach: overflow shows down, hides up at top",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
    lv_obj_t* cont = make_scrollable(20); // ~1000px content in 300px viewport
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* root = lv_obj_find_by_name(cont, "scroll_buttons");
    lv_obj_t* up = lv_obj_find_by_name(cont, "scroll_btn_up");
    lv_obj_t* down = lv_obj_find_by_name(cont, "scroll_btn_down");
    REQUIRE_FALSE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
    REQUIRE(lv_obj_has_flag(up, LV_OBJ_FLAG_HIDDEN));        // at top
    REQUIRE_FALSE(lv_obj_has_flag(down, LV_OBJ_FLAG_HIDDEN)); // can go down
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "tap down pages by 80% and reveals up",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* up = lv_obj_find_by_name(cont, "scroll_btn_up");
    lv_obj_t* down = lv_obj_find_by_name(cont, "scroll_btn_down");

    int step = helix::ui::scroll_buttons_step_px(lv_obj_get_content_height(cont));
    lv_obj_send_event(down, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(cont);
    REQUIRE(lv_obj_get_scroll_y(cont) == step);
    REQUIRE_FALSE(lv_obj_has_flag(up, LV_OBJ_FLAG_HIDDEN)); // no longer at top

    lv_obj_send_event(up, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(cont);
    REQUIRE(lv_obj_get_scroll_y(cont) == 0); // clamped back to top
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "down hides at bottom of range",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* down = lv_obj_find_by_name(cont, "scroll_btn_down");

    // Scroll all the way down programmatically; SCROLL event drives recompute.
    lv_obj_scroll_to_y(cont, LV_COORD_MAX, LV_ANIM_OFF);
    lv_obj_update_layout(cont);
    REQUIRE(lv_obj_has_flag(down, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "mode Off hides; live change to On shows",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(2); // Off
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* root = lv_obj_find_by_name(cont, "scroll_buttons");
    REQUIRE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));

    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1); // On
    // Observer callbacks are deferred via queue_update — drain first [L048]
    helix::ui::UpdateQueue::instance().drain();
    REQUIRE_FALSE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "Auto resolves to Off without remote backend",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(0); // Auto
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);
    lv_obj_update_layout(cont);
    lv_obj_t* root = lv_obj_find_by_name(cont, "scroll_buttons");
    REQUIRE(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "attach is idempotent",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);
    helix::ui::attach_scroll_buttons(cont); // second call must be a no-op
    int count = 0;
    for (uint32_t i = 0; i < lv_obj_get_child_count(cont); ++i) {
        const char* name = lv_obj_get_name(lv_obj_get_child(cont, i));
        if (name && strcmp(name, "scroll_buttons") == 0) {
            count++;
        }
    }
    REQUIRE(count == 1);
}

TEST_CASE_METHOD(ScrollButtonsTestFixture, "clean and delete are UAF-safe",
                 "[scroll_buttons][behavior]") {
    helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
    lv_obj_t* cont = make_scrollable(20);
    helix::ui::attach_scroll_buttons(cont);

    SECTION("container clean destroys overlay; re-attach works") {
        lv_obj_clean(cont); // deletes overlay root → cleanup path fires
        helix::ui::UpdateQueue::instance().drain();
        // Setting change after clean must not touch freed state
        helix::InputSettingsManager::instance().set_scroll_buttons_mode(2);
        helix::ui::UpdateQueue::instance().drain();
        // Rebuild + re-attach
        for (int i = 0; i < 20; ++i) {
            lv_obj_t* item = lv_obj_create(cont);
            lv_obj_set_size(item, 360, 50);
        }
        helix::InputSettingsManager::instance().set_scroll_buttons_mode(1);
        helix::ui::UpdateQueue::instance().drain();
        helix::ui::attach_scroll_buttons(cont);
        lv_obj_update_layout(cont);
        REQUIRE(lv_obj_find_by_name(cont, "scroll_buttons") != nullptr);
    }

    SECTION("deleting the scrollable does not crash") {
        lv_obj_delete(cont);
        helix::ui::UpdateQueue::instance().drain();
        helix::InputSettingsManager::instance().set_scroll_buttons_mode(2);
        helix::ui::UpdateQueue::instance().drain();
        SUCCEED("No crash");
    }
}
```

Note: `lv_obj_get_name` — if the component root isn't auto-named `scroll_buttons` by `lv_xml_create`, name it explicitly in `attach_scroll_buttons` via `lv_obj_set_name(root, "scroll_buttons")` (part of Step 3) so the idempotence check and panel lookups work.

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: FAIL — attach is a stub; no overlay is created.

- [ ] **Step 3: Implement attach behavior**

Replace the stub in `src/ui/ui_scroll_buttons.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_scroll_buttons.h"

#include "input_settings_manager.h"
#include "observer_factory.h"
#include "ui_observer_guard.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace helix::ui {

namespace {

// Hysteresis so 1-2px of rounding slop doesn't flicker the edge buttons.
constexpr int kEdgeEpsilonPx = 4;

struct AttachContext {
    lv_obj_t* scrollable = nullptr;
    lv_obj_t* root = nullptr; // scroll_buttons component instance
    lv_obj_t* btn_up = nullptr;
    lv_obj_t* btn_down = nullptr;
    int mode = 0; // cached settings_scroll_buttons_mode value
    ObserverGuard mode_observer;
};

bool remote_backend_active() {
    // DisplayBackendType::REMOTE (Phase 1b of the ESP32 display design) does
    // not exist yet, so Auto currently resolves to disabled. When the remote
    // backend lands, query DisplayManager::backend()->type() here.
    return false;
}

void update_visibility(AttachContext* ctx) {
    if (!ctx || !ctx->root || !ctx->scrollable || !ctx->btn_up || !ctx->btn_down) {
        return;
    }
    if (!scroll_buttons_mode_enables(ctx->mode, remote_backend_active())) {
        lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    int32_t scroll_y = lv_obj_get_scroll_y(ctx->scrollable);
    int32_t scroll_bottom = lv_obj_get_scroll_bottom(ctx->scrollable);
    if (scroll_y <= 0 && scroll_bottom <= 0) {
        // Content fits in the viewport — nothing to page through.
        lv_obj_add_flag(ctx->root, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(ctx->root, LV_OBJ_FLAG_HIDDEN);
    if (scroll_y > kEdgeEpsilonPx) {
        lv_obj_remove_flag(ctx->btn_up, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->btn_up, LV_OBJ_FLAG_HIDDEN);
    }
    if (scroll_bottom > kEdgeEpsilonPx) {
        lv_obj_remove_flag(ctx->btn_down, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->btn_down, LV_OBJ_FLAG_HIDDEN);
    }
}

void on_scroll_or_layout(lv_event_t* e) {
    update_visibility(static_cast<AttachContext*>(lv_event_get_user_data(e)));
}

void page_scroll(AttachContext* ctx, int direction) {
    if (!ctx || !ctx->scrollable) {
        return;
    }
    int step = scroll_buttons_step_px(lv_obj_get_content_height(ctx->scrollable));
    int32_t scroll_y = lv_obj_get_scroll_y(ctx->scrollable);
    int32_t target;
    if (direction < 0) {
        target = std::max<int32_t>(0, scroll_y - step);
    } else {
        int32_t remaining = lv_obj_get_scroll_bottom(ctx->scrollable);
        target = scroll_y + std::min<int32_t>(step, std::max<int32_t>(0, remaining));
    }
    // ANIM_OFF: deterministic, single repaint — friendly to the remote
    // display backend this feature was designed alongside, and to tests.
    lv_obj_scroll_to_y(ctx->scrollable, target, LV_ANIM_OFF);
    update_visibility(ctx);
}

void on_up_clicked(lv_event_t* e) {
    page_scroll(static_cast<AttachContext*>(lv_event_get_user_data(e)), -1);
}

void on_down_clicked(lv_event_t* e) {
    page_scroll(static_cast<AttachContext*>(lv_event_get_user_data(e)), +1);
}

// Single cleanup path, tied to the overlay ROOT's deletion. Covers both
// lv_obj_clean(scrollable) (rebuild paths) and scrollable teardown, since
// the root is a child of the scrollable either way.
void on_root_deleted(lv_event_t* e) {
    auto* ctx = static_cast<AttachContext*>(lv_event_get_user_data(e));
    if (!ctx) {
        return;
    }
    ctx->mode_observer.reset(); // reset(), never release() — see ui_observer_guard.h
    if (ctx->scrollable) {
        lv_obj_remove_event_cb_with_user_data(ctx->scrollable, on_scroll_or_layout, ctx);
    }
    delete ctx;
}

} // namespace

bool scroll_buttons_mode_enables(int mode, bool remote_backend_active) {
    switch (mode) {
    case 1:
        return true; // On
    case 2:
        return false; // Off
    default:
        return remote_backend_active; // 0 (and anything unexpected) = Auto
    }
}

int scroll_buttons_step_px(int viewport_content_h) {
    return std::max(1, viewport_content_h * 8 / 10);
}

void attach_scroll_buttons(lv_obj_t* scrollable) {
    if (!scrollable) {
        return;
    }
    if (lv_obj_find_by_name(scrollable, "scroll_btn_up")) {
        return; // already attached (idempotent — safe after repopulate)
    }
    auto* root = static_cast<lv_obj_t*>(lv_xml_create(scrollable, "scroll_buttons", nullptr));
    if (!root) {
        spdlog::error("[ScrollButtons] failed to create scroll_buttons component");
        return;
    }
    lv_obj_set_name(root, "scroll_buttons");

    auto* ctx = new AttachContext{};
    ctx->scrollable = scrollable;
    ctx->root = root;
    ctx->btn_up = lv_obj_find_by_name(root, "scroll_btn_up");
    ctx->btn_down = lv_obj_find_by_name(root, "scroll_btn_down");
    ctx->mode = helix::InputSettingsManager::instance().get_scroll_buttons_mode();

    lv_obj_add_event_cb(scrollable, on_scroll_or_layout, LV_EVENT_SCROLL, ctx);
    lv_obj_add_event_cb(scrollable, on_scroll_or_layout, LV_EVENT_SIZE_CHANGED, ctx);
    lv_obj_add_event_cb(scrollable, on_scroll_or_layout, LV_EVENT_CHILD_CHANGED, ctx);
    if (ctx->btn_up) {
        lv_obj_add_event_cb(ctx->btn_up, on_up_clicked, LV_EVENT_CLICKED, ctx);
    }
    if (ctx->btn_down) {
        lv_obj_add_event_cb(ctx->btn_down, on_down_clicked, LV_EVENT_CLICKED, ctx);
    }
    lv_obj_add_event_cb(root, on_root_deleted, LV_EVENT_DELETE, ctx);

    // Static subject (singleton InputSettingsManager) — ObserverGuard only,
    // no SubjectLifetime needed. Callback is deferred via queue_update;
    // on_root_deleted's reset() removes the observer and the deferred-lambda
    // weak-alive token guards the in-flight case (#174).
    ctx->mode_observer = observe_int_sync(
        helix::InputSettingsManager::instance().subject_scroll_buttons_mode(), ctx,
        [](AttachContext* c, int mode) {
            c->mode = mode;
            update_visibility(c);
        });

    update_visibility(ctx);
}

} // namespace helix::ui
```

Implementation notes for the executor:
- Verify the `observe_int_sync` handler arity against an existing call site (`grep -rn "observe_int_sync(" src/ui/ | head -3`) — the lambda receives `(Panel*, int)`. Adjust if the codebase form differs.
- `lv_obj_set_name` / `lv_obj_remove_event_cb_with_user_data`: confirm exact names exist in `lib/lvgl` headers (`grep -rn "lv_obj_set_name\b" lib/helix-xml lib/lvgl/src/core/lv_obj* | head`). If `lv_obj_set_name` doesn't exist in this tree, the XML `name=` on the view root may already provide it — adapt the idempotence check to look for `scroll_btn_up` only (already the primary check).
- Do NOT add `transform_scale` press feedback (L078); `ui_button` handles pressed state.

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test && ./build/bin/helix-tests "[scroll_buttons]"`
Expected: all PASS, including the UAF-safety sections.

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_scroll_buttons.cpp tests/unit/test_ui_scroll_buttons.cpp
git commit -m "feat(ui): attach_scroll_buttons paging, visibility, live setting"
```

---

### Task 5: Settings UI row, callback, dropdown sync, i18n

**Files:**
- Modify: `ui_xml/settings_touch_overlay.xml` (after `row_scroll_guard`, ~line 37-42)
- Modify: `src/ui/ui_panel_settings.cpp` (callback near `on_scroll_limit_changed` at :270; register in BOTH lists at ~:384 and ~:1579)
- Modify: `src/ui/ui_settings_touch.cpp` (`init_input_sliders`, ~line 92-115)
- Modify (generated): `translations/*.yml`, `ui_xml/translations/*.xml` via `make translation-sync`

- [ ] **Step 1: Add the XML row**

In `ui_xml/settings_touch_overlay.xml`, after the `row_scroll_guard` block:

```xml
      <setting_dropdown_row name="row_scroll_buttons"
                            label="Scroll Buttons" label_tag="Scroll Buttons" icon="swap_vertical"
                            description="Show tap-to-page up/down buttons on scrollable lists"
                            description_tag="Show tap-to-page up/down buttons on scrollable lists"
                            options="Auto&#10;On&#10;Off" options_tag="Auto&#10;On&#10;Off"
                            callback="on_scroll_buttons_changed"/>
```

(`swap_vertical` is verified present in ICON_MAP; pick another existing ICON_MAP icon if a better glyph is preferred, but do NOT add a new icon — that requires font regen.)

- [ ] **Step 2: Add the callback in `ui_panel_settings.cpp`**

Near `on_scroll_limit_changed` (:270), following the dropdown pattern of `on_bed_mesh_mode_changed` (:160):

```cpp
static void on_scroll_buttons_changed(lv_event_t* e) {
    lv_obj_t* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    int mode = static_cast<int>(lv_dropdown_get_selected(dropdown));
    spdlog::info("[SettingsPanel] Scroll buttons mode changed: {} ({})", mode,
                 mode == 0 ? "Auto" : (mode == 1 ? "On" : "Off"));
    InputSettingsManager::instance().set_scroll_buttons_mode(mode);
    // Live-apply — no restart prompt.
}
```

Register `{"on_scroll_buttons_changed", on_scroll_buttons_changed},` in BOTH `register_xml_callbacks` lists — anchor on the existing `on_scroll_limit_changed` entries at ~:384 and ~:1579 (`grep -n on_scroll_limit_changed src/ui/ui_panel_settings.cpp`).

- [ ] **Step 3: Sync dropdown value on activate**

In `src/ui/ui_settings_touch.cpp`, at the end of `init_input_sliders()` (sliders sync there for the same reason — XML captures construction-time values):

```cpp
    // Dropdowns also capture construction-time state; push persisted value.
    if (lv_obj_t* row = lv_obj_find_by_name(overlay_root_, "row_scroll_buttons")) {
        if (lv_obj_t* dropdown = lv_obj_find_by_name(row, "dropdown")) {
            lv_dropdown_set_selected(dropdown,
                                     static_cast<uint32_t>(input.get_scroll_buttons_mode()));
        }
    }
```

- [ ] **Step 4: Translations [L064]**

```bash
make translation-sync
```

Then verify the new keys appeared (`grep "Scroll Buttons" translations/en.yml`). Stage `translations/*.yml` AND `ui_xml/translations/*.xml`. Do NOT stage `src/generated/lv_i18n_translations.c`.

- [ ] **Step 5: Build + manual smoke test**

```bash
pgrep -f 'make|c\+\+' || make -j
./build/bin/helix-screen --test -vv
```

Manually (or via the executor's screenshot tooling): Settings → Touch & Input → confirm the "Scroll Buttons" row renders, dropdown shows Auto/On/Off, selection persists across overlay close/reopen (check log line `set_scroll_buttons_mode`).

- [ ] **Step 6: Commit**

```bash
git add ui_xml/settings_touch_overlay.xml src/ui/ui_panel_settings.cpp src/ui/ui_settings_touch.cpp translations/*.yml ui_xml/translations/*.xml
git diff --staged --stat   # verify nothing unrelated is staged
git commit -m "feat(settings): Scroll Buttons Auto/On/Off setting in Touch & Input"
```

---

### Task 6: Panel integrations (console, macros, settings, file list)

**Files:**
- Modify: `src/ui/ui_panel_console.cpp` (~:316-331, after `console_container_` lookup)
- Modify: `src/ui/ui_panel_macros.cpp` (~:105-112, after `macro_list_container_` lookup)
- Modify: `src/ui/ui_panel_settings.cpp` (`SettingsPanel::setup` at :484 — the settings_panel root view is itself scrollable per `ui_xml/settings_panel.xml:14`)
- Modify: `src/ui/ui_print_select_list_view.cpp` (`set_container`, `container_ = container;` at ~:81; ALSO at end of the row-rebuild method — find via `grep -n "lv_obj_clean\|safe_clean_children\|populate" src/ui/ui_print_select_list_view.cpp`)
- Modify: `src/ui/ui_print_select_card_view.cpp` (same pattern, `container_ = container;` at ~:127 + end of rebuild method)

Each integration is the same two lines — include + call:

```cpp
#include "ui_scroll_buttons.h"
```

```cpp
    helix::ui::attach_scroll_buttons(<container>);
```

- [ ] **Step 1: Console** — after `console_container_` null-check (`if (!console_container_) { ... }` block ends ~:334): `helix::ui::attach_scroll_buttons(console_container_);`
- [ ] **Step 2: Macros** — after `macro_list_container_` null-check (~:112 block): `helix::ui::attach_scroll_buttons(macro_list_container_);`. If the macro list is rebuilt on activate (grep for `lv_obj_clean`/`safe_clean_children` on `macro_list_container_`), ALSO call attach at the end of that rebuild method — attach is idempotent and the overlay dies with `clean()`.
- [ ] **Step 3: Settings panel** — in `SettingsPanel::setup(lv_obj_t* panel, ...)` (:484) after `PanelBase::setup(panel, parent_screen);`: `helix::ui::attach_scroll_buttons(panel);`
- [ ] **Step 4: Print select (file list)** — in BOTH view classes: after `container_ = container;` in `set_container`, AND at the end of each method that cleans + repopulates rows/cards (the overlay is destroyed by `lv_obj_clean`; re-attach after rebuild). Find rebuild sites: `grep -n "lv_obj_clean\|safe_clean_children" src/ui/ui_print_select_list_view.cpp src/ui/ui_print_select_card_view.cpp`. Guard with `if (container_)`.
- [ ] **Step 5: Build + full test suite**

```bash
pgrep -f 'make|c\+\+' || make -j
make test-run
```

Expected: clean build, all tests pass (pre-existing failures unrelated to scroll buttons must be noted, not absorbed).

- [ ] **Step 6: Manual verification (requires user interaction [L060])**

```bash
HELIX_LOG_LEVEL= ./build/bin/helix-screen --test -vv 2>&1 | tee /tmp/scroll-buttons-test.log
```

Run as a background task, then ask the user to: set Settings → Touch & Input → Scroll Buttons = On; visit Console, Macros, Settings, Print Select (list + card views); confirm ghost buttons appear only on overflowing lists, page up/down on tap, hide at range edges; set Off and confirm they disappear live; press 'S' for screenshots (BMP — convert before viewing). Wait for the user's confirmation; then read /tmp/scroll-buttons-test.log.

- [ ] **Step 7: Commit**

```bash
git add src/ui/ui_panel_console.cpp src/ui/ui_panel_macros.cpp src/ui/ui_panel_settings.cpp src/ui/ui_print_select_list_view.cpp src/ui/ui_print_select_card_view.cpp
git diff --staged --stat   # verify only these five files
git commit -m "feat(ui): ghost scroll buttons on console, macros, settings, file list"
```

---

### Task 7: Final review pass

- [ ] **Step 1:** Re-run everything: `make -j && make test-run && ./build/bin/helix-tests "[scroll_buttons]"` — all green, with output captured (verification before completion).
- [ ] **Step 2:** `git log --oneline main@{u}..HEAD 2>/dev/null || git log --oneline -8` — confirm each commit contains only its task's files (pre-commit clang-format may re-stage; verify HEAD contents per project convention).
- [ ] **Step 3:** Check L081 lint and formatting ran clean in the pre-commit output (they run automatically).
- [ ] **Step 4:** Follow-ups to note in the PR/commit description, NOT to implement: history list adoption (`list_content` in `ui_panel_history_list.cpp` — has virtual-scroll interplay, deferred), Auto-mode wiring to `DisplayBackendType::REMOTE` when Phase 1b lands, optional fade-instead-of-hide polish.

## Out of Scope (YAGNI)

- No fade animations on button show/hide (binary hidden flag only).
- No left-handed / position setting.
- No horizontal scroll support.
- No history-list integration (virtual scrolling needs its own look).
- No remote-backend detection beyond the `remote_backend_active()` stub returning false.
