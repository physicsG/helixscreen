# Snapmaker U1 + multiACE — support plan

> **v2, 2026-08-09 — verified against live hardware** (U1 at `192.168.2.242`, Klipper
> `1.5.2.13_20260722102206`, multiACE `0.99.6.1b`). v1 of this plan was written from source
> reading alone and guessed the wrong root cause for the merged-toolhead drawing; §2 is the
> real one, read off the machine. Mockups: `2026-08-09-multiace-mockup.html`.
> Captured payload: `tests/fixtures/snapmaker_u1/u1-afc-shim-and-multiace-idle.json`.

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

`get_topology()` now derives from the parsed units — their common value, `MIXED` when they
disagree, `HUB` before any unit has been seen (unchanged for a Box Turtle). Because
`get_unit_topology()` calls it *while holding* `mutex_`, the logic lives in a new
`topology_locked()` helper and `get_topology()` is the locking wrapper; calling the public
form from inside the lock would self-deadlock.

### The fix (implemented)

`parse_afc_unit_object()` now falls back to the unit's own lanes when the unit reports no
extruders: if every lane's extruder is known and they are **distinct**, that count becomes
the unit's extruder set, and the existing `extruders.size() > 1 → PARALLEL` arm answers
correctly. Four distinct extruders → four independent toolheads.

Deliberately conservative in three ways, because guessing PARALLEL on a genuine hub unit
de-merges a merger that physically exists:

- fires **only** when `unit.extruders` is empty — a real AFC install is untouched;
- requires **every** lane's extruder to be known, so a partial Moonraker delta cannot
  trigger it (the same hazard the hub-routing parse already guards against, #1229 defect 4);
- requires the extruders to be **distinct** — four lanes into one extruder stays a hub.

| File | Change |
|---|---|
| `include/ams_backend_afc.h` | new `lane_extruder_` map (mirrors `lane_hub_routing_`); `topology_locked()` decl |
| `src/printer/ams_backend_afc.cpp` | populate `lane_extruder_` in the lane parse; derive-from-lanes fallback in `parse_afc_unit_object()`; `get_topology()` derives from units instead of returning a constant |
| `tests/unit/test_afc_shim_unit_topology.cpp` | 6 cases: U1 shim frame → PARALLEL (per-unit **and** system-wide); partial delta → unchanged; same-extruder lanes → HUB (both); unit-declared extruders → unchanged |

**Verified on the live printer** (2026-08-09, `192.168.2.242`, run locally at `-s tiny`):
the panel now draws four independent toolheads with T3 highlighted holding its PETG,
instead of four lanes fanning into a hub box and one nozzle.

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

**Fix for the ACE collision** — disambiguate on shape, not name, using the predicate the ACE
backend already applies:

```
ace with top-level slots[]           → AmsType::ACE       (Anycubic, unchanged)
ace with aces[] and device_count     → AmsType::MULTIACE  (new)
```

---

## 4. Phase plan

### Phase 1 — draw the U1 as four toolheads ✅ done (§2)

The immediate ask. Fixes the merger for any AFC-shim-over-toolchanger machine, not just the
U1, and needs no printer-side change.

### Phase 2 — multiACE detection and backend

1. **Detect it** (§3) — `AmsType::MULTIACE`, plus regression tests for both directions using
   the captured fixture.
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
