# Interactive Toolhead Plate Implementation Plan

> **For agentic workers:** execute task by task in order, in `.worktrees/toolhead-plate`
> (branch `feature/toolhead-plate`). Steps use checkbox (`- [ ]`) syntax. The repo's
> Verification Cadence overrides the superpowers execution skills: per task you run only
> `make t F='<the task's tags>'` (via `gmake` on the Mac), prove each new test red once by
> breaking the line it pins, and commit. No sweeps, gates, mutation runs or reviews per task:
> the orchestrator runs one review, one scoped `mutate-diff`, screenshots and one `land-gate`
> at the end. Run the app only headless: `HELIX_HEADLESS=1 SDL_VIDEODRIVER=dummy`.

**Goal:** Make the home Toolhead widget's plate interactive (tap to aim + Move here, drag
to move live), share one plate (drawing, gestures, lift rule) with the Motion panel's Bed
tab, lift above a print still on the bed, stop the Motion panel from homing on its own, and
give the 2x3 widget configurable preset slots.

**Architecture:** Pure decision headers (lift rule, bed occupancy, plate layers, gesture,
preset slots) carry the rules and the unit tests. Three runtime pieces sit on them:
`BedOccupancyState` (per-printer, history-derived, persisted), `ToolheadMoveSender`
(coalesced sends, split out of MotionPanel), and the drag-surface capability for home edit
mode. MotionPanel's Bed tab and ToolheadWidget are both rewired onto these. Nothing about
the plate is written twice.

**Tech Stack:** C++17, LVGL 9.5 + helix-xml declarative XML, Catch2, Moonraker mock.

**Spec:** `docs/devel/plans/2026-10-27-toolhead-plate-design.md` (read it first: the
decisions and the reasons live there).

**Contracts already in the tree (declarations, syntax-checked):** `include/motion_presets.h`
(PlateLift block), `include/bed_occupancy.h`, `include/bed_occupancy_state.h`,
`include/bed_plate_draw.h`, `include/toolhead_move_sender.h`, `include/bed_plate_gesture.h`,
`include/bed_plate_context.h`, `include/ui_drag_surface.h`, `include/home_macro_run.h`,
`include/toolhead_preset_slots.h`, `src/ui/panel_widgets/toolhead_layout.h`
(`presets_extra_h`, `show_presets`). Implement against them. If a declaration turns out
wrong, change it in its own task and note it in the commit body. Never fork a second one.

## Global Constraints

- `.claude/rules/declarative-ui.md`: event callbacks, visibility, text and styling in XML.
  The C++ exceptions are draw hooks, gesture/press handlers, measured layout, and
  `mark_drag_surface()`.
- `.claude/rules/threading.md`: RPC callbacks go through `lifetime_.bg_cb(...)`. Any raw
  `lv_timer_t*` is cancelled in the destructor too. Observers take their `SubjectLifetime`.
- Every new `src/*.cpp` the app links is added to
  `firmware/helixscreen-esp32/components/helixapp/app_srcs.txt` and is exception-free:
  `json::parse(s, nullptr, false)`, `json_util::safe_*`, no `j.value`/`j.at`/`try`.
  `scripts/check_esp32_app_srcs.py` gates it. Large file-scope statics get `HELIX_PSRAM_BSS`.
- Constants are `kCamelCase`. JSON comes from `#include "hv/json.hpp"`. Log with spdlog, no
  printf.
- UI strings use `lv_tr("...")` with US English and no em dashes. Follow the translation
  procedure in the project memory note "Adding translated strings" (yaml entry, then regen).
- No comment archaeology: comments state present-tense constraints.
- `moves_machine="true"` goes on every new control that moves the head. Bump the matching
  `kGuardedFiles` row in `tests/unit/test_job_holds_machine.cpp`.
- Config: new keys need no migration (they default when absent). Version 31 is reserved for
  this branch if one turns out to be needed.
- Exact values: clearance setting `motion/bed_map_clearance` (default 5mm),
  `kPartClearanceMinMm = 1.0`, `kPlateDragThresholdPx = 8`, `kMinGridSpacingPx = 12`,
  ghost timeout 5000 ms, Z step `kToolheadZStepMm = 10`, `kParkZLiftMm = 10` (existing),
  preset slots default Front / Center / Park.

## Review Focus

1. A tap meant to open Motion lands on an inert plate (unhomed, printing, paused, edit
   mode). Expected: Motion opens and nothing is sent. Pinned in Task 9.
2. History says COMPLETED, but Klipper restarted, so the exclude-object geometry is empty.
   Expected: the lift still uses the job's height, and the outlines come from the snapshot.
   Pinned in Task 3.
3. A drag started on a homed plate when the printer starts a job mid-drag (or a move is
   refused). Expected: streaming stops and no further moves go out. Pinned in Tasks 6 and 9.
4. A part taller than z_max minus 1mm. Expected: Refuse, no XY move, and a notice telling the
   user to remove the part and tap Cleared. Pinned in Tasks 1, 6 and 7.
5. A widget recycled across a home rebuild while a ghost is showing or a move is in flight.
   Expected: no callback reaches a detached widget, and the ghost is cleared in `detach()`.
   Pinned in Task 9.

---

### Task 1: Lift rule

**Files:** Modify `src/motion_presets.cpp`, `include/motion_presets.h` (delete
`bed_map_lift_z`), `src/ui/ui_panel_motion.cpp` (`bed_lift_z` uses `plate_lift`). Test:
`tests/unit/test_motion_presets.cpp`.

**Interfaces:**
- Produces: `plate_z_floor`, `plate_lift`, `clamp_z_down_to_floor`, `PlateLift`,
  `kPartClearanceMinMm`.
- `MotionPanel::bed_lift_z()` keeps returning `std::optional<double>` for now: Lift gives
  `z`, otherwise nullopt, called with `part_height_mm = nullopt`. Task 6 replaces it.

- [ ] **Step 1: Tests** `TEST_CASE("plate_lift decides the lift floor", "[motion][presets][lift]")`:
  - `plate_lift(0.2, 5, 300, nullopt)` gives `{Lift, 5}`
  - `plate_lift(6, 5, 300, nullopt)` gives `None`
  - `plate_lift(5 - 1e-7, 5, 300, nullopt)` gives `None`
  - `plate_lift(0.2, 5, 300, 38)` gives `{Lift, 43}`
  - `plate_lift(0.2, 5, 40, 38)` gives `{Lift, 40}`
  - `plate_lift(0.2, 5, 38.5, 38)` gives `Refuse`
  - `plate_lift(0.2, 5, 3, nullopt)` gives `{Lift, 3}`
  - `plate_lift(3, 5, 3, nullopt)` gives `None`
  - `plate_lift(45, 5, 300, 38)` gives `None`

  `TEST_CASE("clamp_z_down_to_floor keeps a lowered nozzle above a part", "[motion][presets][lift]")`:
  - `(20, 5, 38)` gives 43
  - `(50, 5, 38)` gives 50
  - `(0, 5, nullopt)` gives 0
  - `plate_z_floor(5, nullopt) == 5` and `plate_z_floor(5, 38) == 43`

  Move the existing `bed_map_lift_z` cases onto `plate_lift` with `nullopt`.
- [ ] **Step 2:** `make t F='[motion][presets]'`: FAIL (undefined).
- [ ] **Step 3:** Implement the three functions. Delete `bed_map_lift_z` and update its
  one caller.
- [ ] **Step 4:** `make t F='[motion][presets],[motion]'`: PASS.
- [ ] **Step 5:** Commit `feat(motion): lift floor that clears a part on the bed`.

### Task 2: Job height, occupancy decision, outline snapshot (pure)

**Files:**
- Modify: `include/print_history_data.h` (add `double object_height = 0.0;` to
  `PrintHistoryJob`), `src/.../print_history_parse` (`parse_history_job` reads
  `metadata.object_height` with `json_util::safe_*`), `src/api/moonraker_client_mock_history.cpp`
  (put the file header's `object_height` in each job's metadata).
- Create: `src/printer/bed_occupancy.cpp`; add it to `app_srcs.txt`.
- Tests: `tests/unit/test_bed_occupancy.cpp` (new), plus one case in the existing history
  parse test file.

**Interfaces:** Produces `decide_bed_occupancy`, `bed_outline_snapshot_to_json`,
`bed_outline_snapshot_from_json`, and `PrintHistoryJob::object_height`.

- [ ] **Step 1: Tests** `[bed_occupancy]`:
  - COMPLETED, id "a", height 38, cleared "" gives `{occupied, 38, "a"}`.
  - The same with cleared "a" gives not occupied.
  - CANCELLED and ERROR are occupied. UNKNOWN and IN_PROGRESS are not.
  - `print_active` gives not occupied.
  - No newest job gives not occupied.
  - Height 0 gives occupied with `part_height_mm == nullopt`.
  - Snapshot round trip keeps the points.
  - Writing 70 objects and 40 points each persists 64 objects and 32 points each.
  - `from_json("garbage")`, `from_json("{}")` and a wrong `objects` type all give nullopt.

  `[history]`: `parse_history_job` reads `metadata.object_height = 12.5`, and a missing
  value gives 0.
- [ ] **Step 2:** Run the tags: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** `make t F='[bed_occupancy],[history]'`: PASS.
- [ ] **Step 5:** Commit `feat(history): job object height and the bed-occupancy decision`.

### Task 3: BedOccupancyState

**Files:**
- Create: `src/printer/bed_occupancy_state.cpp` (add to `app_srcs.txt`).
- Modify: `src/app_globals.cpp` (the `helix::get_bed_occupancy_state`/`set_bed_occupancy_state` global),
  `src/application/printer_session.cpp`. Create it right after `m_history_manager`, then
  call `init_subjects()`, then `set_bed_occupancy_state()`. Reset it after
  `StaticSubjectRegistry::deinit_all()`, the same way `m_job_queue_state` is ordered. Clear
  the global before the reset.
- Test: `tests/unit/test_bed_occupancy_state.cpp`.

**Interfaces:**
- Consumes Task 2.
- Produces the `get_bed_occupancy_state()` global, the subjects `bed_occupied` and
  `bed_occupancy_version`, `outlines()` and `mark_cleared()`.

**Behavior:**
- **Connect:** observe the connection state. On CONNECTED, call
  `history->ensure_loaded(HistoryScope::RECENT)`.
- **History:** use `history->add_observer`, registered in `init_subjects` and removed in the
  destructor.
- **Print lifecycle:** observe `get_print_lifecycle_subject()`.
  - On a transition into Complete, Cancelled or Error, call `capture_outline_snapshot()`.
    It reads `excluded_objects_state()`: each object's polygon, or its bbox as 4 corners.
    It stores `{filename = get_effective_print_filename(), objects}` under
    `df() + kBedOutlineSnapshotKey`.
  - Then call `refresh()`.
- **Newest job:** `get_jobs().front()`, not `get_newest_existing_job()`, because a part is on
  the bed even when its file was deleted.
- **mark_cleared:** writes `df() + kBedClearedJobKey`, then `config->save()`.
- **On change:** bump `bed_occupancy_version` whenever `current_` or the outline source
  changes.
- **Logging:** each verdict change at info level with the job id, height and reason.

- [ ] **Step 1: Tests** `[bed_occupancy][state]` (LVGL fixture with the mock client):
  - The newest mock job is COMPLETED with height H: `bed_occupied == 1` and
    `current().part_height_mm == H`.
  - `mark_cleared()`: `bed_occupied == 0`, and a new `BedOccupancyState` over the same
    config stays 0.
  - Lifecycle set to Printing: 0.
  - Defined objects with polygons, then lifecycle Complete, then clear the excluded-objects
    state: `outlines()` still returns them from the snapshot.
  - A snapshot whose filename differs from the newest job's: `outlines()` is empty.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[bed_occupancy]'` PASS.
- [ ] **Step 5:** Commit `feat(printer): per-printer bed occupancy from print history`.

### Task 4: Shared plate layers (grid and outlines)

**Files:**
- Create: `src/ui/bed_plate_draw.cpp` (add to `app_srcs.txt`).
- Modify: `src/ui/ui_panel_motion.cpp`. `draw_bed_grid` becomes `plan_bed_plate_layers` plus
  `draw_bed_plate_layers`:
  - outlines come from `get_bed_occupancy_state()->outlines()`;
  - colors and opacity come from the current plate style, with `grid_opa` at 30% of the
    border opacity as today and the outline color from the `warning` token;
  - the plate is invalidated whenever `bed_occupancy_version` changes (an observer held by
    MotionPanel).
- Test: `tests/unit/test_bed_plate_draw.cpp`.

**Interfaces:** Produces `plan_bed_plate_layers` and `draw_bed_plate_layers`. Task 9 uses
both.

- [ ] **Step 1: Tests** `[bed_plate_draw]`:
  - A 0..350 square bed in a 357px viewport gives 12 grid segments (6 interior lines per
    axis), all inside the viewport.
  - The same bed at 60px gives an empty grid (50mm is about 8.6px).
  - A circular bed gives segments whose endpoints lie inside the circle.
  - Square outline (100,100)-(150,150) maps to the `mm_to_px` corners.
  - An outline point at x = -20 is clamped to the plate edge.
  - A 2-point outline is dropped.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. The grid comes from `bed_grid_lines(...)`; do
  not compute it again.
- [ ] **Step 4:** `make t F='[bed_plate_draw],[motion]'` PASS.
- [ ] **Step 5:** Commit `feat(motion): one plate drawing for grid and print outlines`.

### Task 5: ToolheadMoveSender

**Files:**
- Create: `src/ui/toolhead_move_sender.cpp` (add to `app_srcs.txt`).
- Modify MotionPanel (`include/ui_panel_motion.h`, `src/ui/ui_panel_motion.cpp`):
  - replace `jog_coalescer_`, `target_start_z_` and `send_jog_move` with a
    `ToolheadMoveSender move_sender_`;
  - its hooks do what the current `on_jog_ack`/`on_jog_error` bodies do (refresh the bed
    readout; set the gesture failed; `stop_hold_repeat()`);
  - the "Jog failed" notification moves into the sender.
- Test: `tests/unit/test_toolhead_move_sender.cpp`.

**Interfaces:** Produces `ToolheadMoveSender`. Tasks 6, 7, 9 and 10 use it.

- [ ] **Step 1: Tests** `[move_sender]` (MoonrakerAPI mock that captures gcode and lets the
  test ack or fail by hand):
  - A first target is sent at once. A second and third, while the first is in flight, send
    nothing. The ack sends only the third.
  - A delta after a pending target discards the target.
  - An error resets the coalescer, calls `on_error` once, and the next target is sent at once.
  - A target setting Z with `clamp_bounds.has_z == false` returns false and sends nothing.
  - A target outside the bounds is sent clamped.
  - `target_start_z` returns the pending target's Z.
- [ ] **Step 2:** FAIL. **Step 3:** Implement by moving the existing bodies, not rewriting
  them.
- [ ] **Step 4:** `make t F='[move_sender],[motion],[jog_coalescer]'` PASS.
- [ ] **Step 5:** Commit `refactor(motion): coalesced move sending as its own class`.

### Task 6: Plate gesture and plate context; Bed tab on them

**Files:**
- Create: `src/ui/bed_plate_gesture.cpp` and `src/ui/bed_plate_context.cpp` (both to
  `app_srcs.txt`).
- Modify MotionPanel:
  - `handle_bed_touch` delegates to a `BedPlateGesture bed_gesture_` member, and the
    `bed_*` gesture members go;
  - `bed_plate()` becomes `current_bed_plate()`, `clamp_bounds()` becomes
    `move_clamp_bounds()`, and `bed_lift_z()` becomes `current_plate_lift()`;
  - a Released tap goes to `bed_gesture_.send_once(target)`.
- Tests: `tests/unit/test_bed_plate_gesture.cpp`, plus an existing motion test file.

**Interfaces:**
- Consumes Tasks 1, 3 and 5.
- `current_plate_lift` reads `get_bed_occupancy_state()` (none present means no part).
- `plate_moves_allowed` is all three of `motion_*_homed`, `nav_buttons_enabled`,
  `!machine_motion_blocked`, and `!is_external_blocking_operation_active()`.

**Readout:**
- With a part on the bed: `"Z {}, will lift to {}mm: last print is {}mm tall"`.
- Otherwise: the existing text.
- On Refuse, a `NOTIFY_WARNING`: `"The part on the bed is too tall to clear. Remove it, then tap Cleared."`

- [ ] **Step 1: Tests** `[bed_plate_gesture]` (pure, hooks recorded):
  - Press and release within 8px gives `tap_target` set, `was_drag = false`, nothing sent.
  - Moving 9px sends the target under the finger.
  - Two samples on the same target send once.
  - `lift()` is called once per gesture, and its `{Lift, z}` is set as `target.z` on every
    sent target.
  - Lift returns Refuse: `refused()` is called and nothing is sent for the whole gesture.
  - `can_move()` false: `press()` returns false, and `pressing`/`release` send nothing.
  - `mark_failed()` stops further sends.
  - A drag release sends the release point only if it differs from the last sent target.
  - `lost()` sends nothing.
  - `send_once` asks `can_move` and `lift` afresh each call.

  `[motion]`: with the Bed tab active and `bed_occupied` at height 38, a tap sends a move
  whose Z is 43.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[bed_plate_gesture],[motion],[motion][presets]'` PASS.
- [ ] **Step 5:** Commit `refactor(motion): Bed tab gestures through the shared plate gesture`.

### Task 7: No automatic homing in Motion; presets lift; Cleared on the Bed tab

**Files:** `src/ui/ui_panel_motion.cpp`, `ui_xml/motion_panel.xml`,
`ui_xml/components/move_preset_grid.xml`, `ui_xml/components/motion_bed_map.xml`, and the
translation yaml. Tests: `tests/unit/test_motion_move_tab.cpp` and
`tests/unit/test_motion_panel_coordinates.cpp`.

**Behavior:**
- **Remove automatic homing.** Delete `ensure_homed_then` / `ensure_xy_homed_then` from
  `request_axis_target`, `handle_preset`, `handle_park` and `commit_bed_target`. Delete the
  static `ensure_xy_homed_then`. A call while unhomed returns and sends nothing.
- **XML: presets and Park.** The 9 presets and `move_park` get
  `<bind_state_if cond="motion_x_homed eq 0 or motion_y_homed eq 0 or motion_z_homed eq 0" state="disabled"/>`.
- **XML: coordinates.** Each `header_pos_{x,y,z}_group` / coordinate item is disabled while
  its own axis is unhomed.
- **XML: Bed tab surface.** `bed_map_area` gets the same all-axes condition. Its existing
  readout text ("Home all axes to use the map") stays.
- **Preset lift.** `handle_preset` builds the target with `motion_preset_target`, then sends
  through `bed_gesture_.send_once`, so it gets the lift and the Refuse notice.
- **Park lift.** `park_over_plate(lift_z)` lifts to the higher of current + `kParkZLiftMm`
  and the plate floor, capped at z_max. A Refuse from `current_plate_lift` blocks the park.
  The ParkToolhead macro path is unchanged apart from the unhomed refusal.
- **Bed tab Cleared button.** A `bed_map_cleared` button sits in `motion_bed_map.xml`:
  - label `lv_tr("Cleared")`;
  - hidden unless `bed_occupied eq 1`;
  - callback `on_motion_bed_cleared`, which calls `get_bed_occupancy_state()->mark_cleared()`;
  - no `moves_machine`.
- **Census.** Re-count the `kGuardedFiles` rows (presets and Park keep their guards).

- [ ] **Step 1: Tests** `[motion][xml]`:
  - Unhomed mock, preset tapped: no gcode at all, and no `G28`.
  - Same for Park and coordinate entry.
  - Unhomed, tap on the Bed tab: no gcode.
  - Homed with a part at height 38, preset Front: the move's Z is 43.
  - Homed with no part and Z = 0.2, preset: the move's Z is 5.
  - Unhomed: the preset button has `LV_STATE_DISABLED`.
  - `bed_map_cleared` is hidden at `bed_occupied = 0` and visible at 1, and clicking it sets
    `bed_occupied` to 0.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[motion],[job_holds_machine]'` PASS.
- [ ] **Step 5:** Commit `feat(motion): no automatic homing, presets clear a part on the bed`.

### Task 8: Drag-surface capability

**Files:**
- Create: `src/ui/ui_drag_surface.cpp` (add to `app_srcs.txt`).
- Modify: `src/ui/ui_panel_home.cpp#should_suppress_edit_mode` and the mirrored check in
  `src/ui/ui_fan_control_overlay.cpp`. Both call `is_drag_surface_hit(target, current)` in
  place of the class walk.
- Test: a new case in `tests/unit/test_home_edit_page_swipe.cpp` (`EditHomeFixture`).

**Behavior:** `mark_drag_surface` does four things:
- adds `kDragSurfaceFlag` and `LV_OBJ_FLAG_CLICKABLE`;
- removes `LV_OBJ_FLAG_SCROLLABLE` and `LV_OBJ_FLAG_SCROLL_CHAIN`;
- adds `LV_OBJ_FLAG_PRESS_LOCK`;
- removes `LV_OBJ_FLAG_GESTURE_BUBBLE`.

- [ ] **Step 1: Tests** `[home][drag_surface]`:
  - A marked `lv_obj` inside a home widget: a held press (synthetic pointer, past the
    long-press time) does not enter edit mode, and a horizontal drag starting on it does
    not change the carousel page.
  - The same object unmarked: the hold enters edit mode.
  - A fan arc still suppresses edit mode.
  - `mark_drag_surface` leaves the flag set and `SCROLL_CHAIN` clear.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[home],[fan]'` PASS.
- [ ] **Step 5:** Commit `feat(home): widgets can declare a drag surface`.

### Task 9: Interactive widget plate

**Files:** `src/ui/panel_widgets/toolhead_widget.{h,cpp}`,
`ui_xml/components/panel_widget_toolhead.xml`, the translation yaml,
`tests/unit/test_job_holds_machine.cpp`. Test: `tests/unit/test_widget_size_toolhead.cpp`
(extend).

**Interfaces:** Consumes Tasks 1-8 (`current_bed_plate`, `current_plate_lift`,
`plate_moves_allowed`, `BedPlateGesture`, `ToolheadMoveSender`,
`plan_bed_plate_layers`/`draw_bed_plate_layers`, `mark_drag_surface`, `BedOccupancyState`).

**Behavior:**
- **Plate mapping.** The plate maps `current_bed_plate()->area`, which is also what the Bed
  tab maps. Build the plate input's `BedDimensions` from that area. The plate stays drawn by
  `plan_toolhead_plate`; grid and outlines go through `draw_bed_plate_layers` with the same
  mapper over the plate rect. The grid follows `kMinGridSpacingPx`, so it only appears when
  the plate is large enough.
- **Plate object.** `toolhead_plate` becomes an `lv_obj` (not a `pass_through`) with
  `moves_machine="true"` and event_cbs: `pressed`, `pressing`, `released`, `press_lost`,
  `clicked` (`toolhead_plate_*_cb`). `attach()` calls `mark_drag_surface(plate_)`.
- **Interactive state.** The plate is interactive when the rung is at least Wide,
  `plate_moves_allowed()` is true, and nothing is printing.
  - Interactive: the plate's `clicked` stops bubbling, so the root does not open Motion.
  - Inert: the press handlers do nothing and `clicked` bubbles to the root, which opens
    Motion as it does today.
  - The readout row sits outside the plate object. The Z bar is drawn on the same object
    (`plan_toolhead_plate` reserves it beside the plate), so a press whose point falls in
    `geometry.z_bar` or outside `geometry.plate` starts no gesture, and its `clicked`
    bubbles to the root, which opens Motion. Test: a tap on the Z bar of a homed, idle
    plate opens Motion and sends nothing.
- **Tap.** Sets the ghost target and publishes:
  - `toolhead_ghost_active = 1`;
  - `toolhead_ghost_text`: `"-> X{} Y{}"`, plus `", lift to {}mm"` when the lift is Lift, or
    the Refuse text.
  - The draw hook draws a ring at the ghost and a dashed line from the head to it.
  - A 5000 ms timer clears the ghost. Use `ui::CoalescedTimer` or an `lv_timer_t` cancelled
    in the destructor and in `detach()`.
- **Move here / dismiss.**
  - `toolhead_move_here` (`moves_machine="true"`) calls `gesture.send_once(ghost)` and
    clears the ghost.
  - `toolhead_ghost_dismiss` clears it.
  - The ghost also clears when the plate turns inert, and in `detach()`.
- **Drag.** Live, through the gesture and `ToolheadMoveSender`, exactly as on the Bed tab.
  While dragging, the readout shows the target and the lift.
- **Cleared chip.** `toolhead_bed_cleared` is visible when `bed_occupied eq 1 and
  toolhead_ghost_active eq 0` and calls `mark_cleared()`.
- **Redraw and census.** The plate is invalidated on `bed_occupancy_version`. The
  `kGuardedFiles` row for `panel_widget_toolhead.xml` becomes 3 (Home All, plate, Move here).

- [ ] **Step 1: Tests** `[widget_size][toolhead][interactive]` (existing toolhead widget
  fixture, 2x2):
  - Homed and idle, tap at the plate centre: `toolhead_ghost_active == 1`, no gcode, Motion
    not opened.
  - Move here: one `move_to` whose Z line (5 at Z 0.2) comes before the XY line, and the
    ghost clears.
  - Dismiss: the ghost clears and nothing is sent.
  - Advancing the tick 5001 ms clears the ghost.
  - Unhomed, tap on the plate: no ghost, no gcode, and the Motion overlay is on the nav
    stack.
  - Printing, tap: the same as unhomed.
  - Homed drag of 30px: at least one target sent and no ghost.
  - `bed_occupied = 1`: the Cleared chip is visible, and clicking it sets `bed_occupied` to 0.
  - `detach()` with a ghost active, then the tick advanced: no crash and no callback.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[toolhead],[widget_size][toolhead],[job_holds_machine]'` PASS.
- [ ] **Step 5:** Commit `feat(home): interactive Toolhead plate (tap to aim, drag to move)`.

### Task 10: 2x3 preset slots and their picker

**Files:**
- Create:
  - `src/ui/panel_widgets/toolhead_preset_slots.cpp`;
  - `src/ui/home_macro_run.cpp`;
  - `src/ui/modals/toolhead_preset_config_modal.{h,cpp}` and
    `ui_xml/toolhead_preset_config_modal.xml`.

  Add all of them to `app_srcs.txt`.
- Modify: `toolhead_widget.{h,cpp}`, `panel_widget_toolhead.xml`,
  `src/ui/widgets/favorite_macro_widget.cpp` (`fetch_and_execute` becomes a call to
  `run_home_macro`), the translation yaml, and `test_job_holds_machine.cpp`.
- Tests: `tests/unit/test_toolhead_preset_slots.cpp`, extend
  `test_widget_size_toolhead.cpp` and `test_toolhead_layout.cpp`.

**Behavior:**
- **Layout.**
  - `presets_extra_h` is measured like the other rungs: the slot button height (two lines
    of `font_small` text plus `space_xs` padding), plus `space_xs` gap, plus one
    `font_small` line for the status line.
  - At Full with room, the plate grows to the cell width and the row shows. That gives a
    2x3 (233x348) the row and keeps a 2x2 (233x231) without it.
  - Check both sizes with `ctl home resize toolhead 2 3` / `2 2` and `ctl geom`.
- **Slots.** XML `toolhead_preset_0..2`, each an icon plus a label bound to
  `toolhead_preset_N_icon`/`_label`, with `moves_machine="true"`. The status line binds
  `toolhead_lift_text`.
- **Execution.**
  - **Position:** `motion_preset_target` then `send_once`.
  - **Park:** the ParkToolhead standard macro when configured, as `MotionPanel::handle_park`
    does. Otherwise a `plate_rear_park` target through `send_once`, with Park's lift rule
    from Task 7. Share that rule as one free function in `bed_plate_context`; do not copy it.
  - **ZUp:** Z target at current + 10, capped at z_max.
  - **ZDown:** Z target `clamp_z_down_to_floor(current - 10, ...)`, not below z_min.
  - **Macro:** `run_home_macro({name, true, parent_screen_, "ToolheadWidget"})`.
  - Every slot except Macro is inert while `plate_moves_allowed()` is false.
- **Picker.** `has_edit_configure()` returns true. `on_edit_configure()` opens
  `ToolheadPresetConfigModal`:
  - three slot rows; tapping one lists the 9 positions, Park, Z +10, Z -10, Macro, and
    Empty;
  - Macro opens the macro list (`api->hardware().macros()`, sorted, without `_` names, as
    `FavoriteMacroConfigModal::populate_macro_list` does; reuse it if it can be shared);
  - it saves with `save_widget_config(toolhead_preset_slots_to_config(...))`.

- [ ] **Step 1: Tests**
  - `[toolhead][presets]`:
    - the defaults are Front, Center, Park;
    - a JSON round trip keeps all kinds;
    - a missing array gives the defaults;
    - an entry with an unknown kind gives Empty;
    - an unknown position token gives Empty;
    - `motion_preset_token` and `motion_preset_from_token` round-trip all 9.
  - `[toolhead][layout]`:
    - with `presets_extra_h = 60`, a box of `needs.full.h + 60` gives `show_presets`;
    - `+ 59` does not;
    - Wide never does.
  - `[widget_size][toolhead][presets]`:
    - at 2x3 the row shows and at 2x2 it does not;
    - homed with no part, Z 0.2, slot Front: the move's Z is 5;
    - ZDown at Z 20 with a part at 38 sends Z 43;
    - unhomed: the slot buttons are disabled and send nothing.
  - `[macro]`: the existing favorite-macro tests pass unchanged.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:**
  `make t F='[toolhead],[widget_size][toolhead],[macro],[job_holds_machine]'` PASS.
- [ ] **Step 5:** Commit `feat(home): configurable preset slots on the tall Toolhead widget`.

### Task 11: Documentation

**Files:** every row of the design doc's "Documentation" tables: the user guide
(`guide/motion.md`, `guide/home-panel.md`, `guide/settings/printing.md`,
`TROUBLESHOOTING.md`, and `USER_GUIDE.md` where it mentions these screens), and the
developer docs (`PANEL_WIDGET_GUIDE.md`, `HOME_EDIT_MODE.md`, `LVGL9_XML_GUIDE.md` only if
an XML attribute was added, `architecture/09-home-widgets.md`, the Motion chapter in
`architecture/08-panels-navigation.md`, `architecture/05-printer-state.md` for
`BedOccupancyState`, and `CHANGELOG_1_1_DRAFT.md` or whichever release draft main fills).

- [ ] **Step 1:** Update each section the design doc names to describe the behavior that
  shipped, not the plan. Write user docs in the end-user voice (`docs/user/CLAUDE.md`).
  Developer docs cite places as `path#symbol`, never line numbers.
- [ ] **Step 2:** `make check-doc-anchors` (advisory). Fix any anchor this branch broke.
- [ ] **Step 3:** Commit `docs: interactive Toolhead plate, bed occupancy lift, no auto-home in Motion`.

### Finish (orchestrator only)

1. One review of the whole branch, with a fresh Opus reader using
   `docs/devel/REVIEW_RUBRIC.md`.
2. `make mutate-diff MUTATE_ARGS="--tests '[motion],[bed_occupancy],[history],[bed_plate_draw],[move_sender],[jog_coalescer],[bed_plate_gesture],[home],[fan],[toolhead],[widget_size][toolhead],[macro],[job_holds_machine]'"`.
3. Screenshots, headless: the widget at 2x2 (idle, ghost, unhomed), the widget at 2x3, the
   Bed tab with outlines and the Cleared chip, the Move tab unhomed, and the preset picker.
4. Regenerate any user-guide images the docs embed.
5. One `make land-gate`, then land per the shared-tree rules, and delete both plan files in
   the landing change.
