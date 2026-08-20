# Home Bypass Toggle Widget — Design

Date: 2026-08-18
Status: approved design, pre-implementation

## Problem

Bypass is now a first-class capability across four backends (CFS, AD5X IFS,
AFC, Happy Hare) with companions shipped in this cycle: runout arming, the
external-spool `lane_data` publish, and the print-start gate behavior. But
engaging it still requires navigating Settings → Hardware & Devices →
Multi-Filament System Management (or the AMS panel's sidebar) — three-plus
taps away from the home screen where the decision is usually made.

## Goal

A 1×1 home-grid tile that shows bypass state and toggles it in one tap, with
the same guards as the existing surfaces.

## Design

### Registry & gating

New `bypass` row in `PanelWidgetRegistry` (`src/ui/panel_widget_registry.cpp`):
title "Bypass", icon `source_branch`, 1×1 default / 1×1 min / 2×1 max,
`hardware_gate_subject = "ams_supports_bypass"`, gate hint "Requires a
filament system with bypass". The existing gate machinery hides the tile on
ACE / Snapmaker / QIDI / tool changers and drives the edit-mode catalog;
`default_layout.json` gets no anchor (auto-placed).

### Tile visuals (`ui_xml/components/panel_widget_bypass.xml`)

All declarative; no C++ painting:

- **Icon pair, state-swapped** — two `<icon>` elements in the same slot, each
  with `bind_flag_if_eq subject="ams_bypass_active"` (ref 0 / 1):
  - OFF: `arrow_left_right`, `variant="muted"` — the Device-Operations
    bypass-row glyph (the *control* surface)
  - ON: `source_branch`, `variant="success"` — the filament-path bypass-node
    glyph (the *active path* surface)
- **Title** "Bypass" via `text_small`/`text_tiny` per other tiles, plus the
  standard `show_widget_labels` binding.
- **Active-state detail**: external spool color dot + material name, bound to
  the existing global subjects (`ams_external_spool_color` and the external
  spool material subject) — shown only while `ams_bypass_active == 1`.
- **Disabled-while-printing look**: the whole tile dims while a job is active
  — bound to the existing `print_state_enum` subject (PrintJobState:
  0=standby, 1=printing, 2=paused; the tile disables on 1 or 2 via an inline
  `cond="print_state_enum eq 1 or print_state_enum eq 2"` word-form
  expression, per declarative rule 7), the same subject the runout guidance
  modal binds.
- Tap target: the whole tile, `<event_cb trigger="clicked"
  callback="bypass_widget_clicked_cb"/>`.

### Shared toggle logic — `BypassToggleController`

Extracted verbatim from `AmsOperationSidebar::handle_bypass_toggle` plus its
unload-completion observer arm (the `pending_bypass_enable_` state machine,
including the ERROR-disarm fix) into
`include/ui_bypass_toggle_controller.h` + `src/ui/ui_bypass_toggle_controller.cpp`.
The controller owns **widgets-free** logic and toasts only:

1. **Print guard (new, enforced here):** if `get_printer_state()`'s
   `print_state_enum` reports a job active (printing or paused), refuse with
   a toast ("Bypass cannot be changed while printing") — fully disabled, per
   product decision. The controller is the enforcement point so the sidebar
   gains the same guard; the tile merely renders disabled.
2. No backend → warning toast (sidebar's existing message).
3. Hardware-sensor bypass → refusal toast (sensor owns the state).
4. Disable path: `backend->disable_bypass()`, "Bypass disabled" toast.
5. Enable path: `should_unload_before_bypass()` chaining discipline (#1229)
   — unload first with `pending_bypass_enable_` armed, enable on
   UNLOADING→IDLE, disarm on UNLOADING→ERROR. The observer arm lives in the
   controller (it observes the AMS action subject), so both surfaces share
   one state machine instead of two drifting copies.

`AmsOperationSidebar` refactors to own one; `BypassWidget` owns one per
instance. The sidebar's own logs/toasts are preserved — same strings, new
home.

### Widget class

`src/ui/panel_widgets/bypass_widget.{h,cpp}` in the `motion_widget.cpp`
shape (64 lines): `register_bypass_widget()` registers the factory id
`"bypass"` and the static click callback; `attach()` finds the tile button
and registers the click handler; `handle_click()` routes to the controller
after `record_interaction()`. Instances are recycled across rebuilds — the
controller observer uses the widget's `SubjectLifetime`, and `attach()`
re-runs anything stateful (per the PanelWidget recycling rule).

### Subject & backend contract

**No new subjects, no backend changes.** Consumed: `ams_supports_bypass`,
`ams_bypass_active`, external-spool color/material subjects, print-state
subject — all existing globals.

## Testing

- **Controller unit tests** (new `tests/unit/test_bypass_toggle_controller.cpp`):
  guard matrix — printing refuses; hardware sensor refuses; no backend warns;
  disable path; enable path with and without the unload chain; ERROR-disarm
  (the regression the sidebar fix already covers — must survive the
  extraction). Toast-layer seams minimal: assert on returned action / called
  backend mock, not on toasts.
- **Sidebar regression**: existing `[ams]` suites green after the refactor.
- **Tile render/gate**: existing home-widget test patterns (gate hides tile
  without bypass support; icon swap follows `ams_bypass_active`; disabled
  while printing) — XML-side where possible, per test conventions.

## Out of scope

Orca lane publish, runout arming, gate behavior (all shipped this cycle);
any settings UI for the widget beyond the standard edit-mode catalog entry.
