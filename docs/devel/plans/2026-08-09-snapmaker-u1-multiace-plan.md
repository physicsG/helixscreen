# Snapmaker U1 + multiACE — support plan

> ## ▶ START HERE (new session, 2026-08-10)
>
> **Branch:** `feat/snapmaker-multiace` — 6 commits, **nothing pushed**. `main` is synced to
> upstream `c80f0be4e`.
>
> **State:** the U1 multi-filament panel is fixed and hardware-verified — four independent
> toolheads, correct mount election, mounted≠loaded, and the loaded card populated with
> Unload enabled. **Tests are green** (§10.1). The multiACE *backend* has not been started.
>
> **Read §2's ⚠️ banner and §3 before trusting this doc's "implemented" claims.** Two were
> wrong: §2's AFC topology fix was never built (the merger was fixed a different way), and
> §3's shape-based discriminator is impossible and was replaced by a name-based one that
> shipped. Both corrected in place on 2026-08-10.
>
> **Do these in order:**
> 1. ~~§10.1 — failing unit tests.~~ ✅ **done 2026-08-10.** It was **6** failures, not 3
>    (three sit outside the `[snapmaker]` tag — always run `"[ams]"`). The recorded candidate
>    fix was wrong; see §10.1 for what it actually took. `[ams]` is now 1656/0.
> 2. **§10.2 — ACE-fed unload leaves the UI stuck.** Needs the Phase 2 backend, not a patch.
> 3. **§10.3 — toolhead context menu.** Design settled, nothing built.
> 4. **Phase 2 proper** (§4) — `AmsBackendMultiAce`. Step 1 (detection) is half done.
>
> **Pre-existing failure, not yours:** the full suite (`./build/bin/helix-tests`, no filter)
> segfaults in `test_clock_widget.cpp:157` — "ClockWidget: timer fires during LVGL
> processing". Deterministic across seeds in the full run, passes under `"[clock_widget]"`
> alone, and **reproduces with this branch stashed**, so it is inherited from `main`. Full
> suite = 2757 cases, that 1 failure. Unrelated to filament work; do not chase it here.
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

1. **Detect it** (§3) — ✅ half done. Misdetection-as-Anycubic is fixed and covered by
   `test_multiace_vs_anycubic_detection.cpp`; `AmsType::MULTIACE` exists but is never
   assigned. Flip that when the backend lands.
2. **`AmsBackendMultiAce`, deriving from `AmsBackendSnapmaker`.** The U1's four heads stay
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

### 10.2 ACE-fed head: unload never terminates in the UI

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

### 10.3 Toolhead context menu — not started

Design settled, nothing built. The canvas already hit-tests the nozzle separately from the
spool (`ui_filament_path_canvas.cpp:174-199`); both regions currently call the same
`slot_callback`, so the work is a second callback plus a menu.

- **Select** — `T{n}`, shown when this head is not mounted
- **Park** — `PARK_EXTRUDER` (native, undescribed so absent from `gcode/help`; used bare in
  `PRINT_END`), shown only when this head IS mounted
- **Load / Unload** — per-head, gated on presence

### 10.4 Misleading wording in existing tests

Several Snapmaker tests and comments say filament is retracted "to the buffer". The U1 has no
buffer — that term is borrowed from AFC's TurtleNeck. The U1's own vocabulary is **preload**
(`preloading` / `preload_finish`). Worth a comment-only pass so the next reader is not sent
looking for hardware that does not exist.
