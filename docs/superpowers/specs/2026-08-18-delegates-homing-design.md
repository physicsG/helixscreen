# Delegates Homing to Printer — Design

Date: 2026-08-18
Status: Approved (brainstorm session, 2026-08-18)
Issue: prestonbrown/helixscreen#1265 (Disable homing check for AFC commands)

## Problem

#1265: loading a lane from an idle printer prompts "home printer first?"
even when AFC's own `auto_home` (AFC.cfg `[AFC]` section, default false)
already homes-if-needed inside the macro. For those users the prompt is
an unnecessary click, and the G28 HelixScreen synthesizes after a
confirm is redundant work on printers where homing is slow.

Three sites ask the question when the toolhead is unhomed:

1. `src/ui/ui_ams_sidebar.cpp` — AMS sidebar, before preheat (~line 1417)
2. `src/ui/ui_panel_filament.cpp` — filament panel, before preheat (~line 1259)
3. `src/printer/ams_subscription_backend.cpp` — `ensure_homed_then()`,
   the dispatch-time prompt + synthesized G28 (~line 466)

The sidebar/panel prompts arm `arm_home_preconfirmed()` so the backend
does not ask twice; the first prompt is the click that costs the user.

## Decisions (settled in brainstorm)

| Question | Decision |
|----------|----------|
| Behavior when `auto_home: true` and unhomed | Skip BOTH the prompt and our G28 (CFS Fork `skip_homing` precedent). AFC homes exactly when it needs to; a synthesized G28 wastes 10-30s on deltas/Vorons |
| Naming | `delegates_homing_to_printer()` — printer-side vocabulary (companion rename already shipped: `printer_reports_spool_ids`, `printer_retains_spool_info`) |
| Which sites consult it | All three. Filament panel's no-backend path (plain single-extruder printers) keeps today's prompt — no backend, no capability, no change |
| Config not yet loaded | False-until-loaded: the capability answers false until `afc_config_` has landed, so we may ask one redundant question but never skip a needed home |
| Relationship to `filament_ops_self_home()` | None — that virtual gates PAUSED-print refusal semantics and stays untouched. Cross-reference both doc comments so nobody "unifies" them |
| Wiring approach | A: virtual on `AmsBackend`, consulted at each site. No subjects, no policy structs |

Rejected alternatives: B (subject published by backend, UI learns via
managed subject — extra plumbing, nothing to display), C (generalize
`skip_homing` into a per-backend policy object — over-engineered for one
boolean; CFS Fork's per-call variant selection is genuinely call-site
specific).

## Architecture

### 1. Capability virtual

On `AmsBackend`, beside `filament_ops_self_home()`:

```cpp
/// Does the printer-side system arrange its own homing for filament
/// load/unload ops, so HelixScreen should neither prompt nor send G28?
/// AFC answers true when [AFC] auto_home is set in AFC.cfg. Default
/// false; "not sure" must stay false (a redundant prompt is an extra
/// click, a skipped home can crash the toolhead into the bed).
[[nodiscard]] virtual bool delegates_homing_to_printer() const {
    return false;
}
```

`AmsBackendAfc` overrides:

```cpp
[[nodiscard]] bool delegates_homing_to_printer() const override {
    return afc_config_ && afc_config_->is_loaded() &&
           afc_config_->parser().get_bool("AFC", "auto_home", false);
}
```

Reads under no lock (config manager owns its own), mirroring
`update_tip_method_from_config()`. Only AFC overrides; Happy Hare's own
config, if it grows an equivalent, is a future override — vendor
knowledge stays in the AFC module per the vendor-abstraction rule.

### 2. Consumption sites

- **`ensure_homed_then()`**: first guard becomes
  `if (skip_homing || delegates_homing_to_printer() || toolhead_homed())`
  — dispatch immediately, no prompt, no G28. CFS Fork's per-call
  `skip_homing` is untouched. The `home_preconfirmed_` consume sits after
  this guard (delegating short-circuits first; an armed flag survives
  for a later non-delegating dispatch).
- **Sidebar + filament panel pre-prompt guards**:
  `if (!toolhead_is_homed(...) && !backend->delegates_homing_to_printer())`
  → prompt; else fall straight into preheat without arming
  `arm_home_preconfirmed()`.
- **Filament panel no-backend path**: `backend == nullptr` → capability
  unchecked, prompt exactly as today.

## Edge cases

| Case | Behavior |
|------|----------|
| Config not yet loaded (first ~1-2s after connect) | Capability false → prompt as today; one redundant click possible, never a skipped home |
| `AFC.cfg` fetch fails / absent | `is_loaded()` false → same; no special handling |
| `auto_home` toggled mid-session | Answered live per call, no caching. HelixScreen's own AFC config editor writes through `afc_config_`, so an in-app edit takes effect on the next op |
| Paused print | Unchanged — `filament_ops_self_home()`'s jurisdiction |
| Happy Hare / CFS stock / IFS / ACE | Base default false; no overrides in this change |
| Mock backend | False — the prompt path stays visible/testable under `--test` |
| `home_preconfirmed_` armed | Delegating check precedes the consume; armed flag persists for later dispatches |

## Testing

- **AFC backend unit** (`tests/unit/test_afc_delegates_homing.cpp`),
  harness follows the existing AFC config-test seam
  (`test_afc_config_manager.cpp`): config loaded with `auto_home: true` →
  true; not loaded → false; key absent/false → false.
- **`ensure_homed_then` dispatch matrix**: unhomed + delegating → payload
  dispatched, no `G28` captured, no prompt; unhomed + not delegating →
  prompt (already pinned by existing tests); homed + anything →
  unchanged.
- **UI sites**: the guard predicate is 2-3 lines duplicated; test each
  site's prompt-skip with a delegating backend stub if the existing
  sidebar/panel harness allows, otherwise pin the predicate shape.
- **Mutation**: override returning `true` unconditionally → dispatch +
  UI tests red.

## Docs

- `docs/devel/FILAMENT_MANAGEMENT.md`: AFC section note — `auto_home:
  true` suppresses HelixScreen's homing prompt.
- `docs/user/guide/filament.md`: one sentence in the AFC load flow: "If
  AFC's `auto_home` is enabled in AFC.cfg, HelixScreen skips its
  home-first prompt."
- Reply on #1265 when complete.

## Out of scope

- Happy Hare auto-home equivalents.
- Any setting inside HelixScreen to force the old behavior (the user
  already owns this via AFC.cfg).
- Touching paused-print refusal or the G28-during-print rejection layers.
