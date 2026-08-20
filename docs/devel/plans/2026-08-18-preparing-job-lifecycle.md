# Preparing-job lifecycle: make `Preparing` reachable at commit

Status: plan, not yet implemented
Branch: `fix/preparing-job-lifecycle`
Field report: K2 Plus, 2026-08-18

## The bug, stated without naming a vendor

There is a window between *the user commits to printing file X* and *the printer
reports it is printing file X*. During that window the print status panel renders
printer-reported job state, which still describes the **previous** job.

The window exists on every printer: upload from USB, gcode rewrite, metadata scan,
filament remaps, AMS lane changes, heat soak, blocking pre-start macros. On most
printers it is a few hundred milliseconds and nobody notices. It is not a
Creality-specific defect; Creality is only where it got long enough to see.

## Field evidence

K2 Plus, helix clock (klippy runs 1h behind on this device):

| Time | Event |
|---|---|
| 01:09:30 | Previous job completes. `outcome=COMPLETE`, `layer 0/0`, `elapsed 1389s` |
| 12:51:38 | User presses Print. `PrintStartController` navigates to the status panel |
| 12:51:38 | `BED_MESH_CALIBRATE_START_PRINT ... BED_TEMP=105` dispatched as a blocking `printer.gcode.script` |
| 12:51:43 | Panel fires `Deferred G-code load` for the **previous** filename |
| 12:51:43-12:59:13 | `Request Tracker` reports the RPC pending, 300174ms -> 455581ms |
| 12:59:13 | Macro returns (`G29_TIME` 222.354s inside it) |
| 12:59:17 | `print_state transition: 3 -> 0`, `New print starting - clearing outcome`, `0 -> 1` |

For 7m39s Klipper reported `print_stats=complete` with the old filename while
`bed_target=105, bed_temp=105.3`. The panel was faithfully rendering the last thing
the printer said. Temperatures come from `heater_bed`/`extruder`, which carry no
lifecycle guard, which is why temps were live while job data was frozen.

HelixScreen missed no event: it reacted to the real transition within ~100ms.

## Root cause

`PrintState::Preparing` (`include/print_lifecycle_state.h:17-25`) is a fully built
state. `is_active()` includes it (`:142`), so progress/layer/duration updates flow
through it. It has elapsed/remaining tracking, five dedicated handling sites in
`ui_panel_print_status.cpp`, and existing tests.

It is entered by exactly one thing, `print_lifecycle_state.cpp:176-186`:

```cpp
bool PrintLifecycleState::on_start_phase_changed(int phase, PrintJobState current_job_state) {
    bool preparing = (phase != 0);
    if (preparing) { current_state_ = PrintState::Preparing; ... }
```

`print_start_phase` is raised only by `PrintStartCollector::start()`, whose only
caller is the `print_state_enum` observer at `moonraker_manager.cpp:735`.

**So `Preparing` is unreachable until the printer confirms the print.** The state
that exists to describe "we are getting ready to print" cannot be entered during the
period when we are getting ready to print.

## Design

`Preparing` becomes enterable at user commit rather than printer confirmation.
Everything downstream already works, unchanged, on every printer. No new state, no
printer-database flag, no vendor branch.

### The preparing job

While in `Preparing`, one record identifies the job being prepared:
`{filename, path, source}`. This promotes
`ActivePrintMediaManager::thumbnail_source_filename_` from a media-only override,
applied at Moonraker-confirm, to the panel-wide answer to "which job is this?"
during the window: preview, thumbnail, gcode viewer, filename label.

It is also the reconciliation key. When `print_stats` reports a filename we compare:

- match -> retire, hand off to `Printing`
- mismatch -> someone started a different print elsewhere; discard our claim rather
  than silently adopting theirs

Today nothing can notice that case. This makes the externally-started path strictly
safer than it is now, not merely unregressed.

### Reconciliation rules

Only a **PRINTING** report settles a preparing job. The previous job going
terminal while ours prepares is the entire scenario this exists for, so a
terminal report must leave the claim intact. A PRINTING report with no filename
yet also waits.

Matching is on the **bare name**, after `resolve_gcode_filename()`:

- the report may be path-qualified (`subdir/mine.gcode`) while the user committed
  a bare name
- a modification rewrite hands the printer a `.helix_temp/modified_*` file
  standing in for the file the user chose

Match -> `Confirmed`. Mismatch -> `Superseded`: something else started a different
print while ours was preparing, so our claim is dropped rather than silently
adopted. Nothing could distinguish those before, because no identity was recorded
to compare against.

Reconciliation is idempotent and runs from both the job-state and the filename
parse points, since either can arrive first in a status payload.

### API

```cpp
begin_preparing(PrintJobRef job);
retire_preparing(PreparingExit reason);
const PrintJobRef& preparing_job() const;

enum class PreparingExit { Confirmed, Superseded, Failed, Cancelled, TimedOut };
```

One entry point, one exit point. Today arming, `reset_for_new_print()`,
outcome-clearing, thumbnail-setting and navigation are five independent effects
firing at four different times from three files, which is why this failure mode was
reachable at all.

**There are two arming paths, and both stay.** An earlier draft of this plan had
`MoonrakerManager`'s observer stop arming and become pure reconciliation. That is a
regression: an externally started print (Mainsail / Fluidd / Orca) has no user
commit, so it would never enter `Preparing` at all. It does today.

- **Commit arming** - user presses Print. Unambiguous.
- **Printer-edge arming** - `standby -> printing` with no live preparing job.
  Ambiguous, and keeps every guard it has today (see #1042 below).

`begin_preparing()` is idempotent: whichever path fires first owns the window, the
other reconciles. That still dissolves the branch-ordering hazard at
`moonraker_manager.cpp:730-760`, where an already-active collector reaching
`printing` would skip the authoritative completion branch, and where the
`complete -> standby` hop would call `collector->stop()` on a phase we raised.

### Fast printers

A sub-second `Preparing` must not flash the overlay. That is a presentation
concern: withhold the overlay until the phase has persisted past a threshold. It
does not belong in the state machine and carries no vendor knowledge.

## Why not the alternatives

**Move the mesh inside the job** (inject after `START_PRINT` via the existing gcode
rewrite). Rejected: two files for one logical print breaks print statistics, and the
mitigation for that is a Moonraker plugin that is beta-tagged and that most users do
not have installed.

**Flag the slow pre-start option in `printer_database.json`.** Rejected: encodes
"the K2 is slow" as data. Fixes two Creality models and leaves the same hole open on
every other printer.

## The blocking invariant

`Preparing` today is **a sub-state of Moonraker `PRINTING`**, not a state that
precedes it. Two places hard-code that, and both must be re-scoped before commit
arming can work at all.

`src/printer/printer_print_state.cpp:1084`:

```cpp
if (phase != PrintStartPhase::IDLE && lv_subject_get_int(&print_active_) == 0 &&
    !is_new_print_start) { return; }
```

`is_new_print_start` is only the IDLE -> non-IDLE edge, so the *first* phase lands
and **every subsequent phase (HOMING, HEATING, BED_MESH) is silently dropped** while
`print_active == 0`. Separately, `:447-462` force-resets the phase to IDLE on the
`print_active -> 0` edge.

A naive "arm at commit" therefore freezes the overlay on phase 1 and then has it
slammed back to IDLE. This is not optional cleanup; it is the precondition.

## Two things the census found on the way

### A live bug, independent of this work

`src/application/moonraker_manager.cpp:689`:

```cpp
static bool s_is_initial_transition = true;
```

`init_print_start_collector()` re-runs on every printer switch
(`application.cpp:3599`). Line 684 reassigns `s_prev_print_state`; line 689 cannot,
because a function-local static initializes once per process. After the first print,
mid-print-join suppression is permanently off: switch to a printer already partway
through a job and a full "Preparing..." overlay is drawn over a running print.

`print_completion.cpp:373`, `print_start_navigation.cpp:91` and
`telemetry_manager.cpp:3162` all re-arm correctly at init. This one is the outlier.
Fixed first, on its own, with its own test.

### The commit hook exists, pointing backwards

`PrintStatusPanel::end_preparing()` has one caller:
`ui_print_start_controller.cpp:259`, inside the `start_print()` **success callback**.
That fires when the RPC succeeds, which is when `PRINT_START` begins running. So the
app calls `end_preparing(true)` at the instant preparation *starts*, forcing the
lifecycle into `Printing` ahead of Moonraker, consuming the
`should_reset_progress_bar` edge early, and zeroing `preparing_visible` - after which
the phase observer puts it back into `Preparing`.

It is deleted and replaced by "enter Preparing".

## One owner for "how long until printing starts?"

Three estimators answer this question today and none of them knows about the others:

| Estimator | Available when | Knows |
|---|---|---|
| `PreprintPredictor::predicted_total_from_config()` (history median, 60s cached) | Browsing files | Nothing about this job |
| `PrintPreparationManager::recalculate_estimate()` (thermal model + homing + per-option phases) | Options picked, not started | Target temps, chosen options |
| `PrintStartCollector::update_eta_display()` (live composite, mesh-probe extrapolation) | Preparation running | What is actually happening |

They are not competing answers. They are a **fidelity ladder**, each the best answer
available at its moment. The defect is that they are three separate subjects, split
across two owners - `preprint_estimate_subject_` on `PrintPreparationManager` (bound
in `print_file_detail.xml`), `preprint_remaining` / `print_start_time_left` on
`PrinterState` (bound in the status panel, portrait, and the home widget) - which the
user compares across one flow and finds inconsistent.

The preparing-window owner owns the question and publishes **one** answer plus a
source tier. The three estimators become strategies behind it.

Two properties to commit to:

- **Monotonic handoff.** A higher-fidelity estimator taking over must not snap the
  displayed number. That jump is the user-visible defect, not the existence of three
  models.
- **Close the loop.** The gap between predicted and actual is exactly the training
  signal `PreprintPredictor` wants. Today the three never compare notes, so nothing
  learns.

## Status

Branch `fix/preparing-job-lifecycle`. Suite green (95/95 shards) at every commit.

| # | Step | State |
|---|---|---|
| 1 | `s_is_initial_transition` re-arming | **done** - `PrintCollectorArming`, mutation-verified |
| 2 | Pure deletions | **done, narrowed** - `is_in_print_start` and `clear_print_info` kept (step 5 gives both callers); `clear_gcode_loaded` field removed; layer-count formatter fixed |
| ~~3~~ | Media identity | folded into 5 |
| 4 | State-machine consolidation | **partial** - see "Outstanding" |
| 5 | `Preparing` at commit | **done** - ownership, reconciliation, both entry points, media identity, `end_preparing`/`set_state` deleted |
| 5b | Cancel during preparing | **done** |
| 6 | Estimate ladder | **not started** |
| 7 | Elapsed/remaining strings | split out to its own PR |

Runtime evidence (mock, headless):

```
Preparing 'ECC_0.4_stand_PLA0.2_2h42m.gcode'   <- commit arms
print_state transition: 0 -> 1                  <- printer confirms
PRINT_START collector started                   <- printer-edge arming intact
Retiring preparing job '...': confirmed         <- reconciled
```

The third line is the regression guard: externally started prints still arm.

## Risks and gaps - read before merging

### 1. Hardware: verified on the K2, including the steps

Tested twice on the K2 Plus 2026-08-18 with a host-side
`BED_MESH_CALIBRATE_START_PRINT`, cancelled mid-mesh both times so no material was
laid.

**Run 1** (before the collector arming) confirmed arming, identity and cancel, and
exposed the gap: the phase stayed pinned at `INITIALIZING` for the whole window with
`collector_active=false`. The overlay came up with the right job and a generic
"Preparing Print..." and never advanced - the second half of the original report,
still broken.

**Run 2** (after arming the collector at commit):

```
23:36:49.043  Starting print: 3DBench_PLA_21m47s.gcode (pre-print: mesh=true, ...)
23:36:49.044  [PrinterPrintState] Preparing '3DBench_PLA_21m47s.gcode'
23:36:49.137  [MoonrakerManager] PRINT_START collector started (commit)
```

Phase progression through the host-side mesh, while Klipper still reported the
PREVIOUS job as `complete`:

```
t+35s   phase=2
t+70s   phase=6
t+105s  phase=7
```

Cancel mid-mesh, both runs: `Retiring preparing job '...': cancelled`, phase back to
`IDLE`, and when the macro returned `continue_print_start` abandoned the start -
`print_stats` still showed the previous job (`SpeedTestStructure_ASA`, 1618s) and
`start_print` was never called.

Confirmed fixed on hardware: badge/identity cleared at commit, media resolves the new
job, pre-print steps advance during a host-side block, cancel stops the start.

Still unverified on hardware: CB1/Voron no-pre-print baseline (overlay flash), and a
`Superseded` race.

Incidental finding worth keeping: **a restart cannot reproduce the stale badge.**
`print_outcome` is set on the *edge* into a terminal state, so an app that boots
while `print_stats` already reads `complete` never sets it. Staging this bug
requires either a real completion or a cancel while the app is running.

### 2. ~~Heaters left on, and `TimedOut` retiring silently~~ FIXED

A pre-start block heats to print temperature, so cancelling a K2 mesh left the bed
at its target with no job and nothing on screen explaining the heat. Observed twice
on hardware; cleared manually both times.

The retirement reason now drives one decision,
`decide_preparing_exit_action(PreparingExit)`:

| Reason | Notify | Cool down |
|---|---|---|
| `Cancelled` | cancelled | yes |
| `Failed`, `TimedOut` | failure | yes |
| `Confirmed`, `Superseded` | no | **no** |

`Superseded` not cooling down is the load-bearing case: another print is running and
owns the printer, so dropping its heaters would sabotage it.

Targets go through `TemperatureController` rather than a raw `TURN_OFF_HEATERS`,
since it is the single authority for heater targets (lint-enforced). Klipper queues
gcode behind a running macro, so the cooldown lands when the macro finishes - which
is the correct moment anyway.

This also closes the separate gap where `TimedOut` retired without telling anyone.
The observer lives in `print_completion`, which already owns "the print ended, tell
the user", and sees every retirement rather than only the cancel-button path.

**Verified on the K2** 2026-08-19: heaters up at bed 50 / nozzle 140 with the phase
advancing (3), cancel mid-mesh, and both targets fell to 0 with no manual
intervention. `print_stats` still showed the previous job, so nothing printed.

### 3. ~~The overlay debounce is designed, not built~~ BUILT

`PrintStatusPanel` now withholds the preparing overlay until `Preparing` has
persisted `PREPARING_SHOW_DELAY_MS` (750ms). Hiding stays immediate - once
preparation ends the overlay must go at once - and the timer re-checks the phase
when it fires, since preparation can end while it waits.

Debounced on **elapsed** time, not on a predicted duration. A prediction fails
closed: predict "fast" against a ten-minute mesh and the overlay never appears at
all, which is the very bug this plan exists to fix. Elapsed time cannot fail that
way and needs no history on first run.

Cancelled from both `cleanup()` and the destructor via one shared
`cancel_preparing_show_timer()` using `lv_timer_cancel_safe()`, per CLAUDE.md
threading rule 5. `scripts/check_timer_destructor_cancel.py` passes at baseline.

Verified against the mock that a real preparation still shows the overlay
(`preparing_visible=1` at phase 3), so the debounce delays without suppressing.
The sub-second case still wants the CB1/Voron baseline run.

### 4. Consolidation: done, and smaller than the census claimed

The census counted eight prev-state trackers and read them as duplication. That was
partly wrong, and the correction matters more than the count.

**They do not all ask the same question.** Two questions look alike:

1. *"What just happened to the UI-level print lifecycle?"* - one authority,
   `print_lifecycle` + `print_lifecycle_prev`, published from the single place that
   computes the transition.
2. *"Did a print become active on the printer?"* - a **printer-state** question,
   correctly asked against the raw `print_state_enum`.

Only `print_completion` was asking (1) while keeping its own copy. It now reads both
halves from the authority, and its file-static `prev_print_state` plus arming latch
are gone. The rule lives in `should_notify_print_ended()`, which deliberately
excludes `Preparing`.

The other two are asking (2), and repointing them at the lifecycle would **break**
them:

- **`telemetry_manager.cpp`** tracks the highest pre-print phase reached and resets
  it on "transition to PRINTING from non-PAUSED". On the raw job state that is
  `STANDBY -> PRINTING`: one reset, at the real start. On the lifecycle it becomes
  `Idle -> Preparing -> Printing`, so the reset would fire on `Preparing -> Printing`
  and wipe the max-phase at exactly the moment preparation ends - destroying the data
  it exists to collect.
- **`print_start_navigation.cpp`** fires on inactive -> active to open the status
  panel. On a lifecycle that now includes a host-side `Preparing`, it would navigate
  for a job the printer has not accepted, duplicating `PrintStartController`'s own
  optimistic push - the double-push the `is_panel_in_stack` guard exists to absorb.

Both are (c): separate for a real reason. Recorded so the next reader does not
"finish the consolidation" and regress them.

Still genuinely outstanding: `PrintStatusPanel::was_preparing_` and
`preparing_visible_subject_` remain panel-local mirrors of the phase, and
`PrintLifecycleState` still lives inside the panel rather than being the app-wide
authority - the subject took over that role instead.

### 5. Docs sweep

Done: `PRINT_STATE_MACHINE.md`, `architecture/05-printer-state.md`,
`docs/user/guide/printing.md`, `PRINT_START_INTEGRATION.md`, `PREPRINT_PREDICTION.md`.

`PRINT_START_INTEGRATION.md` gained the two arming paths, and three stale timeout
claims were corrected: it documented a flat "45 seconds in PRINTING state", while the
code has used an adaptive ladder (1.5x predicted, 300s without history, 1800s absolute
ceiling) plus a 90s quiet requirement for some time.

`PREPRINT_PREDICTION.md` had four provably stale claims, one of them hazardous:

| Claim | Reality |
|---|---|
| "the last 3 timing entries" | `MAX_ENTRIES = 10` |
| Per-count weight table (40/60, 20/30/50) | Exponential time decay, `lambda = 0.23` |
| "Entries over 900s are silently rejected by `add_entry()`" | No cap exists; rejection is per-phase MAD |
| Schema omits `temp_bucket` | It has been persisted for some time |

The 900s claim is the hazardous one. Reinstating it - which the doc invites - would
discard every K2 commit-armed entry, since those run ~1140s. The doc now says so
explicitly.

**Not changed: `docs/user/guide/print-monitoring.md`.** The plan listed it, but its
scope is filament checks and camera failure detection. The user-visible change (the
previous job's badge no longer persists into a new print's preparation) belongs to
`printing.md`, where it now lives. Listing a doc is not a reason to edit it.

Note `docs/user/guide/printing.md` currently describes the **target** affordances
("Cancel is available", "Pause is unavailable"). Per audit A1/A2 the code does the
opposite in each half, so until that lands the doc is aspirational. It is the spec;
the code is what moves.

### 6. Environmental, not caused by this work

- **One shared `lib/` across all worktrees.** A concurrent session's `git clean` in
  `lib/libhv` deleted patch-created files and broke every build on the machine
  mid-session. Repaired by reapplying the `hsocket.c` + `hplatform.h` hunks. Any
  verification run can hit this again while other agents are active.
- **The orphaned `layers` translation key survives.** `format_layer_count()` uses
  `"%u layers"` / `"1 layer"`, so bare `layers` is now dead - but the obsolete-key
  detector cannot see it, because `ui_xml/*.xml` uses `icon="layers"` and the
  matcher cannot distinguish an icon name from a translation tag. Left in place;
  the detector has a false negative worth knowing about.

## Audit findings: parity and regressions (2026-08-19)

Two audits of the surfaces commit arming newly touches. Everything below is proven
from code with file:line evidence unless marked otherwise.

### Verification correction: `ctl click` cannot prove reachability

`ctl click` calls `lv_obj_send_event(widget, LV_EVENT_CLICKED, nullptr)`
(`src/remote/remote_control_server.cpp:426`), bypassing the input-device layer that
suppresses clicks for `LV_STATE_DISABLED` (`lib/lvgl/src/indev/lv_indev.c:1391`).

So the earlier K2 result "cancel during pre-print works" proved the *handler* runs and
the heaters cool down. It did **not** prove a user can reach it - and per B1 below,
they cannot. Affordance must be verified from the widget's `disabled` state flag
(`remote_control_server.cpp:2061` exposes it), never by sending a synthetic click.

### A. Button affordances - the #798 contract is not met

The contract: Pause/Tune/Cancel affordance is a function of `PrintState` only, never
of whether pre-print work is host-side or firmware-side.

`compute_control_button_view()` (`include/print_control_view.h:31`) takes only
`PrintJobState`. It cannot express the contract, and `PrintControlButtons`' sole
observer is `print_state_enum` (`src/ui/print_control_buttons.cpp:48-59`) - nothing
re-runs `recompute()` when the lifecycle changes.

| Gap | Evidence | Effect |
|---|---|---|
| **A1. Cancel is disabled during host-side preparing** | `stop_enabled = active && cancel_available`, `active` = `PRINTING\|PAUSED` (`print_control_view.cpp:15-19`) | The `retire_preparing(Cancelled)` branch added in 5b is **unreachable by touch** from both the status panel and the home widget |
| **A2. Pause is enabled during firmware-side preparing** | same `active`, true throughout Klipper's `PRINT_START` | A live Pause button for the whole window; pressing it sends `PAUSE` mid-macro, then a 25s "Pausing..." spinner and a timeout toast |
| **A3. Two authorities gate one button row** | Pause/Cancel from `PrintJobState` in a singleton; Tune/Timelapse from `PrintState` in the panel (`ui_panel_print_status.cpp:3216-3231`) | They can and do disagree |
| **A4. Tune's preparing enablement is order-dependent** | `update_button_states()` is never called on the Idle->Preparing edge (`ui_panel_print_status.cpp:3037-3115`) | Works on a normal start only because `on_activate()` happens to follow `begin_preparing()`; after **Reprint** the panel is already active, so Tune stays greyed for the whole window |
| **A5. One confirmation dialog for both cases** | `ui_xml/print_cancel_confirm_modal.xml:18-28` | During host-side preparing it says "All progress will be lost" about a print that never started, and "Stop" implies an instant halt a blocking macro cannot honor |

A2 and A1 are the same root cause: the row is wired to the printer's axis, not the
UI lifecycle axis.

### B. Media identity - two regressions introduced by commit arming

The #526 fix itself is **intact**: `skip_thumbnail` gates only the thumbnail
*download*, never the metadata fetch (`active_print_media_manager.cpp:276-294`,
`:411`). But moving identity adoption to commit moves the metadata fetch to a moment
when the file may not be uploaded or scanned.

| Defect | Mechanism | Effect |
|---|---|---|
| **B1. The metadata retry budget is spent during preparation** | Commit fires `process_filename` -> `load_thumbnail_for_file` -> metadata RPC. Failures burn the 10-attempt ladder (~217s total, `active_print_media_manager.cpp:634-670`). When the printer confirms, `process_filename` early-returns on the unchanged effective filename (`:223`), and `thumbnail_retry_count_` is never reset on print start | `layer_total` and `estimated_print_time` stay 0 for the whole job - the #526 symptom by a new route. Worst case is exactly this branch's target scenario: a host-side block longer than the 217s ladder |
| **B2. Mid-scan metadata silently forfeits `layer_count`** | A commit-time fetch that *succeeds* while Moonraker is mid-scan returns `layer_count == 0`, falls to the 16KB header scan, whose error handler only logs (`:397-400`). Nothing re-arms | Same symptom, no retry at all. Fetching earlier makes partial metadata materially more likely |
| **B3. Reprint of a modified print poisons the display name** | `initiate_reprint` passes the raw Moonraker name, which for a modified print is `.helix_temp/modified_<ts>_orig.gcode`. Setting it as the thumbnail source disables the auto-resolve at `:211` (guarded on `thumbnail_source_filename_.empty()`) | The panel shows `modified_1748..._orig`. Could not happen pre-branch, because reprint did not re-run `process_filename` |
| **B4. gcode load-complete stamps the wrong identity** | The success callback records `gcode_displayed_file_` from the panel's filename *at completion*, not the one requested (`ui_panel_print_status.cpp:1544-1568`) | Previous print's geometry pinned as current. Pre-existing; this branch widens the window from ~5s to minutes |
| **B5. A dead preparing job leaves the placeholder** | Commit publishes `no_thumbnail_placeholder()` for the new file (`:251-256`); `release_identity()` deliberately republishes nothing | Home-panel widget shows the placeholder until the next filename change. Cosmetic |

`decide_preview_action()` itself is sound: it is pure, has no "clear" outcome in its
vocabulary, and cannot blank a good preview. The premise that commit->Preparing calls
`ensure_preview_current()` was **wrong** - it is reached only via the pre-existing
optimistic navigation. What changed is the *inputs*, not the trigger.

### C. Prediction history is now two populations in one bucket

The collector's measurement window starts at `start()`
(`print_start_collector.cpp:105`). Commit arming therefore puts any host-side
pre-start block inside the measured total, while an externally started print still
measures from the printer edge. Both land in the same history bucket, which is keyed
only on cold/warm (`:1974`).

With `MAX_ENTRIES = 10` and exponential recency weighting, a K2 mixing ~1140s
screen-started samples with ~300s Mainsail-started ones produces an estimate wrong
for both. Worse, `predicted_total` feeds the collector's own adaptive timeout
(`elapsed > predicted_total * 1.5`), so a history skewed short can complete the
pre-print *while it is still running* - the precise failure this branch exists to
prevent.

Fixed by bucketing entries on which window they measured (`PreprintWindow`). Legacy
entries map to `PrinterEdge`, which is a fact about the data rather than a guess:
commit arming did not exist when they were recorded.

## Merge from main, 2026-08-19

63 commits. **No source conflicts, and main touched none of this branch's core
files** - `ui_print_start_controller.cpp`, `printer_print_state.cpp`,
`print_completion.cpp`, `moonraker_manager.cpp`, `ui_panel_print_status.cpp`,
`print_start_collector.cpp`, `ui_print_preparation_manager.cpp` all had zero
commits on main since the merge base. The only file both sides touched is
`include/state/subject_macros.h`, and that one helps.

### What helps

`c7cc96670` - `INIT_SUBJECT_INT/STRING` never handed the XML name to
`register_subject()`, so `SubjectManager::deinit_all()`'s withdrawal loop was
inert. Any owner that does not outlive the process left its subject names
resolving to freed storage, and the next `lv_subject_add_observer()` walked a
garbage `subs_ll`. That is the nightly TSan SIGSEGV.

This matters here specifically. `print_lifecycle` is declared with
`INIT_SUBJECT_INT(..., register_xml)` (`printer_print_state.cpp:100`), and this
branch adds two new observers against per-instance `PrinterState` subjects - one
in `PrintControlButtons`, one in `ActivePrintMediaManager`. More
`add_observer()` calls against a fixture-owned `PrinterState` is exactly the
shape that trips the dangling-name bug, which is the leading hypothesis for the
media test abort described below.

`43367bbf9` is the same family: `MoonrakerClientMock`'s forced teardown deleted
its calibration `lv_timer`s but leaked their payloads, and `PanelWidgetManager`
freed grid descriptors LVGL still held raw pointers to. Both were surfaced by
unmasking the sanitizer gates.

### A failure the merge resolved

Before the merge, `"A confirmed print re-arms media that failed to load while
preparing"` aborted in **fixture teardown** - not in its assertions, all 9 of
which passed. The abort was `std::system_error: Owner died` inside
`MoonrakerClient::~MoonrakerClient` taking `state_callback_mutex_`
(`moonraker_client.cpp:186`): a poisoned mutex, so memory corruption from
earlier.

Deferring the re-arm out of the subject-observer dispatch changed nothing, and
that speculative change was reverted rather than kept.

**The merge fixed it.** All 27 `[preparing]` cases pass, 104 assertions. The
cause was `c7cc96670`: this branch adds observers against a fixture-owned
`PrinterState`, and a previous test's dead `PrinterState` had left
`print_lifecycle` resolving to freed storage, so `lv_subject_add_observer()`
walked a garbage list. Worth recording as a general hazard: a test abort in an
unrelated destructor, in a suite where fixtures own their own `PrinterState`,
should send you to subject-name teardown before you start bisecting your own
change.

### What to watch

Main's new `BypassToggleController` guards on
`print_occupies_toolhead(PrintJobState)` (`ui_bypass_toggle_controller.cpp:28`),
which is `PRINTING || PAUSED` - **the same axis that produced A1/A2**. During a
host-side pre-start window the raw job state is STANDBY or COMPLETE, so the
guard passes and the toggle drives filament through a toolhead that is homing,
meshing or purging for a print the user has already committed to. The helper is
correct for what it asks; the question "does a job own the toolhead right now?"
simply cannot be answered from `PrintJobState` alone once `Preparing` is
reachable before the printer accepts the job.

## Outstanding work, in dependency order

1. ~~Arm the collector at commit~~ **done** - verified on the K2; phases advance
   through a host-side mesh. Teardown routes through
   `should_stop_print_collector()` so the transient `complete -> standby` hop into
   our own print no longer stops it.
2. **CB1/Voron baseline** - no pre-start block; confirm no overlay flash and no
   regression for externally started prints.
3. ~~Implement the 750ms overlay debounce~~ **done** - gate passes; CB1 baseline
   still wanted for the genuinely sub-second case.
4. ~~Notify on `TimedOut`, decide what cancel does to the heaters~~ **done and
   hardware-verified**.
5. ~~Migrate the remaining trackers~~ **done for the one that was duplication**;
   the other two are legitimately separate and must stay (risk 4).
6. **Button contract (A1-A5)** - move the row onto the lifecycle axis. Blocks the
   5b cancel work, which is currently unreachable by touch.
7. **Media regressions (B1-B3)** - re-arm the metadata retry budget when the print
   actually starts; stop reprint poisoning the display name with the temp filename.
8. **Prediction window bucketing (C)** - in progress.
9. **Estimate ladder** (step 6) - one owner for "how long until printing starts?",
   three estimators demoted to strategies, monotonic handoff.
10. **Finish the docs sweep** (risk 5), then `scripts/check_doc_refs.py`.
11. **Split step 7** (elapsed/remaining rendered strings into `PrinterPrintState`)
    into its own PR.

## Scope and sequencing

Too large for one commit. Each step is independently reviewable, and each earlier
step shrinks the next.

| # | Step | Character |
|---|---|---|
| 1 | Fix `s_is_initial_transition` re-arming | Live bug. Small, own test. Lands regardless of the rest. |
| 2 | Pure deletions: `clear_print_info`, `is_in_print_start`, `clear_gcode_loaded`, the duplicate `StateChangeResult` boolean, the ignored `on_job_state_changed` `outcome` param, the `print_completion` layer-string reimplementation (it renders `"1 layers"` and `"0 layers"`, both of which `format_layer_count()` gets right) | No behavior change |
| ~~3~~ | **Folded into step 5.** Not separable - see below | - |
| 4 | Move `PrintLifecycleState` out of `PrintStatusPanel`; consolidate the eight prev-state trackers behind one edge source; settle on one definition of "active"; move `is_active_print_state` to `printer_state.h` | Pure refactor, large |
| 5 | Re-scope the `print_active == 0` invariant, then `Preparing` at commit with `begin_preparing` / `retire_preparing` | The actual fix, now small |
| 6 | One owner for the preparation estimate; three estimators demoted to strategies | Structural |
| 7 | Elapsed/remaining rendered strings into `PrinterPrintState`, following `print_progress_text` (`52b6cef7d`) | Separable; own PR |

Steps 1-6 in this branch. Step 7 split out.

Dependency order, not size order: step 4 gives step 5 somewhere to put the owner.

### Why step 3 folded into step 5

The two `set_thumbnail_source` methods are not an accidental clone. The call site
comment states the fanout is deliberate:

```
// - Panel: local gcode viewer and thumbnail display
// - Manager: shared subjects for HomePanel
```

Both consumers genuinely need the identity. The defect is that every caller must
remember to call both, and the two copies have **different lifetimes**:

| Copy | Set by | Cleared |
|---|---|---|
| `PrintStatusPanel::thumbnail_source_filename_` | 3 call sites | `ui_panel_print_status.cpp:2719`, on `print_ended` -> Idle |
| `ActivePrintMediaManager::thumbnail_source_filename_` | 2 of those 3 | **never** - both `.clear()` sites sit in `clear_thumbnail_source()` and `clear_print_info()`, neither of which has a production caller |

So the panel forgets its override when a print ends and the media manager keeps it
for the life of the process - and the media manager is the one feeding the shared
subjects the HomePanel reads. A print that sets an override (USB path,
`.helix_temp/modified_*`) leaves it live in APMM indefinitely.
`PrintStartController::initiate_reprint` sets neither, so a reprint after such a
print runs with the previous job's identity still in force on the home card. That is
the #526 mechanism reached by a different route.

Deleting the panel method standalone also drops its `displayed_file_.clear()`, which
invalidates panel-local preview cache that APMM does not own - risking #1044's
family.

Both are symptoms of there being no single owner of job identity. `begin_preparing(job)`
is that owner, so this work belongs with it rather than ahead of it.

### Consolidation targets

Six independent representations of "we are preparing" collapse to one:
`print_start_phase` (demoted to "which phase"), `PrintStartCollector::active_`,
`PrintLifecycleState::current_state_ == Preparing`, `PrintStatusPanel::was_preparing_`,
`preparing_visible_subject_`, and `print_in_progress`.

Eight prev-state edge trackers collapse to one source with subscribers:
`moonraker_manager.cpp:683`, `print_completion.cpp:35`,
`print_start_navigation.cpp:20`, `PrintLifecycleState::current_state_`,
`telemetry_manager.cpp:2995`, `PrintStatusPanel::was_preparing_`, plus two that stay
separate for real reasons (`U1StockSource::last_state_` is a vendor-scoped
`-> PAUSED` edge behind a capability gate; `PrintStartController::print_state_observer_`
is a self-cancelling one-shot for filament-remap restore).

Three definitions of "active" collapse to one: `PRINTING|PAUSED` (nav, widget,
exclude manager, filament tracker), `PRINTING|PAUSED|Preparing`
(`PrintLifecycleState::is_active`), and the JSON-string form
`status_indicates_active_print` (`printer_print_state.cpp:273`).

Four resets currently fire only on the Moonraker `-> PRINTING` edge, i.e. *after*
preparation: `end_overlay_dismissed`, `complete_view_mode_`, the view-toggle icon,
and excluded objects. A reprint started from the end overlay carries the previous
job's dismissal state and view mode through the entire preparing window. All move to
the owner's "job begins" edge.

Two auto-navigation owners collapse to one: `PrintStartController`'s optimistic push
and `print_start_navigation.cpp:43`'s inactive->active push. The `is_panel_in_stack`
guard at `:46` exists only because the first owner already pushed. The nav module
keeps the recovery case (already active at init, `:101`).

### Deliberately kept

`print_progress` vs `print_progress_display` (documented at
`printer_print_state.h:87-97`: the raw value must stay unfrozen because the pre-print
estimates key off it being zero). `ThumbnailOrigin`'s three values (all produced,
load-bearing for the recovery ladder). `ActivePrintMediaManager`'s two identity
fields (override vs current - the duplication is with the panel, not within APMM).

`ActivePrintMediaManager::clear_thumbnail_source()` is listed elsewhere as dead code,
but this plan gives it its production caller. It stays.

## Cancel during the preparing window

The preparing job doubles as the go/no-go token for the start, which is why cancel
needs no separate flag.

`PrintControlButtons::handle_stop_button` branches before `AbortManager`: if a job
is being prepared and the printer is not `PRINTING`/`PAUSED`, it retires the job as
`Cancelled` and stops. Routing that through `AbortManager` would send `CANCEL_PRINT`
to an idle printer, whose own state watcher reads the resulting terminal state as an
immediate success (`abort_manager.cpp:722-729`) while the queued `start_print` fires
anyway once the macro returns.

`PrintPreparationManager::continue_print_start()` is the choke point every pre-start
path funnels through before a job is actually started, so it is where a cancellation
can be honoured. It abandons the start when the preparing job is gone.

The guard is scoped by `armed_at_start_`, snapshotted in `start_print()`. Without
that, a caller that never armed a job could never print at all - which is exactly
what happened: the first version broke
`tests/unit/test_pre_start_timeout_gate.cpp`, whose fixture drives the manager
directly. Distinguishing "cancelled" from "never armed" is the difference between a
guard and a bug.

A running macro still finishes its current motion; Klipper cannot interrupt one. The
guarantee is that no print begins, and the UI says so rather than implying an
instant stop.

## Notifying when a start dies before it prints

**Decided.** A pre-print that fails must notify **as a failure**, and an early
cancel must read as **cancelled** - not as a completion, and not as each other.

`PreparingExit` already carries the distinction, so the notification keys off the
retire reason rather than off a terminal job state that never arrives:

| Retire reason | User-facing outcome |
|---|---|
| `Failed`, `TimedOut` | Failure notification |
| `Cancelled` | Cancelled notification |
| `Superseded` | Silent - a different print is now running and owns the UI |
| `Confirmed` | Nothing; the print is under way |

This matters because a start can die during a ten-minute mesh without Klipper ever
reporting a terminal state: `print_stats` still holds the PREVIOUS job's outcome, so
the existing completion path cannot see the failure at all, and would report the old
job's result if it did fire.

### Why the existing consumers cannot simply be repointed

`print_completion.cpp:268` gates on

```cpp
bool was_active = (prev_print_state == PRINTING || prev_print_state == PAUSED);
```

`PrintLifecycleState::is_active()` (`include/print_lifecycle_state.h:141`)
**includes `Preparing`**. Repointing this consumer at the lifecycle subject widens
the predicate, and `Preparing -> Error` would fire the *completion* path for a print
that never started (L108: the old guard is what kept the callee correct). The
notification must therefore be driven from the retire reason, keeping
`print_completion`'s own guard as-is.

`telemetry_manager.cpp:3039` has the same shape (`PRINTING && prev != PAUSED`).
`print_start_navigation.cpp` differs again: it fires on `COMPLETE -> PAUSED` for
power-loss restore (#1099), which the collector tracker deliberately does not.

## Constraints from issue history

Ranked by how much they bind. Closed issues encode regressions already paid for.

### #1042 - stuck "Starting Print...", collector restart during AFC recovery
Fixed `72de7da26`. `PRINTING -> ERROR -> PRINTING` with no reset looked like a fresh
start; the collector restarted and could never auto-complete. Close comment:

> only the `ERROR` recovery leg is guarded. A recovery routed via `STANDBY` is
> indistinguishable from a real reprint with stale `print_duration`

Commit arming dissolves this ambiguity, which is the strongest argument for this
change. But the printer-edge path must keep `should_start_print_collector()` and its
guards intact. Its regression tests (`tests/unit/application/test_moonraker_manager.cpp:368-400`,
"the #1042 repro") are **preserved, not retired**. If the predicate moves, the tests
move with it.

### #546 - stale print screen, Reprint shown instead of Cancel
Regressed **three times** (`19bec23d` -> `a5684bfd` -> a third fix). Its invariant is
at the exact site this plan modifies, `printer_print_state.cpp:1080-1104`: the stale
guard in `set_print_start_state()` must allow `IDLE -> non-IDLE` **even when
`print_active == 0`**.

Arming at press moves the outcome clear strictly earlier. Required test: badge and
Cancel/Reprint swap clear on the press, and do **not** un-clear when the deferred
`reset_for_new_print()` lands.

Reporter's repro, verbatim, is a test case: *start a print from the screen, cancel it
as it starts, go home, pick a new file - the Cancel button is a Reprint button.*

Second constraint from fix 2 (`a5684bfd`, also #633): PrintStatusPanel is a
persistent overlay whose lifecycle registration was lost on navbar switches, so
`on_activate()` never fired on re-push. Anything made `on_activate()`-driven inherits
that fragility.

### #17 item 6 - preparing overlay never goes away
Product contract to preserve: `M118 HELIX:READY` at the end of a user's
`PRINT_START` retires the overlay immediately, with a timeout fallback. Stated
policy: *"definitely not looking to force people to change theirs."*

Commit-armed `Preparing` is **more** dangerous than today's, because it can arm on a
printer that never reaches `state=printing` at all (upload failure, rejected
`PRINT_START`, printer offline). `TimedOut` must be **ungated**.

Counter-constraint from #991: a wall-clock backstop was deleted there because it
raced slow cold-nozzle resumes and surfaced false modals (15s timer vs a legitimate
37s `M109`). So `TimedOut` must be generous, and must not pop anything user-visible
unless a real failure signal accompanies it.

### `should_complete_preprint()` - the async reset race (RESOLVED)

Documented at `include/moonraker_manager.h:220-300`. `reset_for_new_print()` is
dispatched asynchronously **after** the collector becomes active, so stale
layer/progress subjects are live while `Preparing` already shows.

An earlier draft assumed commit arming widens that window. It does not - it closes
it on the new path. The defer exists for one stated reason
(`printer_print_state.cpp:1070-1072`):

```cpp
// CRITICAL: Defer to main thread via ui_queue_update to avoid LVGL assertion
// when subject updates trigger lv_obj_invalidate() during rendering.
// This is called from WebSocket callbacks (background thread).
```

That holds for the printer-edge path. It does not hold for a button press, which is
already on the main thread. So `begin_preparing()` on the commit path runs
`reset_for_new_print()` and the outcome clear **synchronously**, and the stale-subject
window is zero. The printer-edge path keeps its defer unchanged.

To verify during implementation: that `reset_for_new_print()`'s observers do not
delete widgets, since a synchronous call from an LVGL event callback would then meet
CLAUDE.md threading rule 3. If any do, route those specific effects through
`safe_delete_deferred()` rather than reintroducing a blanket defer.

Encoded regressions not to disturb:
- Completion must be **edge**-relative, never a level read: a stale `current_layer`
  of 250 from the previous print would complete the new pre-print phase instantly.
- Discriminate on the sticky `printer_reports_layers`, never per-print
  `has_real_layer_data` (the U1 premature-completion regression).
- `seen_layer_zero || layer_advanced`, because coalesced `notify_status_update` can
  make the first observed sample >= 1 and hang the overlay for the whole print.

### #1048 - two commit entry points, not one
`PrintStartController::execute_print_start` (primary) and `::initiate_reprint`
(status-panel Reprint, lightweight `job().start_print`, deliberately skips the prep
manager). Both must arm. Neither may arm before the U1 native pre-print config is
sent. Hardware-verified ordering: `U1 pre-print config` -> `SET_PRINT_USED_EXTRUDERS`
-> `Starting print` -> collector started.

### #798 - Pause/Tune/Cancel during Preparing (RESOLVED)

Governing invariant:

> Pause/Tune/Cancel affordance is a function of `PrintState` only. It never depends
> on whether pre-print work is host-side or firmware-side.

On most printers homing/heating/purge/mesh live inside `PRINT_START` and are under
Klipper's control. On others (K2 optional bed mesh) that work runs in front of the
job, under ours. The user must not get different controls because of that split. It
is the same vendor-neutrality rule as the rest of this plan, applied to the UI.

**Cancel - enabled, always.** Uniform promise ("the print does not happen"),
different mechanism, mechanism hidden from the user.

- Pre-print inside `PRINT_START`: `print_stats=printing`, `AbortManager`'s normal
  `CANCEL_PRINT` path already works.
- Pre-print in front of the job: there is no job to cancel, and routing through
  `CANCEL_PRINT` is actively wrong. `AbortManager::on_print_state_during_cancel`
  (`src/abort/abort_manager.cpp:722-729`) treats `STANDBY`/`COMPLETE` as proof the
  cancel succeeded, so firing it at an idle printer reports instant success while
  the pending `start_print` is still queued to fire. Instead: retire `Preparing` as
  `Cancelled`, set the flag so `start_print` never fires when the macro returns, and
  drop the heaters we requested.

A blocking macro cannot be interrupted - a Klipper limitation, not something to
design around. So Cancel guarantees the print will not start, *not* that motion stops
immediately: the printer finishes its current leveling pass. The confirm dialog for
this case says so rather than implying an instant stop.

**Pause - disabled during `Preparing`.** Uniform, and not merely because there is no
job in the host-side case. Even where Klipper would accept it, `PAUSE` mid-`PRINT_START`
is a footgun: the macro keeps running and the pause lands at the next print move,
which breaks many custom macros. Nothing meaningful to pause in either architecture.

This **narrows** #798, which enabled all three. Deliberate, not a regression.

**Tune - enabled.** `ui_xml/print_tune_panel.xml` carries speed, flow and Z-offset
babystep only; no temperature, which removes the obvious hazard.

- Speed/flow are `M220`/`M221` multipliers: global, persistent, applied when the
  print starts. Useful during pre-print and harmless.
- Z-offset babystep may be wiped by the file's own `PRINT_START` (`G28`, or an
  explicit `SET_GCODE_OFFSET Z=0`). That is true whether or not a host-side pre-start
  block ran first, since the file's `PRINT_START` executes in both cases. Same
  behavior as today for the firmware-side case.

Leave Tune fully enabled. Disabling Z-offset only during `Preparing` would itself
break the invariant above, by behaving differently depending on architecture.

### #1221 - SIGSEGV on the print-start failure path
Fixed `f992a0e2c`. `go_back()` only enqueues a pop; the `show_detail()` beside it
runs synchronously, and the queue drains between a null-check and a use. The `Failed`
retirement lands in this lambda, so it runs inside `process_pending()`: CLAUDE.md
threading rule 3 applies, and pointers must be re-checked across any drain.

### #526 - layers 0/0, caused by the thumbnail pre-set
Fixed `c667f7a2`. A pre-set thumbnail (from `PrintStartController`) skipped the
metadata fetch, losing `layer_count` and `estimated_time`. The preparing job hands
media an early identity, so verify the metadata call still always proceeds and only
the thumbnail *download* is skipped.

### #1099 - power-loss recovery must stay independent
Fixed `d0ea6aa1a`: the screen now auto-opens for **any active job, including at
startup**, not only a fresh start. Keep that trigger as its own path, independent of
commit arming.

### #1044 - preview reconcile gains a new trigger
`ensure_preview_current()` / `decide_preview_action()` run on every `on_activate()`
plus print-phase change. Commit -> `Preparing` is a new phase change at a moment when
the file may not be uploaded or parsed. Idempotent by design, but check
`decide_preview_action()` handles "preparing job known, file not yet present".

## Testing

Test-first. Existing coverage to extend:

- `tests/unit/test_print_lifecycle_state.cpp` - `Preparing` entry/exit, exit reasons
- `tests/unit/application/test_moonraker_manager.cpp:440-490` - observer as pure reconciliation
- `tests/unit/test_layer_tracking.cpp:460-600` - reset/collector race
- `tests/unit/test_printer_print_char.cpp:1144-1180`, `:1652-1720` - reset semantics
- `tests/unit/test_preprint_adaptive.cpp` - pre-start emission unchanged

New cases required:

1. Commit -> `Preparing` entered, prior terminal state cleared (badge, progress,
   layer, elapsed) before any printer transition
2. Confirm with matching filename -> `Confirmed`, single clean handoff to `Printing`
3. Confirm with different filename -> `Superseded`, our claim discarded
4. `complete -> standby -> printing` hop does not retire a live preparing job
5. Pre-start gcode error -> `Failed`, overlay does not strand
6. Quiet timeout -> `TimedOut`
7. Externally started print (no commit) still arms on the `standby -> printing` edge
   and still enters `Preparing` - the regression this plan nearly introduced
8. Printer with no pre-start work never observably enters `Preparing`
9. Cancel during a commit-armed `Preparing`, before any job exists, aborts the
   pending start and does not send `CANCEL_PRINT` to an idle printer (#798)
9b. Cancel during a firmware-side `Preparing` still routes through `AbortManager`'s
    normal `CANCEL_PRINT` path - same affordance, different mechanism
9c. Pause is disabled for the whole of `Preparing`, in both architectures
9d. Tune remains enabled for the whole of `Preparing`, in both architectures
10. Reprint button path (`initiate_reprint`) arms too, and not before the U1 native
    pre-print config is sent (#1048)
11. Badge clears on press and does not un-clear when the deferred
    `reset_for_new_print()` lands (#546)
12. `M118 HELIX:READY` retires a commit-armed `Preparing` (#17)
13. `TimedOut` fires when the printer never reaches `state=printing` at all
14. Pre-set media identity does not suppress the metadata fetch (#526)
15. `freeze_progress_display()` unfreezes at press, so cancel-then-reprint does not
    show the previous print's frozen 100% through the preparing window

## Documentation sweep

Required, not optional (L106): a change this size that does not land in the docs
becomes the next person's archaeology. Targets are named because "update the docs"
is not a task.

The docs currently carry the same blind spot as the code, which is itself worth
recording as evidence the diagnosis is right.

### Devel

| Doc | Action |
|---|---|
| `docs/devel/PRINT_STATE_MACHINE.md` (149 lines) | **Primary rewrite.** Today it describes `PrintLifecycleState` as *the* print state machine, with no mention that it is a private member of one screen (`include/ui_panel_print_status.h:402`) and no mention that three other components track the same transitions independently. Add the ownership, both arming paths, the retire reasons, and the commit-vs-printer-edge distinction. Delete every claim the change falsifies. |
| `docs/devel/architecture/05-printer-state.md` (237 lines) | **Zero** current mentions of `PrintLifecycleState` or `Preparing`. The chapter that owns printer state cannot see the print state machine, because it lives in a panel. If the state machine moves, this chapter gains it; if it does not, this chapter at minimum gains the routing. |
| `docs/devel/architecture/15-known-debt.md` | Its duplication-debt catalogue already holds entries of exactly this shape ("One aspirational abstraction, three parallel wirings"). Consolidating the edge trackers **removes** debt here rather than adding it. If consolidation is partial, record precisely what is left and why. |
| `docs/devel/PRINT_START_INTEGRATION.md` (318 lines) | The `M118 HELIX:READY` contract and the pre-start mechanism live here. Both gain a commit-armed path. |
| `docs/devel/PREPRINT_PREDICTION.md` | Note that the overlay-withhold threshold deliberately does **not** consume a prediction, and why (a prediction fails closed). |

### User

The behavior changes are user-visible, so this is not devel-only.

| Doc | Action |
|---|---|
| `docs/user/guide/printing.md` | Pause is disabled during pre-print (a narrowing of #798). Cancel during pre-print guarantees the print will not start but does not stop an in-flight leveling pass. |
| `docs/user/guide/print-monitoring.md` | The print status screen no longer shows the previous job's completion badge and progress while a new print is preparing. |

Run `scripts/check_doc_refs.py` after the sweep.

## Verification

Unit suite, then on hardware:

- CB1/Voron: externally started and screen-started prints, no regression, no flash
- K2 Plus: the reported case end to end, plus a Mainsail-started print while a
  screen-started job is preparing (case 3)

## Resolved design questions

### 1. Where `PrintJobRef` lives

New `include/print_job_ref.h`, included by `printer_state.h`.

No new dependency edge is created either way: `ActivePrintMediaManager` already
includes `printer_state.h` and holds a `PrinterState&`
(`include/active_print_media_manager.h:11,53`). The separate header exists only so
`PrintPreparationManager` and `PrintStartController` can name the type without
pulling in 2491 lines of `printer_state.h`.

Corroboration that this is where the code already expected the concept:
`ThumbnailOrigin::PreSet` is documented as "an externally supplied path (USB /
embedded G-code, via PrintStartController)". The media manager already models
"the controller told me about a job before Moonraker did". We are naming and
generalising that, not inventing it.

### 2. `retire_preparing(Superseded)` and media reset

**Answered twice, wrongly both times; here is what the code actually needs.**

First answer was `clear_thumbnail_source()` - under-specified, because it leaves
`thumbnail_origin_` set and a stale `ThumbnailOrigin::PreSet` **skips the thumbnail
fetch**, which is the mechanism behind #526.

Second answer was `clear_print_info()` - it does reset the origin, but it *also*
defers `set_print_display_filename("")`. On a supersede that deferred blank lands
**after** the incoming filename has been resolved synchronously, wiping it; and even
when the ordering happens to work it flashes an empty filename.

The two concerns were tangled in one function. They are now split:

- `release_identity()` - synchronous bookkeeping: override, idempotence key,
  retry state, `thumbnail_origin_`. Publishes nothing.
- `clear_print_info()` - calls `release_identity()`, then defers the subject
  blanking. For a genuine "there is no print" clear.

A supersede calls `release_identity()` only. Whatever the printer reports next
repopulates the subjects with no blank flash.

Dispatch matters as much as the reset: the epoch observer uses
`observe_int_immediate`, not `observe_int_sync`. `_sync` routes through
`queue_update`, so the identity release would land after a synchronously
dispatched filename update had already early-returned on the stale override -
leaving the previous job's name on screen, which is the bug this is meant to fix.

Note both media functions had **no production caller** before this work.

### 3. Overlay-withhold threshold

**Flat elapsed-time debounce, not a `PreprintPredictor`-derived value.** This
reverses the initial lean toward deriving it.

A prediction can fail closed. Predict "fast", reality is slow, and the overlay is
suppressed through a seven-minute wait - reproducing the exact bug this plan exists
to fix. A prediction also has no history on first run.

Debouncing on *elapsed* `Preparing` time cannot fail that way: the overlay appears
once the state has actually persisted past the threshold, so being wrong about
duration is self-correcting. A sub-second window on a fast printer never renders;
a long one renders after the threshold. No history required.

Start at 750ms and tune on hardware.

Implementation constraint: this is an `lv_timer_t` in the panel. Per CLAUDE.md
threading rule 5 it must be cancelled in the destructor as well as in `cleanup()`,
via a shared `cancel_*_timer()` using `lv_timer_cancel_safe()`.
