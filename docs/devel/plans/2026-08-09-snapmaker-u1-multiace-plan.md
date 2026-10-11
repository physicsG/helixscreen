# Snapmaker U1 + multiACE — support plan

> ## ▶ START HERE (new session, 2026-08-11)
>
> **Branch:** `feat/snapmaker-multiace`, **31 commits**. It IS pushed —
> `origin/feat/snapmaker-multiace` is at `5053dd26b`, with the last 6 local only. The plan's
> older "nothing pushed" note is obsolete.
>
> **State: Phases 1-2 are done and hardware-verified, and the ACE dryer works.** The U1 draws
> four independent toolheads; multiACE is detected as its own `AmsType`; the ACE appears as
> unit 1 ("ACE 2 Pro") with live temp/RH; ACE-fed heads load and unload through
> `ACE_LOAD_HEAD`/`ACE_UNLOAD_HEAD`; drying runs from the stock environment panel.
>
> **Read §2's ⚠️ banner and §3 before trusting any "implemented" claim in this doc.** Three
> were wrong and are corrected in place: §2's AFC topology fix was never built, §3's
> shape-based discriminator is impossible, and the fixture it cited was never committed (a
> real one now lives at `tests/fixtures/snapmaker_u1/u1-multiace-head-mode-idle.json`).
>
> ### Open, in the order I would take them
>
> 1. **Auto-dry toggle — ✅ unblocked and built (2026-08-11).** The parameter names are no
>    longer unknown; see § "Auto-dry" below for the full surface. No hardware probe was
>    needed and none should be run.
> 2. **§10.2 stuck unload — fixed but never re-observed.** The dispatch is unit-tested against
>    the captured payload; nobody has run an actual unload on T3 since. Confirm before closing.
> 3. **Stale spool duplicates in Moonraker's DB.** `5122216e5` stops an empty MMU bay claiming
>    a tool's spool, but assignments are cached in Moonraker's database as well as
>    `tool_spools.json` — deleting the local file restores the old values. Existing duplicates
>    (tool 0+1 both spool 3, tool 2+3 both spool 6) outlive the fix and need clearing.
> 4. **Phase 3's richer half.** The duplicate nozzle is gone and each unit only draws lines to
>    heads it feeds, but the mockup's per-head *source stack* in the seat ("PETG · ACE bay 1")
>    and the rows-vs-columns switch of §6 are not built.
>    Mockup: `2026-08-10-multiace-ui-improvements-mockup.html`.
> 5. **Phase 1's AFC generalisation** (§2 banner) — never built, still open. Your U1 is fine
>    either way; another AFC-shim-over-toolchanger machine still draws one merger.
>
> **Decided against:** replacing the dryer panel's preset dropdown with pills. That panel
> (`ams_environment_overlay`) is **upstream stock**, added by Preston in `211596fba`
> (2026-03-25), and is shared with the QIDI Box and CFS dryers; its presets are material-based
> ("PLA 45 °C/4h"), which carries more than bare temperatures. Per Gordian: keep it as stock as
> possible. This branch's only change there is a 10-line per-unit humidity fix.
> Mockup (with pills, NOT built): `2026-08-10-multiace-dryer-mockup.html`.
>
> ### Traps this session cost hours to find — read before debugging anything
>
> - **Moonraker sends DELTAS: absent ≠ cleared.** Treating a missing `head_source` as "no
>   longer seated" wiped the ACE→head binding a second after it arrived, so bays lost their
>   tool badges and the unit detail fell back to hub-only. Every parse must leave untouched
>   what the frame does not mention. Invisible to any test that feeds one full frame.
> - **`<bind_flag_if_eq>` loses to a later imperative `lv_obj_clear_flag()`.** Three buttons in
>   the slot menu are shown that way and needed a C++ gate as well as the binding.
> - **`ACED__DRY_START_n` is NOT the dryer API.** Those are multiACE's parameterless Fluidd
>   macro buttons, and reading only the README's macro table produced a wrong conclusion that
>   temp/duration need a config edit plus a Klipper restart. The real commands take parameters:
>   `ACE_DRY ACE=n [TEMP=] [DURATION=]`, `ACE_STOP_DRYING [ACE=n]`. Check `gcode/help` on the
>   machine, and OrcaSlicer's `resources/web/multiace/index.html`, before believing a doc.
> - **`ctl click` cannot reach the filament canvas's hit regions** — it sends a widget event
>   with no coordinates. Use `ctl press <x> <y>` / `ctl release`; nozzles sit at
>   `canvas.y + canvas.h * 0.55`.
> - **Run `"[ams]"`, never `"[snapmaker]"`.** Half the relevant tests carry neither tag pair.
> - **A slot's colour is drawn by FILL.** `display_fill_level()` is 0 when not present, and
>   every spool visual sizes its coloured ring by fill — so an assigned-but-empty lane renders
>   as bare grey chrome unless the fill is floored (`SPOOL_ASSIGNED_MIN_FILL_PCT`).
>
> **Test state:** `"[ams]"` = 1681/1681. Full suite = 2780 cases with **1 pre-existing
> failure**: `test_clock_widget.cpp:157` segfaults in the unfiltered run, passes under
> `"[clock_widget]"` alone, and **reproduces with this branch stashed** — inherited from
> `main`, do not chase it here. `scripts/quality-checks.sh` also fails pre-existing on
> "Missing icon codepoints" (needs `./scripts/regen_mdi_fonts.sh`); every gate this branch can
> affect is clean, with the imperative-UI ratchet held at its 384 baseline.
>
> **Formatting — the old note here was wrong twice.** CI pins **clang-format 18.1.8**, not 14
> (`requirements.txt`), and the CI check is **non-blocking** — `scripts/quality-checks.sh` has
> `EXIT_CODE=1` commented out under "Don't fail CI for formatting". So drift will not reject a
> PR; it just gets reflowed by the next machine whose pre-commit hook has the binary.
>
> The binary is now available (2026-08-11): `python3 -m venv .venv && .venv/bin/pip install -r
> requirements.txt`, which needed `sudo apt install python3.14-venv` first. `.venv` is
> gitignored. **The branch has real drift in 18 of its touched files** — 31 commits were made
> with no formatter present. Fix it as its OWN commit and add that SHA to
> `.git-blame-ignore-revs`, the way `54650149f` (the tree-wide 18.1.8 reflow, an ancestor of
> this branch) is recorded. The baseline itself is clean; `include/filament_database.h` is the
> one exception and is long string literals clang-format cannot break.
>
> **Driving the real printer from a dev box** (no deploy, no SSH — this is the whole loop):
> ```bash
> export HELIX_SOCK=/tmp/helix-u1.sock HELIX_CONFIG_DIR=/tmp/helix-cfg-u1
> mkdir -p "$HELIX_CONFIG_DIR" && cp /tmp/helix-seed/settings.json "$HELIX_CONFIG_DIR/"
> ./build/bin/helix-screen --moonraker 192.168.2.242:7125 -s tiny -vv \
>     --log-dest file --log-file /tmp/helix-app.log --remote-socket "$HELIX_SOCK" &
> ./build/bin/helix-screen ctl -s "$HELIX_SOCK" click tour_skip_btn
> ./build/bin/helix-screen ctl -s "$HELIX_SOCK" navigate filament
> ./build/bin/helix-screen ctl -s "$HELIX_SOCK" click ams_bars_container   # -> ams_panel
> ./build/bin/helix-screen ctl -s "$HELIX_SOCK" screenshot /tmp/x.png
> ```
> `-s tiny` **is** the U1's 480x320. `--log-dest file` is mandatory — the default routes to
> journal and stdout looks silent. `navigate ams` does not exist. **It is read-write against
> the live machine**: navigate and screenshot freely, but any Load/Unload really moves
> filament.
>
> **Seed config caveat:** `/tmp/helix-seed/settings.json` is a completed-wizard config and
> lives in `/tmp` — it will not survive a reboot. Without it every run hits the first-run
> wizard plus an 8-step tour. If it is gone, run once without it, click through the wizard by
> hand, then copy the resulting `settings.json` back to `/tmp/helix-seed/`.


> **v2, 2026-08-09 — verified against live hardware** (U1 at `192.168.2.242`, Klipper
> `1.5.2.13_20260722102206`, multiACE `0.99.6.1b`). v1 of this plan was written from source
> reading alone and guessed the wrong root cause for the merged-toolhead drawing; §2 is the
> real one, read off the machine. Mockups: `2026-08-09-multiace-mockup.html`.
> ~~Captured payload: `tests/fixtures/snapmaker_u1/u1-afc-shim-and-multiace-idle.json`.~~
> **That file does not exist** — never committed, and not on disk anywhere (checked
> 2026-08-10). The live values quoted in §1 are the only surviving record of the capture.
> Re-capture from the machine if a fixture is needed:
> `curl -s '192.168.2.242:7125/printer/objects/query?ace&print_task_config&filament_feed%20left&filament_feed%20right'`.

---

## 0. TL;DR

1. **The merger is a topology-inference fallthrough, and it is fixed.** Your U1 runs a
   community *AFC compatibility shim* (`extended/klipper/afc.cfg`) that declares its four
   toolheads as four `AFC_lane`s. HelixScreen detects AFC, tries to infer whether the unit is
   a hub or four independent heads, finds nothing to count, and falls through to `HUB`. Four
   independent toolheads get drawn merged into one merger box. Fix in §2.
2. **Do not delete the shim.** It is the only reason your heads appear at all, and it bridges
   colour/material edits to the U1's native API. Removing it lands you on a *worse* path (§3).
3. **Three filament stacks coexist on your machine** — native Snapmaker, the AFC shim, and
   multiACE — and HelixScreen can only pick one. That is the real architectural problem, and
   it is what the phase plan is about.
4. **Your rig is head-mode with one ACE**, not multi-mode with several. That changes what the
   default view should optimise for (§4).

---

## 1. What is actually running on your printer

Three filament-management stacks are installed simultaneously. All three publish Klipper
objects; HelixScreen's detection picks exactly one.

| Stack | Objects | What it really knows |
|---|---|---|
| **Native Snapmaker** | `filament_detect`, `filament_feed left`/`right`, `print_task_config`, `extruder`…`extruder3` | The truth. Per-head filament type/vendor/colour, `channel_state`, RFID, the 32→4 `extruder_map_table`. |
| **AFC shim** (`extended/klipper/afc.cfg`) | `AFC`, `AFC_unit U1`, `AFC_lane E0…E3` | A hand-written adapter: 4 lanes → `extruder`/`extruder1`/`extruder2`/`extruder3`, each with its own `toolhead_sensor`, plus `SET_COLOR`/`SET_MATERIAL`/`SET_VENDOR` macros that forward to the native `SET_PRINT_FILAMENT_CONFIG`. No `[AFC_extruder]` sections, no hubs, no steppers. |
| **multiACE** | `ace`, `ace_bg_swap`, `ace_tipform` | 1 ACE Pro 2 (`protocol: v2`), `mode: head`, per-head source/feeder/manual maps, Spoolman binding. |

**Live state when captured:**

```
ace.mode          = "head"                     ace.device_count = 1
ace.head_feeder   = {0:true, 1:true, 2:true, 3:false}   ← heads 0-2 on stock feeders
ace.head_ace      = {0:0, 1:1, 2:2, 3:0}       ace.ace_heads = [3]   ← only head 3 is ACE-fed
ace.head_source   = {0:null, 1:null, 2:null, 3:{ace_index:0, slot:0}}
ace.aces[0]       = connected, 32 °C, 33 % RH, gate_status [1,0,0,0]

print_task_config.filament_type  = ["NONE","NONE","NONE","PETG"]
print_task_config.filament_vendor= ["NONE","NONE","NONE","Kingroon"]
filament_feed     = e0/e1/e2 "wait_insert", e3 "load_finish"
```

So: four independent heads, three on direct stock feeders, one fed from a single ACE Pro 2 —
and at capture time only head 3 was loaded (PETG Kingroon `#83AFFF`).

---

## 2. Why the four toolheads were drawn as one merger — root cause

`AmsBackendAfc::parse_afc_unit_object()` infers per-unit topology. The documented rule is
"1 extruder → HUB, N extruders (N == lane count) → PARALLEL". Three inputs feed it, and on
this machine **all three are blank**:

| Input | Real AFC hardware | Your shim |
|---|---|---|
| `AFC_unit U1.extruders[]` | one entry per `[AFC_extruder]` section | `[]` — the shim declares none |
| `AFC_unit U1.hubs[]` | one entry per `[AFC_hub]` | `[]` |
| `AFC_lane E*.hub` | `"direct"` or a hub name | **absent** — the key is not published |

With no lane carrying a `hub` field, `lane_hub_routing_` has no entry for any lane, so both
`has_direct` and `has_hub_routed` stay false. Every arm of the chain in
`ams_backend_afc.cpp:2903-2921` then fails in turn, and control reaches:

```cpp
} else {
    unit_info.topology = PathTopology::HUB;   // default
}
```

`PathTopology::HUB` makes `render_linear_hub()` draw entry lanes fanning into a hub box and
out to **one** nozzle. That is the merger you saw. Nothing about it was Snapmaker-specific —
any AFC shim over a real toolchanger hits it.

**The information was there the whole time.** Each lane publishes its own extruder:

```
AFC_lane E0.extruder = "extruder"    map = "T0"
AFC_lane E1.extruder = "extruder1"   map = "T1"
AFC_lane E2.extruder = "extruder2"   map = "T2"
AFC_lane E3.extruder = "extruder3"   map = "T3"
```

and the backend already parses it (`slot.extruder_name`, `ams_backend_afc.cpp:2513`) — it
just never fed topology inference.

### The second half: the canvas never saw the per-unit answer

Deriving the unit's extruders was necessary but **not sufficient**. Verified on hardware:
the log said `→ Parallel (Tool Changer)` while the panel still drew the merger.

`AmsState` publishes `path_topology_` — the subject the canvas observes — from the
**system-wide** `backend->get_topology()` (`ams_state.cpp:1385`), not from
`get_unit_topology()`. And `AmsBackendAfc::get_topology()` was a hardcoded
`return PathTopology::HUB;`. So the per-unit inference could be perfectly correct and
never reach the drawing.

That constant also fed `AmsBackend::slot_has_independent_path()`, which decides
load-vs-swap: every lane looked shared, forcing an unload-before-load that a machine with
four independent toolheads never needs. So this was a behavioural bug, not only a visual one.

The intended answer — also **not built**, see the banner below — was for `get_topology()` to
derive from the parsed units: their common value, `MIXED` when they disagree, `HUB` before any
unit has been seen (unchanged for a Box Turtle). Because `get_unit_topology()` calls it *while
holding* `mutex_`, the logic would live in a `topology_locked()` helper with `get_topology()`
as the locking wrapper; calling the public form from inside the lock would self-deadlock.

### ⚠️ The fix below was NEVER IMPLEMENTED (verified 2026-08-10)

Everything in this subsection describes an approach that was designed and then **not built**.
Verified against the tree, not inferred: `AmsBackendAfc::get_topology()` still returns a
hardcoded `PathTopology::HUB` (`ams_backend_afc.cpp:555-558`), `lane_extruder_` and
`topology_locked()` do not exist, `tests/unit/test_afc_shim_unit_topology.cpp` does not
exist, and `git diff main...HEAD` touches no `*afc*` file at all.

**What actually fixed the merger** was `a1a6c33da` (§3): detection stopped claiming the bare
`ace` object, so the U1 falls through to the **native Snapmaker backend**, which draws its
four heads correctly — AFC is never selected on this machine, so its HUB constant no longer
matters *here*. The render half was `01f77654c`'s `compute_slot_render_states()` change in
`ui_filament_path_topology.cpp`.

**So this is still open, and Phase 1's stated generalisation is not delivered:** any OTHER
AFC-shim-over-toolchanger machine — one without the U1's `filament_detect` signature to fall
through on — still hits the `HUB` fallthrough and still draws four heads as one merger. The
analysis above is sound and worth building; treat it as a design, not a record.

The unbuilt design was: `parse_afc_unit_object()` falls back to the unit's own lanes when the
unit reports no extruders — if every lane's extruder is known and they are **distinct**, that
count becomes the unit's extruder set, and the existing `extruders.size() > 1 → PARALLEL` arm
answers correctly. Four distinct extruders → four independent toolheads.

Deliberately conservative in three ways, because guessing PARALLEL on a genuine hub unit
de-merges a merger that physically exists:

- fires **only** when `unit.extruders` is empty — a real AFC install is untouched;
- requires **every** lane's extruder to be known, so a partial Moonraker delta cannot
  trigger it (the same hazard the hub-routing parse already guards against, #1229 defect 4);
- requires the extruders to be **distinct** — four lanes into one extruder stays a hub.

The files it *would* touch (none of these changes exist — this is the build list, not a
changelog):

| File | Change |
|---|---|
| `include/ams_backend_afc.h` | new `lane_extruder_` map (mirrors `lane_hub_routing_`); `topology_locked()` decl |
| `src/printer/ams_backend_afc.cpp` | populate `lane_extruder_` in the lane parse; derive-from-lanes fallback in `parse_afc_unit_object()`; `get_topology()` derives from units instead of returning a constant |
| `tests/unit/test_afc_shim_unit_topology.cpp` | 6 cases: U1 shim frame → PARALLEL (per-unit **and** system-wide); partial delta → unchanged; same-extruder lanes → HUB (both); unit-declared extruders → unchanged |

**What WAS verified on the live printer** (2026-08-09, `192.168.2.242`, run locally at
`-s tiny`): the panel draws four independent toolheads with T3 highlighted holding its PETG,
instead of four lanes fanning into a hub box and one nozzle. That is the native Snapmaker
backend doing it after the §3 fall-through — not AFC, and not the design above.

---

## 3. Detection precedence — why deleting the shim makes it worse

Three stacks, one winner. `printer_discovery.h:543` runs `if (has_mmu_) … else if
(has_snapmaker_) …`, and `has_mmu_` is set by the first MMU-ish object name seen.

| Scenario | HelixScreen picks | Result |
|---|---|---|
| Today (AFC + ace + native) | **AFC** | Four heads visible. Merged before the §2 fix; correct after it. |
| Delete `afc.cfg` | **ACE** — `printer_discovery.h:311` matches the bare name `ace` | **Broken.** `AmsBackendAce` requires a top-level `slots[]` array (`ams_backend_ace.cpp:972`). multiACE publishes `aces[].slots[]` instead — verified live, there is no top-level `slots` key. It falls through to a `/server/ace/*` REST bridge multiACE does not serve, 404s, and gives up. Empty panel. |
| Delete `afc.cfg` **and** multiACE | **Snapmaker** | The native 4-head backend, which is correct — but you have thrown away multiACE. |

So the shim is currently load-bearing, and the ACE misdetection is a real latent bug for
every multiACE user who does *not* have the shim installed. Both need fixing in HelixScreen;
neither is fixed by changing your printer.

**Fix for the ACE collision — ✅ shipped in `a1a6c33da`, but NOT by the mechanism proposed
here.** Disambiguating on shape is impossible at this point: detection runs off
`objects.list`, which carries **names only**, so no payload exists to inspect. Do not try to
rebuild it this way.

What shipped is name-based, in `finalize_ams_detection()` once the whole object list is
visible (`printer_discovery.h:556-580`). Either signal is sufficient:

```
ace + (ace_bg_swap | ace_tipform)  → multiACE   (the extras multiACE always ships)
ace + filament_detect              → multiACE   (U1 firmware signature; no Anycubic has it)
ace, neither marker                → AmsType::ACE (Anycubic, unchanged)
```

On a multiACE match it **leaves the object unclaimed** and falls through to the native
Snapmaker backend. `AmsType::MULTIACE = 9` exists in `ams_types.h` with its string, but
**nothing ever assigns it** — deliberately, "until `AmsBackendMultiAce` exists". Regression
tests both directions: `tests/unit/test_multiace_vs_anycubic_detection.cpp` (121 lines).

So Phase 2 step 1 is half done: the misdetection is fixed and tested; the affirmative
`MULTIACE` claim is the line to flip when the backend lands.

---

## 4. Phase plan

### Phase 1 — draw the U1 as four toolheads ✅ done for the U1 (§2, §3)

The immediate ask, delivered — but by routing the U1 to its native backend, **not** by the
§2 AFC fix, which was never built. So the stated generalisation to "any AFC-shim-over-
toolchanger machine" is **not** delivered: a shim machine without the U1 signature still
draws one merger. Needs no printer-side change either way.

### Phase 2 — multiACE detection and backend

1. **Detect it** (§3) — ✅ **done 2026-08-10.** `AmsType::MULTIACE` is now claimed outright.
   **Claiming the type is only half the wiring**, and the missing half is invisible to a
   type-only assertion: `has_mmu_` skips the `has_snapmaker_` fallback that populated
   `detected_ams_systems_`, and `AmsState` builds backends by iterating *that list*. With no
   MULTIACE arm the list came back empty, no backend was built, and the panel logged
   `navigate_to_ams_panel called with no backend` on a U1 that had worked seconds earlier.
   Caught on live hardware, now pinned by a test. Four more sites needed the type too:
   `is_tool_changer()`, `is_filament_system()`, the `AmsBackend::create` factory, and the
   discovery sequence's subscription block (which is where `ace` gets subscribed at all).
2. ✅ **done 2026-08-10.** `AmsBackendMultiAce` derives from `AmsBackendSnapmaker` as planned;
   the §7 risk was real but small — `NUM_TOOLS` and `validate_slot_index()` had to move from
   `private` to `protected`, nothing more. Three payload facts that no doc states and that
   cost a live debugging round each:
   - `head_ace` carries an index for **every** head (`{0:0,1:1,2:2,3:0}` while only head 3 is
     ACE-fed), so it cannot decide *whether* a head is ACE-fed. `head_feeder`/`head_manual`
     are the authority.
   - The per-head maps are keyed by **string** (`"0"`..`"3"`). An int-keyed lookup finds
     nothing and every head silently reads feeder-fed.
   - `slots[].color` is an `[r,g,b]` **array**, not a hex string.

   And one that cost the most: `handle_status_update` must unwrap `params[0]` exactly as the
   base does. Reaching for a `"status"` key instead compiles, passes every unit test that
   feeds the bare object, logs nothing, and leaves the backend behaving exactly like the plain
   Snapmaker one. There is now a test using the real `notify_status_update` wrapper.

   *Original plan text follows.* The U1's four heads stay
   unit 0 with all the hard-won native behaviour intact (the `channel_state` load latch,
   `is_stuck_motion_sensor_runout`, `prepare_for_resume`, the 5-step load model,
   `print_task_config` parsing). The subclass adds the `ace` subscription, units 1..N for the
   ACE hardware, `head_source[h]` → which unit+slot is seated at head *h*, and
   `ACE_SWAP_HEAD` dispatch for ACE-fed heads with fall-through to the inherited `FEED_AUTO`
   path for feeder heads. Not a fork — CLAUDE.md's "extend the near-fit helper" applies hard
   here, because the U1 half is subtle.
3. **Retire the shim's role.** Once the native backend covers everything, the AFC shim
   becomes redundant and you can delete it — but only then, and detection must prefer
   MULTIACE over AFC so the order stops mattering.

**Everything needed is on the WebSocket.** `MultiAce.get_status()` publishes `mode`,
`device_count`, `active_device`, `head_ace`, `head_feeder`, `head_manual`, `head_source`,
`swap_phase`, and full per-unit inventory. No dependency on multiACE's optional FastAPI
service, no second HTTP client, no auth story.

Control surface, all plain gcode: `ACE_SWAP_HEAD HEAD=h ACE=a [SLOT=s]`, `ACE_LOAD_HEAD` /
`ACE_UNLOAD_HEAD`, `ACE_SWITCH TARGET=n`, `ACE_SET_HEAD_ACE|FEEDER|MANUAL`, `ACE_BG_SWAP`,
`ACED__Dry_Start_0..3` / `ACED__Dry_Stop`.

### Auto-dry — `ACE_SET_AUTO_DRY`, the full surface

Read off **multiACE's own web UI on the machine**, not guessed and not probed:
`http://192.168.2.242/multiace/app.js` (`setAutoDry`, `_AUTO_DRY_RANGE`, `autoDryPairInvalid`)
plus the template in `.../multiace/`. That UI is the FastAPI service nginx mounts at
`/multiace/`; the local `multiACE/` checkout is **older than the installed firmware** and has
no auto-dry at all, so do not read it for this.

```
ACE_SET_AUTO_DRY ACE=<idx> [ENABLE=0|1] [TEMP=35..80] [RH_START=5..95]
                           [RH_END=1..94] [MASTER=<ace idx|-1>] [ADD_TIME=0..600]
```

- **Every field is independent.** The web UI sends one per edit. Arming must therefore send
  `ENABLE` *alone* — restating a threshold would silently overwrite one set elsewhere, and the
  setting persists.
- **`RH_END` must be strictly below `RH_START`.** The firmware rejects the pair otherwise
  (`autoDryPairInvalid` is `end >= start`), and the UI refuses to send it.
- **`TEMP` is not auto-dry-only** — it is the unit's one drying temperature, shared with the
  manual `ACE_DRY`. multiACE's own UI puts it in the manual row for that reason.
- **v2 vs v1 is the real split.** Only the ACE 2 Pro (`protocol: v2`) has a humidity sensor, so
  only it evaluates thresholds. A v1 unit FOLLOWS a v2 unit's cycle instead: `MASTER` +
  `ADD_TIME`, and it cannot be armed until a master is picked. `ace.auto_dry_masters` (live
  `[0]`) lists the units eligible to be one.

Live shape, confirmed against the machine and present in the committed fixture:
`ace.aces[i].auto_dry = {enabled, rh_start, rh_end, temp, master, add_time}`, plus the
**sibling** `ace.aces[i].auto_dry_running` — a separate key, not a field inside the block, so
it needs its own parse or a frame carrying only it is dropped.

Built 2026-08-11 as `AutoDryInfo` + `get_auto_dry_info()` / `set_auto_dry_enabled()` on
`AmsBackend`, overridden in `AmsBackendMultiAce`, with a `compact_toggle_row` in the stock
`ams_environment_overlay` gated on its own visibility subject. Deliberately a **toggle only** —
thresholds are displayed, not editable, keeping that upstream-stock panel close to stock.

**Closed, not open: how auto-dry and Start/Stop interact.** Per Gordian (2026-08-11), the
current behaviour is fine and is not to be changed. For the record, so nobody "fixes" it
again: while the rule is armed and humidity is above `rh_start` it will restart a cycle that
Stop has just ended; the panel's Temp box is a local value while the rule uses its own
persisted `auto_dry.temp`; and the countdown is drawn from `duration`/`remain_time`, which do
not govern a humidity-ended cycle (`auto_dry_running` is what distinguishes one). All known,
all accepted.

### Binding a spool used to pin a material forever (2026-08-12)

The SnapSwap panel showed materials the printer disagreed with — a head reporting PLA read
PETG, an empty head read PLA — and it survived every restart. Four links, each of which had to
be broken:

1. Binding a Spoolman spool routes through `apply_spool_to_slot()`, which writes the **spool's**
   material into `SlotInfo`.
2. `set_slot_info(persist=true)` then stamped `user_locked_material = !material.empty()`, so
   **linking a spool manufactured a material lock** the user never asked for.
3. That override persists to **Moonraker's `lane_data` namespace on the printer**, not just to
   the local config dir.
4. `apply_overrides()` applied material unconditionally, never consulting the lock, so it
   replayed over `print_task_config` on every parse.

**A clean local config dir does NOT clear this** — link 3 is why. A "clean config" run that
still shows the wrong material is not evidence the override store is innocent; check
`curl '<printer>:7125/server/database/item?namespace=lane_data'` before concluding anything.
That mistake cost several rounds here.

Fixed by making `apply_overrides()` lock-aware (firmware's material wins unless the user
genuinely locked one — ACE bays are unaffected because firmware states nothing for them), and
by comparing against `last_firmware_material_` when stamping the lock, so a bind that agrees
with firmware locks nothing. Mirrors AD5X's `last_firmware_color_` guard (#965).

**How it was found:** four temporary `spdlog::info` probes — one at each writer of
`slot->material` (ptc / RFID / override) and one where `AmsState` publishes the subject, each
logging *old → new*. One run named the overwriter outright. Worth repeating for any
"where does this value come from" question; inference had been wrong twice by then.

### A bay's spool identity is in the TABLE, not in `slots[]` (2026-08-11)

`aces[].slots[]` carries `material`, `brand`, `color` — and on real hardware they are all
**empty**, because those fields are filled from RFID only. Everything a user types into
multiACE's web UI lands in a spool table instead:

```
ace.spool_binding = {"0_0": "15", "0_1": "10", "0_3": "16"}   # "<ace>_<slot>" -> spool id
ace.spools        = { "15": {material, vendor, color, label, weight_g, sku, spoolman_id}, ... }
```

Reading only `slots[]` meant the ACE panel showed none of it, falling back to HelixScreen's
own override store — so the panel disagreed with the machine entirely. Four traps, each
pinned by a test in `[spools]`:

- **`spoolman_id` is a STRING here**, an int everywhere else in this codebase.
- **`color` is bare hex with no `#`**, while `slots[].color` is an `[r,g,b]` array.
- **Unbinding DELETES the key** rather than nulling it, so `spool_binding` must be replaced
  wholesale — merging strands a removed spool forever.
- **The table and the bindings are INDEPENDENT deltas.** Moving a spool between bays sends
  `spool_binding` with no `spools`, so they must be cached separately; resolving them in one
  pass threw every description away on the next binding change.

multiACE's table deliberately **outranks** the local override store for a bay it has a binding
for — the same doctrine `slot_identity_owner_unit()` states — or a stale local edit keeps
masking what the user typed. And `SlotInfo` objects are reused across rebuilds (only a change
in unit COUNT reallocates them), so `spool_name`/`brand`/`spoolman_id`/`remaining_weight_g`
must be cleared each pass or a bay keeps the name of the spool taken out of it.

**There are THREE identity layers, and only two are on the WebSocket.** In precedence order,
lowest first:

| Layer | Where | Covers |
|---|---|---|
| `aces[].slots[]` | `ace` object | RFID only — **empty** for every hand-entered spool |
| `spools` + `spool_binding` | `ace` object | bays with a spool bound; carries `spoolman_id` |
| `slot_overrides.json` | **a FILE** | material/brand/subtype/colour, incl. bays with NO spool bound |

multiACE's own web UI resolves from the top one — every bay it reports comes back
`source: "override"`. The file is **not published in the `ace` object at all**, which is why a
bay carrying a material and colour but no spool binding read as empty here.

It is reachable through Moonraker's file API at
`config/extended/multiace/slot_overrides.json`, so this needs nothing beyond multiACE — the
optional FastAPI service stays unnecessary, as § 4 intended. Nothing in the `ace` object can
say the file changed, so the fetch is triggered off `event_seq` (multiACE's
bump-on-any-state-change counter) with the first frame fetching it at all.

**Colour is encoded three different ways across these layers** — `"#RRGGBB"` in the override
file, bare `"RRGGBB"` in the spool table, `[r,g,b]` in `slots[]`. All three are parsed.

**Weights come from SPOOLMAN, not multiACE.** Its `weight_g` is a local copy, so taking
remaining from it while total came from Spoolman computed a percentage across two sources.
Only `spoolman_id` is taken from the table; `tracks_weight_locally()` stays false so
SpoolmanManager fills remaining AND total as one pair. Both are cleared each rebuild —
clearing only remaining left a stale total behind a wrong percentage.

**Still open — the SnapSwap side.** `helix-screen/tool_spool_assignments` in Moonraker's DB
holds assignments for all four heads (0=Red/23, 1=SIlver/15, 2=Black/3, 3=Gray/10) while
`print_task_config.filament_exist` reads `[true,false,false,true]`, so T1/T2 display filament
that is not there — and spool 15 is claimed by both tool 1 and ACE bay 0. This is § 10's open
item 3. Two separable questions: clearing the stale rows (a write to the user's DB), and
whether an assignment should display at all once the head reports empty — note
`slot_has_retained_identity()` is a deliberate feature, so today's behaviour may be intended.

### Spool numbering — the label is not the index (2026-08-11)

The ACE's bays were badged **5-8** for a rig with seven spools. Global slot indices are dense
over every *addressable* slot — the U1's four heads take 0-3, so the ACE starts at 4 — while
the badge should count *spools*, and the ACE-fed head and the bay behind it are one spool
counted twice.

`owned_spool_slots()` already existed and already answered this (`[0,1,2,4,5,6,7]`); nothing
labelled from it. Added `AmsBackend::spool_display_number()` plus
`slot_identity_owner_slot()` — the companion to the existing `slot_identity_owner_unit()`,
resolving a viewing slot to the slot that actually holds the spool. The U1's heads now read
1,2,3 and the ACE's bays 4,5,6,7; T3 shares **4** with the bay feeding it rather than
consuming a number.

**Do not "fix" this by changing `first_slot_global_index`.** The index is the addressing key —
subjects, `get_slot_info()`, load/unload dispatch and the active/target comparison all use it,
and two slots cannot share one. Presentation and addressing are separate on purpose. Backends
whose slots all own their spools are unaffected: `owned_spool_slots()` is then every slot in
order and the number is `slot_index + 1` exactly.

### Numeric keyboard — the default one existed and was dead (2026-08-11)

`keyboard_hint="numeric"` routed to the `?123` symbol page. The intended numpad
(`kb_map_num_improved`) was registered against LVGL's `LV_KEYBOARD_MODE_NUMBER` and **never
displayed** — nothing calls `lv_keyboard_set_mode()` with that mode, because KeyboardManager
drives the button matrix directly. Two things had therefore never been exercised:

- **`LV_KEYBOARD_CTRL_BUTTON_FLAGS` does not include `CUSTOM_1`**, which is this codebase's
  non-printing marker. Every action key on that map would have inserted its raw icon bytes.
- **Four keys had no handler at all** (`+/-`, `ICON_CHECK`, both chevrons).

Now lives in `keyboard_layout_provider.cpp` as `KEYBOARD_LAYOUT_NUMERIC` with both fixed.

**Trap worth keeping:** `LV_BUTTONMATRIX_WIDTH_MASK` is `0x000F` — a key width above **15**
overflows into the flag bits and silently drops keys from the rendered row rather than failing.
A first attempt used widths of 16 and 20 and lost a whole row. Pinned by a test.

### Phase 3 — head-major layout

With Phase 1, four heads draw as four columns. With multiACE, each head also has a **stack of
candidate sources** behind it (up to 4 ACE slots + stock feeder + manual). Group every slot in
the system by `mapped_tool` and render the stack under its head. Pure regroup of data the
model already holds — no new topology enum — and it also fixes the existing
`HELIX_MOCK_AMS=mixed` scenario (12 slots → 6 toolheads, drawn today as three disconnected
unit cards).

Your rig makes the *asymmetric* case the default: three feeder heads with exactly one source
each, one ACE-fed head with four. The layout has to look right when most columns have a stack
of one — see the mockup.

### Phase 4 — logical tools and >4 colours

`extruder_map_table[32]` maps logical `T0..T31` → physical `0..3`. Full API in
`SNAPMAKER_U1_PRINT_TASK_CONFIG.md`. HelixScreen should show the plan and the swap bill before
the print, then send `SET_PRINT_EXTRUDER_MAP` per remap and `SET_PRINT_USED_EXTRUDERS
EXTRUDERS=<csv>` before start — the latter also fixes the standing empty-head false-runout on
a bare U1. Leave `ACE_SWAP_HEAD` emission to multiACE's post-processor; display and validate,
don't rewrite gcode.

**Open question:** map project colours to **heads** or directly to **(ACE, slot)**? Orca has
the same question open; both surfaces should answer it the same way.

### Phase 5 — the rest

Per-ACE dryer (`get_dryer_info(unit)` already takes a unit index), humidity/temp per unit,
mode switching with a confirmation step, saved loadouts, `swap_phase`/`last_swap_result`
routed through `classify_error()` so a failed swap becomes an actionable card, and Spoolman
binding (your `ace.spool_binding` is already populated).

---

## 5. Testing locally

`HELIX_MOCK_AMS=u1` already exists (undocumented — `ams_backend.cpp:177`). Phase 2 adds
`HELIX_MOCK_AMS=multiace` parameterised to reproduce your rig and the ones you don't have:

```bash
HELIX_MOCK_AMS=multiace HELIX_MOCK_ACE_COUNT=1 HELIX_MOCK_ACE_MODE=head \
  HELIX_MOCK_ACE_FEEDER=0,1,2 ./build/bin/helix-screen --test -vv    # your machine
HELIX_MOCK_ACE_COUNT=3 HELIX_MOCK_ACE_MODE=multi                     # the 3-ACE case
HELIX_MOCK_ACE_SWAP=3                                                # mid-swap, step bar live
```

Against the real printer, with an isolated socket and config dir so it cannot collide with
another instance:

```bash
export HELIX_SOCK=/tmp/helix-u1.sock HELIX_CONFIG_DIR=/tmp/helix-config-u1
mkdir -p "$HELIX_CONFIG_DIR"
./build/bin/helix-screen --moonraker 192.168.2.242:7125 -vv --remote-socket "$HELIX_SOCK" &
./build/bin/helix-screen ctl -s "$HELIX_SOCK" navigate ams
./build/bin/helix-screen ctl -s "$HELIX_SOCK" screenshot /tmp/u1-ams.png
```

XML is loaded at runtime, so layout iteration needs no rebuild: set `HELIX_HOT_RELOAD=1`, edit
`ui_xml/*.xml`, and the running panel rebuilds within ~500 ms.

---

## 6. Display budget — 480 × 320

The U1's panel is 480×320 (3.5", TLSC6x touch, DRM/KMS), HelixScreen's **TINY** tier and the
smallest resolution it supports. `docs/devel/480x320_UI_AUDIT.md` already lists the filament
panel as having cards pushed off-screen at this size, before any of this work.

Real budget, measured from `ams_panel.xml` and the tokens:

| Region | Cost |
|---|---|
| Nav rail | 42–56 px of the 480 |
| Panel header | 34 px of the 320 |
| Usable content | **~424 × 278 px** |
| Per head, 4 across | **~103 px wide** |

103 px is about six characters of body text — enough for one head with one source, not enough
for a head with four.

**The resolution: two layouts, both fitting 480×320, chosen by the backend.**

| Layout | Per head | Right when |
|---|---|---|
| **Columns** (4 across) | 103 px | Each head has a single source — the stock U1, and your rig today. Keeps the spatial mapping to the four physical heads. |
| **Rows** (4 stacked) | 406 px | A head has somewhere else to go. Fits the live source, its state and a count chip with no truncation; four rows still leave ~60 px of vertical slack. |

Swapping the axis is a better answer than paging (which is how the U1's own stock UI dodges
the problem) because it keeps all four heads on screen at once — the thing the merger bug was
hiding in the first place. Both forms are mocked up at 1:1 in the companion HTML, which
measures each screen in the browser on load and labels it `fits` or `overflows` rather than
asking you to take the numbers on trust.

---

## 7. Open risks

- **The shim's `map` field is `T0..T3`.** The AFC docs warn that `map` is a *virtual* tool
  number, not a physical one, and must not be used to count nozzles. Here it happens to agree
  with the physical head. Do not build on that agreement — use topology, as the fix does.
- **`mode: head` vs `multi` changes the slot→head mapping.** In multi mode slot *s* of every
  ACE feeds head *s*; in head mode an ACE binds to one head and all four of its slots feed
  *that* head — a hub, not a parallel fan. So per-unit topology genuinely differs by mode and
  `get_unit_topology()` has to answer dynamically. Your machine is in head mode, so this is
  the shape to build first.
- **Three stacks, one winner** stays true until Phase 2 lands. Any change to what is installed
  on the printer silently changes which backend HelixScreen picks.
- **`AmsBackendMultiAce : AmsBackendSnapmaker`** means the U1 backend gains a subclass its
  `protected` surface was not designed for. Expect a small refactor and re-run the U1
  regression tests hard.

---

## 10. Handoff — open items (2026-08-10)

Everything below is on `feat/snapmaker-multiace`, nothing pushed.

### 10.1 Failing unit tests — ✅ fixed (2026-08-10)

**It was 6 failures, not 3.** `[snapmaker]` showed 3; the other three live in
`test_ams_realtime_filament_state.cpp` under tags that do not include `[snapmaker]`, so only
`"[ams]"` sees the whole family. Now `[ams]` = **1656 cases, 0 failed** (was 1653 / 6).

| Test | Line |
|---|---|
| `can_unload_from_toolhead offers unload for every loaded toolhead` | `test_ams_backend_snapmaker.cpp:497` |
| `motion-sensor runout path is independent of the loaded latch` | `test_ams_backend_snapmaker.cpp:1067` |
| `overrides slot LIVE accessors from sensor + LOADED status` | `test_ams_realtime_filament_state.cpp:113` |
| `AmsState publishes per-slot LIVE subjects on sync` | `test_ams_realtime_filament_state.cpp:186` |
| `Active-loaded subject is the single highlight source on unload` | `test_ams_realtime_filament_state.cpp:314` |
| `AMS clears filament_loaded after unload completes` | `test_ams_realtime_filament_state.cpp:483` |

The diagnosis held: all six encode "mounted + spool present ⇒ LOADED", a weaker and more
defensible claim than the mounted+empty conflation `70ce3345b` actually removed.

**The candidate fix recorded here was wrong** — it would have fixed one of the six. Four of
them assert `get_slot_info(i).status`, a **stored** field that `filament_present_at_tool_locked()`
does not feed; adding a term to that predicate leaves the status untouched. The real fix is
three parts:

1. **The third presence term** (as proposed) in `filament_present_at_tool_locked()`. Needed
   because `port_sensor_filament_present_` starts false and stays false until a
   `filament_feed` frame names the tool, so a machine publishing only `print_task_config`
   answers "nothing loaded" on the two sensor terms alone.
2. **Recompute the mounted slot's `status` per frame**, next to the `filament_loaded`
   recompute. It was written *only* by the election, which fires on `active != current_tool`,
   so a load completing while the tool stayed mounted — the normal case — left the slot
   reading `AVAILABLE` indefinitely. Same staleness `5174f0f91` fixed for `filament_loaded`,
   left behind on the status.
3. **Fold the motion-sensor runout clear into that recompute.** It was its own block *above*
   the recompute, which then put `filament_loaded` straight back on the strength of the loaded
   latch. Latent since `5174f0f91` and invisible because the test that covers it aborted on an
   earlier `REQUIRE`. The runout gates `filament_loaded` only, never
   `filament_present_at_tool_locked()` — after a runout the canvas must break the line to the
   nozzle while Unload stays offered.

Three regression tests added, each verified by mutation (revert the fix → the test goes red):
mounted-but-EMPTY is not loaded (the `70ce3345b` case, which shipped with no test at all),
a load completing without a tool change promotes the slot, and runout clears `filament_loaded`
while keeping Unload.

### 10.2 ACE-fed head: unload never terminates in the UI — ✅ addressed 2026-08-10

The hypothesis below was right and is now implemented: `AmsBackendMultiAce::do_unload_filament`
sends `ACE_UNLOAD_HEAD HEAD=n` for a head the ACE feeds, and leaves feeder heads on the
inherited native path. **Not yet re-observed on hardware** — the fix is unit-tested against the
live payload, but nobody has run an unload on T3 since. Confirm before closing.

The second defect (the 5-step LOAD model rendered during an unload) is untouched and still
open; it lives in the step-model selection, not the dispatch.

*Original analysis:*

Live repro on T3 (the only ACE-fed head): Unload from the multi-filament panel **succeeds on
the printer** — `channel_state` reaches `preload_finish`, the toolhead motion sensor drops to
false, `print_stats` stays `standby` — but the panel stays on "Unloading" forever.

Two defects, probably one cause:

1. The step list rendered during the unload is the **5-step LOAD model**
   (Home/Select/Heat nozzle/Feed filament/Purge), not the 4-step unload model.
2. The operation never terminates, even though `preload_finish` is marked
   `is_terminal` in the channel-state table.

Hypothesis: HelixScreen dispatches the U1's native unload and waits for `unload_finish`, but
an ACE-fed head terminates at `preload_finish` because the ACE performs the retract. This is
the first concrete case of the Phase 2 rule — **an ACE-fed head must be driven with
`ACE_UNLOAD_HEAD HEAD=n`, not the native path** — and it will not be fixed properly until the
backend knows which heads the ACE feeds (`ace.head_ace` / `ace.head_feeder`).

### 10.3 Toolhead context menu — ✅ built and hardware-verified (2026-08-10)

Built as designed: a second canvas callback (`ui_filament_path_canvas_set_toolhead_callback`)
plus `AmsToolheadMenu`, modelled on `AmsSelectorMenu`. Unregistered, both canvas regions still
go to `slot_callback`, so no other panel changes behaviour.

- **Select** — `select_slot()`, which already emits `T{n}` on this backend. No new gcode.
- **Park** — new `AmsBackend::park_toolhead()` + `supports_toolhead_park()`, Snapmaker sends
  `PARK_EXTRUDER`. **Confirmed on the live machine**, since it is absent from `gcode/help`:
  `configfile.settings['gcode_macro print_end']` calls it bare and parameterless, right after
  `SM_PRINT_END_AUTO_UNLOAD_FILAMENT`. It is a carriage op — a docked head keeps its filament,
  so Park must never unload.
- **Load / Unload** — mutually exclusive on `can_unload_from_toolhead()`.

The rule is a pure function (`toolhead_menu_model()`), tested without LVGL in
`test_ams_toolhead_menu_model.cpp`. Entry visibility is published as subjects and bound with
`<bind_flag_if_eq>`; hiding from C++ would have pushed the imperative-UI ratchet above its 384
baseline. A head with no applicable action shows no menu rather than an empty card.

**Verified against the U1** (T3 mounted, T0–T2 parked): tapping T3 offered **Park + Load**,
tapping T0 offered **Select T0** alone. Load rather than Unload on T3 is correct and worth
keeping in mind — `filament_exist[3]` is true but `channel_state` is `preload_finish`, so the
PETG is staged in the channel and not at the nozzle. `retraction_seen_` catches exactly that,
and the sidebar agrees ("Currently Loaded: ---", Unload greyed).

The canvas hit-test only reaches this on PARALLEL topology, and `ctl click` cannot reach it at
all — it sends a widget event with no coordinates. Drive it with the synthetic pointer
(`ctl press <x> <y>` / `ctl release`); nozzles sit at `canvas.y + canvas.h * 0.55`.

### 10.4 Misleading wording in existing tests

Several Snapmaker tests and comments say filament is retracted "to the buffer". The U1 has no
buffer — that term is borrowed from AFC's TurtleNeck. The U1's own vocabulary is **preload**
(`preloading` / `preload_finish`). Worth a comment-only pass so the next reader is not sent
looking for hardware that does not exist.

### 10.5 From the code review (2026-08-14) — A/B done, C/D/E open

A four-angle review (reuse / simplification / efficiency / altitude) of the whole branch. The
mechanical findings are fixed in `e0f22f471`; the items below were held back because they change
architecture rather than tidy it. **The two latent bugs (A, B) are fixed in `23a0cc95c`
(2026-08-15). C, D and E remain open** — they alter behaviour rather than close a hole, so they
want doing deliberately rather than folded into a cleanup.

**A. `park_toolhead()` was outside the NVI gate — ✅ fixed 2026-08-15 (`23a0cc95c`).**
`Park` joined `FilamentOp`; `park_toolhead()` is `final` on `AmsSubscriptionBackend` and routes
through `run_filament_op()`; backends implement only the protected `do_park_toolhead()` hook,
whose default refuses. Snapmaker's hand-written gate is deleted. Mutating the gate away
reproduces the original bug and `[park]` catches it, asserting no gcode leaks on refusal.
Original diagnosis retained below.

**A (original).** `park_toolhead()` is outside the NVI gate — latent, ordered first.
`load_filament` / `unload_filament` / `select_slot` / `change_tool` are `final` on
`AmsSubscriptionBackend` precisely so a backend *cannot* forget the print-active gate; that
class's own comment records that opt-in gating already shipped one backend with no gate at all
(`329e731e9` added it to seven and missed the eighth). `park_toolhead()` (`ams_backend.h`) is a
plain virtual whose only enforcement is a `@warning` telling each implementer to hand-write
`check_preconditions(true)`, and it skips the `FilamentOpClaim` test-and-set, so a park can
dispatch while a load is in flight. Latent only because `supports_toolhead_park()` is true on
exactly one backend today — the second one to implement it docks the head mid-print.
*Fix:* add `Park` to `FilamentOp`, make `park_toolhead()` `final` on `AmsSubscriptionBackend`
dispatching to a protected `do_park_toolhead()`. Snapmaker then drops its hand-written gate.

**B. The toolhead menu keyed slot-indexed APIs with a VIRTUAL tool number — ✅ fixed 2026-08-15
(`23a0cc95c`).** Resolved once via `mapped_tool` in both `show_at()` and the shared dispatch,
falling back to the raw index for a backend publishing no mapping. Worse than first described:
`can_unload_from_toolhead(int slot_index)` is slot-indexed *despite its name*, and the dispatch
was passing the tool number to three further slot-indexed calls — so every backend call in the
menu took a slot while receiving a tool. `[tool_index]` pins it with a deliberately remapped
machine alongside the U1's identity case. Original diagnosis retained below.

**B (original).** The toolhead menu keys slot-indexed APIs with a VIRTUAL tool number — latent.
`ui_system_path_canvas.h` documents the callback argument as "the VIRTUAL tool number shown on
the badge, not the physical column". `AmsToolheadMenu::show_at()` passes it unconverted to
`get_slot_info()`, `can_unload_from_toolhead()`, `select_slot()`, `load_filament()` and
`unload_filament()` — all slot-indexed. It works only because tool == slot on the U1;
`ams_backend.h` records that toolchanger tool numbers diverge from slots under `ASSIGN_TOOL`
remapping, and the overview registers this callback for every backend. (The missing bounds
check is already restored in `e0f22f471`; the index is still the wrong *kind* of index.)
*Fix:* resolve tool → slot once via `tool_layout.virtual_to_physical` + `mapped_tool` at the
top of `show_at()`.

**C. OPEN — the toolhead menu bypasses `plan_load()`.**
The sidebar, filament panel, runout handler and print-status widget all funnel through
`plan_load()` / `BackendCaps`. This menu calls `load_filament()` / `unload_filament()` /
`select_slot()` directly, so it gets none of `requires_slot_selection_for_load`,
`needs_unload_before_load`, the already-mounted refusal, the preheat flow, the step bar — or
`change_tool_completes_load`, the capability this very branch added to that planner. Related:
its print-blocks gate is a fifth copy of the same preamble and omits the `is_busy()` term the
per-slot menu carries.

**D. OPEN — `change_tool_completes_load()` should be derived, not declared.**
The planner arm it guards substitutes `change_tool(mapped_tool)` for "load slot N", which is
valid exactly when the tool number *identifies* the slot. On an ACE in head mode four bays share
one `mapped_tool`, so it is ambiguous — and that is visible in the `AmsSystemInfo` `plan_load()`
already holds. Deriving it ("take this arm only when `target_slot` is the only slot with that
`mapped_tool`") deletes the virtual, the `BackendCaps` field and its three call sites, keeps
AFC/HH/CFS behaviour (they map lanes 1:1), and protects the next many-to-one backend for free.

**E. OPEN — also flagged, smaller.** `detect_step_operation()`'s guard fixes the UNLOAD direction only —
the same mid-operation transient resets the bar during a LOAD. `slot_identity_owner_unit()` and
`slot_identity_owner_slot()` are one concept split in two with an unenforced invariant. Bars and
badge text in `ui_ams_slot.cpp` are read once at widget construction, so they go stale on any
inventory change that does not alter the slot count. `override_refetch_wanted_` is armed on every
`event_seq` bump — confirm multiACE does not bump it for telemetry, or that is a continuous HTTP
loop.

---

---

## 11. Session log — 2026-08-10/11

25 commits. Grouped by what they were for, since the order they landed in is not the order
they make sense in.

### Phase 2 — the backend

| Commit | What |
|---|---|
| `2c568b983` | `AmsBackendMultiAce`, deriving from `AmsBackendSnapmaker`. Unit 0 stays the U1; units 1..N are the ACE. ACE-fed heads dispatch `ACE_LOAD_HEAD`/`ACE_UNLOAD_HEAD`. |
| `29c8e5a46` | Stop rebuilding the ACE units wholesale each frame — it discarded user edits and reset the view. |
| `910400da2` | **A partial `ace` frame no longer wipes head sources.** The delta bug; see the traps list. |
| `5053dd26b` | The dryer: `ACE_DRY` / `ACE_STOP_DRYING` + `get_dryer_info()`. |

Three payload facts no document states, all found by reading the live machine:

- `head_ace` carries an index for **every** head (`{0:0,1:1,2:2,3:0}` while only head 3 is
  ACE-fed), so it cannot decide *whether* a head is ACE-fed. `head_feeder`/`head_manual` can.
- The per-head maps are keyed by **string** (`"0"`..`"3"`). An int key silently finds nothing.
- `slots[].color` is an `[r,g,b]` **array**, not a hex string.

And one that cost the most: `handle_status_update` must unwrap `params[0]` exactly as the base
does. Reaching for a `"status"` key compiles, passes every unit test that feeds a bare object,
logs nothing, and leaves the backend behaving like the plain Snapmaker one.

### Detection

`AmsType::MULTIACE` needed **five** sites, not one. `has_mmu_` skips the `has_snapmaker_`
fallback that populates `detected_ams_systems_` — the list `AmsState` iterates to build
backends — so claiming the type alone produced NO backend and an empty panel. The others:
`is_tool_changer()`, `is_filament_system()`, the `AmsBackend::create` factory, and the
subscription block (which is where `ace` gets subscribed at all).

### UI

| Commit | What |
|---|---|
| `6f8fcbac0` | Recompute the mounted slot's load state every frame (§10.1's real fix). |
| `024ac964f`, `1c5a1180d` | Per-toolhead context menu, on both the detail canvas and the overview's nozzle row. |
| `b83cf6bbd` | An ACE-fed head's spool identity belongs to the ACE: the slot menu drops Edit/Spoolman/Clear and offers "Open in ACE". |
| `6366ee716`, `e74823a48` | One nozzle per head, and a unit draws lines only to heads it feeds. |
| `7c0cb0b0e`, `4925782d1` | Count spools rather than slots (7, not 8); the home widget's bar cap is per row. |
| `64dc47738`, `d6e0aa7d7` | No hub box on a shared toolhead; dim heads that are not active; outline an externally-fed slot. |
| `5fc8c6024` | A unit feeding one known head draws through to that toolhead. |
| `65ee32bf9`, `a02bd6fab`, `5122216e5` | Spool assignment on ACE bays; assigned-but-empty lanes show their colour; an empty bay cannot claim a tool's spool. |
| `be61ed894` | The environment overlay reads the unit it is showing (10 lines; upstream's bug). |

### What was verified how

Everything above was checked against the live U1 at `192.168.2.242`, not just unit tests —
mostly by driving the running app with `ctl` and reading screenshots. Two findings came from
**sampling rendered pixels** rather than eyeballing: the assigned-but-empty lane really drew
zero coloured pixels (not a dim tint), and the fix produced 70. When a screenshot and a theory
disagree, decode the PNG.

