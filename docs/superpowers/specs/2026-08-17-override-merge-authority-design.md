# Override Merge Authority + Spool Retention Setting — Design

Date: 2026-08-17
Status: Approved (brainstorm session, 2026-08-17)
Issue: prestonbrown/helixscreen#1281 (Spoolman info doesn't clear on AFC lane)
Depends on: `fix/spool-commit-consolidation` merged to main (write-path half)

## Problem

#1281 reported stale Spoolman info on an AFC lane after eject. Investigation
split it into designed behavior plus two real gaps:

1. **Designed**: HelixScreen's override records deliberately survive an
   eject/insert cycle, so pulling a spool for maintenance and reloading it
   keeps the assignment ("reload-previous-spool-details", a higher-level
   architecture decision, backend-agnostic). `Clear Spool` in the context
   menu is the designed escape hatch. Confirmed by `94ea3705e`: "Retention
   across an eject now belongs to the override store (#1158)."
2. **Gap A (external re-bind shadowed)**: when another well-behaved writer
   (Mainsail, AFC macros) sets a *different* spool on the lane, our stale
   override still wins the merge and the user's explicit re-bind is
   invisible in HelixScreen (#1281 step 7).
3. **Gap B (no user control)**: some workflows want "start fresh on eject"
   instead of retention; there is no setting for it.

The write path (HelixScreen-initiated commits across the seven backing
stores) is already consolidated by `fix/spool-commit-consolidation`
(`AmsState::commit_slot_edit` / `commit_external_spool_edit`). This design
is the read path: how externally-reported firmware truth wins back a slot.

## DRY mandate

Seven backends each hand-roll `apply_overrides()` — the same if-chain
implementing spec §5 ("override wins field-by-field") seven times, drifting
apart (AFC grew catalog fields; others didn't). This design gives that
policy exactly **one** code implementation, and encodes the new cross-field
rules in the same place so every backend (and every future one) inherits
them for free. Backends keep only what is genuinely per-backend: parsing
and their §6 hardware-swap fingerprint baselines.

## Architecture

### 1. Shared merge function (read-path single authority)

New pure function in `include/filament_slot_override_store.h` /
`src/printer/filament_slot_override_store.cpp`:

```cpp
namespace helix::ams {

struct MergeOptions {
    bool keep_spool_info_on_eject = true;  // from SettingsManager
};

struct MergeResult {
    bool cleared_rebind = false;   // firmware re-bind won; caller must persist
    bool cleared_eject = false;    // eject signal won (setting OFF); persist
};

// Single implementation of filament_slots.md §5, plus the two new
// cross-field rules below. Pure: no IO, no locks, no subjects.
// `slot` carries the FIRMWARE-reported values on entry (pre-merge);
// on return it carries the merged values the UI should paint.
MergeResult merge_override(SlotInfo& slot, const FilamentSlotOverride& o,
                           const MergeOptions& options);

} // namespace helix::ams
```

Field-merge body = the existing spec §5 rule, written once: a non-empty /
non-zero / non-negative override field replaces the firmware value;
sentinels (empty string, id 0, weight -1, colour unset) fall through.

Each backend's `apply_overrides(SlotInfo&, int)` shrinks to:
lock → lookup in `overrides_` → call `merge_override` → if the result says
cleared, also drop the in-memory `overrides_` entry and issue
`override_store_->clear_async(slot_index)` (persistence stays in the
store — the merge function itself never does IO).

### 2. Cross-field rule 1 — external re-bind always clears

Inside `merge_override`, **before** the field merge:

- Firmware (pre-merge `slot.spoolman_id`) reports a **positive** id,
- AND the override holds a **positive** id,
- AND the two **differ**

→ the whole override record is dropped (spec §6 DELETE semantics: the new
spool's firmware/Spoolman truth paints the slot unshadowed). Return
`cleared_rebind = true`.

A **different positive id** is an explicit statement by another writer —
not a guess — so this rule fires unconditionally; no setting gates it.
Zero/null firmware id (eject) must **never** fire this rule. This is the
fix for #1281 step 7, applies generically to every backend that reports a
`spool_id` from firmware.

### 3. Cross-field rule 2 — eject signal, setting-gated

Inside `merge_override`, before the field merge:

- Firmware reports **no** spool id (`slot.spoolman_id <= 0`, includes
  eject's null/0),
- AND the override holds a positive id

→ consult `options.keep_spool_info_on_eject`:
- **ON (default)** — today's designed retention: keep the record, merge
  normally. Same-spool maintenance reload is unaffected either way (ids
  match, nothing fires).
- **OFF** — "start fresh": drop the whole record, return
  `cleared_eject = true`.

### 4. Setting: "Keep spool info on eject"

- Level: AMS-level (not per-backend), stored under the existing `ams/`
  config namespace in SettingsManager (`ams/keep_spool_info_on_eject`,
  default `true`), exposed as a managed subject the same way
  `ams/always_show_bypass_spool` is.
- UI: a toggle in the AMS Management overlay, beside "Always Show Bypass
  Spool". Label: "Keep Spool Info on Eject"; help text: "When a lane is
  emptied, remember its spool details so reloading the same spool needs no
  re-selection. Turn off to start fresh when a lane empties."
- Jurisdiction: the eject signal **only**. External re-bind always clears
  (rule 2); spec §6 hardware-swap fingerprints stay per-backend and
  untouched.

### 5. Boundaries

| Concern | Owner |
|---------|-------|
| Spec §5 field merge + rules 2/3 | `merge_override` (one implementation) |
| Persistence of clears | `FilamentSlotOverrideStore::clear_async` (unchanged API) |
| Parsing, §6 fingerprints/baselines | each backend (unchanged) |
| HelixScreen-initiated writes | `commit_slot_edit` / `commit_external_spool_edit` (worktree, unchanged) |
| Setting storage + subject | SettingsManager |
| AFC/HH override namespace | private `OVERRIDE_NAMESPACE` (#1158), unchanged |

### 6. Build order

1. Merge `main` into `fix/spool-commit-consolidation` (3-way; main +17 /
   branch +5, overlap in `ams_backend_afc`/`cfs`), resolve, `make test-run`.
2. Merge the branch into `main` (`--no-ff`).
3. Branch `fix/override-merge-authority` off fresh main.

## Error handling

- `merge_override` is pure — it cannot fail. Persistence failures in
  `clear_async` are logged (warn) and retried on next save, same as
  existing store behavior; a failed clear only means the record survives
  until the next status frame re-detects the condition.
- The setting read happens at merge-call time in the backend (under its
  lock); SettingsManager subjects are plain int reads, safe there.

## Testing

- **`merge_override` unit matrix**: {no override, override id == firmware
  id, different positive firmware id (re-bind), firmware id 0/null (eject)}
  × {setting ON, OFF} × sentinel fall-through fields. Assert merged slot
  fields AND `MergeResult`.
- **Per-backend regression**: each backend's test asserts
  `apply_overrides` delegates (one merge-behavior test each, not seven
  copies of the matrix).
- **#1281 replay integration test**: assign spool via HelixScreen → eject
  (spool_id null) → assert retention (ON) / fresh (OFF) → external re-bind
  to a different id → assert clear + firmware truth paints → Mainsail
  visibility restored.
- **Setting test**: subject default true, persistence round-trip.

## Docs

- `docs/specs/filament_slots.md` §5: document the re-bind rule as an
  amendment ("a firmware-reported different positive `spool_id` outranks
  the override; the whole record is dropped"). §6: add the setting note.
- `docs/devel/FILAMENT_SLOT_METADATA.md`: merge-policy section points at
  `merge_override` as the single implementation.
- `docs/devel/FILAMENT_MANAGEMENT.md`: retention subsection updated (now
  setting-gated for the eject signal; re-bind always clears).
- `docs/user/guide/filament.md` + `docs/user/CONFIGURATION.md`: new toggle
  documented; eject/reload behavior explained in user terms.
- #1281: reply explaining intended behavior + citing the setting and the
  re-bind fix, then close.

## Out of scope

- Per-backend retention settings (single AMS-level knob only).
- Touching §6 fingerprint detectors beyond calling the shared merge.
- Migrating or renaming the private override namespaces.
