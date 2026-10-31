# Interactive toolhead plate: design

Status: design, approved in conversation 2026-10-27, not yet planned.

## Why

A QIDI Q2 user (Felix14_v2, Discord) asked for the home Toolhead widget to be interactive
"like the Bed motion page", with every move made only after lifting to Z >= 5, because a bed
can carry a large tilt. His video shows what happens today: he presses and drags on the
widget's plate, and each attempt either enters home edit mode (a hold) or swipes the home page
(a drag). The widget only opens the Motion panel on tap.

Investigating it surfaced two safety gaps in what already ships:

- The Motion panel's Bed tab homes on a stray tap. With the printer unhomed, one tap on the
  plate sends G28 and then moves the head (`MotionPanel::commit_bed_target` ->
  `ensure_homed_then`). Verified on the mock: `[ensure_homed_then] Not fully homed, sending G28`
  immediately after a tap.
- No move from the motion UI knows that a finished print may still be on the bed. The Bed tab
  lifts to `motion/bed_map_clearance` (default 5mm) only; Move-tab presets never command Z.

Success: a user can move the head from the home screen as easily as from the Bed tab, nothing
moves unless the user deliberately asked for motion on a homed machine, and no plate move can
drag the nozzle through a print the user has not removed.

## Goals

1. One shared plate (rendering + decisions) used by the home Toolhead widget and the Motion
   panel's Bed tab.
2. The widget's plate is interactive at the rungs that draw a plate (Wide and up).
3. Plate, presets and Bed tab are inert until the printer is homed, and only an explicit Home
   action homes.
4. Moves lift above any print that may still be on the bed.
5. The tall 2x3 widget uses its height: a bigger plate and a row of configurable preset slots.

## Non-goals

- Bed mesh tint on the plate (considered, not wanted this pass).
- Changing the Jog tab or jog arrows. Jog is small deliberate steps; no lift rule there.
- Interactivity at the Glance/Strip/Column/Cell rungs (no plate drawn there).
- Any change to Felix's QIDI load wizard work.

## Anti-goals (failure even if it "works")

- Motion caused by brushing or cleaning the screen, by a tap meant to open something, or by a
  swipe meant to change pages.
- A G28 the user did not explicitly ask for.
- A plate move that travels low across a part still on the bed.
- Two copies of the plate's rules (widget and Bed tab drifting apart).
- Losing the ability to enter edit mode on a home page full of toolhead widgets.

## Decisions (made by Preston)

### Gestures on the widget plate

- **Tap**: drops a ghost target on the plate and shows a **Move here** chip plus a dismiss
  (x). Nothing moves until the chip is tapped. The readout line shows the target and the lift
  ("-> X60 Y290, lift to 43mm").
- **Drag**: moves the head live, exactly like the Bed tab (the head follows the finger,
  coalesced latest-wins, one move in flight). The drag must start on the plate.
- A drag that starts on the plate must not swipe the home page, and a hold on the plate must
  not enter edit mode. The plate is a drag-consuming surface the way the fan arcs are.
- In edit mode the plate is not interactive (edit mode's existing shield).
- During a print the plate is read-only: it shows the live head over the current print's
  objects.

- **Opening the Motion panel**: a tap on the readout row or the Z bar opens it, and so does a
  tap on the plate while the plate is inert (unhomed or printing), as today. On a homed, idle
  machine, a tap on the plate drops the target instead.

### Unhomed: nothing in the motion UI homes except a Home button

- Plate (widget and Bed tab) is dimmed and inert while any axis is unhomed; the existing Home
  button (widget Home All, Bed tab centre Home) is the only way to home from there.
- The rule covers the whole Motion panel. The Bed tab tap, Move-tab presets, Park and
  coordinate entry each call `ensure_homed_then` / `ensure_xy_homed_then` today
  (`src/ui/ui_panel_motion.cpp`). They become dimmed and inert while unhomed instead.
- Out of scope: the calibration panels (bed mesh, screws tilt) and filament load, which home
  as part of their job.

### Lift rule ("lift floor")

- Before any XY move from the plate (tap-confirmed or drag) or a position preset, Z rises to
  the lift floor if below it: `max(clearance, last_print_height + clearance)` while a print may
  be on the bed, else `clearance` (clearance = `motion/bed_map_clearance`). Never above z_max.
- Applies to: widget plate, Bed tab, widget preset slots, **Move-tab presets** (which today
  never command Z).
- Does not apply to: jog arrows, macro slots (a macro owns its own motion).
- The readout says why when the print raises it: "Z 0.20, will lift to 43mm: last print is
  38mm tall".
- Z down (the Z -10 preset slot) does not descend below the lift floor while a print may be on
  the bed.

### "A print may be on the bed"

- Becomes true when a print ends (completed, and also cancelled or errored, since those leave a
  partial part; the builder picks the height used for a partial, conservatively).
- Cleared by: the next print starting, or the user tapping a **Cleared** chip shown on the
  plate while the state is true. **Homing does not clear it** (decided after discussion: a
  successful home only proves the Z homing point was clear, and many printers home Z without
  touching the plate at all; a wrong "cleared" crashes the nozzle, a wrong "still there" costs
  one 3-4s lift).
- Must survive an app restart (a restart must not forget a part on the bed).

### Plate contents

- The 50mm reference grid (shared, `include/bed_grid.h`, already "every bed view draws").
- Object outlines of the current or last print (dashed, translucent), on the widget AND the Bed
  tab, drawn by shared code. Outlines vanish with the Cleared chip / next print.
- No mesh tint.

### Sizes

- 2x2 (233x231 at 800x480): plate + Z bar + readout, as today, plus grid and outlines. The
  Move-here chip overlays the bottom of the plate.
- 2x3 (233x348): plate grows to the cell width (~206px square at 800x480), then a row of three
  preset slots with two-line labels (icon + name) and a status line (lift reason), then the
  readout. Mockup "D1".
- Full rung otherwise as today (Home All while unhomed, status row while printing).

### Preset slots (2x3 and up)

- Configured from the widget's edit-mode gear (`has_edit_configure()`), up to 3 ordered slots.
- Choices: the Move tab's 9 positions (`MotionPreset`), Park (`plate_rear_park` / the
  ParkToolhead standard macro, as the Move tab's Park does), Z +10, Z -10, and **a macro**
  (picked the way the Macros home widget picks one).
- Default: Front / Center / Park.
- Position slots and Park obey the lift floor and need every axis homed (inert otherwise).

## Calls made while writing this up (Claude's, flagged for Preston to check)

- **Source of "a print may be on the bed"**: derived on every connect from Moonraker, not only
  from end-of-print transitions the app happened to watch: the newest job in print history
  (`completed`, `cancelled`, `error`), its file's `object_height` metadata, and a persisted
  per-printer "cleared" marker keyed by that job's id. A job that ends while the app is off,
  or one started from Mainsail or a slicer, is still covered. No history (fresh install,
  history disabled) means no part is assumed. A job without `object_height` keeps the state
  true (outlines and Cleared chip shown), but the floor stays at `clearance`, because there is
  no height to add.
- **Per printer**: the state and the cleared marker are per printer under multi-printer.
- **One shared state**: the widget and the Bed tab read the same state, and both show the
  Cleared chip while it is true. While a Move-here ghost is showing, the chip is hidden, so the
  two never stack.
- **Floor above z_max**: lift to z_max. If z_max is still less than 1mm above the part's
  height, the move is refused with a notice that says to remove the part and tap Cleared.
- **Sequencing**: Z then XY ride in one target, as `commit_bed_target` already sends them
  (`move_to` emits the Z line before the XY travel), so drag and tap-confirm share one send
  path. The first drag sample carries the lift and later samples carry the same height
  (existing `bed_gesture_lift_z_`).
- **Homed predicate**: `toolhead_is_homed` (all three axes), the same predicate the Bed tab's
  lift already requires. "Inert" means dimmed and not clickable, with the Home button showing.
- **Paused**: the plate is read-only while paused, because the existing gate
  `machine_motion_blocked` (`job_holds_machine` or the spool latch) covers paused. The widget
  uses the same gate as the Bed tab; nothing new.

## Constraints

- Long-press is owned by home edit mode; the drag-consuming check in
  `src/ui/ui_panel_home.cpp#should_suppress_edit_mode` currently names arcs and sliders by
  class. Generalise it to a capability ("does the pressed widget consume drags?") rather than
  adding a third class name.
- Declarative UI rules (`.claude/rules/declarative-ui.md`): appearance in XML, draw hooks and
  measured layout are the C++ exceptions. Threading rules for the move send path.
- Measured-layout pattern for the widget (`docs/devel/PANEL_WIDGET_GUIDE.md`): the rung
  decision stays a pure function (`decide_toolhead_layout`), geometry stays pure
  (`plan_toolhead_plate`).
- No vendor names in generic code (`.claude/rules/vendor-abstraction.md`).
- `machine_motion_blocked` / `is_external_blocking_operation_active()` gate every move as the
  Bed tab does today.
- New strings go through the translation system; US English.
- 1.1.0 freeze is 2026-10-28; this is post-freeze work for main.

## Existing pieces to reuse (searched)

| Need | Exists |
|---|---|
| Pixel <-> bed mapping, target from a touch | `bed_map_mapper`, `bed_map_target` (`src/motion_presets.cpp`), `BedCoordMapper` |
| Lift decision | `bed_map_lift_z` (extend for the print height, do not fork) |
| Drag streaming, latest-wins | `MotionPanel::handle_bed_touch`, `send_bed_gesture_target`, `commit_bed_target`, `dispatch_target` |
| Grid lines | `bed_grid_lines`, `bed_outline_segments` (`include/bed_grid.h`) |
| Object outlines | `PrinterExcludedObjectsState::ObjectInfo` (polygon/bbox), drawn today by `ExcludeObjectMapView` |
| Preset positions | `motion_preset_target`, `preset_area`, `plate_rear_park` (`include/motion_presets.h`), `MotionPanel::handle_preset/handle_park` |
| Widget plate geometry | `plan_toolhead_plate` (`src/ui/panel_widgets/toolhead_plate.h`) |
| Edit-mode configure | `PanelWidget::has_edit_configure/on_edit_configure` (fan, LED, camera widgets) |
| Macro picking | the Macros home widget's picker (`docs/devel/MACROS_PANEL.md`) |

## Documentation (part of the work, not a follow-up)

Each item ships in the same change as the behavior it describes. The plan gives each one its own
task, and the batch's final review checks every doc against the shipped behavior. These
documents currently describe the old behavior.

**User guide** (`docs/user/`, end-user voice, no source references, `docs/user/CLAUDE.md`):

| File | Section | What changes |
|---|---|---|
| `guide/motion.md` | Position Display | Tapping a coordinate no longer homes first. Unhomed, the coordinates are dimmed and you home with a Home button |
| `guide/motion.md` | Move Tab | Presets lift to the lift floor before travelling (the "Z is never changed" sentence goes). They are dimmed while unhomed and no longer home first |
| `guide/motion.md` | Park | Same unhomed rule |
| `guide/motion.md` | Bed Tab | Unhomed: "Tapping the plate instead homes first and then moves" goes, and the plate is inert. New: object outlines, the print-on-bed lift and its readout text, the Cleared chip and when the state clears by itself (next print), the too-tall refusal |
| `guide/motion.md` | Motion Settings, Bed Map Clearance row | The clearance now also applies to Move-tab presets and the home widget, and a print on the bed adds its height |
| `guide/home-panel.md` | Available Widgets, Toolhead row | Interactive plate at 2x2 and up: tap then Move here, drag to move live, readout row and Z bar open Motion, grid and outlines, 2x3 preset row, inert while unhomed or printing |
| `guide/home-panel.md` | Configuring a Widget table | New Toolhead row: the preset slot picker (positions, Park, Z +10/-10, a macro; default Front/Center/Park) |
| `guide/home-panel.md` | Entering Edit Mode | A long press on the Toolhead plate (like the Fan arcs) does not enter edit mode. Long-press anywhere else instead |
| `guide/settings/printing.md` | Motion line | Wording follows the Bed Map Clearance change, if the name or scope changes |
| `USER_GUIDE.md`, `TROUBLESHOOTING.md` | any Bed tab / toolhead mentions | Check and correct; add a troubleshooting entry: "the plate won't respond" (unhomed, printing, paused, or edit mode) |

Screenshots: if the guide pages embed images of the Toolhead widget or the Bed tab
(`docs/images/user/`), regenerate them with `scripts/screenshot.sh` after the UI lands.

**Developer docs:**

| File | What changes |
|---|---|
| `PANEL_WIDGET_GUIDE.md` | Toolhead row in the sized-widget table (interactive plate, preset slots, edit-configure). The toolhead-safety census row for `panel_widget_toolhead.xml` gets the new guard count (plate, Move here, preset slots) |
| `HOME_EDIT_MODE.md` | "Entering": the suppression check names a drag-consuming capability, not arcs and sliders by class. Say how a widget opts in |
| `LVGL9_XML_GUIDE.md` | The opt-in attribute or flag for a drag-consuming surface, if it is exposed in XML |
| `architecture/09-home-widgets.md` | Toolhead entry: shared plate with the Bed tab, configurable slots |
| `architecture/08-panels-navigation.md` or the Motion section that covers the Bed tab | Shared plate component, lift-floor function, the "never auto-home in the motion UI" rule |
| `architecture/05-printer-state.md` | Where the print-on-bed state lives (history-derived, per printer), if it becomes printer state |
| `CONFIG_MIGRATION.md` / config notes | Only if new persisted settings need a migration (preset slots, cleared-job marker) |
| `CHANGELOG_1_1_DRAFT.md` (or whichever release draft main is filling when this lands) | User-facing entries: interactive Toolhead widget, the print-on-bed lift, the no-auto-home change in Motion |

When the work ships, this plan file is deleted (`docs/CLAUDE.md`), and any lasting design
reasoning moves into the developer docs above.

## Left to the builder

- Where the shared plate lives (component vs. helper + draw hook) and how the widget and Bed
  tab each host it, as long as both use one implementation of drawing and of the decisions.
- How "a print may be on the bed" and its height are sourced and persisted (print history,
  file metadata `object_height`, exclude-object geometry), and the height used for a cancelled
  partial (conservative).
- Exact chip timeout (proposed: 5s with no input dismisses the ghost), drag threshold (reuse
  the Bed tab's `kBedMapDragThresholdPx`), chip placement details.
- Icons for preset slots, picker sheet layout.
- Smallest breakpoint behaviour: measure with `ctl geom` and keep the rung rules.

## Mockups

Session scratchpad (not tracked): `widget/mock/m1.png` (states A-E), `widget/mock/m2.png`
(2x3 D1/D2 and picker sketch).
