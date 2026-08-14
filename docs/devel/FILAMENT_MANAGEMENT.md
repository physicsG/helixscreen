# Filament Management (Developer Guide)

Multi-material system support in HelixScreen: architecture, backend implementations, mock testing, and extension guide.

**User-facing doc**: [docs/user/USER_GUIDE.md](../user/USER_GUIDE.md) (filament panel usage, slot operations, troubleshooting)

---

## Architecture Overview

HelixScreen uses a backend abstraction layer to support multiple multi-filament and multi-tool systems through a single UI. The `AmsBackend` interface hides all backend-specific protocols and exposes a uniform API for the UI layer.

```
                         ┌─────────────┐
                         │  AmsState   │  Singleton LVGL subject bridge
                         │ (ams_state) │  Thread-safe subject updates
                         └──────┬──────┘
                                │ owns backends_[] vector
              ┌─────────────────┼─────────────────┐
              ▼                 ▼                  ▼
       Backend 0 (primary)   Backend 1        Backend N
       flat slot subjects    BackendSlot-      BackendSlot-
       (backward compat)     Subjects          Subjects
              │                 │                  │
    ┌─────────▼─────────┐      │                  │
    │     AmsBackend     │  Abstract interface     │
    │  (ams_backend.h)   │  Factory: create() / create_mock()
    └─────────┬──────────┘                         │
     ┌────────┼─────────┬───────────┬──────────────┘
     ▼        ▼         ▼           ▼           ▼           ▼           ▼
  ┌────────┐ ┌────────┐ ┌────────┐ ┌──────────┐ ┌──────────┐ ┌────────┐ ┌──────────┐
  │Happy   │ │  AFC   │ │  ACE   │ │  Tool    │ │ AD5X IFS │ │  CFS   │ │  Mock    │
  │Hare    │ │Backend │ │Backend │ │ Changer  │ │ Backend  │ │Backend │ │ Backend  │
  └────────┘ └────────┘ └────────┘ └──────────┘ └──────────┘ └────────┘ └──────────┘
       │          │          │           │            │           │            │
  Moonraker  Moonraker   REST API   Moonraker   Moonraker  Moonraker    In-memory
  WebSocket  WebSocket   Polling    WebSocket   WebSocket  WebSocket    simulation

                         ┌─────────────┐
                         │  ToolState  │  Singleton: tool abstraction
                         │(tool_state) │  Maps tools ↔ AMS backends
                         └─────────────┘
```

### Key Files

| File | Purpose |
|------|---------|
| `include/ams_backend.h` | Abstract interface with factory methods |
| `include/ams_types.h` | Shared types: `AmsType`, `SlotInfo`, `AmsAction`, `PathTopology`, etc. |
| `include/ams_error.h` | Error types with user-friendly messages |
| `include/ams_state.h` | LVGL subject bridge (singleton) |
| `include/slot_registry.h` | SlotRegistry: single source of truth for per-slot state |
| `src/printer/slot_registry.cpp` | SlotRegistry implementation (name/index mapping, reorganize, tool map) |
| `include/ams_backend_happy_hare.h` | Happy Hare MMU implementation |
| `include/ams_backend_afc.h` | AFC (Armored Turtle / Box Turtle) implementation |
| `include/ams_backend_ace.h` | ACE (Anycubic ACE Pro) implementation |
| `include/ams_backend_toolchanger.h` | Physical tool changer (viesturz/klipper-toolchanger) |
| `include/ams_backend_ad5x_ifs.h` | FlashForge AD5X IFS (Intelligent Filament Switching) |
| `include/ams_backend_cfs.h` | Creality Filament System (K2 series, RS-485) |
| `include/ams_backend_mock.h` | Mock backend for development and testing |
| `src/printer/ams_backend.cpp` | Factory methods, plus the shared endless-spool validation / reset / eligibility base |
| `src/printer/ams_endless_spool.cpp` | Backend-agnostic endless-spool model: restriction text, group builders, the one group-to-edge projection ([§ Endless Spool](#endless-spool-shared-model)) |
| `include/printer_discovery.h` | Hardware detection from Klipper object list |
| `include/tool_state.h` | Tool abstraction: `ToolInfo`, `ToolState` singleton, tool-backend mapping |
| `include/printer_temperature_state.h` | `ExtruderInfo` struct, multi-extruder dynamic subjects |
| `include/ui_ams_context_menu.h` | Slot context menu (load, unload, edit, spoolman) |
| `include/ui_ams_device_operations_overlay.h` | Device operations overlay (home, recover, bypass, etc.) |

### Data Flow

1. **Discovery**: `PrinterDiscovery::parse_objects()` scans Klipper's `printer.objects.list` for `mmu`, `AFC`, `toolchanger`, `ace`, `AFC_stepper lane*`, `AFC_hub *`, `tool T*`, and `filament_switch_sensor _ifs_port_sensor_*` objects.
2. **Backend Creation**: `AmsState::init_backend_from_hardware()` calls `AmsBackend::create()` with the detected `AmsType` and Moonraker dependencies.
3. **Slot State**: Each backend stores per-slot state in its `SlotRegistry` instance (`slots_`), which provides indexed access, name lookup, and multi-unit reorganization. Moonraker status updates write to the registry under the backend's mutex.
4. **State Sync**: Backend emits events (`STATE_CHANGED`, `SLOT_CHANGED`, etc.) which `AmsState` translates to LVGL subject updates.
5. **UI Binding**: XML widgets bind to subjects (`ams_type`, `ams_action`, `current_slot`, `slots_version`, etc.) for reactive updates.

### SlotRegistry (Per-Slot State)

Each backend owns a `helix::printer::SlotRegistry` instance (`slots_`) that serves as the single source of truth for all per-slot indexed state. Before SlotRegistry, backends maintained parallel vectors (`lane_names_`, `lane_sensors_`, `gate_sensors_`, etc.) that had to be kept in sync manually -- a frequent source of index mismatch bugs.

**What SlotRegistry manages:**
- Slot names and bidirectional name-to-index lookup
- Per-slot sensor states (prep, load, loaded_to_hub, tool_loaded)
- Per-slot error and buffer health
- Per-slot filament weight tracking
- Tool-to-slot mapping
- Multi-unit reorganization (preserving slot data when unit topology changes)

**How backends use it:**

```cpp
// Initialize (once, during startup or first data arrival)
slots_.initialize("AFC Box Turtle", lane_names);   // AFC
slots_.initialize("Happy Hare MMU", gate_count);    // Happy Hare

// Read state
int idx = slots_.index_of("lane3");         // Name -> index
std::string name = slots_.name_of(2);       // Index -> name
const auto* entry = slots_.get(idx);        // Read-only access
auto info = slots_.build_slot_info(idx);    // Build SlotInfo for API

// Write state (under backend mutex)
auto* entry = slots_.get_mut(idx);
entry->sensors.prep = true;
entry->info.color_rgb = 0xFF0000;

// Multi-unit reorganization (AFC multi-unit topology changes)
slots_.reorganize(unit_lane_map);           // Preserves slot data across layout changes
```

**Key design decisions:**
- SlotRegistry does NOT hold a mutex -- the owning backend's mutex protects all access
- `build_slot_info()` constructs a `SlotInfo` snapshot, avoiding shared mutable state
- `reorganize()` takes an ordered vector of unit/lane pairs — caller controls unit ordering
- Slot names remain backend-specific ("lane1" for AFC, "Gate 0" for Happy Hare) -- SlotRegistry is agnostic

### Per-Slot Load Authority

Two `AmsBackend` predicates answer "is *this* slot loaded?" and everything the user can
tap about a slot is derived from them: the active-lane highlight, the Load/Unload gate on
the filament panel, and the context menu's Unload/Eject/Recover choice.

| Predicate | Question | Default |
|-----------|----------|---------|
| `slot_is_actively_loaded(i)` | Firmware considers this slot seated at the toolhead | see below |
| `slot_has_filament_at_toolhead(i)` | A per-slot toolhead sensor is tripped | `false` |
| `can_unload_from_toolhead(i)` | Offer Unload (and suppress Load) | `status == LOADED`, or `is_present()` on PARALLEL |
| `slot_unloads_to_toolhead(i, hint)` | The unload is a heated toolhead unload, not a cold eject | `hint` |

`slot_is_actively_loaded()` has **two** rules, chosen by
`has_per_slot_loaded_authority()` (default `false`):

- **`false`** — derive from the aggregate pair `get_current_slot() + is_filament_loaded()`.
- **`true`** — read the slot's own `SlotStatus::LOADED`.

The aggregate rule is only as good as our tracking of a single active-slot pointer. When
that pointer names the wrong slot or lags a toolchange, every affordance above inherits
the wrong answer — that was #1194, which surfaced as Load staying enabled on an AFC lane
the firmware had already seated (#1183) and as Recover being offered on a lane that only
reached the hub.

**Opting a backend in is not free.** The per-slot rule believes `get_slot_info(i).status`,
so a backend that never stamps `LOADED` on its seated slot would report *every* slot
unloaded and blank the active-lane highlight. Before flipping a backend to `true`, confirm
its parse sets `SlotStatus::LOADED` on the seated slot on **every** path that also sets the
aggregate, and add a test that fails if it stops.

| Backend | Authority | Basis |
|---------|-----------|-------|
| AFC | `true` | `AFC_stepper.<lane>.tool_loaded` (#1194) |
| Snapmaker | overrides outright | returns `status == LOADED` verbatim |
| AD5X IFS | `true` | firmware active-lane pointer + head sensor (#1199) |
| QIDI Box | `true` | `save_variables slot<N> == 2` (#1199) |
| CFS | `true` | `T{n}.filament` letter + toolhead switch (#1199) |
| ACE | `true` | arbitrated seated slot, stamped every parse path (#1199) |
| Happy Hare | `false` | `mmu.gate` / `mmu.filament` *are* the firmware truth |
| Toolchanger | `false` | `toolchanger.tool_number` *is* the firmware truth; no per-tool filament signal exists |

Every backend that opted in derives its stamp from the same inputs the aggregate pair is
assigned from, so the per-slot and aggregate rules cannot disagree. That is deliberate: it
makes "believing the per-slot status blanks the highlight" structurally impossible rather
than merely tested against. The value of opting in is not divergence-fixing but that
`can_unload_from_toolhead()` — which keys on `status == LOADED` for serial topologies —
finally reads true on a seated slot.

AFC's opt-in rests on `AFC_stepper.<lane>.tool_loaded`, which upstream's `set_loaded()` /
`set_unloaded()` assign in lockstep with `AFC.current_load` and
`AFC_extruder.lane_loaded`. Note that AFC's lane `status == "Loaded"` means *loaded to
hub*, not to the toolhead — only `tool_loaded` answers the toolhead question, which is why
`parse_afc_stepper` maps `"Loaded"` to `AVAILABLE`.

AD5X IFS derives its stamp from the same two inputs `system_info_.filament_loaded` is
assigned from, so the two cannot disagree — but only after dropping the lane's own port
sensor from the condition. A runout clears `port_presence_` while the filament that lane
fed is still at the toolhead (#995, the state `can_unload_from_toolhead()` keeps the unload
gate open for); requiring the port sensor demoted the lane to `EMPTY` at exactly the moment
the user needs to recover it.

QIDI Box is the opposite shape: `slot<N> == 2` is the Box's own per-slot statement and
needs no active-slot pointer, while the aggregate pair is written *only* from
`last_load_slot` — so a Box that never writes that variable reported nothing loaded at all.
`parse_save_variables()` reconciles the stamp against the aggregate at the end of every
pass, since the `slot<N>` loop runs before the `last_load_slot` block and would otherwise
demote the seated slot on a payload that repeated one without the other.

Happy Hare deliberately stays on the aggregate rule. `mmu.gate` and `mmu.filament` are
Happy Hare's own values parsed verbatim from one object, so the aggregate is already
firmware truth; `gate_status` carries fill state, not seating, so the per-gate stamp is
derived *from* the aggregate and believing it back would only add staleness. It would also
drop the highlight on a gate that ran out (`gate_status 0`) while its filament is still at
the toolhead. The stamp itself is re-derived on every `printer.mmu` frame
(`refresh_gate_statuses_locked()`) because `gate_status`, `gate` and `filament` arrive in
independent deltas — a toolchange typically carries the latter two alone.

Toolchanger stays on the aggregate rule too, and for a reason none of the others have: it
carries **no filament signal at all**. `get_slot_filament_segment()` returns `NOZZLE`
unconditionally, no per-tool switch is read, and `is_filament_loaded()` is nothing more than
`tool_number >= 0`. The only fact the parse can state is *which tool is on the carriage*,
which is single-valued — precisely what the aggregate pair encodes, assigned verbatim from
klipper-toolchanger's own `toolchanger.tool_number`. Being `PARALLEL` does not change that:
the topology describes independent filament paths, but this backend cannot see filament in
any of them, and its load/unload verbs are `SELECT_TOOL` / `UNSELECT_TOOL` — mount and
unmount, of which exactly one tool at a time is the subject.

`tool <name>.mounted` is emphatically *not* that authority. It arrives on a separate
Moonraker object from the one that writes the aggregate, and an all-tools-mounted payload is
a shape HelixScreen emits itself in mock mode (`moonraker_client_mock_objects.cpp` gives
every `tool T<n>` `mounted: true`). The parse used to write `mounted ? LOADED : AVAILABLE`
straight into `slot.status` from that object alone, so such a payload marked every tool
`LOADED` — the exact state that would make an opt-in report every tool as the active one.
`refresh_slot_statuses_locked()` now derives the stamp from the carriage tool on every parse
path, so the two writers cannot disagree; opting in afterwards would be safe but pointless,
since the stamp is derived *from* the aggregate.

The `PARALLEL` arm of `can_unload_from_toolhead()` is the part that did need fixing.
`is_present()` is true for every toolchanger slot forever — a slot here is a physical
toolhead, never `EMPTY` or `UNKNOWN` — so it read true everywhere. Through
`decide_can_load()`'s inverted `!toolhead_unload` factor that left **Load disabled on every
tool**, and through `decide_unload_mode()` it offered Unload (`UNSELECT_TOOL T=<n>`) on
tools parked in their docks. The backend overrides it with `slot_index == current_tool`.
`slot_unloads_to_toolhead()` stays on the base rule: an unmount *is* a toolhead operation,
and with no lane eject or lane recovery a docked tool correctly lands on
`UnloadMode::Unavailable`.

CFS earns it differently, and the difference is worth naming: its firmware publishes no
per-slot loaded flag at all. The seated bay is the intersection of two signals that arrive
on separate notifications — the per-unit `T{n}.filament` letter ("A".."D") naming the
engaged lane, and `filament_switch_sensor filament_sensor.filament_detected` at the
toolhead. `handle_status_update()` derives `SlotStatus::LOADED` from that pair at the end
of every frame, so the per-slot status can never disagree with the aggregate rather than
being independently authoritative. That still buys the real fix: before it, CFS wrote only
`AVAILABLE`/`EMPTY`, so `can_unload_from_toolhead()` — `status == LOADED` on a HUB
backend — was false on every CFS slot and the panel never offered Unload (#1199). The
stamp is applied even over a bay firmware calls `EMPTY`: a spool pulled while still
threaded leaves filament at the toolhead the user has to be able to unload. Removing it
restores the status the parse wrote, not a guessed `AVAILABLE`.

ACE derives it the same way, and its opt-in is a case study in *not* believing a firmware
string. The per-slot `"loaded"` token that its `slot_status_from_string()` maps to
`AVAILABLE` exists only in the community ValgACE dialect, where it sits in the same
enumeration as `"available"` and `"ready"` — the same slot-local trap as AFC's `"Loaded"`
meaning loaded-to-hub. Native Anycubic GoKlipper has no per-slot `"loaded"` at all; its
vocabulary is `empty`/`ready`/`preload`/`running`/`runout` and it answers the seated
question with the separate top-level `current_filament` (`"<unitId>-<localIndex>"`). So the
vocabulary map is left alone. Instead `apply_seated_slot_stamp_locked()` stamps whichever
slot the parse *arbitrated* to — from the ValgACE `"loaded"` scan, `loaded_slot`, or
`current_filament`, in that precedence — and a HUB backend has exactly one. The REST
fallback needs both ends of the stamp because `/status` owns `loaded_slot` while `/slots`
owns the slot vector: without it, each `/slots` poll would demote the seated slot and
report a spurious change every 500 ms.

`slot_has_filament_at_toolhead()` stays at its `false` default unless the sensor genuinely
exists *and* is attributable to one slot. AFC's `AFC_extruder` carries
`tool_start_status` / `tool_end_status` plus the `lane_loaded` that owns them; a trip with
no owning lane reads `false` rather than being blamed on an arbitrary lane.

### Threading Model

All Moonraker/libhv callbacks arrive on a background thread. Backends update internal state under mutex, then `AmsState` posts subject updates to the LVGL thread via `lv_async_call()`. The UI never directly accesses backend state.

---

## Multi-Backend Architecture

Some printers have multiple filament management systems simultaneously (e.g., a tool changer where each toolhead has its own AFC unit). AmsState supports multiple concurrent backends via a `backends_` vector that replaces the former single `backend_` pointer.

### Backend Storage

```cpp
// AmsState private members
std::vector<std::unique_ptr<AmsBackend>> backends_;       // All backends
std::vector<BackendSlotSubjects> secondary_slot_subjects_; // Per-backend subjects (index 1+)
```

- **Primary backend (index 0)** uses the existing flat `slot_colors_[MAX_SLOTS]` and `slot_statuses_[MAX_SLOTS]` subject arrays. This preserves backward compatibility with all existing XML bindings and single-backend printers.
- **Secondary backends (index 1+)** each get a `BackendSlotSubjects` struct with dynamically allocated `lv_subject_t` vectors:

```cpp
struct BackendSlotSubjects {
    std::vector<lv_subject_t> colors;
    std::vector<lv_subject_t> statuses;
    int slot_count = 0;
    void init(int count);   // Allocate and init subjects
    void deinit();          // Deinit subjects
};
```

### Discovery of Multiple Systems

`PrinterDiscovery::parse_objects()` collects all detected AMS/filament systems into a `detected_ams_systems_` vector of `DetectedAmsSystem` structs:

```cpp
struct DetectedAmsSystem {
    AmsType type = AmsType::NONE;
    std::string name;  // "Happy Hare", "AFC", "Tool Changer"
};
```

A printer with both a tool changer and an AFC unit will have two entries. The `init_backends_from_hardware()` method iterates this list and creates a backend for each detected system.

### Backend Selection

Two new subjects track backend selection:

| Subject | Type | Description |
|---------|------|-------------|
| `backend_count_` | int | Number of registered backends |
| `active_backend_` | int | Index of the currently selected backend |

The AMS panel UI shows a backend selector when `backend_count > 1`, allowing users to switch between systems. API:

- `active_backend_index()` -- returns the currently selected backend index
- `set_active_backend(int)` -- switches the active backend (bounds-checked)

### Per-Backend Event Routing

When backends are added via `add_backend()`, each backend's event callback captures its backend index at registration time:

```
Backend 0 emits STATE_CHANGED  -->  on_backend_event(0, "STATE_CHANGED", ...)
Backend 1 emits SLOT_CHANGED   -->  on_backend_event(1, "SLOT_CHANGED", ...)
```

The `on_backend_event()` handler routes to `sync_backend(int)` or `update_slot_for_backend(int, int)` which update the correct set of subjects. All subject updates are posted via `ui_queue_update()` for thread safety.

### Per-Backend Subject Access

Two-argument overloads of `get_slot_color_subject()` and `get_slot_status_subject()` route to the correct subject storage:

```cpp
// Backend 0: flat arrays (backward compat)
lv_subject_t* get_slot_color_subject(0, slot_index);  // -> slot_colors_[slot_index]

// Backend 1+: per-backend storage
lv_subject_t* get_slot_color_subject(1, slot_index);  // -> secondary_slot_subjects_[0].colors[slot_index]
```

### Tool-Backend Integration

The `ToolState` singleton (see `tool_state.h`) maps tools to specific AMS backends via two fields on `ToolInfo`:

```cpp
struct ToolInfo {
    int backend_index = -1;  // Which AMS backend feeds this tool (-1 = direct drive)
    int backend_slot = -1;   // Fixed slot in that backend (-1 = any/dynamic)
    // ... other fields
};
```

- `backend_index = -1` means the tool uses direct-drive filament (no AMS).
- `backend_index >= 0` maps the tool to a specific AMS backend. For example, on a dual-toolhead printer where each head has its own AFC unit, T0 might map to backend 0 and T1 to backend 1.
- `backend_slot` pins the tool to a specific slot within that backend, or `-1` for dynamic slot selection (e.g., Happy Hare tool-to-gate mapping).

`ToolState` and `AmsState` coordinate through `PrinterDiscovery`: tools are discovered from `tool T*` Klipper objects, and the mapping between tools and AMS backends is established during `init_backends_from_hardware()`.

---

## Persistence of slot metadata

HelixScreen writes user-edited slot metadata (brand, spool name, Spoolman
link, weights, color/material) to the Moonraker `lane_data` namespace,
following the AFC-originated convention. This is the same namespace
OrcaSlicer 2.3.2+ reads for filament sync, so user edits automatically
flow to the slicer on Moonraker-based printers. The flow is one-directional:
HelixScreen writes, OrcaSlicer reads (it never writes `lane_data` back), so
"round-trip" here means the user's edit reaching the slicer's filament
panel — not a slicer-to-printer write.

- **Wire-format spec (public):** [`../specs/filament_slots.md`](../specs/filament_slots.md)
- **Implementation notes (internal):** [`FILAMENT_SLOT_METADATA.md`](FILAMENT_SLOT_METADATA.md)

### AD5X IFS material/color reconcile (locks, insert, #1065/#1071)

Native ZMOD has **no per-port RFID or spool identity** — the only per-lane
signals are a color (`ffmColor`) and a material type (`ffmType`), read via
`GET_ZCOLOR` / `IFS_STATUS`, plus a presence bit from `IFS_STATUS Ports`. Because
there's no identity, a lane's `FilamentSlotOverride` bundles two conceptually
different kinds of data, and they follow different rules:

- **Display data** — `color_rgb` + `material`. Should track what's physically loaded.
- **Identity data** — `spoolman_id`, `brand`, `spool_name`, weights. Attached by
  the user; firmware knows nothing about it. Retained across an eject/insert
  cycle so a re-inserted same spool keeps its assignment (**#1071**).

The `user_locked_color` / `user_locked_material` flags gate whether the
`OverwriteAlways` auto-mirror (`mirror_firmware_to_lane_data`) may refresh the
display fields from firmware truth. A locked field is **never** auto-refreshed —
this exists to protect a deliberate user choice from the AD5X post-print
`FFMInfo` revert, which re-emits the *old* type after a print (**#965**).

**The reconcile detectors** live in `ams_backend_ad5x_ifs.cpp`:
`check_external_color_change` and `check_external_type_change`, both called from
`update_slot_from_state`. Each keeps a per-slot baseline (`last_firmware_color_`
/ `last_firmware_material_`); a baseline≠observed delta on a *present* lane fires
`sync_override_to_firmware_locked`, which runs the auto-mirror.

Two footguns this area has repeatedly hit (fixed in #1065; keep them fixed):

1. **Baseline swallow on presence lag.** On modern ZMOD the firmware
   color/type can surface one parse frame *before* `IFS_STATUS Ports` flips the
   slot present. The detectors must **hold** the baseline while the slot reads
   not-present (advancing it only for a genuine empty-lane/`""` eject reading).
   If the baseline advances during the lag, the delta is consumed while the sync
   is skipped, and when presence catches up there's no delta left — the change is
   swallowed (classic symptom: *color updated on screen, material stuck*).

2. **Insert can't clear a lock, so the display sticks.** The only thing that
   clears a lock is an external `CHANGE_ZCOLOR` in the gcode stream (**#981**,
   emitted by the ZMOD COLOR macro / LCD). A **physical insert emits no
   `CHANGE_ZCOLOR`**, so a lane whose material was locked — either by a menu
   type-set (`set_slot_info`) or by the pessimistic `!material.empty()` load
   default in `from_lane_data_record` — keeps painting the *previous* spool's
   type after a new spool goes in. This is why "change type via the COLOR macro"
   worked while "insert a new spool and change its type" did not.

   Fix: `unlock_auto_tracked_override_on_insert_locked()` runs on the
   empty→present edge (both `apply_zcolor_result` presence sites). It drops the
   two lock flags **only when the lane has no real Spoolman binding**
   (`spoolman_id <= 0`) — an auto-tracked material is a guess that a fresh insert
   invalidates, so firmware truth should win. `brand` / `spool_name` /
   `spoolman_id` / weights are never touched, so a retained binding still paints.

   **Why gate on the Spoolman binding.** On insert we can't tell "same spool back
   after maintenance" from "brand-new spool" — there's no identity signal. The
   two want opposite things for the identity fields, so we don't guess: a lane
   with a deliberate Spoolman binding is left entirely alone (**#1071** retains
   it), and only auto-tracked lanes (no binding) refresh material/color from
   firmware. The residual case — a genuinely different spool re-inserted into a
   *bound* lane — keeps the stale binding until the user re-binds, the same
   tradeoff #1071 already accepts.

### OrcaSlicer compatibility — by backend

All HelixScreen-managed AMS backends write the AFC-standard `lane_data`
record on edit, so every one of them round-trips to OrcaSlicer with no
additional configuration. **Verified against OrcaSlicer upstream/main
(post-2.4.0-beta nightly)**, source `MoonrakerPrinterAgent.cpp`
`fetch_moonraker_filament_data()`.

| Backend | Writer | Key style | How OrcaSlicer picks it up |
|---------|--------|-----------|----------------------------|
| AD5X IFS | HelixScreen (`FilamentSlotOverrideStore`) | `laneN` (1-based) | `lane_data` namespace |
| Snapmaker U1 | HelixScreen (`FilamentSlotOverrideStore`) | `T<n>` (0-based) — tool changer | `lane_data` namespace |
| ACE (Anycubic ACE Pro) | HelixScreen (`FilamentSlotOverrideStore`) | `laneN` (1-based) | `lane_data` namespace |
| CFS (Creality K2) | HelixScreen (`FilamentSlotOverrideStore`) | `laneN` (1-based) | `lane_data` namespace |
| AFC / Box Turtle | AFC's own Klipper plugin | `laneN` (1-based) | `lane_data` namespace (AFC is the originator) |
| Happy Hare | Happy Hare's own Klipper plugin (`components/mmu_server.py` `push_lane_data`) | `laneN` (1-based) | `lane_data` namespace — Orca prefers it over the live `mmu` object |
| Tool Changer | (not applicable — no per-slot metadata) | — | N/A |

The key style is derived from the AMS type (`lane_key_style_for(get_type())`),
not hardcoded per backend: tool changers (Snapmaker U1, generic
klipper-toolchanger) write `T<n>`, filament systems write `laneN`. See the
interoperability subsection below.

IFS, Snapmaker, ACE, and CFS share the `FilamentSlotOverrideStore`
infrastructure and publish to `lane_data`; AFC and Happy Hare each write
`lane_data` via their own Klipper plugins. **HelixScreen never writes
`lane_data` for the AFC or Happy Hare backends** — those plugins own their
records, and HelixScreen's AFC/HH backends route user edits through G-code
(`SET_COLOR`/`SET_MATERIAL`, `MMU_GATE_MAP`) only. The reason is stronger than
clobber risk: AFC deletes every key in the namespace on each Klipper boot
(`AFC.py` `delete_lane_data()`) and rebuilds it lane by lane as PREP advances,
so a record we wrote there would vanish on reboot, and a *read* landing in that
window sees a partial namespace. Treat `lane_data` as neither durable nor
atomic for AFC. User overrides go to a private namespace instead (#1158).
(Earlier docs said HH reached Orca solely via the live `mmu` Klipper object.
That is outdated: HH's `push_lane_data` now writes the namespace directly and
Orca prefers it; the `mmu` object is the fallback.)

#### Schema lineage and what Orca actually matches on

- **AFC pioneered the base schema** — keys `color` / `material` / `bed_temp` /
  `nozzle_temp` / `scan_time` / `td` / `lane` / `spool_id`, DB key 1-based
  (`lane1`…), inner `lane` field 0-based. AFC emits **no** vendor field.
- **Happy Hare extended it** with `vendor_name`, `name`, and `filament_id`
  (`push_lane_data`). HH's `filament_id` is a **Spoolman** DB id, not an
  OrcaSlicer preset id.
- **OrcaSlicer reads only `lane` / `material` / `color` / `bed_temp` /
  `nozzle_temp`**, and matches a lane to a filament preset **by the `material`
  type string alone** (`filament_id_by_type`, falling back to generic
  OrcaFilamentLibrary ids like `OGFL99`). It ignores `vendor`, `vendor_name`,
  and `filament_id` today, and never writes `lane_data` back. So brand has no
  effect on the slicer's preset pick — "Generic PLA" and "Elegoo PLA+" both
  resolve to a generic PLA preset. Emit canonical material strings (`PLA`,
  `PETG`, `ABS`…); marketing names won't match.

#### Two-string identity: `material` (Orca wire) vs `helix_material` (HelixScreen)

A lane's display type and its Orca match string are **not the same string**.
HelixScreen stores the precise identity the user chose — `ASA-GF`, `PLA Silk`,
`PPS-CF` — but Orca can only match a type string its own library carries. Writing
the precise string verbatim is what caused the original bug: OrcaSlicer resolves an
unmatched `material` to **the first library preset whose name contains "PLA"**
(`Preset.cpp:3300`), and because that bogus id then resolves cleanly it
**short-circuits the similarity search** that would otherwise have found a closer
type (`PresetBundle.cpp:3320-3346`). So `ASA-GF` synced as *Generic PLA* — PLA
temperatures on a glass-filled ASA — while the color came through untouched.
(Verified against the pinned OrcaSlicer source, not secondhand docs.)

`to_lane_data_record()` therefore emits two keys:

- **`material`** — the Orca wire string, derived by `filament::orca_match_type()`
  (`filament_variants.cpp`): explicit `orca_type_overrides` entry → the type itself
  if Orca's library carries it → `extract_base_material()` base polymer if the
  library carries *that* → otherwise **omitted entirely** (better an empty tray in
  Orca than a confident wrong match). The library-type set and the override table
  are generated into `assets/filaments.json` (`orca_library_types`,
  `orca_type_overrides`) by `scripts/import_orca_filaments.py`.
- **`helix_material`** — the precise identity, written unconditionally. Orca ignores
  it; HelixScreen's reader (`from_lane_data_record()`) prefers it over `material`,
  so the on-device AMS screen still shows `ASA-GF` even though the same lane synced
  to Orca as `ASA`.

**Healing existing installs.** Records written before this split carry an
unmatchable `material` and no `helix_material`. `load_blocking()` rewrites
helix-authored records — proven by a `helix_locked_*` key, never a foreign
co-author's — in place: `helix_material` = the precise identity, `material` =
`orca_match_type()` of it (or dropped if nothing matches). Mutating in place
preserves `scan_time` and any co-author's fields. The heal is gated on
`orca_tables_available()` — a missing or stale `assets/filaments.json` would
otherwise strip `material` from every lane in one pass — and it re-runs on **drift**
(a later library regeneration that drops a type we used to match), converging once
`orca_match_type(material) == material`. The tables are pre-warmed on the main
thread at startup (`filament::warm_orca_tables()`, called from
`SubjectInitializer`) so the first match never parses the asset on a WebSocket
background thread.

#### Forward-compat aliases (`vendor_name` / `name`)

HelixScreen's writer (`to_lane_data_record()` in
`filament_slot_override_store.cpp`) emits **both** key spellings: `vendor`
(AFC-style base) **and** `vendor_name` (HH extension), plus `spool_name` **and**
`name`. It's unsettled which spelling Orca will consume when it eventually adds
vendor-aware matching, so emitting both is a zero-cost hedge — Orca ignores
unknown keys. The reader tolerantly falls back to the HH aliases. **Do not add
a HelixScreen-side `filament_id` resolver:** Orca reads the field from nowhere,
there is no deterministic (vendor, material) → Orca `setting_id` catalog (the
ids number in the hundreds and churn across releases), and we do not ship a
forked OrcaSlicer that could add the read path.

### `lane_data` interoperability (outer-key contract)

`lane_data` is a **shared namespace with multiple writers and multiple
readers**. The authoritative, source-verified contract lives in the public
spec — [`../specs/filament_slots.md` § "Interoperating readers and
writers"](../specs/filament_slots.md#8-interoperating-readers-and-writers).
Read that section before touching key formatting, the load filter, or the
migration. The summary:

- **Writers and their key style**: HelixScreen (`T<n>` on tool changers,
  `laneN` otherwise), AFC (`laneN`), Happy Hare (`laneN`), Mainsail #2510
  (`T<n>` on Spoolman + tool changer).
- **Readers**: OrcaSlicer is **key-opaque** (reads the inner `lane` field, never
  the outer key — `MoonrakerPrinterAgent.cpp:780`), requires the inner `lane`
  to be a JSON **string**, and does **no deduplication**. HelixScreen's reader
  is **key-agnostic** and prefers the canonical key for its own style on
  duplicates (`load_blocking` in `filament_slot_override_store.cpp`).
- **The collision hazard is not a wrong outer key** — it is the **same inner
  `lane` under two different outer keys**, which Orca renders as two trays for
  one slot. A tool changer converges on `T<n>` (matching Mainsail) and migrates
  its own stale `laneN` records to `T<n>` on load to avoid exactly this.

**Lesson (recorded inline so we don't re-derive it):** verify wire-format
claims against the tools' **source**, not their PR or release text. Mainsail
#2510's companion PR broadened an AFC `map` TypeScript type to `string[]`,
which looked like a schema change but was speculative — upstream AFC still
emits a scalar `map`. Confirming against `MoonrakerPrinterAgent.cpp` (Orca) and
the AFC plugin source, not the PR descriptions, is what kept this change
correct. Cite exact source lines in the spec so a future reader re-verifies the
same way.

---

## Filament Catalog (`filaments.json`)

**Design spec:** [`specs/2026-07-02-filament-catalog-merge-design.md`](specs/2026-07-02-filament-catalog-merge-design.md)

HelixScreen ships a single generated catalog of **branded** filament products —
`assets/filaments.json` — that unifies what used to be two disconnected data
sources: the generic material-**type** table in `include/filament_database.h`
(PLA, ABS, PETG, … — untouched, still `constexpr`, still the source of
physical truth) and the old CFS-only `assets/cfs_materials.json` (renamed and
superseded). The catalog is generic infrastructure — not CFS-specific — even
though the CFS backend is currently its only consumer.

### Schema

Each entry in the `filaments` array is one branded product:

```json
{
  "id": "creality-cr-abs",     // stable slug; user overrides target this
  "brand": "Creality",
  "name": "CR-ABS",            // display = "{brand} {name}"
  "type": "ABS",               // resolves to a filament_database.h type
  "nozzle": 260,                // recommended nozzle temp (°C)
  "bed": 60,                    // recommended bed temp (°C)
  "nozzle_min": 240,            // OPTIONAL — only emitted when it differs from the type's range
  "nozzle_max": 280,            // OPTIONAL
  "density": 1.24,              // OPTIONAL — else inherit type
  "codes": { "cfs": "07001" },  // OPTIONAL, open scheme-keyed map (see below)
  "orca_id": "OGF...",          // provenance: OrcaSlicer filament_id (NOT a CFS code)
  "source": "orca"              // provenance: orca | cfs-seed | user
}
```

Most Orca-derived entries are **thin** — just `id, brand, name, type, nozzle,
bed, source`. Everything else (nozzle range, bed if unset, chamber temp, dry
temp/time, `compat_group`, density) **inherits from the base `type`**, the
same way a product does at runtime (see `EffectiveFilament` below). A field is
only written to the file when it *differs* from what the type would already
supply — keeps the catalog small and keeps regen diffs meaningful.

### Type inheritance

Products don't duplicate physical data — they carry deltas over their base
material type:

```
EffectiveFilament = filament::find_material(product.type)   // type defaults
                     ◀ product's own JSON fields              // product overrides
                     ◀ user overlay entry (same id), if any    // user overrides
```

`nozzle_min` / `nozzle_max` / `bed` / `density` / `chamber_temp_c` /
`dry_temp_c` / `dry_time_min` / `compat_group` all come from the type unless
the product JSON explicitly sets them. A product whose `type` string doesn't
resolve in `filament_database.h` (an Orca material HelixScreen doesn't map
yet) is only valid if it's self-sufficient — i.e. the importer emitted
explicit `nozzle_min`/`nozzle_max` for it directly; see the data-integrity
lint in `tests/unit/test_filaments_data.cpp`.

### The `codes` map (scheme-keyed, open-ended)

Hardware/RFID codes live in a scheme-keyed map so multiple, possibly-colliding
namespaces coexist and a new decoder drops in with **no schema change**:

| scheme | meaning | status |
|--------|---------|--------|
| `cfs` | Creality Filament System numeric hardware code | **live** — decodes CFS box-reported material codes |
| `rfid` | future vendor-neutral / generic RFID standard | reserved (not populated) |
| `snapmaker` | Snapmaker U1 `filament_sku` | reserved — U1 exposes a real per-material SKU (`print_task_config.filament_sku`), but no seed table exists yet |
| `bambu` | Bambu `filament_id`/RFID (`GFA00`…) | reserved — could auto-derive from `orca_id` later |

Each scheme is indexed independently (`by_code[scheme][code] -> product`), so
a `cfs` code can never collide with an `rfid` or `snapmaker` code that happens
to share the same digits.

### `FilamentCatalog` — transient, on-demand access layer

`include/filament_catalog.h` / `src/printer/filament_catalog.cpp`. **No
`::instance()` singleton** — unlike the rest of the printer-state layer, this
is a scoped value type: construct it, query it, let it fall out of scope. Idle
RAM footprint is zero; nothing is parsed until something asks for it.

```cpp
// Small slice: only products carrying a code in one scheme. Used by CFS decode —
// built once at the top of a box-state enrichment pass, destroyed at the end.
auto cat = FilamentCatalog::load_codes("cfs");
const EffectiveFilament* mat = cat.resolve_code("cfs", mat_id);

// Whole catalog + user overlay merged in. For a future offline picker (Phase 2);
// transient for the lifetime of a picker session, not resident otherwise.
auto full = FilamentCatalog::load_full();
```

Other query methods: `resolve_id(id)`, `products_for_type(type)`,
`products_for_brand(brand)`, `all_brands()`, `all_products()`. The tradeoff
(accepted): CFS re-parses its small coded slice on every poll rather than
caching — worth it for zero idle RAM on memory-constrained devices (AD5M,
K1). A debounce cache is a future escape hatch only if profiling ever shows
the re-parse cost matters.

**Today's only consumer** is `AmsBackendCfs` (`src/printer/ams_backend_cfs.cpp`),
which replaced the old `CfsMaterialDb` JSON table with
`FilamentCatalog::load_codes("cfs").resolve_code("cfs", mat_id)`. Behavior is
unchanged for CFS users — same slot fields get filled — the catalog is just
richer and no longer CFS-gated. A user-editable overlay
(`config/user_filaments.json`, read-write, merged by `load_with_overlay()`)
exists at the load-path level today; the UI to author it is Phase 3 (out of
scope here).

### User overlay format

`config/user_filaments.json` is the on-disk shape for everything a user
contributes about filaments — product entries (override/add to the built-in
catalog) and Orca-type hints (so a display name not in our snapshot resolves
correctly in OrcaSlicer without waiting for a HelixScreen release). The file
does not exist by default; it is created the first time the Phase 3 edit UI
writes a change. The on-disk format is an internal concern — users interact
through the UI and never see JSON.

```jsonc
{
  "filaments": [
    // Product entries: override built-ins by id, or add new ones. Merged by
    // FilamentCatalog::load_with_overlay(). See the "effective filament"
    // structure in include/filament_catalog.h for the full field set.
    {"id": "polymaker-abs-pro", "nozzle_min": 265, "nozzle_max": 285, "source": "user"},
    {"id": "acme-custom-petg", "brand": "Acme", "name": "Custom PETG",
     "type": "PETG", "nozzle": 240, "source": "user"}
  ],
  "orca_type_map": {
    // Helix display name -> Orca wire string. Single map by design — users
    // contribute *overrides*, not library-type membership, which stays a
    // shipped-asset concept (assets/filaments.json's `orca_library_types`).
    // Resolution at orca_match_type() step 1 makes user entries always win
    // over shipped ones. An empty-string value is the documented "suppress"
    // case: emit nothing for this type rather than a wrong match. See the
    // spec's § Drift for the safety rationale.
    "PLA-BioTough": "PLA",
    "WeirdResin": "",
    "CustomASA": "ASA"
  }
}
```

The two sections are independent: a user can carry only `filaments`, only
`orca_type_map`, both, or neither. The shipped asset
(`assets/filaments.json`) keeps its own split between `orca_library_types`
(list) and `orca_type_overrides` (map) because the importer generates those
two differently — that distinction does not propagate to the user overlay.

**Wiring.** `SubjectInitializer::init_core_and_state()` warms the Orca tables
on the main thread (`warm_orca_tables()`), then immediately calls
`FilamentCatalog::load_user_orca_type_map()` and feeds the result to
`filament::merge_user_orca_overrides()`. The merge runs under
`g_orca_mutex`, so it is safe against concurrent `orca_match_type()` callers.
User entries land in `g_orca_overrides`, where resolution step 1 picks them
up before any shipped lookup. An empty `orca_type_map` (the common case when
no user overlay exists) is a no-op.

**Writing the overlay.** `FilamentCatalog::save_user_products(products)`
replaces the `filaments` section via a temp-file + `rename` (POSIX rename is
atomic within a filesystem, so a **process** crash mid-write never leaves a
partial overlay — the rename either fully happens or doesn't). It does **not**
`fsync`, so this is not a power-loss durability guarantee; on the rare power
cut mid-save a filesystem could still surface a truncated file. That trade is
deliberate: the overlay is written only on user filament edits, and the
original is never modified until the rename succeeds. It performs
read-modify-write to preserve any existing `orca_type_map`, migrates legacy
bare-array overlays to object form on first save, recovers from a corrupt
existing file rather than blocking the save (preserving the unparseable
original as `<path>.bak` for hand-recovery), and creates missing parent
directories. On a fresh install where no overlay exists yet, the write target
falls back to the canonical `config/user_filaments.json` so the first save can
create the file. The caller supplies pre-built
`nlohmann::json` product objects (one per entry, minimum field `id`) —
typically the modal's form-handler builds these. `orca_type_map` has no
write API today: contributing Orca-type hints is a power-user hand-edit
concern (see issue #1120 and the design spec's § Drift for the rationale —
a UI that invites "add Orca type" misleads users into thinking HelixScreen
can teach Orca new presets, which it cannot; Orca only matches against
types already in its own library).

### Regenerating the catalog

```bash
make regen-filaments ORCA_TAG=v2.4.1     # ORCA_TAG defaults to a pinned tag in mk/filaments.mk
```

This shallow-clones OrcaSlicer's `resources/profiles` at the pinned tag into
`build/orca-profiles` (sparse checkout, blob-filtered), runs
`scripts/import_orca_filaments.py` to resolve `inherits` chains, extract
facts, and union them with the preserved CFS-code seed
(`scripts/fixtures/cfs_seed.json`), writes `assets/filaments.json`, mirrors it
to `android/app/src/main/assets/assets/filaments.json`, and discards the
cloned Orca checkout. Nothing from the Orca clone is committed — only the
derived output. Bump `ORCA_TAG` to refresh against newer Orca data.

`assets/filaments.json` is **generated but committed** (same pattern as fonts
and translations) — cross-compiled targets need the file present without
running Python/git-clone during the build.

### Licensing and attribution

OrcaSlicer is AGPL-3.0; HelixScreen is GPL-3.0-or-later. HelixScreen never
ships OrcaSlicer's profile files — the importer clones them into scratch,
derives **facts** (nozzle/bed temps, density — not copyrightable expression),
and discards the clone. `filaments.json` carries a top-level `_attribution`
field naming OrcaSlicer, its repo URL, the pinned tag, and its license, e.g.:

```json
"_attribution": "Factual filament data derived from OrcaSlicer (github.com/SoftFever/OrcaSlicer, tag v2.4.1, AGPL-3.0). No OrcaSlicer profile files are shipped."
```

---

## UI Panels

### AMS Panel (`ui_panel_ams`)

The detail panel showing slots, path visualization, hub sensors, and the currently loaded filament for a single backend. Opened as an overlay from the Filament nav panel or from the AMS Overview Panel.

Key features:
- Slot grid with overlap layout for >4 slots (shared via `ui_ams_slot_layout.h`)
- Path canvas showing filament routing from slots through hub to toolhead
- Backend selector (shown when `backend_count > 1`)
- Unit scoping: can display a subset of slots for a single unit within a multi-unit backend

### AMS Overview Panel (`ui_panel_ams_overview`)

Grid of unit cards showing all units across the system. Each card is a miniature visualization of the unit's slots. Clicking a card transitions inline to a detail view of that unit's slots.

Key files:
| File | Purpose |
|------|---------|
| `include/ui_panel_ams_overview.h` | Class with detail view state |
| `src/ui/ui_panel_ams_overview.cpp` | Card creation, inline detail view, slot layout |
| `ui_xml/ams_overview_panel.xml` | Two-column layout: cards/detail left, loaded info right |
| `ui_xml/ams_unit_card.xml` | Mini unit card with slot bars and hub sensor dot |

**Current scope**: The overview panel queries `get_backend(0)` and displays all units from that single backend's `AmsSystemInfo`. This covers the common case of a single multi-unit AMS system (e.g., AFC with multiple Box Turtle units).

**Future: multi-backend aggregation**: When multiple backends are active simultaneously (e.g., an AFC system on one toolhead + a Happy Hare on another), the overview panel should iterate all backends via `AmsState::get_backend(i)` for `i` in `0..backend_count` and aggregate their units into the card grid. The per-backend slot subject storage (`secondary_slot_subjects_`) and event routing already support this — the UI aggregation is the remaining integration point.

### Error State Visualization

Per-slot error indicators and per-unit error badges, driven by `SlotInfo.error` and `SlotInfo.buffer_health` from the backend layer. See `docs/devel/plans/2026-02-15-error-state-visualization-design.md` for full design.

**Data model** (`ams_types.h`):
- `SlotError` — message + severity (INFO/WARNING/ERROR), `std::optional` on `SlotInfo`
- `BufferHealth` — AFC buffer fault proximity data, `std::optional` on `SlotInfo`
- `AmsUnit::has_any_error()` — rolls up per-slot errors for overview badge

**Detail view** (`ui_ams_slot.cpp`):
- 14px error badge at top-right of spool (red for ERROR, yellow for WARNING)
- 8px buffer health dot at bottom-center (green/yellow/red based on fault proximity)
- Both pulled from `SlotInfo` during refresh (same pattern as material/tool badge)

**Overview view** (`ui_panel_ams_overview.cpp`):
- 12px error badge at top-right of unit card (worst severity across slots)
- Mini-bar status lines colored by error severity

**Backend integration**:
- AFC: per-lane error from `status` field + buffer health from `AFC_buffer` objects
- Happy Hare: system-level error mapped to `current_slot` via `reason_for_pause`
- Mock: `set_slot_error()` / `set_slot_buffer_health()` + pre-populated errors in AFC mode

### Two error channels

A backend fault reaches the user through one of **two independent channels**. They are not alternatives and not a fallback pair — they are fed by different transports, fire at different moments, and a backend may implement either, both, or neither. Getting this wrong is how a fault double-surfaces or vanishes.

| | **Channel A — line driven** | **Channel B — status driven** |
|---|---|---|
| Hook | `AmsBackend::classify_error(raw_line, ctx)` | `AmsBackend::current_error()` |
| Transport | Moonraker `notify_gcode_response` | Moonraker `notify_status_update` |
| Dispatched from | `GcodeErrorRouter::process_line()`, `src/application/gcode_error_router.cpp:474` — **exactly once per line**, before the generic `error_classify::classify()` | `AmsErrorBridge::on_action_changed()`, `src/application/ams_error_bridge.cpp:68` — **only on the rising edge** into `AmsAction::ERROR` |
| Pre-filtering | **None.** Every response line is handed to every backend. Each override gates itself | The `AmsAction::ERROR` edge is the entire gate. A backend that never assigns that action is never asked, even if it overrides the hook |
| Presentation | `decide_presentation()` → toast / modal / `MODAL_WITH_RECOVER` | `RecoveryModalPresenter::present()` directly |
| Returning `nullopt` | Defers to `error_classify::classify()` | Falls through to the bridge's last-resort toast (`surface_unhandled_error()`) |

**Per-backend gates and recovery sets:**

| Backend | Channel A gate | Channel B gate | Recovery actions (`build_recovery_actions()`) |
|---------|----------------|----------------|-----------------------------------------------|
| **AFC** | `is_bang_line()`, then a `tool_end` jam/break/runout signature, else any pausing `!!` while `error_state_` | `error_state_` set (the stuck-action latch returns `nullopt` — that fault is ours, not AFC's) | Resume (primary, hot) · Unload (hot) *or* Eject lane (cold) depending on `tool_start_sensor_` · AFC_RESET (danger) |
| **Happy Hare** | `is_bang_line()`, then paused **and** (`AmsAction::ERROR` or a recognized cause in `reason_for_pause_`) | — | Backend-derived; title is "Filament runout" when the detail says runout |
| **AD5X IFS** | — | `AmsAction::ERROR`, raised by `evaluate_runout_locked()` or by an operation timeout | Runout: Resume (primary, hot) · Purge `M83`+`G1 E` (hot) · `IFS_UNLOCK` (danger, cold). Timeout: `IFS_UNLOCK` alone. **No "Load slot N"** — every IFS load path self-homes and trash-moves into the part |
| **CFS** | **inverted** — `is_bang_line()` returns `nullopt`, so `!!` `key8xx` codes stay with the generic classifier. Claims only paused `respond_info` lines matching the auto-refill give-up wording | — (never assigns `AmsAction::ERROR`) | Resume (primary, hot) · Reset CFS = `BOX_ERROR_CLEAR` (danger, cold) |
| **QIDI Box** | — | stub | — (hardcodes a lone dismiss) |
| **ACE**, **Tool changer**, **Snapmaker** | — | — | — (generic runout modal owns these; see below) |

**Why CFS inverts the usual gate.** Creality's box reports coded faults as `!!` lines carrying a `key8xx` JSON payload, which `error_classify::classify()` already decodes into a CRITICAL event (and a "Reset CFS" button for `key840`). Claiming those in `classify_error()` would either duplicate that path or silently replace it. The runout give-up messages ride the *other* half of the same channel — plain `respond_info` output that no classifier looks at — so taking non-`!!` lines and only non-`!!` lines is what keeps the two from colliding.

**Cross-channel dedup.** `fault_surface_correlation` (`src/application/fault_surface_correlation.cpp`, 3 s window, exact-string match) is the shared claim ledger. The router records every detail it surfaces; the bridge's fallback toast checks it before speaking. `RecoveryModalPresenter` separately dedups on `detail` **plus** the action set — the action set is part of the identity because AFC legitimately emits byte-identical text on both channels with different affordances (#1171). Backends should populate `ErrorEvent::raw_detail` with the firmware's untranslated wording when `detail` has been rewritten, or the ledger has nothing the other channel can match.

**Who owns the runout surface.** Both channels compete with a third, older surface: the generic sensor-driven modal (`FilamentRunoutHandler` on the pause edge, `PrintStatusWidget` when idle), gated by `RuntimeConfig::should_show_runout_modal()`. The rule is **one surface per printer**: that predicate returns false exactly for the backends in the table above that raise their own runout fault (AFC, Happy Hare, AD5X IFS, CFS), and true for hub backends that raise nothing (ACE, QIDI Box) — which the old blanket "is it a hub AMS" test silenced with nothing put in its place (#1250).

Note that for AFC, Happy Hare, AD5X IFS and CFS the generic surface is *also* structurally blind: each claims its own sensors through `owns_filament_sensor()`, so `PrinterHardware::is_ams_sensor()` hides them from the wizard's sensor picker, they never get a `FilamentSensorRole`, and `FilamentSensorManager::has_real_runout()` skips them. The suppression above is belt-and-braces for the configs where an AMS lane sensor *does* carry a role (AFC's `...eN_filament` naming is the case `has_real_runout()`'s lane-mapping branch exists for).

---

## Filament Op Dispatch: Which Surface Owns What

More than one screen can start a Load. Every time one of them grew its own answer to
"what do I do when there is no AMS backend?", the answers diverged: a full three-tier
fallback on the Filament panel, a silent return in the AMS sidebar, and a navigate-away in
both runout dialogs. The already-mounted guard existed only in the sidebar, so the same
firmware no-op that the sidebar refused left the Filament panel's Load button spinning for
the full 120 s guard timeout (bundle 9KRXZ62P). On Snapmaker U1 the two surfaces sent
*different G-code for the same button label* — `T{n}`, which seats the carriage and feeds
nothing, versus `AUTO_FEEDING EXTRUDER={n} LOAD=1`.

The decision is now one shared, display-free layer; the surfaces own only how the answer is
presented.

| Header | Owns |
|--------|------|
| `include/filament_op_dispatch.h` | `plan_load()` / `plan_unload()` — which tier, which backend call, or which refusal. Also `unload_target_is_loaded()`. Header-only, takes plain values (`AmsSystemInfo` + `BackendCaps`), no `AmsBackend*` |
| `include/filament_op_slot_resolver.h` | `resolve_op_button_slot()` — which slot a tool's buttons act on; `compute_op_button_gating()` — whether Load/Unload are enabled |
| `src/ui/filament_op_router.{h,cpp}` | Tiers 2 and 3: `dispatch_filament_macro()` with its `ParamPolicy`, the shared `MacroParamModal`, and `filament_load_fallback_gcode()` / `filament_unload_fallback_gcode()` |

Tier 1 deliberately stays with the callers — the backend call is inseparable from each
surface's own guard, stepper, and spinner bookkeeping.

### The four dispatch surfaces

| Surface | Entry point | Raised by | Dispatches? |
|---------|-------------|-----------|-------------|
| Filament panel | `FilamentPanel::execute_load()` / `execute_unload()` | The Load / Unload buttons on the Filament nav panel | Yes — full ladder, `ParamPolicy::Prompt` |
| AMS operation sidebar | `AmsOperationSidebar::handle_load_with_preheat(slot)` / `handle_unload(slot)` | Slot grid + context menu on the AMS panel and the AMS Overview panel (both own a `unique_ptr` to one) | Yes — full ladder, `ParamPolicy::Prompt` |
| Mid-print runout dialog | `FilamentRunoutHandler::dispatch_load()` | `RunoutGuidanceModal`'s Load button during a print or runout pause | Yes — full ladder, `ParamPolicy::Suppress` |
| Idle runout dialog | `PrintStatusWidget::show_idle_runout_modal()` | A real runout detected while STANDBY / COMPLETE / CANCELLED | **No** — hands off to the Filament panel |

The idle dialog is the one surviving "navigate away", and it is correct *because* it never
dispatches: with the printer idle the Filament panel is reachable, so `set_active(PanelId::
Filament)` inherits that panel's routing instead of forking a fourth answer. That is only
true while it stays a pure hand-off. The moment it wants to load without leaving the modal,
it goes through `plan_load()` like the other three.

### The three-tier ladder

| Tier | What runs | Chosen when |
|------|-----------|-------------|
| 1 `FilamentTier::AmsBackend` | `load_filament()`, `unload_filament()`, or `change_tool()` — carried in `FilamentOpPlan::ams_call` / `ams_arg` | A backend owns the operation (see the two asymmetries below) |
| 2 `FilamentTier::Macro` | The user's configured `StandardMacroSlot::LoadFilament` / `UnloadFilament`, via `dispatch_filament_macro()` | No tier 1, and the slot is non-empty |
| 3 `FilamentTier::RawGcode` | `filament_load_fallback_gcode()` (fast bowden move, then a slow push into the melt zone) or `filament_unload_fallback_gcode()` (tip-shape, then a long retract) | Nothing else is configured |
| — `FilamentTier::Refused` | Nothing. `FilamentOpPlan::refusal` says why | See the refusal table |

`AmsCall::ChangeTool` carries a **tool number**, not a slot index — it comes from the target
slot's `mapped_tool`. Every other call takes the slot.

| Refusal | Meaning | Reached from |
|---------|---------|--------------|
| `SelectSlot` | The backend wants a slot and none resolved | Load only |
| `AlreadyMounted` | The requested tool is already on the carriage. `SELECT_TOOL` on it is a firmware no-op (9KRXZ62P) | Load only, tool changers only |
| `NothingLoaded` | No slot resolved, or nothing at that slot worth pulling | Unload only — its *only* refusal |

### Two deliberate asymmetries between load and unload

These are not oversights, and symmetrising them breaks real printers.

**1. Bypass falls through on load and stays on the backend for unload.**

`plan_load()` gates tier 1 on `caps.present && caps.requires_slot_selection_for_load`, not on
the backend merely existing. `AmsBackend::requires_slot_selection_for_load()` defaults to
`!is_bypass_active()`, so an active bypass drops straight to the user's `LOAD_FILAMENT`
macro — that is how a bypass spool loads at all.

`plan_unload()` gates tier 1 on `caps.present` alone. AFC runs the user's unload macro
itself as part of its own unload, so routing a bypass unload to tier 2 would run that macro
twice.

**2. Load-vs-swap and already-mounted exist only on the load side.**

A machine with filament already seated cannot simply feed another lane, so when
`needs_unload_before_load(info)` is true and the target slot has a `mapped_tool`, `plan_load()`
rewrites the call to `change_tool(mapped_tool)`. Centralized so the UI and the backend agree
(#968). A target with **no** tool mapping falls through to a plain `load_filament()` rather
than synthesising an unload: every backend that arm could reach already chains the unload
inside its own load (ACE's `change_tool()` *is* `load_filament()`; QIDI prepends the unload
itself; AFC's `CHANGE_TOOL` is the toolchange verb), and Happy Hare — the one backend whose
`load_filament()` is a bare `MMU_LOAD GATE={n}` — is precisely the backend the UI is
forbidden to help (`allows_implicit_chaining()` is false, #1229). Unload asks none of this.

Neither asymmetry is visible in `plan_unload()`'s signature, which is why both call sites
carry a comment saying so. Read `include/filament_op_dispatch.h` before "fixing" either.

### Shared policy vs per-surface presentation

**Shared — one answer, in the planner.** A second answer here is a user-visible bug.

| Question | Answered by |
|----------|-------------|
| Which tier does this operation take? | `plan_load()` / `plan_unload()` |
| Is this a fresh load or a swap? | `plan_load()` via `needs_unload_before_load()` -> `AmsCall::ChangeTool` |
| Is the requested tool already mounted? | `plan_load()` -> `FilamentRefusal::AlreadyMounted` |
| Is there anything at this slot to unload? | `unload_target_is_loaded()` — actively loaded, **or** filament at the toolhead, **or** it is the current slot (the runout-recovery case, #995 / #1199) |
| Which slot do this tool's buttons act on? | `resolve_op_button_slot()` |
| Are Load / Unload enabled right now? | `compute_op_button_gating()` — load state *and* print state |

**Per-surface — presentation, and correctly different.**

| Surface | Owns |
|---------|------|
| `FilamentPanel` | `begin_operation_guard()` / `operation_guard_`, the `backend_op_active_` gate on `ams_action_observer_`, the on-button spinner (`op_started` / `op_succeeded` / `op_failed`), and `navigate_to_ams_panel()` on `SelectSlot` |
| `AmsOperationSidebar` | The step model (`start_operation(StepOperationType::LOAD_FRESH / LOAD_SWAP / UNLOAD)`) and the preheat state machine (`get_load_temp_for_slot()`, `pending_load_slot_`, `check_pending_load()`, `ui_initiated_heat_`) |
| `FilamentRunoutHandler` | Staying put. Every outcome is a toast; navigating would tear down the dialog the user is standing in |
| All three | Toast copy, and whether to toast at all |

Two consequences worth naming, because they look like bugs and are not:

- **The sidebar is silent on a refusal; the panel toasts.** The AMS grid already highlights
  the mounted slot and greys the unpickable ones, so a toast there narrates what the user
  can see. On the Filament panel the button is the only feedback there is.
- **Tool changers skip the sidebar's preheat entirely.** `SELECT_TOOL` owns its own heat
  sequence and the backend sets `SELECTING` at dispatch, resolving on the macro ack (#1183);
  an optimistic `HEATING` stepper would fight it. Only the *decision* is shared.

Two more where the surface deliberately does **not** use the plan's value:

- The sidebar **re-plans after preheat** (`check_pending_load()`) instead of replaying the
  plan it computed before heating — the firmware may have picked up or dropped a tool while
  the nozzle came up, which flips load-vs-swap.
- The sidebar passes its caller's raw `slot_index` to `unload_filament()`, **not**
  `plan.ams_arg`: its own Unload button means "whatever is active" and passes `-1`, which the
  AD5X IFS backend keys on to send `IFS_REMOVE_CURRENT_PRUTOK`. The Filament panel does the
  opposite and passes its resolved slot explicitly, because re-resolving `current_slot` inside
  the backend was the U1 wrong-tool unload bug.

### The lifetime hazard in tier 2

`get_filament_param_modal()` returns a **function-local static** — one `MacroParamModal` for
the whole process. `MacroParamModal` stores its `on_execute_` callback and **does not clear it
on dismiss**; only the next `show_for_*()` overwrites it. A callback handed to that modal can
therefore fire arbitrarily later, long after the object that built it is gone.

| Surface | Lifetime | What tier 2 must capture |
|---------|----------|--------------------------|
| `FilamentPanel` | Immortal singleton | Bare `[this]` is safe, annotated `[L012]` |
| `AmsOperationSidebar` | `unique_ptr` on the AMS / AMS Overview panel — destroyed when the panel closes | **Must** capture `lifetime_.token()` and re-enter through `token.defer(tag, ...)`, which re-checks the generation on the main thread. A bare `this` here is a live use-after-free |
| `FilamentRunoutHandler` | Owned by the print-status panel | Uses `ParamPolicy::Suppress`, so `run` fires synchronously inside `dispatch_filament_macro()` and is never retained |

`ParamPolicy::Suppress` is not only a lifetime dodge — it is required for any surface that
already owns a dialog. A `MacroParamModal` raised from the runout dialog would stack on top of
a live modal whose observers keep firing underneath it.

`dispatch_filament_macro()` returns **true when a prompt was raised**, which is exactly the
"your callback outlived this call" signal: `false` means `run` already executed with an empty
`MacroParamResult`. Tests reach the prompt branch without a screen via
`set_filament_param_prompter()`; pass a default-constructed `ParamPrompter` to restore the
shared modal.

### Rules for contributors

**Adding a fifth dispatch surface.** Do not write another ladder.

1. Read the backend's answers into a `BackendCaps` (`present`,
   `requires_slot_selection_for_load()`, `needs_unload_before_load(info)`, `get_type() ==
   AmsType::TOOL_CHANGER`) — the existing surfaces do this in three or four lines each.
2. Call `plan_load()` / `plan_unload()` and `switch` on `plan.tier`. Handle all four arms,
   including `Refused`.
3. Tier 1 is yours (the backend call sits inside your own guard/stepper bookkeeping). Tiers 2
   and 3 come from `dispatch_filament_macro()` and the two fallback-G-code helpers — do not
   re-derive either.
4. Pick a `ParamPolicy`: `Suppress` if your surface already owns a dialog, `Prompt`
   otherwise. If you pick `Prompt` and you are not immortal, capture a lifetime token.
5. Add a case to `tests/unit/test_filament_dispatch_surfaces.cpp` — its whole point is that
   all surfaces answer the same question the same way.

**Adding a new backend.** Do not add a UI branch for it. The plan is driven entirely by
`requires_slot_selection_for_load()`, `needs_unload_before_load()`, `is_bypass_active()`,
`get_type()`, `slot_is_actively_loaded()`, and `slot_has_filament_at_toolhead()`. If the plan
is wrong for your hardware, the fix is in one of those predicates or in
`filament_op_dispatch.h` — never in a surface. See also "Per-Slot Load Authority" and
"Developer Guide: Adding a New Backend".

**Deciding whether a new question is shared policy or presentation.** In order:

1. *Would two surfaces answering it differently be a bug the user could see?* Yes -> shared.
   The four divergences above all failed this test.
2. *Does the answer depend on the printer, the firmware, or the backend — or on which screen
   the user is standing on?* Printer -> shared. Screen -> presentation.
3. *Does answering it need a widget, a timer, a stepper, or `this`?* If yes it cannot live in
   the planner, which takes plain values by design so the whole decision compiles and runs in
   a binary with no printer and no display (`tests/unit/test_filament_op_dispatch.cpp`,
   `test_filament_op_slot_resolver.cpp`). If a question fails 3 but passes 1, split it: the
   *rule* goes in the planner, the *effect* stays in the surface. That split is exactly what
   `plan.ams_call` is.

---

## Swap Preheat: Hold Previous Filament Temp

When a user switches filament, the nozzle must stay hot enough to purge the material already in the melt zone. Dropping straight to the new material's temperature (e.g. ABS 250 → TPU 230) leaves un-purged high-temp filament clogging the path.

**The rule.** A "switching material" send floors the nozzle target at:

```
load_target = max(new_material_temp, last_nonzero_nozzle_target, current_actual_nozzle_temp)
```

- `new_material_temp` — what the tapped preset / load op requested.
- `last_nonzero_nozzle_target` — an **in-session latch** of the last non-zero nozzle target. It **survives the target cooling to 0**, so even a cold swap reheats to the old material's temp to purge it. Latched in `PrinterTemperatureState::update_from_status()` (per-`ExtruderInfo.last_nonzero_target`, per-extruder).
- `current_actual_nozzle_temp` — covers a physically-hot nozzle whose target was already cleared.

**Latch lifecycle.**
- **Set:** every status update with `target > 0` (per extruder).
- **Survives:** cooldown to 0 (the whole point).
- **Reset:** on **unload only** — the filament is physically pulled, so nothing is left to purge. `FilamentPanel::execute_unload()` and `AmsOperationSidebar::handle_unload()` call `PrinterState::clear_nozzle_load_latch()`.
- **Not persisted** across restart — a power cycle means a cold printer that reheats anyway, and persistence is where staleness would bite.

**Where the guard lives.** `TemperatureController::set_target(HeaterType, celsius, opts)` applies the floor when `opts.keep_previous_hot` is set. Nozzle only — bed/chamber and any send without the flag are untouched, so cooldown-to-0 and deliberate manual keypad lowers still work.

**Which calls set `keep_previous_hot`.**
| Call site | Flag | Rationale |
|-----------|------|-----------|
| Material preset tap (`handle_preset_button`, `handle_spool_preset_button`) | ✅ on | "I'm switching material" |
| Op preheat (`start_preheat_for_op` — load/extrude/purge/etc.) | ✅ on | controller computes the max; replaced the old target-only check |
| AMS load-with-preheat (`handle_load_with_preheat`) | ✅ on | skip/wait decision also uses `max(actual, latch)` so a cooled nozzle still reheats to purge |
| Manual keypad entry (`handle_custom_nozzle_confirmed`) | ❌ off | deliberate override |
| Cooldown-to-0 | ❌ off | must still reach 0 |

**User feedback.** When (and only when) the guard raises the target above the request, an info toast fires: *"Holding nozzle at N°C to purge previous filament."* (plus an `spdlog::info` line). No toast when the request already clears the floor.

---

## Supported Backends

### AmsType Enum

```cpp
enum class AmsType {
    NONE = 0,         // No AMS detected
    HAPPY_HARE = 1,   // Happy Hare MMU (mmu object in Moonraker)
    AFC = 2,          // AFC-Klipper-Add-On (AFC object, lane_data database)
    ACE = 3,          // AnyCubic ACE Pro (ValgACE/BunnyACE/DuckACE Klipper drivers)
    TOOL_CHANGER = 4, // Physical tool changer (viesturz/klipper-toolchanger)
    AD5X_IFS = 5,     // FlashForge AD5X IFS (Intelligent Filament Switching)
    CFS = 6,          // Creality Filament System (K2 series, RS-485)
    SNAPMAKER = 7,    // Snapmaker U1 SnapSwap toolchanger
    QIDI_BOX = 8      // QIDI Box (PLUS4 / Q2 / MAX4, hub-style, 4 slots chainable to 16) — STUB
};
```

Helper functions: `is_tool_changer()` and `is_filament_system()` distinguish between the two categories.

---

## Endless Spool (shared model)

Every backend means the same thing by "endless spool" - when a slot runs dry mid-print,
something switches to another slot that can stand in for it - but each firmware answers a
different set of questions about it. The shared model is three things:

| Piece | Where |
|-------|-------|
| `EndlessSpoolCapabilities` - what a backend can do, on three axes | `include/ams_types.h` § "Endless Spool Types" |
| `EndlessSpoolConfig` / `EndlessSpoolGroup` - the relation itself | same |
| Restriction text, the two config builders, the one projection | `src/printer/ams_endless_spool.cpp` |

Nothing in `ams_endless_spool.cpp` touches a backend, a mutex or LVGL, so it is directly
unit-testable (`tests/unit/test_ams_endless_spool.cpp`).

### Three axes, not one bool

| Axis | Type | Values |
|------|------|--------|
| Availability | `EndlessSpoolAvailability` | `Unsupported` / `RequiresPlugin` / `Available` |
| Enablement | `EndlessSpoolEnabled` | `Unknown` / `Off` / `On` |
| Editability | `EndlessSpoolEditability` | `ReadOnly` / `PerSlot` / `Group` |

The axes are independent because real backends occupy the corners. CFS is
available-and-read-only whether auto-refill is on or off, so a single `supported` bool
rendered both states identically. `RequiresPlugin` is retained in the enum for a future
backend whose package genuinely can be missing; no backend currently uses it, since the
AD5X stock-zMod path moved to `Available`/`FirmwareManaged` once source-read of
`ANALOG_PRUTOK` established that switchover is always-on there. `Unknown` is not `Off`: only
`Off` justifies telling the user that no automatic switchover will happen.

Editability carries a shape, not just a yes/no, because the write shape matters to the UI: a
`PerSlot` write touches one slot (AFC `SET_RUNOUT`), while a `Group` write can move other
slots' relations as a side effect because the transport rewrites the whole partition (Happy
Hare `GROUPS=<csv>`).

`EndlessSpoolRestriction` says **why** editing is restricted, as an enum: `None`,
`MultiUnit`, `FirmwareManaged`, `NotReady`, `PluginMissing`, `PluginReadOnly`. Display text
comes from `endless_spool_restriction_text()`, which is the only place `lv_tr()` is involved.
The struct's one free-text field is `provider`, the proper noun of the package implementing
the feature (`"lessWaste"`, `"bambufy"`), empty when the backend or firmware implements it
natively - a product name is never translated, which is why it is allowed to be free text.
`available()` and `editable()` are the convenience predicates callers use; `editable()`
implies `available()`.

This replaced `{bool supported; bool editable; std::string description;}`, where
`description` was never displayed yet carried load-bearing state as untranslated English
("Auto-refill enabled", "...read-only on multi-unit") that no UI could safely show in any
language but ours.

### Groups, and the single projection

`get_endless_spool_config()` returns **one** `EndlessSpoolConfig` for the whole system,
holding `std::vector<EndlessSpoolGroup>`. A group has `id` (the backend's own group number,
or -1 for one we synthesised), `members` (global slot indices) and `ordered`:

- `ordered = true` - `members[i]` hands off to `members[i+1]`; the last member has no
  successor. An AFC `SET_RUNOUT` edge is a two-member ordered group. Overlapping ordered
  groups are legal: AFC permits 0->2 and 1->2, which is two pairs sharing slot 2.
- `ordered = false` - any member substitutes for any other. Happy Hare's gate group is one
  undirected group of arbitrary size, and an unordered relation is a partition.

Two builders construct it. `endless_spool_config_from_edges(edges)` takes per-slot directed
backups (`-1` or a self-edge is skipped) and emits one two-member ordered group per edge.
`endless_spool_config_from_groups(group_ids)` takes per-slot group ids - Happy Hare's shape -
and emits one unordered group per id, dropping any group with fewer than two members: a group
of one backs nothing up, and emitting it would make "is grouped" and "has a backup" disagree,
which matters because Happy Hare gives every ungrouped gate its own standalone id.

Anything that needs one successor per slot calls the projection and never re-derives it:
`endless_spool_backup_edges(cfg, slot_count)` for a whole system,
`endless_spool_backup_for(cfg, slot)` for one slot. Ordered groups project along their order.
Unordered groups project onto a **ring**: `members[i] -> members[i+1]`, last back to first.
Both entry points agree that the first group to give a slot a successor wins, so they cannot
disagree on a hand-built config with two successors for one slot. The two production callers
are `AmsPanel::update_endless_arrows_from_backend()` (`src/ui/ui_panel_ams.cpp`) and
`AmsContextMenu::get_current_backup_for_slot()` (`src/ui/ui_ams_context_menu.cpp`), so the
arrows and the dropdown cannot disagree about a Happy Hare group.

**Why a ring and not "the first other member".** The projection originally pointed every
member at the first *other* member, reproducing the arrow set Happy Hare's backend computed
inline with a `// Use first match` loop. For a 4-gate group that draws 0->1, 1->0, 2->0, 3->0
— a picture that says "gate 1 is everyone's backup", which is not what a clique means. A ring
gives every member exactly one successor and visits the whole group, which is the closest a
one-target-per-source edge view can get to "any member substitutes for any other".

**What the arrow widget cannot express, and is not asked to.** `ui_endless_spool_arrows`
(`src/ui/ui_endless_spool_arrows.cpp`) takes `backup_slots[source] = target`: one target per
source, at most 16 slots, drawn as a directed dashed up-over-down line with an arrowhead at
the target. It has no primitive for a pool — no bracket, no shared container, no undirected
edge — so an N-member clique genuinely cannot be drawn as a clique, and drawing all N*(N-1)
directed arrows would be unreadable at 480x272 even if it were correct. The ring is the honest
fallback, not a claim to be the whole relation. The widget does now clamp its stacked route
heights to the canvas: an N-member group projects to N mutually-overlapping arrows, and the
unclamped height ladder used to walk past the bottom edge and draw the "vertical" segments
inverted, outside the widget.

### The status line

Capabilities are only worth having if the user can see them. `endless_spool_status(caps)`
(`src/printer/ams_endless_spool.cpp`) is the one place the enums become a sentence. It returns
`{EndlessSpoolStatusKind kind, std::string text}`; `AmsState::sync_endless_spool_from_backend()`
publishes those on two backend-neutral XML subjects, from `sync_from_backend()` — main thread,
because the `EVENT_STATE_CHANGED` handler already marshals through `helix::ui::queue_update()`.

| Subject | Type | Meaning |
|---------|------|---------|
| `ams_endless_state` | int, `EndlessSpoolStatusKind` | `Hidden` 0 / `On` 1 / `Off` 2 / `Unknown` 3 / `NeedsPlugin` 4. A UI contract — append, never renumber. `Hidden` is 0 so one `bind_flag_if_eq ref_value="0"` hides the row |
| `ams_endless_text` | string | The translated sentence, possibly with an embedded newline. Bind to a `long_mode="wrap"` label |

Wording rules, all pinned by tests in `test_ams_endless_spool.cpp`:

- `Unsupported` renders **nothing**. Not "off": a printer with no such mechanism is not a
  printer with the mechanism switched off, and the row disappears rather than asserting
  something about a feature that does not exist.
- `Unknown` is phrased as unknown. Only `Off` says "nothing will switch" — that is the whole
  reason enablement is tri-state, and flattening `Unknown` to `Off` is a promise we cannot
  keep.
- `RequiresPlugin` names `provider` when the backend knows the package
  ("Needs the lessWaste package to switch spools") and otherwise falls back to the restriction
  text. **No backend populates `provider` in that state today**: AD5X cannot know whether the
  user would install lessWaste or bambufy, so the generic
  "No automatic backup-spool package is installed" is what actually renders on stock zMod.
- A non-`None` restriction is appended on its own line, from
  `endless_spool_restriction_text()` and never a second copy of that prose. "It will not
  switch" and "and here is why you cannot change that from here" are two different facts.
- A non-empty `provider` is attributed parenthetically ("… on runout (bambufy)"). A proper
  noun needs no translation, so this costs no string.

**Where it renders, and where it deliberately does not.** Two homes, one component
(`ui_xml/components/ams_endless_status.xml`, registered in `src/xml_registration.cpp` ahead of
`filament_panel.xml` because the AMS panel registers itself lazily). The component is entirely
subject-driven and needs no C++ of its own.

| Surface | Why |
|---------|-----|
| AMS panel, inside `slot_area` under the slots | It explains the arrows at the top of that same container. Growth is absorbed by `path_container`, which is `flex_grow="1"` and whose canvas scales. Note it goes in `slot_area`, not directly in `ams_unit_card` — the card has no `flex_flow`, so a second child there stacks *on top of* the slots |
| Slot context menu, under `backup_dropdown_row` | Where the disabled-or-absent backup dropdown actually is, and the only surface a CFS user reaches at all: CFS hides the dropdown row entirely (no per-slot relation), so without this line tapping a slot said nothing about runout behaviour |

**Not on the filament panel**, despite it being the obvious second home. Its `left_column` is a
fixed height budget with `temp_graph_card` as the flexible remainder
(`FilamentPanel::apply_left_column_sizing()`), so any row added to `spool_card` is paid for
entirely by the temperature graph. Measured with a two-line status: 172 -> 76 px at LARGE,
118 -> 30 px at MEDIUM; and at MICRO it does not fit at all (`spool_card` is 74 px there, 48 of
it `ams_manage_row`).

**Measured at 480x272 (MICRO).** Label width 259 px in the AMS card, 15 px per line. Every
headline fits one line in all nine languages. Four of the five restriction texts need two lines
in `ru` and `es`, two do in `fr`, two in `pt`, one in `de`; `en`, `it` and `zh` fit all five on
one line, `ja` needs two for one. Worst total is therefore 3 lines / 45 px, which takes
`path_canvas` from 112 to 97 px with `scroll_bottom` staying negative — nothing clips, because
`long_mode="wrap"` cannot clip. The Russian `Unknown` headline was shortened to
"Резервная катушка: состояние неизвестно" precisely to hold that 3-line ceiling; at its
original length the worst case was 4 lines / 60 px and `path_canvas` fell to 82 px.

### What the base owns, what a backend supplies

`AmsBackend::set_endless_spool_backup()` is **deliberately not virtual**
(`src/printer/ams_backend.cpp` § "Endless Spool - shared validation"). It owns every
rejection, in order: feature unavailable; feature read-only (carrying the translated
restriction reason); `endless_spool_slot_count() <= 0`, reported as `NotReady`; `slot_index`
out of range; `backup_slot` out of range; `backup_slot == slot_index`. Three backends used to
write those same guards with three different phrasings of the self-backup error.

A backend supplies only these:

| Hook | Responsibility |
|------|----------------|
| `apply_endless_spool_backup(slot, backup)` (protected virtual) | Transport only. Reached **after** the base accepted the write, so it must not re-check availability, editability, ranges or self-backup - and must not update a local mirror of the mapping before its transport has accepted the command. |
| `endless_spool_slot_count()` (protected virtual) | How many slots the relation spans; drives range validation and the reset loop. Default `get_system_info().total_slots`. Override when the transport's slot space differs, or to report 0 while not ready. |
| `is_endless_spool_backup_eligible(slot, backup)` | Is this pairing acceptable? Base default is the material-compatibility test the AMS context menu has always applied (`filament::are_materials_compatible()`, with an unknown material on either side counting as eligible rather than blocking a slot the user simply has not labelled). AD5X IFS overrides it with the rule its firmware actually enforces - exact material **and** exact colour **and** the port reporting filament present - sharing `backup_eligible_locked()` with `find_backup_slot_locked()` so its runout hint text and its eligibility answer cannot diverge. |

`reset_endless_spool()` has a real base implementation: walk
`set_endless_spool_backup(slot, -1)` over every slot, continue past failures so it clears as
many as it can, return the first error. That loop was AFC's private implementation; AFC
deleted its copy and every editable backend gets it now. Happy Hare overrides it because its
firmware has an actual primitive.

`get_endless_spool_capabilities()` and `get_endless_spool_config()` overrides take the
backend's own `mutex_`, so callers must not hold it. `set_endless_spool_backup()` holds no
lock and hands off to the hook with no lock held.

### `endless_spool_enabled` is a carrier, not a second answer

`AmsSystemInfo::endless_spool_enabled` is the ENABLE axis only. It exists because the
WebSocket parse builds an `AmsSystemInfo` off the main thread and commits it under the
backend mutex, so the parsed bit needs a home in that struct: CFS `box.auto_refill` (stock) /
`box.runout_swap_enabled` (flat fork), Happy Hare `mmu.endless_spool_enabled`, AD5X
`variable_backup` from the `_ifs_vars` macro's status dict.
`get_endless_spool_capabilities()` is the single source of truth for all three axes and
**derives** `caps.enabled` rather than answering independently, so the two cannot diverge.
Read the capabilities, not the field.

CFS, Happy Hare and the mock read the field directly. Two backends are one step removed and
say so at the site: AD5X IFS keeps a `std::optional<bool>` (`ifs_backup_variable_`) as its
source of truth because a plain bool cannot express `Unknown`, and mirrors it into the field
so `get_system_info()` agrees; AFC has no enable bit to read at all and reports `On`
unconditionally, its field seeded once from `afc_default_capabilities()`.

The field replaced `AmsSystemInfo::supports_endless_spool`, which answered the *availability*
question a second time and provably disagreed with `get_endless_spool_capabilities()` on CFS
whenever auto-refill was off.

### Per-backend state

| Backend | Availability | `enabled` derived from | Editability | Restriction | Per-slot relation? |
|---------|--------------|------------------------|-------------|-------------|--------------------|
| AFC | `Available` | Hardcoded `On` - a lane either names a runout lane or it does not, so there is no on/off switch to read | `PerSlot` (`SET_RUNOUT LANE= RUNOUT=`) | `None` | Yes - `endless_spool_config_from_edges(slots_.backup_edges())` |
| Happy Hare | `Available` | `mmu.endless_spool_enabled`; forced to `Unknown` before `slots_` is initialised | `Group` on a single unit, `ReadOnly` otherwise | `None`; `MultiUnit` on a multi-unit rig; `NotReady` before the registry initialises | Yes - `endless_spool_config_from_groups()` over each gate's `endless_spool_group` |
| CFS | `Available` | `box.auto_refill` / `box.runout_swap_enabled` | `ReadOnly` | `FirmwareManaged` | **No, deliberately** - see below |
| AD5X IFS | `Available` in all three modes (stock zMod, bambufy, lessWaste) | stock zMod: always `On` (ANALOG_PRUTOK has no toggle); plugin path: `variable_backup`, with a genuine `Unknown` when it was never read | `ReadOnly` | `FirmwareManaged` on stock zMod; `PluginReadOnly` on the plugin path | No |
| ACE, QIDI Box, Snapmaker U1, Tool Changer | `Unsupported` (base default; no override at all) | -- | `ReadOnly` | `None` | No |
| Mock | `set_endless_spool_supported()` | `system_info_.endless_spool_enabled` | `PerSlot` when `set_endless_spool_editable(true)`, else `ReadOnly` | `FirmwareManaged` when read-only | Yes - edges from its `SlotRegistry` |

CFS, and AD5X IFS in every mode (stock zMod, bambufy, lessWaste), report `Available` while
leaving `get_endless_spool_config()` unoverridden. That is the truthful answer, not an
omission: the firmware picks the backup itself and exposes no per-slot mapping to read, so
the base's empty relation is correct, and it is what keeps the context menu from drawing a
dropdown that could only ever read "None" (see [Context Menu Actions](#context-menu-actions)).

---

## Happy Hare (MMU)

Happy Hare is a Klipper add-on for ERCF, Tradrack, and other selector-based multi-filament systems.

### Detection

Klipper object `mmu` in `printer.objects.list` sets `AmsType::HAPPY_HARE`.

### Moonraker Variables

| Variable | Type | Description |
|----------|------|-------------|
| `printer.mmu.gate` | int | Current gate (-1=none, -2=bypass) |
| `printer.mmu.tool` | int | Current tool number |
| `printer.mmu.filament` | string | "Loaded" or "Unloaded" |
| `printer.mmu.action` | string | "Idle", "Loading", "Unloading", "Forming Tip", etc. |
| `printer.mmu.gate_status` | int[] | Per-gate: -1=unknown, 0=empty, 1=available, 2=from_buffer |
| `printer.mmu.gate_color_rgb` | int[] | Per-gate RGB colors (0xRRGGBB) |
| `printer.mmu.gate_material` | string[] | Per-gate material names |
| `printer.mmu.filament_pos` | int | 0-8 filament position for path visualization |

### G-code Commands

| Command | Action |
|---------|--------|
| `MMU_LOAD GATE={n}` | Load filament from gate |
| `MMU_UNLOAD` | Unload current filament |
| `MMU_SELECT GATE={n}` | Select gate without loading |
| `T{n}` | Tool change (unload + load) |
| `MMU_HOME` | Home the selector (reset) |
| `MMU_RECOVER` | Attempt error recovery |
| `MMU_TTG_MAP TOOL={n} GATE={g}` | Set tool-to-gate mapping |
| `MMU_SELECT_BYPASS` | Select bypass position |

### Path Topology

`PathTopology::LINEAR` -- Selector picks one input from multiple gates. Filament path: `SPOOL -> PREP -> LANE -> HUB (selector) -> OUTPUT (bowden) -> TOOLHEAD -> NOZZLE`.

Happy Hare's `filament_pos` (0-8) maps to `PathSegment` via `path_segment_from_happy_hare_pos()`.

### Capabilities

| Feature | Supported | Editable |
|---------|-----------|----------|
| Endless Spool | `Available` | `Group` on a single-unit MMU; `ReadOnly` + `MultiUnit` on multi-unit, `ReadOnly` + `NotReady` before the gate registry initialises (see [Endless Spool](#endless-spool-shared-model)) |
| Tool Mapping | Yes | Yes (via `MMU_TTG_MAP`) |
| Bypass Mode | Yes | Yes (selector position -2), when `[mmu_machine] has_bypass` is set. `has_bypass: 0` hides the UI but `MMU_SELECT_BYPASS` still works - see [the force override](#bypass-visibility-and-the-force-override) |
| Spoolman | Yes | -- |
| Auto-Heat on Load | No | UI manages preheat |
| Dryer | Yes | `MMU_HEATER` (see [Happy Hare Specifics](#happy-hare-specifics)) |
| Lane Eject | Yes | `supports_lane_eject()` + `eject_lane()` |

Happy Hare's endless spool is group-based and settable at runtime, not a config-file
read: `apply_endless_spool_backup()` builds a full `GROUPS=` array (one non-negative group
id per gate) and sends `MMU_ENDLESS_SPOOL QUIET=1 GROUPS=<csv>`.
`get_endless_spool_capabilities()` reports `Group` editability only when
`system_info_.units.size() <= 1`: `MMU_ENDLESS_SPOOL` has no `UNIT=` parameter and acts on
the currently-selected unit, so a client cannot reliably target one unit's groups on a
multi-unit (EMU) rig. `get_endless_spool_config()` returns the gate group as one unordered
`EndlessSpoolGroup` per group id; flattening it to per-slot arrows is the renderer's job
(see [Endless Spool](#endless-spool-shared-model)).

**`ENABLE=` on edit vs on reset.** An edit sends **no** `ENABLE=`, and
`apply_endless_spool_backup()` refuses with `WRONG_STATE` when
`mmu.endless_spool_enabled` is false: `cmd_MMU_ENDLESS_SPOOL` ignores `GROUPS` while the
feature is off, so the write would fail silently. An unconditional `ENABLE=1` is not the
fix - it turns the feature **on**, persistently via `mmu_state_enable_endless_spool`, as a
side effect of setting one backup gate. `reset_endless_spool()` does keep
`MMU_ENDLESS_SPOOL ENABLE=1 RESET=1 QUIET=1`, because the handler early-returns before
honouring `RESET` while disabled, and `_reset_endless_spool()` then assigns *and* persists
`default_endless_spool_enabled` over the momentary enable - so there it is not a lasting
side effect.

`endless_spool_enabled` is in the `mmu` subscription field list
(`src/api/moonraker_discovery_sequence.cpp`) alongside `endless_spool_groups`. Happy Hare
publishes the bit under two keys, `endless_spool_enabled` and `endless_spool`, both tagged
DEPRECATED in `mmu.py`'s `get_status()` with no replacement shipped, so the parse reads the
newer spelling and falls back to the older one; if a future Happy Hare drops both, the flag
keeps its last value instead of silently flipping to off. It lands in
`AmsSystemInfo::endless_spool_enabled`, which is what `caps.enabled` is derived from. Before
the first frame the flag is still false, which is why the uninitialised-registry branch
reports `Unknown` + `NotReady` rather than `Off`.

`recovers_filament_on_resume()` is **not** overridden here (default `false`), so a Happy
Hare runout gets the dialog with manual **Load** kept prominent, because Resume alone does not
re-feed. `supports_per_tool_spool_assignment()` is not overridden either; it falls through
to `is_tool_changer(get_type())`, which is false for an MMU.

### Reset vs Recover

- **Reset** (`reset()`) sends `MMU_HOME` to home the selector. Used for general state reset.
- **Recover** (`recover()`) sends `MMU_RECOVER` to attempt error recovery without full re-homing.
- **Clear fault** (`clear_fault(slot_index)`) is a third, gate-scoped door onto the same command.
  Happy Hare overrides the base default (which forwards to `cancel()`): `slot_index >= 0` sends
  `MMU_RECOVER GATE=<n>`, and `slot_index < 0` (what both UI callers pass whenever nothing is
  loaded, which is the state Reset is pressed in) drops the parameter and sends bare
  `MMU_RECOVER`, re-syncing the whole selector.

---

## AFC (Armored Turtle / Box Turtle)

AFC-Klipper-Add-On supports multiple hardware types (Box Turtle, OpenAMS) with different topologies. A single AFC installation can mix hardware types — e.g., a Box Turtle feeding 4 toolheads alongside two OpenAMS units each feeding 1 toolhead.

### Hardware Types and Klipper Objects

AFC hardware types register as different Klipper object prefixes:

| Hardware | Klipper Object Prefix | Lane Object | Hub Object | Topology |
|----------|----------------------|-------------|------------|----------|
| Box Turtle | `AFC_BoxTurtle {name}` | `AFC_stepper lane{N}` | `AFC_hub {name}` or none | HUB (standard) or PARALLEL (toolchanger) |
| OpenAMS | `AFC_OpenAMS {name}` | `AFC_lane lane{N}` | `AFC_hub Hub_{N}` | HUB (always) |
| Toolchanger | `AFC_Toolchanger {name}` | — | — | Container only |

**Critical: OpenAMS uses `AFC_lane`, not `AFC_stepper`.** Both have the same JSON schema and are parsed through the same `parse_afc_stepper()` function. We subscribe to both object types.

### Unit Object Structure (Real Production Data)

Each unit-type object provides lane/extruder/hub/buffer membership. This data comes from Klipper, not from individual lane queries.

**Box Turtle in toolchanger mode** (`AFC_BoxTurtle Turtle_1`):
```json
{
    "lanes": ["lane0", "lane1", "lane2", "lane3"],
    "extruders": ["extruder", "extruder1", "extruder2", "extruder3"],
    "hubs": [],
    "buffers": ["TN", "TN1", "TN2", "TN3"]
}
```
- 4 extruders → PARALLEL topology (each lane feeds its own toolhead)
- No hubs (lanes use `hub: "direct_load"` — direct connection to extruder)
- TurtleNeck buffers per lane

**OpenAMS** (`AFC_OpenAMS AMS_1`):
```json
{
    "lanes": ["lane4", "lane5", "lane6", "lane7"],
    "extruders": ["extruder4"],
    "hubs": ["Hub_1", "Hub_2", "Hub_3", "Hub_4"],
    "buffers": []
}
```
- 1 extruder → HUB topology (all 4 lanes converge to 1 toolhead)
- Per-lane hubs: each lane has its own hub (Hub_1 for lane4, Hub_2 for lane5, etc.)
- Hub names do NOT match unit names (Hub_1 ≠ AMS_1)
- No buffers (no TurtleNeck needed)

### Topology Determination

AFC topology is inferred from the extruder count per unit:
- **1 extruder** → `PathTopology::HUB` (all lanes merge to one toolhead)
- **N extruders (N == lane count)** → `PathTopology::PARALLEL` (1:1 lane-to-tool mapping)

This is stored per-unit in `unit_topologies_[]` and queried via `get_unit_topology(unit_index)`.

### The `map` Field Problem

AFC assigns each lane a virtual tool number via the `map` field (e.g., `"T4"`). **For HUB units, AFC gives each lane a unique map value even though all lanes physically feed the same extruder.**

Real production data from a 6-toolhead mixed system:

| Lane | Unit | Hub | Extruder | map | Physical Tool |
|------|------|-----|----------|-----|---------------|
| lane0 | Turtle_1 | direct_load | extruder | T0 | T0 |
| lane1 | Turtle_1 | direct_load | extruder1 | T1 | T1 |
| lane2 | Turtle_1 | direct_load | extruder2 | T2 | T2 |
| lane3 | Turtle_1 | direct_load | extruder3 | T3 | T3 |
| lane4 | AMS_1 | Hub_1 | extruder4 | T4 | T4 |
| lane5 | AMS_1 | Hub_2 | extruder4 | T5 | **T4** (same physical nozzle) |
| lane6 | AMS_1 | Hub_3 | extruder4 | T6 | **T4** (same physical nozzle) |
| lane7 | AMS_1 | Hub_4 | extruder4 | T7 | **T4** (same physical nozzle) |
| lane8 | AMS_2 | Hub_5 | extruder5 | T8 | T5 |
| lane9 | AMS_2 | Hub_6 | extruder5 | T9 | **T5** (same physical nozzle) |
| lane10 | AMS_2 | Hub_7 | extruder5 | T10 | **T5** (same physical nozzle) |
| lane11 | AMS_2 | Hub_8 | extruder5 | T11 | **T5** (same physical nozzle) |

**The `map` field represents virtual tool numbers for AFC's internal routing, not physical toolheads.** The UI must use topology to determine physical tool count for drawing nozzles:
- PARALLEL: `tool_count = max_tool - min_tool + 1` (each map value = different nozzle)
- HUB: `tool_count = 1` (all map values = same nozzle)

### Hub Sensor Propagation

Standard Box Turtle: hub name matches unit name (e.g., hub "Turtle_1" for unit "Turtle_1"), so `hub_name == unit.name` works.

OpenAMS: hub names are per-lane (Hub_1, Hub_2, ..., Hub_8) and do NOT match the unit name (AMS_1, AMS_2). Hub sensor state must be propagated by looking up which unit owns the hub via the `unit_infos_` hub membership lists.

The code uses a two-strategy approach:
1. Check `unit_infos_[].hubs` to find the parent unit (handles OpenAMS)
2. Fallback: direct `hub_name == unit.name` match (handles standard Box Turtle)

If ANY hub in a unit is triggered, `unit.hub_sensor_triggered = true`.

### AFC Lane Status Values

Status values observed in production and their mapping to `SlotStatus`:

| AFC Status | `tool_loaded` | Meaning | SlotStatus |
|------------|---------------|---------|------------|
| `"Tooled"` | any | Actively loaded in toolhead (OpenAMS) | LOADED |
| `"Loaded"` | true | Filament loaded to toolhead | LOADED |
| `"Loaded"` | false | Filament loaded to hub (not toolhead) | AVAILABLE |
| `"Ready"` | false | Filament present, sensors triggered | AVAILABLE |
| `"None"` | false | No filament, no sensors | EMPTY |
| `"Error"` | any | Lane error | AVAILABLE + SlotError |
| `""` (empty) | false | No data yet | EMPTY |

**Critical**: AFC's `"Loaded"` status means hub-loaded, NOT toolhead-loaded. The `tool_loaded` boolean is the authoritative indicator of toolhead presence. Only `tool_loaded: true` or `status: "Tooled"` maps to `SlotStatus::LOADED`. The `"Loaded"` status string alone (with `tool_loaded: false`) maps to `AVAILABLE`.

### Other AFC Lane Fields

Fields present in production `AFC_lane` data but not in `AFC_stepper`:
- `buffer: null` and `buffer_status: null` (OpenAMS has no buffers)
- `dist_hub: 60` (OpenAMS, short distance) vs `1940-2230` (Box Turtle, long bowden)
- `td1_td`, `td1_color`, `td1_scan_time` — TD1 filament tag detection sensor data (not currently used by HelixScreen)

### Detection

Klipper object `AFC` in `printer.objects.list` sets `AmsType::AFC`. Lane names come from `AFC_stepper lane*` and `AFC_lane lane*` objects, hub names from `AFC_hub *` objects. Unit-type objects (`AFC_BoxTurtle`, `AFC_OpenAMS`) provide the lane/extruder/hub/buffer membership that determines per-unit topology.

### Data Sources

AFC state comes from multiple Klipper objects:

**Per-lane state** (`AFC_stepper lane{N}` or `AFC_lane lane{N}`):

| Field | Type | Description |
|-------|------|-------------|
| `prep` | bool | Prep sensor triggered |
| `load` | bool | Load sensor triggered (AFC calls this `raw_load_state` internally) |
| `loaded_to_hub` | bool | **DO NOT USE — see "Fields that do not mean what they say"** |
| `tool_loaded` | bool | Filament loaded to toolhead |
| `status` | string | "Loaded", "Tooled", "Ready", "None", "Error" |
| `color` | string | Filament color hex (`#RRGGBB`) |
| `material` | string | Material type from Spoolman |
| `spool_id` | int | Spoolman spool ID |
| `weight` | float | Remaining weight in grams |
| `buffer_status` | string | Buffer state (e.g., "Advancing") |
| `filament_status` | string | Readiness (e.g., "Ready", "Not Ready") |
| `dist_hub` | float | Distance to hub in mm |

**Hub state** (`AFC_hub {name}`):

| Field | Type | Description |
|-------|------|-------------|
| `state` | bool | Hub sensor triggered. **One sensor per UNIT, shared by every lane on it** — it cannot say whose filament tripped it. Trustworthy, unlike `loaded_to_hub`. |
| `afc_bowden_length` | float | Bowden tube length from hub to toolhead (mm) |

**Extruder state** (`AFC_extruder extruder`):

| Field | Type | Description |
|-------|------|-------------|
| `tool_start_status` | bool | Toolhead entry sensor |
| `tool_end_status` | bool | Toolhead exit/nozzle sensor |
| `lane_loaded` | string | Currently loaded lane name |

**Global state** (`AFC`):

| Field | Type | Description |
|-------|------|-------------|
| `current_lane` | string | Lane AFC is working (or null). Null after a crash-interrupted toolchange — see below. |
| `current_load` | string | Lane being loaded (or null). Fallback when `current_lane` is null. |
| `current_state` | string | "Idle", "Loading", "Unloading", "Error", etc. |
| `error_state` | bool | **Not the error signal.** Measured `false` for an entire session while an error was queued. Use `message.type == "error"`. |
| `message` | object | `{message, type}` — **the HEAD of a FIFO queue, not a scalar.** See below. |
| `lanes[]` | string[] | List of lane names |
| `quiet_mode` | bool | Quiet mode state |
| `led_state` | bool | LED strip on/off |

### Fields that do not mean what they say

Established by measurement on a live BoxTurtle (2026-07-27) and by reading
`AFC-Klipper-Add-On` v1.2.0. Every one of these cost real debugging time; do not re-derive them.

**`AFC_stepper.<lane>.loaded_to_hub` is latched and inert.** It is set once at prep and never
updated. On a 4-lane unit it reads `true` on **all four lanes simultaneously** while the shared
hub sensor reads clear — physically impossible for one hub. It does not change when filament
actually transits the hub. Verified by pushing a lane 250 mm past the hub and retracting it:
`AFC_hub.state` tracked the move exactly, `loaded_to_hub` never moved.

*Use `AFC_hub.<hub>.state` for hub occupancy.* Resolve a lane's hub through the per-lane `hub`
field (`"Turtle_1"`, or the literal `"direct"` meaning no hub in that lane's path).

**`AFC.message` is a FIFO queue head.** Each `AFC_CLEAR_MESSAGE` pops exactly one entry;
Klipper's own help string reads *"clear error and warning message from AFC message queue"*. A
new error raised while an older one is unacknowledged is enqueued **behind** it and cannot
display until the earlier entry is popped. Observed depth 4 during one real failure, with a
slicer-deprecation warning at the head hiding the actionable load error behind it. Clearing an
already-empty queue is a harmless no-op.

*The queue only ever grows on its own.* One entry per `AFC_logger.error()` / `.warning()`
call — **not** one per line; the per-line loop in `AFC_logger.py` writes the log file, and the
`message_queue.append((message, ...))` that follows it sits outside that loop, so a five-line
`TOOL_LOAD` diagnostic is a single entry carrying embedded newlines. Nothing pops entries
implicitly: `reset_failure()` (`AFC_error.py`) and `AFC_RESUME` both leave `message_queue`
untouched, so entries accumulate across a whole session and anything left behind resurfaces as
the next session's stale error. (Verified against the add-on source on a live BoxTurtle,
2026-07-29.)

*A single clear is not enough.* `AmsBackendAfc::clear_fault()` drains **until the queue reports
empty**, bounded by a wall-clock deadline and by `MESSAGE_DRAIN_MAX_CLEARS` as a runaway guard
(not as the expected stopping point); see `message_drain_budget_` / `message_drain_deadline_`.

**`AFC.error_state` is not the *detection* signal.** It stayed `false` for a whole session while
`message` held an error, so `message.type == "error"` is what tells you a fault exists. But
`error_state_` is far from inert: besides `error_segment_` and the `classify_error` catch-all, it
is the entire gate on `AmsBackendAfc::current_error()`, which returns `nullopt` unless it is set.
That is the whole status-driven fault channel for AFC; see
§ [Two error channels](#two-error-channels).

**The hub sensor cannot attribute a strand to a lane.** One sensor per unit, shared. When a
strand is stuck past the hub, every lane on that unit looks identical — during a live failure
lanes 1 and 4 both read `prep=True load=True loaded_to_hub=True` and only lane 4's filament was
actually in the hub. `AFC.current_lane` is the only attribution signal, and it is null after a
Klipper crash mid-toolchange. **Software cannot determine this from sensors.** See
`active_load_lane_` and `can_recover_lane_position()`.

This is not a signal that is merely unwired. It does not exist. The full measured state during
that failure:

```
lane1  prep=True  load=True  loaded_to_hub=True
lane4  prep=True  load=True  loaded_to_hub=True
AFC_hub Turtle_1.state = True   (one sensor, shared by all four lanes)
AFC.current_lane = None
```

Console history pointed at lanes 1 and 2. The answer was lane 4, and only looking at the machine
established it. Anything that claims to pick the stranded lane out of sensor data is guessing,
and it will be wrong three times in four on a 4-lane unit.

**A failed `AFC_LANE_RESET` names the wrong lane, it does not report a failure.**
`cmd_AFC_LANE_RESET` retracts the named lane until the hub clears, bailing if *that lane's* own
switch opens first:

```
"'{lane}' failed to reset to hub, load switch became false during reset"   → wrong lane
"'{lane}' failed to reset to hub, prep switch became false during reset"   → wrong lane
"'{lane}' failed to reset to hub"  (no switch named)                       → nothing owns it
```

The first two also mean **that lane has now been retracted past its own switch** and will fail
its next load with "LOAD TRIGGER NOT TRIGGERED" until advanced forward again. The third means
the retract ran the full bowden without clearing — most likely a snapped fragment in the hub,
which no lane reset can ever clear.

**A wrong lane guess is destructive, not free.** The retract loop runs until *that lane's* own
switch opens, so the guess always ends with the lane pulled back behind its load sensor. In the
observed instance a guess at lane 1 left it `load=False`; a forward lane move of 20 mm restored
the switch (driven that night through BoxTurtle's `BT_LANE_MOVE` wrapper; the portable command
is `LANE_MOVE`), after which `T0` loaded normally. Until that forward move the lane is unusable,
and tapping the lane reset again only drags it further back.

*Automatic sequential retry is therefore rejected, deliberately.* Walking the roster on the
user's behalf leaves every lane it eliminates de-seated: four lanes tried, three working lanes
broken, to reach an answer a person standing at the machine can read off it directly. Do not add
it later as a convenience. The only defensible way to spend a guess is one at a time, with the
resulting de-seat undone before the next.

**When every lane on a hub has been eliminated, the hub holds a broken fragment.** Each
wrong-lane diagnostic rules out one candidate. Once the whole roster routed to that hub has
returned it, nothing on that unit owns the obstruction, no lane reset can ever clear it, and it
comes out by hand. AFC reaches the same conclusion on the load path: `AFC.py` raises *"Hub not
clear when trying to load. Please check that hub does not contain broken filament and is
clear"*. This case is not exotic; it occurred twice in one evening on the `.112` rig. A recovery
flow modelled only on "which lane is it" never terminates here.

**`AFC_LANE_RESET`'s toolhead guard does not actually stop it.** In v1.2.0 (`a06f14d`) the
hub-clear guard has a `return`; the toolhead guard does not:

```python
if not CUR_HUB.state:
    ...AFC_error("Hub is already clear while trying to reset '{lane}'")
    return                                  # returns

if (tool_load := self.get_current_lane_obj()) is not None:
    ...AFC_error("Toolhead is loaded with '{name}'...")
                                            # NO return — falls through and moves filament
```

So AFC logs the refusal and then retracts the lane anyway, while the extruder still grips the
filament. Reported as [AFCProject/AFC-Klipper-Add-On#803](https://github.com/AFCProject/AFC-Klipper-Add-On/issues/803),
open as of 2026-07-28.

*`can_recover_lane_position()`'s `filament_loaded` check is therefore load-bearing safety, not a
politeness mirror of an upstream guard.* Do not remove it as redundant.

**A filament swap resets lane identity when `remember_spool` is false.** AFC re-applies
`[afc] default_material_type` and `full_weight`, discarding material, colour and weight. Lanes
carrying a Spoolman `spool_id` survive; lanes without one silently revert. HelixScreen's
`FilamentSlotOverrideStore` (private AFC namespace) exists to preserve identity across this.

**Moonraker database** (AFC namespace, `lane_data` key -- v1.0.32+):

```json
{
  "lane1": {"color": "FF0000", "material": "PLA", "loaded": false},
  "lane2": {"color": "00FF00", "material": "PETG", "loaded": true}
}
```

### G-code Commands

Verified against `AFC-Klipper-Add-On` v1.2.0. Two kinds exist and the distinction matters:
**Python** commands are registered by AFC's extras modules and are always present; **config
macro** entries ship in AFC's `config/` templates and can be absent, renamed or edited on a
given machine. `BT_*` macros are BoxTurtle-specific and do not exist on other unit types.

| Command | Kind | Action |
|---------|------|--------|
| `CHANGE_TOOL LANE={name}` / `T{n}` | Python | Tool change (unload + load) |
| `TOOL_LOAD LANE={name}` | Python | Load a lane into the toolhead |
| `TOOL_UNLOAD` | Python | Unload the toolhead |
| `LANE_UNLOAD LANE={name}` | Python | Eject a lane's filament back to the spool |
| `LANE_MOVE LANE={name} DISTANCE={float}` | Python | Manual lane move. Negative retracts. **Refuses while printing** unless `FORCE=1`. Zero distance is an error. See the note below on `DISTANCE`'s type. |
| `HUB_LOAD LANE={name}` | Python | Advance a lane to its hub |
| `AFC_LANE_RESET LANE={name}` | Python | Retract a lane from the bowden back to its hub. Requires hub occupied + toolhead free. |
| `AFC_RESET` | Python | **Opens a lane-picker prompt**, not a system reset. Lists lanes with `raw_load_state` true and dispatches `AFC_LANE_RESET` for the chosen one. With no candidates: *"No lanes are loaded, a lane must be loaded to be reset"*. |
| `RESET_FAILURE` | Python | Clear AFC's failure state |
| `AFC_CLEAR_MESSAGE` | Python | Pop **one** entry from the message queue |
| `SET_LANE_LOADED LANE={name}` | Python | Mark a lane as toolhead-loaded without moving filament |
| `UNSET_LANE_LOADED` | Python | Clear the toolhead-loaded marker |
| `SET_MAP LANE={name} MAP=T{n}` | Python | Set lane-to-tool mapping |
| `SET_MATERIAL LANE={name} MATERIAL={type}` | Python | Set a lane's material |
| `SET_COLOR LANE={name} COLOR={hex}` | Python | Set a lane's colour |
| `SET_WEIGHT LANE={name} WEIGHT={g}` | Python | Set a lane's remaining weight |
| `SET_SPOOL_ID LANE={name} SPOOL_ID={id}` | Python | Link a lane to a Spoolman spool |
| `SET_BOWDEN_LENGTH HUB={hub} LENGTH={mm}` | Python | Set bowden length (mux keyed on `HUB`) |
| `SET_RUNOUT LANE={name} RUNOUT={backup_lane}` | Python | Set endless spool backup |
| `RESET_AFC_MAPPING RUNOUT=no` | Python | Reset tool mappings only |
| `AFC_CALIBRATION` | Python | Run calibration wizard |
| `AFC_RESET_MOTOR_TIME LANE={name}` | Python | Reset motor run-time counter |
| `AFC_QUIET_MODE` | Python | Toggle quiet mode |
| `TURN_ON_AFC_LED` / `TURN_OFF_AFC_LED` | Python | Toggle LED strip |
| `AFC_CUT` / `AFC_PARK` / `AFC_BRUSH` / `AFC_POOP` / `AFC_KICK` | config macro | Toolhead servicing. Ship in AFC's config templates; may be absent or edited. |
| `BT_LANE_MOVE` / `BT_LANE_EJECT` / `BT_TOOL_UNLOAD` / `BT_CHANGE_TOOL` / `BT_PREP` | config macro | **BoxTurtle only.** Thin wrappers over the Python commands above — prefer the Python command. |

**`LANE_MOVE`'s `DISTANCE` is a float, and AFC's own metadata says otherwise.** The
`cmd_LANE_MOVE_options` dict (`extras/AFC.py:1010`) labels it `{"type": "int"}`, but nothing
consumes that dict for parsing — the command body does `gcmd.get_float('DISTANCE', 0)`. Read the
function body, not the options metadata, when documenting any AFC command; the metadata is
descriptive and can be wrong about its own command. (This exact mistake was made and caught
while writing this section.)

`LANE_MOVE` also returns early with *"Cannot move lane while printer is printing"* unless
`FORCE=1`, and rejects a zero distance. Anything automating a lane move during a paused print
needs to account for both.

**Commands that do NOT exist.** These appeared in earlier revisions of this document and were
never real — verified absent from both AFC's Python registrations and its shipped config macros.
Do not reintroduce them:

| Fiction | Use instead |
|---------|-------------|
| `AFC_HOME` | Nothing homes AFC. `AFC_RESET` opens a lane picker; `reset()`/`recover()` both send `AFC_RESET`. |
| `AFC_LOAD` | `TOOL_LOAD LANE={name}` or `CHANGE_TOOL LANE={name}` |
| `AFC_UNLOAD` | `TOOL_UNLOAD` (toolhead) or `LANE_UNLOAD LANE={name}` (lane to spool) |
| `AFC_LANE_MOVE` | `LANE_MOVE` — the `AFC_` prefix is not real |

### AFC console response contract

AFC narrates its operations over `notify_gcode_response`, and the toolchange step bar is driven
entirely by matching those strings — there is no structured field for "which phase am I in". This
is the undocumented string contract of #1153; the shapes below were captured verbatim from a live
12-toolchange print on the BoxTurtle rig via `server/gcode_store`
(`N` = a digit run; the verbatim strings live in `tests/unit/test_afc_console_corpus.cpp`). Tests drive the exact
strings: `tests/unit/test_afc_console_corpus.cpp`.

**AFC emits narration on two different channels, and they need different matchers.**

#### Channel 1 — bare lines (no `//`, no `!!`)

`AmsBackendAfc::match_bare_narration_phase()`. These carry the *semantically important* half of a
toolchange. Klipper's `respond_raw` gives them no prefix at all, so before this was split out the
router's `//`-only filter discarded every one of them and the step bar could only ever advance on
the decorative cut/brush lines.

| Shape | Phase | Source |
|-------|-------|--------|
| `Loading laneN` | `feed` | `AFC.py` `TOOL_LOAD` |
| `Unloading laneN` | `unload` | `AFC.py` `TOOL_UNLOAD` |
| `laneN is now loaded in toolhead t:N` | `load` | load complete (`t:N` absent on pre-toolchanger builds) |
| `Lane laneN unload done t:N` | `unload` | unload complete |
| `Tool Change - laneN -> laneN`, `Tool Change - None -> laneN` | *(none)* | toolchange banner — no phase in the template |
| `Total change time: t:N` | *(none)* | toolchange end — no phase in the template |
| `laneN already loaded` | *(none)* | CHANGE_TOOL no-op (#1183). Must **not** read as a completed load |

**Bare lines are matched by anchored shape, never by substring.** The unprefixed channel is the
printer's open console: the same stream carries `B:N /N TN:N /N` temperature reports, `echo:` output
from user macros, `Rotation distance reset : N`, an HTML `<span class=warning--text>…</span>`
deprecation notice — and `File opened: <name>.gcode Size: N`, where the filename is
**user-controlled**. A loose `has("cut")` needle turns anyone's `haircut.gcode` into a Cut-tip step.
So each shape is pinned on fixed words in fixed positions plus a token count: `Loading laneN` matches
only as exactly two whitespace-separated tokens, and the load-complete line needs all five of
`is now loaded in toolhead` in sequence.

> **Residual exposure.** `Loading <one-word>` is the weakest shape — a user macro doing
> `M118 Loading mesh` during an active toolchange would advance the bar one step. Tightening it
> further would mean validating the second token against the configured lane names, which the
> matcher deliberately avoids: it is a pure function today, and reading the lane registry would put
> a lock into a per-console-line path for a cosmetic-only gain.

#### Channel 2 — `//` lines

`AmsBackendAfc::match_narration_phase()`. A `//` body came from a macro's own `respond_info`, so
upstream owns the wording and the matcher is deliberately loose: it normalizes to lowercase words
and substring-matches, so `AFC_Brush: Clean Nozzle`, `AFC Brush - Clean nozzle` and
`[AFC_Brush] Clean Nozzle!` all land on `brush`.

| Shape | Phase |
|-------|-------|
| `// AFC_Cut: …` (`Cut Filament`, `Moving to cutter pin`, `Retract Filament for Cut`, `Cut Move…`, `Final Cut…`, `Push cut tip back into hotend`, `Clearing cutter pin`) | `cut` |
| `// AFC_Brush: …` (`Clean Nozzle`, `Move to Brush.`, `Y Brush Moves`, `X Brush Moves`) | `brush` |
| `// AFC_Poop: …` (`Starting poop`, `Move To Purge Location`) | `poop` |
| `// AFC_Kick: …` | `kick` |
| `// AFC_Park: Park Toolhead` | *(none)* — AFC's park has no step in the template, so it stays unmatched rather than borrowing a neighbour. Adding it would mean adding a real phase. |
| `// Smart Park location: N,N.`, `// Moving filament tip N.Nmms`, `// DESCRIBE_COLOR: …`, `// TOOLCHANGE: filament …`, `// Run Current: …`, `// pressure_advance: N`, `//      Change N out of N` | *(none)* |

`// KAMP purge is not using firmware retraction…` does match `poop` via the loose `purg` needle.
That is accepted: KAMP's advisory only appears around the purge anyway, so the phase it lands on is
the right one.

#### `// Unknown command:"X"` — an aborted macro that still returns `ok`

Klipper reports a macro referencing an undefined command through `respond_info` as
`// Unknown command:"STATUS_PURGING"` — **not** `!!` — and Moonraker still returns `ok` for the
enclosing script. Nothing else in the stack can distinguish "the macro ran" from "the macro died on
line 4", so the operation's success callback fires and the button shows a green checkmark for a
macro that did nothing. (Observed four times in the captured window: a `purge_filament` macro
aborting because the user's LED config has no `STATUS_PURGING`.)

`GcodeNarrationRouter` claims the line before either matcher sees it — `parse_unknown_command()`,
anchored at the start of the body — and hands the command name to
`FilamentPanel::fail_op_on_unknown_command()`, which fails the visibly-running operation and names
the missing command in the toast. Claiming it early also stops the error message itself from driving
the step bar: `has("purg")` reads `STATUS_PURGING` as a real purge phase.

**Correlation is best-effort.** Klipper does not tie a response line to the RPC that provoked it, so
the only handle is "an operation is showing its spinner". An unknown-command line raised by another
client while a filament op happens to be running will fail that op. That is the lesser harm — a
checkmark for a macro that never ran is what sends users hunting the wrong problem.

#### Drift hints

When neither matcher claims a line, `AmsBackend::is_narration_drift_candidate()` decides whether it
is worth a deduped `debug` log. AFC's answer is deliberately looser than its matchers (any line
naming `afc` or a `lane` — the hint exists to catch *rewording*, which by definition no matcher
recognizes) minus the lines it emits every toolchange that have no phase by design (`tool change`,
`already loaded`, `total change time`, `rotation distance reset`). Grep `[GcodeNarration] no phase
matched` after an AFC upgrade.

#### Channel 3 — `!!` lane faults, and the position diagram welded to them

Five AFC error sites append a monospace position diagram to their sentence. Verbatim, exhaustively
(read off a live BoxTurtle, #1184):

```python
AFC.py:1294  'filament did not trigger hub sensor, CHECK FILAMENT PATH\n||=====||==>--||-----||\nTRG   LOAD   HUB   TOOL.'
AFC.py:1345  'filament failed to trigger pre extruder gear toolhead sensor, CHECK FILAMENT PATH\n||=====||====||==>--||\nTRG   LOAD   HUB   TOOL'
AFC.py:1370  'filament failed to trigger post extruder gear toolhead sensor, CHECK FILAMENT PATH\n||=====||====||==>--||\nTRG   LOAD   HUB   TOOL'
AFC.py:1469  'Current lane not loaded, LOAD TRIGGER NOT TRIGGERED\n||==>--||----||-----||\nTRG   LOAD   HUB   TOOL'
AFC_BoxTurtle.py:527  ' FAILED TO LOAD, CHECK FILAMENT AT TRIGGER\n||==>--||----||------||\nTRG   LOAD   HUB    TOOL'
```

**The art is a hardcoded literal per error site, not a rendering of live sensor state**, so
parsing it buys nothing and costs precision: `:1345` (**pre** extruder gear) and `:1370` (**post**
extruder gear) emit byte-identical bars for two faults with different remedies, and
`AFC_BoxTurtle.py` writes `||------||` where `AFC.py` writes `||-----||`. We therefore map the
**message text**, and strip the art.

`helix::afc::afc_fault_position()` (`include/afc_fault_position.h`) — a pure function, no LVGL, no
printer state:

| Message fragment | Filament reached | `PathSegment` |
|---|---|---|
| `LOAD TRIGGER NOT TRIGGERED` | short of the lane trigger | `SPOOL` |
| `CHECK FILAMENT AT TRIGGER` | short of the lane trigger | `SPOOL` |
| `did not trigger hub sensor` | past lane, short of the hub | `HUB` |
| `pre extruder gear toolhead sensor` | past hub, short of the toolhead | `OUTPUT` |
| `post extruder gear toolhead sensor` | at toolhead, short of the extruder gears | `TOOLHEAD` |

Matching is case-insensitive and **anchored on word boundaries** — the same open-console hazard as
Channel 1 applies, and `File opened: check filament at triggering.gcode` must not resolve to a
position. Anything else returns `std::nullopt`, and `afc_strip_position_diagram()` is gated on that
optional: an unrecognised message is returned byte-for-byte, so upstream rewording degrades to the
plain-text rendering we had before rather than mangling the sentence. Tests drive the exact strings:
`tests/unit/test_afc_fault_position.cpp`.

**Where it surfaces.** Both modals that can show an AFC lane fault route their text through
`helix::ui::afc_fault_path_apply()` (`include/ui_afc_fault_path.h`), which publishes the stop point
to the int subject `afc_fault_segment` and returns the stripped text:

| Path | Modal | Call site |
|---|---|---|
| `!!` -> `GcodeErrorRouter` -> `RecoveryModalPresenter` | `ActionPromptModal` | `recovery_modal_presenter.cpp` `present()` |
| `AmsAction::ERROR` rising edge -> `AmsErrorBridge` -> `backend->current_error()` -> `RecoveryModalPresenter` | `ActionPromptModal` | same call site as the row above, reached with no `!!` line involved |
| `printer.AFC.message` -> `AmsAction::ERROR` -> `AmsPanel` | `AmsLoadingErrorModal` | `ui_panel_ams.cpp` `show_loading_error_modal()` |

All three can fire for the same fault. The graphic itself is `ui_xml/components/afc_fault_path.xml` —
four labelled checkpoints joined by the three **gaps** between them, all bound to
`afc_fault_segment` alone; 0 (`PathSegment::NONE`) hides the whole component.

**The gap, not the checkpoint, is what gets marked**, and that is not cosmetic. AFC's own art is
three sections under four labels (`Spool`→`Lane`, `Lane`→`Hub`, `Hub`→`Toolhead`), which is why it
is so often misread as being off by one. The source settles it: `did not trigger hub sensor` fires
*after* `cur_lane.loaded_to_hub = True`, and `pre extruder gear toolhead sensor` fires while homing
down the bowden past an already-cleared hub. Both are failures *between* checkpoints. Colouring the
checkpoint red would tell the user the hub failed, about a hub the filament passed cleanly. The one
exception is `post extruder gear toolhead sensor`, which genuinely fails *at* the toolhead — the
filament cleared the sensor and jammed in the extruder gears — so `TOOLHEAD` marks the node itself.

Position alone is not enough on its own, though: it says which element differs from its
neighbours, not that the difference means failure, and red-against-green is precisely the pair a
colourblind user cannot separate (#1196). So the component also renders one of four captions —
*Stopped between Hub and Toolhead*, and so on — bound to the same subject and mutually exclusive
on it. The caption is also the only thing that can express `TOOLHEAD`, which fails at a node
rather than in a gap.

Every caller must go through `afc_fault_path_apply()` even when the message is not AFC's, or a
previous fault's marker stays on screen.

#### Maintaining the contract across AFC versions

Everything above is a contract with a project that never agreed to one. AFC's console wording is
not an API, is not versioned, and moves when a maintainer improves a sentence. Neither side breaks
loudly when it does: the step bar simply stops advancing, or a lane fault renders as plain text.
This subsection is the maintenance half of #1153 — what we depend on, where it lives on our side,
and what to do on an AFC version bump.

**What the narration actually drives.** A matched phase id is looked up in the *active operation's*
phase template (`AmsBackendAfc::toolchange_phase_template()`), and the step bar advances to that
index. A phase id the running operation's template does not contain is matched and then dropped —
`GcodeNarrationRouter::process_line()` leaves the step subject untouched — so a needle is only ever
as useful as the template it feeds:

| Operation | Phase ids, in order (opt = optional: stays Pending when never narrated) |
|---|---|
| `LOAD_SWAP` (toolchange) | `heat`, `cut` (opt), `unload`, `feed`, `poop` (opt), `brush` (opt), `kick` (opt), `load` |
| `LOAD_FRESH` | `heat`, `feed`, `poop` (opt), `brush` (opt), `kick` (opt), `load` |
| `UNLOAD` | `heat`, `cut` (opt), `unload` |

There is no `park` and no `clean` distinct from `brush` — AFC has exactly one purge macro and one
wipe macro, so adding either needle without first adding the phase would be dead code.

**Our whole side of the contract is four matchers, in two files.** Nothing else needs touching
when upstream rewords:

| File | Owns |
|---|---|
| `src/printer/ams_backend_afc.cpp` `match_narration_phase()` | Channel 2 needles — loose, normalized substring |
| `src/printer/ams_backend_afc.cpp` `match_bare_narration_phase()` | Channel 1 shapes — anchored words plus token count |
| `include/afc_fault_position.h` (impl in `src/printer/afc_fault_position.cpp`) | Channel 3 fault-position fragments |
| `src/printer/ams_backend_afc.cpp` `is_narration_drift_candidate()` | which unmatched lines are worth a drift hint |

The literals to grep upstream for, exhaustively. Channel 2 is matched after collapsing everything
non-alphanumeric to single spaces and lowercasing, so grep case-insensitively and ignore
punctuation:

| Needle(s) | Phase | Emitted by |
|---|---|---|
| `is now loaded in toolhead`, `load complete`, `loaded in toolhead` | `load` | `extras/AFC.py` |
| `unload` | `unload` | `extras/AFC.py` |
| `clean nozzle`, `cleaning nozzle`, `brush` | `brush` | `config/macros/Brush.cfg` (`AFC_BRUSH`) |
| `purg`, `poop` | `poop` | `AFC_POOP`, in AFC's shipped `config/macros/` |
| `kick` | `kick` | `AFC_KICK`, same |
| `cut` | `cut` | `AFC_CUT`, same |
| `retract` | `unload` | `AFC_CUT`'s retract step (#1046) |
| `to hub`, `feed`, `loading lane` | `feed` | `extras/AFC_functions.py`, `extras/AFC_BoxTurtle.py` |
| `heat` | `heat` | toolhead heat-up narration |

**Order is load-bearing in both matchers** and is not an implementation detail: `unload` must be
tested before the `feed` needles, because normalized `unloading lane1` contains `loading lane`;
`cut` must be tested before `retract`, because `AFC_Cut` says *Retract Filament for Cut*. Reordering
the `if` chain silently reassigns phases.

**Channel 2's sources are config macros, not Python.** `AFC_BRUSH`, `AFC_POOP`, `AFC_CUT` and
`AFC_KICK` ship as templates under AFC's `config/macros/` and the user's copy is theirs to edit. A user who
renames a `RESPOND` in their own macro breaks their own step bar and no upstream release is
involved. That is also why Channel 2 is deliberately loose while Channel 1, which runs on the open
console, is anchored.

**On an AFC version bump:**

1. Grep the new AFC tree for each literal in the table above. Anything that has moved needs the
   needle updated *and* the verbatim new string added to `tests/unit/test_afc_console_corpus.cpp`.
2. Re-check Channel 3's five error sites in `extras/AFC.py` and `extras/AFC_BoxTurtle.py`; those
   are matched on message text, not on the position art, so a reworded *sentence* is what breaks
   them, not a redrawn bar.
3. Run `./build/bin/helix-tests "[afc][narration][corpus]" "[narration][router]" "[afc][fault]"`.
4. Drive a real toolchange with `-vv` and grep the log for `[GcodeNarration] no phase matched`.
   That line is deduped and is the only automatic signal that a string moved; a *silent* log with
   a stalled step bar means the wording changed to something `is_narration_drift_candidate()`
   does not recognise as AFC's either, which is the worst case and needs the hint widened too.

**The permanent fix is upstream, not here.** AFC's macros already know which step they are on —
they are the ones emitting the `RESPOND` — so publishing that step as a status field would let
every UI drop string scraping. Until then, this section is load-bearing: four separate features
(step bar, terminating responses #1183, position art #1184, failure classification #1182) all
scrape the same console because there is no structured channel to read.

### Path Topology

`PathTopology::HUB` -- Multiple lanes merge into a common hub/merger. Sensor-based position inference:

```
No sensors            -> SPOOL (filament present but not advanced)
prep only             -> HUB (past prep, approaching hub)
prep + hub            -> TOOLHEAD (past hub, approaching toolhead)
prep + hub + toolhead -> NOZZLE (fully loaded)
```

See `path_segment_from_afc_sensors()` in `ams_types.h`.

### AFC-Specific Features

#### Hub Bowden Length

The bowden tube length from hub to toolhead is read from `AFC_hub.afc_bowden_length` and exposed as a slider in the device actions UI. Adjustable via `SET_BOWDEN_LENGTH LENGTH={mm}` G-code.

#### Per-Lane Stepper Fields

Each `AFC_stepper` object provides sensor states (`prep`, `load`, `loaded_to_hub`), buffer state (`buffer_status`), filament readiness (`filament_status`), and distance to hub (`dist_hub`). These are cached in the `LaneSensors` struct per lane (up to 16 lanes).

#### Buffer Objects

AFC tracks buffer state per lane. The `buffer_status` field indicates the current buffer operation (e.g., "Advancing"). Buffer names are discovered from the Klipper object list.

#### Global State

The `AFC` Klipper object provides global state: `current_lane`, `current_state`, `error_state`, `quiet_mode`, and `led_state`. These drive the UI status display and device action toggles.

#### Maintenance Mode

The device operations overlay exposes AFC maintenance actions:

| Action | G-code | Description |
|--------|--------|-------------|
| Test All Lanes | `AFC_TEST_LANES` | Run test sequence on all lanes |
| Change Blade | `AFC_CHANGE_BLADE` | Initiate blade change procedure |
| Park | `AFC_PARK` | Park the AFC system |
| Clean Brush | `AFC_BRUSH` | Run nozzle cleaning brush cycle |
| Reset Motor Timer | `AFC_RESET_MOTOR_TIME LANE={name}` | Reset motor run-time counter. The command is **per-lane**, so `execute_device_action("reset_motor")` loops every configured lane and sends one `AFC_RESET_MOTOR_TIME LANE=<name>` each, aborting on the first failure. No lanes configured is a `not_supported` error, not a silent success |

#### LED Toggle

The LED toggle sends `TURN_ON_AFC_LED` or `TURN_OFF_AFC_LED` based on the current `afc_led_state_`. The button label and icon dynamically reflect the current state.

#### Quiet Mode

Quiet mode reduces motor noise at the cost of speed. Toggled via `AFC_QUIET_MODE` G-code. The current state is tracked via `afc_quiet_mode_` from the `AFC.quiet_mode` printer object field.

#### Fault Clear vs Lane-Position Recovery

AFC does not have a genuine per-lane reset. What used to be called `reset_lane()` was
actually two unrelated operations that happened to share one name:

- **Fault clear** (`clear_fault(slot_index)`) is bookkeeping only — it never moves
  filament. AFC has no per-lane fault clear, so `slot_index` is ignored: it sends
  `RESET_FAILURE` followed by `AFC_CLEAR_MESSAGE` and arms a drain of
  `printer.AFC.message`, which is a FIFO queue — a second queued error is not visible
  until the first is popped, so a single clear only pops one entry. The drain runs until
  the queue empties rather than for a fixed count — nothing but `AFC_CLEAR_MESSAGE` ever
  pops an entry, so depth is a function of the whole session, not of the current fault.
- **Lane-position recovery** (`recover_lane_position(slot_index)`) is a physical
  retract: it sends `AFC_LANE_RESET LANE={name}` to pull filament stranded in the
  bowden back to its lane. AFC's firmware refuses this unless that lane's hub sensor
  is actually triggered, so `can_recover_lane_position(slot_index)` gates the UI on
  the live `AFC_hub.<hub>.state` field — **not** `AFC_stepper.<lane>.loaded_to_hub`,
  which is latched once at prep time and never updated afterward, so it cannot be
  used as a hub-occupancy signal.

Separately, **Reset** (`reset()`) and **Recover** (`recover()`) both send `AFC_RESET`
today — `reset()` after the usual busy-state preconditions, `recover()` skipping them
so it still works while the system is stuck. Neither homes the system; `AFC_HOME` is
not sent by either.

`can_recover_lane_position()` ends with `lane_name == active_load_lane_ &&
recovery_attribution_valid_unlocked()`, so the targeted per-lane action is offered only when AFC
itself names the lane. There is no all-lanes fallback: an unattributed strand deliberately offers
nothing per lane, and the route out is the sidebar Reset, which dispatches `AFC_RESET` and lets
AFC's own picker list the candidates. That picker's list is built from the firmware's view of its
hardware and is a better answer than anything derivable from a shared hub sensor.

**Known gaps in wrong-lane handling.** The wrong-lane diagnostic described under "Fields that do
not mean what they say" is understood but only partly acted on. Each of the following is verified
absent from `src/` and `include/`, and is recorded here so the reasoning is not re-derived:

- **The diagnostic is not classified.** `AmsBackendAfc::classify_error()` has exactly two
  AFC-owned branches: a toolhead-jam match, and a `ctx.is_paused && error_state_` catch-all
  titled "Filament System Error". `"'<lane>' failed to reset to hub, load switch became false
  during reset"` matches neither on its own. It therefore renders as the generic title when the
  print happens to be paused and AFC is in an error state, and otherwise falls through to the
  generic classifier untouched, since AFC raises it with `pause=False`. The one useful fact in
  the line, the name of a lane now ruled out and de-seated, never reaches the user.
- **There is no elimination set.** Nothing records which lanes have already returned the
  diagnostic for the strand currently in a hub, so the broken-fragment conclusion cannot be
  drawn and the UI keeps inviting another guess. Any such set has to be keyed per hub, because a
  multi-unit machine has independent hubs, and it has to clear when that hub's sensor goes false
  or one session's eliminations permanently suppress recovery for later strands.
- **There is no re-seat action.** Nothing undoes the retract that a wrong guess causes.
  `AmsBackend::clear_fault()` is bookkeeping and moves no filament, and
  `recover_lane_position()` sends `AFC_LANE_RESET`, which retracts *toward* the hub, the opposite
  direction from what a de-seated lane needs. The user is left to work the forward `LANE_MOVE`
  out themselves. A re-seat would have to advance in bounded steps and stop the moment
  `raw_load_state` returns true, never move a fixed distance, since overshoot pushes filament
  back at a hub that is still blocked. `LANE_MOVE`'s printing guard is not an obstacle for the
  common case: `AFC_functions.py`'s `is_printing()` compares `print_stats.state` against
  `"printing"` only, so a paused print does not trip it and no `FORCE=1` is needed.

### Capabilities

| Feature | Supported | Editable |
|---------|-----------|----------|
| Endless Spool | `Available`, always `On` | `PerSlot` via `SET_RUNOUT` (see [Endless Spool](#endless-spool-shared-model)) |
| Tool Mapping | Yes | Yes (via `SET_MAP`) |
| Bypass Mode | Yes | Hardware sensor (auto-detect on Box Turtle) |
| Spoolman | Yes | -- |
| Auto-Heat on Load | Yes | AFC uses `default_material_temps` from config |
| Dryer | No | -- |
| Device Actions | Yes | Setup, Speed, Toolhead, Maintenance, Hub & Cutter, Tip Forming, Purge & Wipe (see [Device Operations Overlay](#device-operations-overlay)) |

`recovers_filament_on_resume()` is **not** overridden (default `false`), so an AFC runout
gets the dialog with manual **Load** kept prominent, because Resume alone does not re-feed.
`supports_per_tool_spool_assignment()` is not overridden either; it falls through to
`is_tool_changer(get_type())`, which is false for AFC.

### AFC Version Reporting

`afc_version_` is **display and diagnostics only. Never gate behavior on it.** AFC has no
trustworthy version signal:

- The `afc-install` database namespace has been an orphan since AFC's `7d20db7` (mid-2025),
  so `detect_afc_version()` finds nothing on any current install.
- `AFC_VERSION` is a hand-bumped literal that sat at `1.1.37` through the whole v1.2.0 release.
- v1.2.0's own `get_status()` publishes no version key at all (upstream #807 is an open PR).
- A live BoxTurtle reported `"1.0.0"` while running v1.1.0.

Capabilities therefore come from **feature detection**, not comparison:

| Hook | What it inspects |
|------|------------------|
| `AmsBackendAfc::status_has_modern_fields()` | `filament_name` / `multi_color_hexes` / `initial_weight` on a lane status. All three ship from one `if not save_to_file:` block in `AFC_lane.get_status()`, so any one proves the whole block. Only meaningful on a **complete** status object, the subscription's first baseline frame; every later frame is a delta where an absent key means "unchanged". |
| `AmsBackendAfc::probe_feature_level()` | Queries one lane object directly (never a status frame) to obtain that baseline. |

The `AFC` / `lane_data` database query follows the same rule: `on_started()` calls
`query_lane_data()` **unconditionally**, because there is no reliable flag to gate on.
AFC's `lane_data_enabled` reports whether Moonraker has the (now unused) `[lane_data]`
section, not whether the namespace holds data; `send_lane_data()` writes regardless. A
live BoxTurtle on 2026-07-26 had `lane_data_enabled=false` with a fully populated
namespace. Lanes are initialized from `PrinterCapabilities` discovery first, so the query
only ever supplements colours / materials / spool ids; a missing namespace just errors and
the probe stays silent.

---

## ACE (Anycubic ACE Pro)

The ACE backend supports the Anycubic ACE Pro multi-material hub. The same hardware
shows up behind **several different software stacks**, and they expose *different*
Klipper/Moonraker interfaces. "ACE support" is therefore not one integration — it is
whichever of these the printer is running:

| # | Stack | Klipper / Moonraker surface | Transport | Audience | Backend status |
|---|-------|-----------------------------|-----------|----------|----------------|
| 1 | **Native Anycubic GoKlipper (via Rinkhals)** | `filament_hub` printer object (config `[ace]`) | WebSocket query/subscribe | **Primary real user base** — stock Kobra 3 / 3 V2 / 3 Max / S1 / S1 Max (Combo) flashed with [Rinkhals](https://github.com/jbatonnet/Rinkhals) | ✅ Handled (parses `filament_hub`) |
| 2 | **Community ValgACE / BunnyACE / DuckACE** | `ace` printer object + `ace_status.py` | `/server/ace/*` REST bridge | ACE Pro bolted onto a **non-Anycubic DIY printer** (niche; DuckACE abandoned) | ✅ Handled (REST fallback) |
| 3 | **Mainline-Python Kobra-S1 fork** (`github.com/Kobra-S1/klipper-kobra-s1`) | custom `[ace]` extra + `[ace_status]` Moonraker component | **unconfirmed** (`ace_status.py` JSON/REST) | KS1 users replacing KobraOS with mainline Klipper (often on an external Pi) | ❓ **Unverified** — status surface not yet inspected |

**How to think about the three:**

- **Path 1 (native)** is what almost every actual ACE user runs — it ships inside
  Anycubic's own GoKlipper firmware and is surfaced when the printer is reflashed with
  Rinkhals. This is the path the backend is built around.
- **Path 2 (community)** is ACE-on-a-DIY-rig: ValgACE (active), plus the BunnyACE/DuckACE
  forks (DuckACE abandoned). Integrates through Moonraker macros/endpoints rather than a
  native Klipper object.
- **Path 3 (KS1 fork)** is newly observed in real logs (2026-06-24) and **not yet
  validated against our backend.** It is a *full Klipper firmware fork* for the Kobra S1
  — related to the Path 2 driver concept (it too ships an `ace_status.py`) but wrapped in
  KS1-specific cutter/purge/toolchange macros. Whether its `[ace_status]` surface matches
  Path 1's `filament_hub`, Path 2's `ace`/REST, or neither is an **open question**.
  Control gcode (`ACE_CHANGE_TOOL`, `ACE_ENABLE/DISABLE_FEED_ASSIST`) does match what the
  backend already sends. Full teardown:
  [`printer-research/ANYCUBIC_ACE_KOBRA_S1_LOG_ANALYSIS.md`](printer-research/ANYCUBIC_ACE_KOBRA_S1_LOG_ANALYSIS.md).

> The sections below (`filament_hub` schema, REST endpoints, etc.) document Paths 1 and 2,
> which the backend handles today. Path 3's status schema is still TBD — see the linked
> log-analysis doc for the open items needed to confirm or extend coverage.

### History

The ACE backend was originally written **blind for ValgACE** (keying on a Klipper object literally named `ace`) and never matched a real Anycubic ACE hub — so Combo printers on Rinkhals got no AMS backend detected at all. Fixed **2026-06-13** to detect `filament_hub` first. The native object name was confirmed in Anycubic GoKlipper `extras_ace.go` and Rinkhals `mmu_ace.py`. The native `ACE_*` G-code verbs turned out to be exactly what the backend was already sending (ValgACE mirrored them), so the fix was a detection + status-parsing change, not a command-dialect rewrite.

### Detection

ACE is detected in two ways:

1. **Object list detection**: `filament_hub` (native Anycubic/Rinkhals) **or** `ace` (community drivers) in `printer.objects.list`.
2. **REST probe fallback**: A probe to `/server/ace/info` via `AmsState::probe_ace()` catches **community** setups where the object list is unavailable. (The native path never needs this — `filament_hub` is always in `objects.list`.)

### Native `filament_hub` Status Schema

The native GoKlipper `filament_hub.get_status()` is **flat and single-hub** (one hub, 4 slots). Multi-unit "Combo" configurations (8 slots) are a Rinkhals-layer abstraction stacked above this single-hub GoKlipper object.

| Field | Type | Meaning |
|-------|------|---------|
| `status` | string | Overall hub status |
| `dryer.status` | string | Dryer running/idle |
| `dryer.target_temp` | int | Dryer target temperature |
| `dryer.duration` | int | Configured drying duration |
| `dryer.remain_time` | int | Remaining drying time |
| `temp` | int | Hub temperature |
| `slots[]` | array | Per-slot state (4 entries) |
| `slots[].index` | int | Slot index |
| `slots[].status` | string | `empty` / `ready` / `preload` / `running` / `runout` |
| `slots[].sku` | string | Filament SKU |
| `slots[].type` | string | Filament material type |
| `slots[].color` | `[r, g, b]` | Slot color |
| `current_filament` | string | Loaded slot as `"<unitId>-<localIndex>"` (e.g. `"0-2"`); empty/absent = nothing loaded |

### G-code Commands (native `ACE_*`)

These are the real native verbs from GoKlipper `extras_ace.go` — the backend drives the native path with exactly these:

| Command | Action |
|---------|--------|
| `ACE_CHANGE_TOOL TOOL={n}` | Load slot (or `-1` to unload) |
| `ACE_FEED INDEX={i} LENGTH={mm} SPEED={s}` | Feed filament from a slot |
| `ACE_RETRACT INDEX={i} LENGTH={mm} SPEED={s}` | Retract filament to a slot |
| `ACE_ENABLE_FEED_ASSIST INDEX={i}` | Enable feed assist on a slot |
| `ACE_DISABLE_FEED_ASSIST INDEX={i}` | Disable feed assist on a slot |
| `ACE_START_DRYING TEMP={t} DURATION={m}` | Start drying |
| `ACE_STOP_DRYING` | Stop drying |

> Note: `ACE_RECOVER` and `ACE_RESET` are **not** native GoKlipper commands — do not send them on the native path.

### REST Endpoints (community fallback only)

These belong to ValgACE's Moonraker component (`ace_status.py`) and are used **only** on the community fallback path; the native Rinkhals deployment never uses them. BunnyACE/DuckACE users must install ValgACE's `ace_status.py` separately to get this bridge.

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/server/ace/info` | GET | System information (model, version, slot count) |
| `/server/ace/status` | GET | Current state (dryer, loaded slot, action) |
| `/server/ace/slots` | GET | Slot information (colors, materials, status) |

### Threading

- **Native path (`filament_hub`):** WebSocket query + subscription. State is held under `mutex_`; updates arriving on the WebSocket background thread are deferred to the main thread via `token.defer(...)` (L081-safe — never mutate UI state directly from the WS callback).
- **Community fallback path (`ace`):** a background polling thread runs at ~500ms intervals when the backend is active, caching state under mutex protection.

### Capabilities

| Feature | Supported | Editable |
|---------|-----------|----------|
| Endless Spool | `Unsupported` | No override; inherits the base default |
| Tool Mapping | No | Fixed 1:1 mapping |
| Bypass Mode | No | `enable_bypass()` returns `not_supported`; [the force override](#bypass-visibility-and-the-force-override) shows the external spool for tracking only |
| Spoolman | No | -- |
| Auto-Heat on Load | No | -- |
| Dryer | Yes | Built-in hardware dryer |

### Dryer Control

ACE is the primary backend with integrated dryer support. The `DryerInfo` struct provides:

- Current/target temperature
- Duration and remaining time
- Fan speed control
- Hardware capability limits (min/max temp, max duration)

Drying presets are derived from the filament database via `get_default_drying_presets()`.

On the native path, live dryer state (status, target temp, duration, remaining time) is parsed directly from `filament_hub.dryer`.

---

## Tool Changer (viesturz/klipper-toolchanger)

Physical tool changers have multiple complete toolheads that are swapped on the carriage, fundamentally different from filament-switching systems.

### Detection

Klipper object `toolchanger` in `printer.objects.list` sets `AmsType::TOOL_CHANGER`. Individual tool names come from `tool T*` objects (e.g., `tool T0`, `tool T1`).

### Key Differences from Filament Systems

- Each "slot" is a complete toolhead with its own extruder
- No hub/selector -- path topology is `PARALLEL`
- "Loading" means mounting the tool to the carriage
- No bypass mode (each tool IS the path)
- Tool mapping is fixed (tools ARE slots)

### Klipper Objects

**Global** (`toolchanger`):

| Variable | Type | Description |
|----------|------|-------------|
| `status` | string | "ready", "changing", "error", "uninitialized" |
| `tool` | string | Current tool name ("T0") or null |
| `tool_number` | int | Current tool number (-1 if none) |
| `tool_numbers` | int[] | All tool numbers [0, 1, 2] |
| `tool_names` | string[] | All tool names ["T0", "T1", "T2"] |

**Per-tool** (`tool T{n}`):

| Variable | Type | Description |
|----------|------|-------------|
| `active` | bool | Is this tool selected? |
| `mounted` | bool | Is this tool mounted on carriage? |
| `gcode_x_offset` | float | X offset |
| `gcode_y_offset` | float | Y offset |
| `gcode_z_offset` | float | Z offset |
| `extruder` | string | Associated extruder name |
| `fan` | string | Associated fan name |

### G-code Commands

| Command | Action |
|---------|--------|
| `SELECT_TOOL TOOL=T{n}` | Mount specified tool |
| `UNSELECT_TOOL` | Unmount current tool (park it) |
| `T{n}` | Tool change macro |

### Path Topology

`PathTopology::PARALLEL` -- Each slot has its own independent path to a separate toolhead. No converging path visualization needed.

### Capabilities

| Feature | Supported | Editable |
|---------|-----------|----------|
| Endless Spool | `Unsupported` | No override; inherits the base default |
| Tool Mapping | No | Fixed (tools ARE slots) |
| Bypass Mode | No | Not applicable - each tool is its own path. [The force override](#bypass-visibility-and-the-force-override) shows the external spool for tracking only |
| Spoolman | No | -- |
| Auto-Heat on Load | No | -- |
| Dryer | No | -- |
| Device Actions | No | -- |

### Discovery Sequence

Tool names must be provided via `set_discovered_tools()` before calling `start()`. The caller (typically `AmsState::init_backend_from_hardware()`) extracts tool names from `PrinterDiscovery::get_tool_names()`.

---

## AD5X IFS (FlashForge Adventurer 5X)

> **Status: TESTING** — This backend is functional but not yet fully supported. It is available for user testing and feedback. Please report issues via GitHub.

The AD5X has a 4-lane Intelligent Filament Switching (IFS) system controlled by a separate STM32 MCU. HelixScreen supports it through ZMOD firmware (ghzserg's Klipper mod for FlashForge printers).

> **Required firmware**: [ZMOD open-source firmware](https://github.com/ghzserg/zmod) **v1.7.0 or newer** (v1.7.0, Mar 2026, is the first release with explicit HelixScreen integration via `DISPLAY_OFF HELIX=1`). Hard minimum: v1.6.2 (Oct 2025), when the `less_waste_*` `save_variables` plumbing first appeared via the bambufy plugin — older versions are missing the slot-color/material surface we read.
>
> Note: this is ZMOD's own version, not FlashForge stock firmware. ZMOD supports stock AD5X bases from v1.0.2 (Jan 2025) onward; no specific FF stock version is required.

### Detection

IFS is detected via `filament_switch_sensor _ifs_port_sensor_{1-4}` or `filament_motion_sensor _ifs_motion_sensor_{1-4}` in `printer.objects.list`. The leading space in sensor names is intentional — it's a Klipper object naming convention.

Detection is gated by `!has_mmu_` — if Happy Hare or AFC is already detected, IFS sensors are ignored (priority: HH > AFC > IFS).

### State Sources

Stock zMod owns two Klipper objects — `zmod_ifs` and `zmod_color` — that hold the authoritative per-channel state, but their rich APIs are `printer.lookup_object()`-only (no `get_status()`), so Moonraker cannot see them. What Moonraker actually exposes depends on whether the lessWaste / bambufy plugins are installed.

**Shared (stock zMod and plugins both provide):**

| Source | Data | Notes |
|--------|------|-------|
| `filament_switch_sensor head_switch_sensor` | Toolhead filament presence | Authoritative NOZZLE/TOOLHEAD indicator |
| `filament_motion_sensor ifs_motion_sensor` | Filament moving **post-hub**, inside the IFS | Single boolean on stock zMod. Maps to `OUTPUT` segment — **not** the toolhead. Replaced by per-port sensors when plugins are installed. |
| `Adventurer5M.json` (Moonraker file API) | Per-channel color + material type | Polled + re-read on sensor edges / gcode responses. No push notifications. |

**Plugin-only (lessWaste / bambufy) — the Moonraker-visible export of `zmod_ifs` / `zmod_color`:**

| Source | Data | Plugin delta over stock zMod |
|--------|------|------------------------------|
| `filament_switch_sensor _ifs_port_sensor_{1-4}` | Per-port HUB presence (4 booleans) | Wraps `zmod_ifs.ifs_data.get_port(port)` — invisible to Moonraker otherwise |
| `save_variables.<prefix>_colors` / `_types` | Atomic per-port color + material | Subscribable; stock requires json polling |
| `save_variables.<prefix>_tools` | 16-element tool→port map | Not exposed on stock zMod |
| `save_variables.<prefix>_current_tool` | Active tool index (-1 or 0-15) | Stock: `zmod_color.get_current_channel()` (lookup-only) |
| `save_variables.<prefix>_external` | Bypass / external mode flag | Stock: `zmod_color.get_printer_data_detail().indepMatlInfo` (lookup-only) |
| `_IFS_VARS` gcode macro | Atomic writes of the above | Stock lacks this — can't persist UI-side changes |

Prefix is `less_waste` (the lessWaste plugin) or `bambufy` (the bambufy plugin); the schema is identical. Auto-detected from whichever keys are present. **Neither prefix comes from stock zMod** — the table above is the plugin-only column, and the `less_waste_*` plumbing first appeared in zMod v1.6.2 *via* the bambufy plugin framework, not in the firmware itself. A stock-zMod machine has no `save_variables` rows under either prefix.

> **Upstream wishlist:** add `get_status()` to `zmod_ifs` and `zmod_color` in stock zMod. That would close the plugin gap entirely and let HelixScreen drop the `Adventurer5M.json` polling path. Until then, users without a plugin see a degraded UI (no per-port HUB presence, no live tool map, no bypass flag, no atomic color updates).

> **Sensor-location correction:** the `ifs_motion_sensor` sits **inside the IFS immediately after the hub**, not at the toolhead. The current backend routes it through `parse_head_sensor()` as a simplification; a proper fix would map it to `PathSegment::OUTPUT` and require the toolhead switch for `filament_loaded` / load-complete detection.

#### The two data sources, and which one owns what

On native ZMOD (no lessWaste / bambufy plugin) the backend reconciles **two** independent reads. They answer different questions and must not be confused — conflating them is the root of the resurrection bug documented below.

| Source | Transport | Question it answers | Code |
|--------|-----------|---------------------|------|
| `Adventurer5M.json` `FFMInfo` | Moonraker `download_file("config", "Adventurer5M.json")`, 5s content-compare poll | What **color / material** is *assigned* to each channel (persisted metadata) | `parse_adventurer_json()`, `poll_adventurer_json()`, `note_json_content()` |
| `GET_ZCOLOR SILENT=1` (and, future, `IFS_STATUS`) | gcode console, on-demand | Which lanes **physically have filament** (RS-485 silk sensor) + the active lane | `query_zcolor_silent()`, `parse_zcolor_silent()`, `apply_zcolor_result()` |

**`Adventurer5M.json` `FFMInfo` has NO per-channel presence field.** It carries `ffmColor{1-4}` / `ffmType{1-4}` plus an active `channel`, and those colors **persist across unload/eject** — zmod never blanks `ffmColorN` when a lane is emptied. So a non-empty `ffmColorN` means "this channel was *assigned* this color", **not** "filament is loaded here". (Field-proven on raza616's hardware: he ejected *and* unloaded channel 1, yet `ffmColor1` stayed populated; a live `IFS_STATUS` reported `Ports:[F,T,T,T]` while the JSON still had `ffmColor1` set — the JSON simply does not track presence.)

**`GET_ZCOLOR SILENT=1`** is the silk-sensor truth. Text format (every line `// `-prefixed), parsed by `parse_zcolor_silent()`:

```
// Extruder: 3: PLA/2750E0 | IFS: True   <- summary: active lane, its mat/hex, IFS-mode flag
// 1: PLA/FFFFFF                          <- one row per LOADED slot (silk-detected)
// 3: PLA/2750E0
```

- Summary `Extruder: None (N)` = nothing at the hotend; `Extruder: N: MAT/HEX` = slot N is feeding the head. The `(N)` paren form carries the current channel.
- A **missing slot number = empty** — zmod filters slot rows by `hasFilament` from the RS-485 `silk_state` bitmask, so an absent row is the presence signal for "this lane is physically empty".
- Slot body is `MATERIAL`, `MATERIAL/HEX`, or `MATERIAL/NAME/HEX`. Material is everything before the first `/`; hex is everything after the **last** `/`. A response with slot rows but no `/HEX` is flagged `is_old_format` (pre-zmod-`ad2802ab`, Apr 2026) — presence only, colors still come from JSON.
- A response whose lines contain `action:prompt_` means **old zmod returned the interactive dialog instead of silent text** → `is_prompt_fallback` (see presence-ownership rule below).

**`IFS_STATUS`** (`zmod_ifs.py` `cmd_IFS_STATUS` → `ifs_data.get_values()`) is a cleaner, structured alternative that ships in zmod 1.7.1 but is **not yet consumed** by HelixScreen. It returns clean JSON:

```json
{"State": 4, "Ports": [false, true, true, true], "Silk": 14,
 "Chan": 4, "Insert": 0, "NeedInsert": false, "Stall": false, "stall_state": 0}
```

`Ports[i]` is `(silk_state >> i) & 1` — the same RS-485 bits `GET_ZCOLOR` filters on, but already decoded to booleans. `Chan` is the active port. **This is the future presence source**: it would let the backend drop both the `GET_ZCOLOR` text-scrape *and* the 5s JSON poll. Tracked as the firmware-integration headline; the upstream wishlist below (`get_status()` on `zmod_ifs`) would close the gap entirely.

#### Presence ownership rule (and the resurrection bug)

> **Presence is owned SOLELY by `GET_ZCOLOR` on modern zmod.** `parse_adventurer_json()` must NOT infer presence from `ffmColorN`. This is the fix for the channel-resurrection bug (commits `35dfcb765`, `2081e5757`).

**The bug.** Earlier code in `parse_adventurer_json()` treated a non-empty `ffmColorN` as `port_presence_[idx] = true` with no guard. Because zmod persists `ffmColorN` across unload/eject, an emptied lane was **resurrected as loaded on every content-changed poll**. The exact field report (raza616, v0.99.78): he externally unloaded channel 1 (Helix failed to clear it), then a `FIRMWARE_RESTART` fixed it (a fresh `GET_ZCOLOR` set `port_presence_[0]=false`), but then **editing channel 4's color in zmod changed the JSON content → triggered a reparse → the persisted `ffmColor1` resurrected channel 1 with its stale color**. One edit resurrected an unrelated emptied lane.

**The fix.** On modern zmod (where `GET_ZCOLOR SILENT=1` works), `parse_adventurer_json()` refreshes `colors_[]` / `materials_[]` only and leaves `port_presence_` untouched. `apply_zcolor_result()` (the silk-sensor read) is the sole presence authority, and it now also drives the `present→absent` override-clear (`clear_override_locked`) that used to ride on the JSON inference. Every JSON content change already schedules a `GET_ZCOLOR` immediately after the parse (`poll_adventurer_json()` calls `schedule_zcolor_query()`), so silk-truth presence re-establishes on the same event — the JSON setting presence was both **wrong and redundant**.

**The pre-SILENT regression (caught in review → commit `2081e5757`).** Making `GET_ZCOLOR` the sole authority breaks presence *entirely* on **old zmod**, where `GET_ZCOLOR SILENT=1` returns a prompt dialog instead of silent text. There, `apply_zcolor_result()` sees `is_prompt_fallback`, latches `zcolor_silent_supported_ = false`, and every subsequent `schedule_zcolor_query()` / `query_zcolor_silent()` no-ops forever. With JSON inference removed, every channel would be stuck EMPTY. The fix **gates the legacy `ffmColorN` inference on `!zcolor_silent_supported_`** (`parse_adventurer_json()`, the `if (!has_per_port_sensors_ && !zcolor_silent_supported_.load())` block):

- **Modern zmod** (`SILENT` works): `GET_ZCOLOR` owns presence; JSON never touches it → resurrection fixed.
- **Pre-SILENT zmod** (`zcolor_silent_supported_` latched false): no silk query exists, so JSON inference is the only fallback — `non-empty color == present`, `empty color while IDLE == eject + override-clear`. The resurrection bug can't bite here because no `GET_ZCOLOR` competes for ownership.

> **Known minor edge (accepted):** on modern zmod, an external color edit that arrives via the JSON poll *before* `GET_ZCOLOR` has confirmed presence won't sync to `lane_data` (the baseline moves without syncing). Rare, and far preferable to the constant resurrection. Confirmed acceptable in review.

**save_variables keys** (all prefixed `less_waste_`):

| Key | Type | Example |
|-----|------|---------|
| `less_waste_colors` | string[] | `['FF0000', '00FF00', '0000FF', 'FFFFFF']` |
| `less_waste_types` | string[] | `['PLA', 'PETG', 'ABS', 'TPU']` |
| `less_waste_tools` | int[16] | `[1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5]` |
| `less_waste_current_tool` | int | `0` (T0), `-1` (none) |
| `less_waste_external` | int | `0` (IFS mode), `1` (bypass/external) |

Tool mapping: array index = tool number (T0-T15), value = physical port (1-4, 5=unmapped).

#### Unattended runout detection (#1250, reported as #1247)

**The hole this fills.** `detect_load_unload_completion()` only reacts to a head-sensor transition while the action is `LOADING` or `UNLOADING`, and `check_action_timeout()` only runs during an operation phase. A head drop at `AmsAction::IDLE` with no phase tracking therefore produced **nothing at all** — the print sat paused with an empty toolhead and HelixScreen said nothing, while the reporter waited for a backup-spool switch that was never going to happen (no plugin installed).

**The authority is the switch pair, never `head_filament_`.** `parse_head_sensor()` writes `head_filament_` from *both* the toolhead switch and `ifs_motion_sensor`, and the motion sensor is device-confirmed to read `filament_detected=false` on a lane that is loaded but idle. The detector uses `head_switch_seen_ && !head_switch_present_` — the same pair as the #1065 row 28 seated head-gate.

**The predicate** (`evaluate_runout_locked()`, run from `handle_status_update()` right after `check_action_timeout()`), all of which must hold:

| Condition | Why |
|-----------|-----|
| A genuine `head_switch_present_` **true → false edge** was seen (`head_empty_since_`) | An edge, not a level: a printer that boots into a paused job with an empty toolhead has no runout to report |
| The edge was armed while nothing was in flight | `note_head_switch_reading_locked()` refuses to arm during a tracked op or a non-IDLE action |
| Print state is **PAUSED** | A real runout stops the job. While PRINTING, the same empty head is the middle of a firmware `A_CHANGE_FILAMENT`. Klipper queues a `PAUSE` behind the running macro, so a swap cannot make the job read PAUSED with the head still empty |
| `!phase_tracker_.active` **and** `action == IDLE` | The tracker covers load/unload; the action covers `do_change_tool()`, which sets `LOADING` without arming the tracker |
| `now - last_filament_op_dispatch_ >= 30 s` | `eject_lane()` and `do_unload_filament()`'s three early returns leave the backend IDLE and armless — the dispatch stamp is the only thing that sees them |
| The head has been empty for the confirm dwell | 30 s normally; **180 s** when a plugin with `variable_backup` on is installed, because that plugin's own switchover pauses, unloads and loads a replacement lane and must not be talked over |

On a raise: `runout_active_ = true`, `system_info_.filament_runout = true`, and `system_info_.action = AmsAction::ERROR`. **ERROR is not decorative** — it is the only edge `AmsErrorBridge` watches, so it is the only route to `current_error()` and the recovery modal. `check_action_timeout()` early-returns on ERROR, so the fault cannot be re-timed-out on top of itself. It clears when filament returns to the switch, when the print leaves PAUSED (which is what dismisses the modal), or via `recover()` / `reset()` / `cancel()`.

**Recovery actions** (`build_recovery_actions()` branches on `runout_active_`): `RESUME` (primary, hot), a plain `M83` + `G1 E50 F600` purge (hot), and `IFS_UNLOCK` (danger, cold-safe). The operation-timeout fault keeps its historical lone `IFS_UNLOCK`.

> **There is deliberately NO "Load slot N" recovery button**, even though a runout is exactly when the user wants one. Every AD5X load path runs `INSERT_PRUTOK_IFS`, whose macro homes itself and then moves the toolhead on its own authority (`_GOTO_TRASH`, `_SBROS_TRASH`, `_CLEAR_REZINA` nozzle wipe) — this is what `filament_ops_self_home()` is about. On the loadcell-Z AD5X that motion reaches **down into the part**; with a job owning the toolhead it trips ZMOD's `ZCONTROL_AUTO` and shuts Klipper down, recoverable only by a firmware restart (bundle `XWPBR2DX`, commit `329e731e9`). A runout state is PAUSED by construction, so the button would fire straight into that. Note the leading `_G28` is *conditional* on `homed_axes` (see `FLASHFORGE_AD5X_IFS_ANALYSIS.md` §12) and usually no-ops mid-print — that is not a reason to relax this: `homed_axes` is cleared by a Klipper error, an `M84`, or a cold resume, and the trash/wipe moves happen either way. `refuse_if_printing()` protects `load_filament()`; it does **not** protect a recovery button, which hands its gcode directly to `MoonrakerAPI::execute_gcode`, and the `_G28` is buried inside the macro where `reject_homing_during_active_print()` never sees it. The purge is a bare extruder move for the same reason — no homing, so it cannot reach the `_G28`. If a verified non-homing load-to-toolhead command ever turns up, that is the time to add the button.

> **Unverified, flagged rather than assumed:** whether a firmware tool change can make the job read PAUSED with the head still empty. The reasoning above (Klipper queues `PAUSE` behind the running macro) is first-principles, not a device observation, and there is no AD5X in the fleet and no `ad5x` mock profile to test it on. If a false runout ever shows up mid-swap, the fix is to lengthen `RUNOUT_CONFIRM_DELAY` past a full swap (~2 min measured in bundle `NJB2U558`), not to loosen the PAUSED gate.

#### Auto-switchover plugin visibility

The `has_ifs_vars_` / `ifs_macro_confirmed_missing_` machinery distinguishes stock zMod from
the lessWaste / bambufy plugin path. #1250 surfaces it because the user needs to know which
system will handle a runout.

**All three modes have automatic slot-to-slot switchover** — verified from source
(`zmod_ifs.py:cmd_ANALOG_PRUTOK`, `bambufy.cfg:_RUNOUT_HEAD`, `lesswaste_src.cfg:_RUNOUT_HEAD`)
and corroborated on-device by raza616 and ninjamida. The original #1247 claim that "stock zMod
has no backup-spool switching at all" was wrong; zmod's own user-facing name for it is
**"Infinite Spool Mode"**.

| Mode | Trigger | Enable flag | Default |
|------|---------|-------------|---------|
| Stock zMod (`!has_ifs_vars_`) | `head_switch_sensor` runout_gcode calls `ANALOG_PRUTOK` (`ad5x_display_off.cfg:39-44`) | none — always on | on |
| bambufy | `_RUNOUT_HEAD` (plugin overrides the sensor's runout_gcode) | `variable_backup` (`bambufy.cfg:_IFS_VARS`) | **on** (`variable_backup: 1`) |
| lessWaste | `_RUNOUT_HEAD` (same shape; lessWaste is a fork of bambufy V1.2.10) | `variable_backup` (`lesswaste_src.cfg:969`) | off (`variable_backup: 0`) |

The match rule is identical across all three: same `ffmType` AND same `ffmColor` AND the
candidate port's presence sensor reads filament. None of the three disables switchover in
multicolor — a report that "bambufy doesn't support multicolor" describes the *de facto*
outcome of multicolor prints typically loading one spool per colour (so no same-colour backup
exists), not a code restriction.

| Getter | Values |
|--------|--------|
| `AmsBackendAd5xIfs::get_plugin()` | `IfsPlugin::None` / `LessWaste` / `Bambufy`. `None` whenever `has_ifs_vars_` is false, so stale `less_waste_*` rows left behind by an uninstalled plugin never read as installed |
| `AmsBackendAd5xIfs::plugin_backup_enabled()` | `std::optional<bool>` — `nullopt` means the macro dict never carried the key (or no plugin is installed), which is **not** the same as off |
| `AmsBackendAd5xIfs::backup_state_locked()` | The live switchover state as a tri-state, `BACKUP_UNKNOWN` (-1) / `BACKUP_OFF` (0) / `BACKUP_ON` (1). Stock zMod reports `BACKUP_ON` (ANALOG_PRUTOK is always-on); the plugin path mirrors `variable_backup` with `BACKUP_UNKNOWN` when the key was never read. Feeds both the runout warning log and `get_endless_spool_capabilities()`' `enabled` axis, so the number in the log and the sentence on screen cannot disagree |

**There are no AD5X-specific XML subjects.** `ams_ifs_plugin` and `ams_ifs_backup_enabled`
existed for one release as this backend's own publication path and are gone: they never
acquired a reader, and a per-firmware subject can only ever describe one printer's answer.
The state reaches the UI through `get_endless_spool_capabilities()`, which `AmsState` turns
into the backend-neutral `ams_endless_state` / `ams_endless_text` subjects for every backend
— see [Endless Spool](#endless-spool-shared-model) § "The status line".

**`variable_backup`.** `gcode_macro _ifs_vars`'s `get_status()` dict used to be reduced to a single "does the macro exist" bool at the `on_started()` probe and thrown away. It now flows into `parse_ifs_vars_macro_locked()`, which reads `variable_backup` (accepting the jinja int form and a bool). Note this object is **not** in the standing `objects.subscribe` set — the `on_started()` query and `recheck_ifs_vars_macro()` (fired on `notify_klippy_ready`) are the only two places it ever reaches us.

Per `printers/FLASHFORGE_AD5X_SUPPORT.md` § "lessWaste-Specific Variables" and the source
variable dumps in `printer-research/FLASHFORGE_AD5X_IFS_ANALYSIS.md`, lessWaste ships
`variable_backup` defaulting **off** (`lesswaste_src.cfg:969`) and bambufy ships it defaulting
**on** (`bambufy.cfg:_IFS_VARS`). **Neither has been observed on a device by us** — the
defaults are source-reads, not device observations. Nothing branches on the value except the
wording, the runout-warning log, and the longer confirm delay.

**The matching rule the hint text promises is strict and must stay strict**: a backup port qualifies only when its filament **type** and **colour** both equal the active spool's *and* its own port sensor reads filament present (`find_backup_slot_locked()`). This mirrors exactly what `ANALOG_PRUTOK` (`zmod_ifs.py:663-667`) and `_RUNOUT_HEAD` enforce on the device.

> **PAUSE-reason follow-up, not implemented:** bambufy and lessWaste both emit `PAUSE REASON=` with one of `jam`, `broken`, `runout`, `empty`, `backup`, `loading`, `nobackup` (the last on a backup-enabled runout with no same-type+colour match — bambufy-only; verified from `bambufy.cfg:149`). That is a direct, unambiguous runout signal — but only on the plugin path, which is precisely the case the sensor-based detector above is *not* needed for. Parsing it would let the plugin path skip the dwell entirely.

### G-code Commands

| Command | Action |
|---------|--------|
| `INSERT_PRUTOK_IFS PRUTOK={port}` | Load filament from port (looks up temp from config) |
| `IFS_REMOVE_PRUTOK` | **Bare, with no `PRUTOK=`: a guaranteed no-op.** `cmd_IFS_REMOVE_PRUTOK` defaults `PRUTOK=0` and returns immediately on `prutok == 0` (`zmod_ifs.py:1113`). Given an explicit `PRUTOK=N` it forwards to the firmware's `_IFS_REMOVE_PRUTOK` macro for lane N, which is how `IFS_REMOVE_CURRENT_PRUTOK` calls it internally. HelixScreen never sends it, bare or otherwise |
| `REMOVE_PRUTOK_IFS PRUTOK={port}` | Toolhead unload (heat + retract the currently-loaded filament). **Not** a per-port jog — `PRUTOK=N` does not eject an idle lane; see note below |
| `IFS_F11 PRUTOK={port} LEN={mm} SPEED={s} CHECK=0` | Cold per-lane retract — reverse one idle lane's feed motor toward the spool; no heat, no presence guard. Used for idle-lane recovery (#996) |
| `A_CHANGE_FILAMENT CHANNEL={port}` | Full tool change |
| `SET_EXTRUDER_SLOT SLOT={port}` | Select slot without loading |
| `IFS_UNLOCK` | Reset IFS driver state machine |
| `_IFS_VARS key=value SHOW=0` | Persist color/type/tool/external changes |

**Variable persistence**: Use `_IFS_VARS` macro (not raw `SAVE_VARIABLE`) to persist slot data. `_IFS_VARS` updates both in-memory gcode variables AND `save_variables` with the correct prefix (`less_waste_*` for lessWaste, `bambufy_*` for bambufy). `SHOW=0` suppresses the interactive dialog. Example: `_IFS_VARS colors="['FF0000', '00FF00']" SHOW=0`. **The macro ships with those two plugins only** — stock zMod does not define `_IFS_VARS` at all, which is why HelixScreen cannot persist UI-side slot edits there (see the "Stock lacks this" row above).

**Plugin compatibility**: HelixScreen auto-detects the variable prefix from whichever `save_variables` are present on the printer. Both lessWaste and bambufy use the same schema, just different prefixes.

**Unload is toolhead-oriented, not per-lane**: `REMOVE_PRUTOK_IFS PRUTOK={port}` runs the toolhead unload sequence — it heats the hotend and retracts whatever filament is currently loaded to the toolhead. The `PRUTOK={port}` argument does **not** select an idle lane to jog independently; observed on a real AD5X (native ZMOD), `REMOVE_PRUTOK_IFS PRUTOK=N` unloaded the currently-loaded filament (in a different slot) and ignored port N. (Bare `IFS_REMOVE_PRUTOK` is not a third way to do this: it is a firmware no-op, see the table above.) An older note here claimed the command "can error `No filament N in IFS`". **It cannot.** That string lives in `print_result()` on `RET_SILK` (`zmod_ifs.py:789`), which is reached only from the load paths; the error the unload chain actually raises is `"Failed to extract filament from extruder"` (`zmod_ifs.py:1140`), when the extruder sensor is still tripped after the retract. A cold per-lane retract, by contrast, **is** available at the gcode layer via `IFS_F11 PRUTOK={n} LEN={mm} SPEED={s} CHECK=0` (core ZMOD — a thin wrapper over raw serial `F11 C{port}…` with no heating and, with `CHECK=0`, no presence guard). This is why HelixScreen keeps the currently-loaded slot unloadable after runout (#995); and #996 implements HelixScreen calling `IFS_F11` directly for idle-lane recovery (e.g. a snapped chunk stuck in a lane's feed path — no hot nozzle involved). See `printer-research/FLASHFORGE_AD5X_IFS_ANALYSIS.md` §12.

#### Per-lane eject (`eject_lane()`)

A real per-lane eject — pulling a whole spool's worth of filament back out of an idle lane so the user can remove it by hand — is **not** a single `IFS_F11`. A bare `IFS_F11` defaults to `LEN=90` (`zmod_ifs.py` `cmd_IFS_F11`, `gcmd.get_int('LEN', 90)`), which barely moves the filament, and an **unclamped** gear doesn't grip at all. `eject_lane()` (commit `dfcc83c0f`) mirrors zmod's own `_REMOVE_PRUTOK_IFS` macro with a three-command sequence:

```
IFS_F24 PRUTOK={port}                          # clamp — the gear now grips
IFS_F11 PRUTOK={port} LEN={tube} SPEED={speed} # cold retract the FULL tube length (no heat, no home)
IFS_F39 PRUTOK={port}                          # unclamp — filament is free to pull out by hand
```

- **`LEN` / `SPEED` are per-material**, resolved from zmod's `/mod_data/filament.json` keyed by the lane's material type. Fetched once at startup by `fetch_filament_json()` (mirrors `read_adventurer_json` threading; 404 on non-zmod is silent), parsed by `parse_filament_json()` into `filament_eject_params_`. Structure:

  ```json
  { "default": { "filament_tube_length": 1000, "filament_ifs_speed": 1200, ... },
    "PLA":     { "filament_tube_length": 1000, "filament_ifs_speed": 1200, ... },
    "PETG":    { "filament_tube_length": 650,  "filament_ifs_speed": 1200, ... } }
  ```

  Resolution order: per-material entry → file's `default` entry → hardcoded **1000 / 1200** (`filament_eject_default_`). `filament_tube_length` is the PTFE-tube length from the IFS module to the extruder (zmod default 1000 mm) — users with non-stock tubes get the right distance automatically. (Field-confirmed on raza616: `LEN=1000` ran the full retract and ended free after `F39`; his PETG tube is 650.)

- **Eject refuses the toolhead-loaded active lane.** If `system_info_.current_slot == slot_index` and the toolhead is not empty, `eject_lane()` returns `WRONG_STATE` ("Lane is loaded in toolhead / Unload from toolhead first") — a cold backward retract would fight the loaded filament. Unload it from the toolhead first. "Not empty" is `!head_empty_for_unload_routing_locked()`, the same predicate the unload router uses (below), *not* a bare `head_filament_` — the two must agree or the router's empty-head eject gets bounced by this refusal.

#### Unload routing: heated toolhead cut vs. cold lane eject

`do_unload_filament()` picks between `_IFS_REMOVE_CURRENT_PRUTOK` (heat, cut, retract the seated lane) and `eject_lane()` (cold `IFS_F24`/`IFS_F11`/`IFS_F39` on one lane). `slot_unloads_to_toolhead()` mirrors the same decision for the context-menu label, and a unit test pins the two together across the whole authority matrix. Order matters — the slot-identity guards run *before* the head test:

| # | Condition | Route | Why |
|---|-----------|-------|-----|
| 1 | Active slot known, tapped slot is neither it nor the IFS_STATUS-seated one | cold eject of the tapped lane | That lane's filament is in the lane, not the nozzle. Otherwise "unload channel 1" heats and backs out channel 3 (raza616 `HKHZFYB2`) |
| 2 | Active pointer lost, seated channel known, tapped slot is not it | cold eject of the tapped lane | Chan is the seated authority; the alternative is a wrong-lane heat+cut (`5HR3HHS6`) |
| 3 | Toolhead reads empty | cold eject (of the tapped slot, else the seated one, else the active one; hard error if none) | `_IFS_REMOVE_CURRENT_PRUTOK` early-returns on an empty extruder sensor, so the cut would home and do nothing (`7AC4SDEX`) |
| 4 | otherwise | heated toolhead cut | Includes the unknown-origin recovery case (both authorities lost, head loaded) |

**"Empty" for row 3 is the switch pair, not `head_filament_`** (`head_empty_for_unload_routing_locked()`):

```
head_switch_seen_ ? !head_switch_present_ : !head_filament_
```

Positive switch evidence is required to claim empty, because the errors are not symmetric. A false *empty* cold-ejects seated, un-cut filament and grinds it (raza616 #981). A false *loaded* only reaches a firmware no-op. `head_filament_`'s known failure mode — `parse_head_sensor()` also writes it from `ifs_motion_sensor`, which reads `filament_detected=false` on a loaded-but-idle lane — produces the dangerous direction, so it can no longer claim empty on its own. Motion-only firmware never sets `head_switch_seen_`, so it falls back to the historical `!head_filament_` unchanged.

`can_unload_from_toolhead()` deliberately does **not** move onto the switch pair: it only decides whether the Unload affordance is offered, and its harmful direction is the opposite one (a false empty would hide the #995 recovery affordance for filament that is physically seated).

> **The switch pair is a proxy for a sensor we do not read.** The firmware's actual gate is `get_extruder_sensor()` (`zmod_ifs.py:1149`), an ADC read of `temperature_sensor filamentValue` (`result = value >= 0.72` when `value > 0.3`, `True` otherwise — a missing reading counts as loaded, `zmod_ifs.py:353-361`). HelixScreen subscribes to it nowhere. Subscribing is the proper fix; it needs a real AD5X to confirm the object is published, and there is no AD5X in the fleet and no `ad5x` mock profile.

#### External-change triggers (the gcode-response listener)

`register_zcolor_listener()` subscribes to `notify_gcode_response`; `on_gcode_response_line()` schedules a `GET_ZCOLOR` re-read when it sees an externally-driven change (commit `cafcff3ad` added the bare-`Extruder:` trigger). The watched signals:

| Token in stream | Why it fires | Notes |
|-----------------|--------------|-------|
| `RUN_ZCOLOR` / `CHANGE_ZCOLOR` | Deliberate external color/material edit (AD5X LCD, Mainsail, zmod COLOR macro). HelixScreen persists colors by writing `Adventurer5M.json` directly and **never** emits these — so they can only be external. | `CHANGE_ZCOLOR SLOT=N` carrying a real locked override also clears that stale override so the new firmware color wins (#981). |
| bare `Extruder: <N>` | zmod's `_SET_EXTRUDER_SLOT` (`zmod_color.py` `cmd_SET_EXTRUDER_SLOT`) emits `Extruder: {zslot}` via `respond_raw` at the channel-commit step near the **end** of an operation. This is the marker that catches **external unloads/loads done via zmod's own color macro**, where the stream carries no `RUN_ZCOLOR`/`CHANGE_ZCOLOR`. | Matched by a **strict** regex (`^\s*(?://\s*)?Extruder:\s*\d+\s*$`). |

**Why `IN_ZCOLOR` is NOT watched.** An external unload via zmod's color macro emits `IN_ZCOLOR SLOT=N NAPR=0/1` (load/unload) — but the literal `IN_ZCOLOR` token only appears in the **dialog button *definition* echo** (`action:prompt_button Unload|IN_ZCOLOR SLOT=N NAPR=1…`) at *prompt-render* time, **not** when the unload actually runs. Watching it would false-fire on dialog-open and still miss the real unload. The bare `Extruder: <N>` channel-commit marker is the reliable terminal signal instead.

**Why the bare-`Extruder:` regex is strict.** It must NOT match the `GET_ZCOLOR SILENT` summary (`Extruder: ... | IFS: True`), the interactive prompt (`action:prompt_text Extruder: ... | IFS:`), or per-slot rows (`3: PLA/HEX`) — all of which carry a ` | IFS:` suffix or a different shape. Combined with the `zcolor_query_active_` early-return guard (which buffers our own in-flight `GET_ZCOLOR` response echoes), this keeps the v0.99.51 **self-feedback spam loop** closed: `GET_ZCOLOR` never emits a bare `Extruder: N`, so a re-read can't re-trigger itself. (HelixScreen's own `SET_EXTRUDER_SLOT` *does* emit a bare `Extruder: N`, but a re-read after a Helix-initiated tool change is harmless — `schedule_zcolor_query()` is debounced + idempotent.) The bg-side pre-filter in `register_zcolor_listener()` admits the cheap `Extruder:` substring; the strict regex runs on the main thread in `on_gcode_response_line()`. **If you add a new trigger, update both the bg-side admit filter and the main-thread branch** — otherwise the new token is silently dropped on busy print streams.

#### zmod IFS command reference

The raw IFS commands (`zmod_ifs.py` registrations + `docs/en/AD5X.md`). `F##` numbers are thin wrappers over raw serial `F## C{port}…`. Not every raw `F##` the IFS firmware accepts is exposed as a zmod gcode macro — e.g. `F19` is used at the raw-serial layer but has no `IFS_F19` command (community knowledge, ninjamida's multi-IFS project); only the subset ZMOD actually drives is wrapped:

| Command | Action |
|---------|--------|
| `IFS_F10` | Insert filament (`F10 C{port} L{len} S{speed}`) |
| `IFS_F11 [LEN=mm] [SPEED=s] [CHECK=0/1]` | Remove/retract filament. **`LEN` defaults to 90** (barely moves) — pass the tube length for a full eject |
| `IFS_F13` | Query IFS state |
| `IFS_F24 PRUTOK=N` | Clamp the lane (gear grips) |
| `IFS_F39 PRUTOK=N` | Unclamp one lane (filament free to pull) |
| `IFS_F18` | Unclamp **all** lanes at once — no `PRUTOK` needed. ⚠️ zmod's `docs/en/AD5X.md` mistranslates this as "Filament purge everywhere"; the actual handler (`cmd_IFS_F18`) responds *"Unlocking all filaments"*. Handy for recovery when you don't know which lane is clamped (community tip, ninjamida) |
| `IFS_F112` | Stop filament feed |
| `IFS_STATUS` | Structured JSON state (`State`/`Ports`/`Silk`/`Chan`/…) — clean future presence source |
| `GET_ZCOLOR SILENT=1` | Per-slot loaded state + active lane as `// `-prefixed text (silk-sensor truth) |
| `REMOVE_PRUTOK_IFS PRUTOK=N` | Toolhead unload (heat + retract); **not** a per-lane jog |
| `IN_ZCOLOR SLOT=N NAPR=0/1` | zmod color-macro load (`NAPR=0`) / unload (`NAPR=1`) — emitted on the AD5X LCD / Mainsail path |

### Path Topology

```
  Port 1 ──┐
  Port 2 ──┤
            ├── Combiner ── Toolhead
  Port 3 ──┤
  Port 4 ──┘
```

`PathTopology::LINEAR` — 4 independent lanes merge at a single combiner before the toolhead.

### Capabilities

| Feature | Supported | Editable |
|---------|-----------|----------|
| Endless Spool | `Available` in every mode (stock zMod `FirmwareManaged` / plugin `PluginReadOnly`) | `ReadOnly` always - see below |
| Tool Mapping | Yes | Yes (16 tools → 4 ports) |
| Bypass Mode | Yes | Via `less_waste_external` |
| Spoolman | Optional | Works if configured |
| Auto-Heat on Load | No | -- |
| Dryer | No | -- |
| Device Actions | No | -- |
| Runout detection | Yes | Sensor-derived, HelixScreen-side — see "Unattended runout detection" above |
| Backup-spool switchover | Firmware-only | `variable_backup` on the `_ifs_vars` macro, read the same way whichever plugin is detected. HelixScreen reports the state, it does not perform the swap |

**Endless spool on IFS is read-only on purpose.** `get_endless_spool_capabilities()`
reports `Available` + `FirmwareManaged` + `provider="zmod"` + `enabled=On` while
`has_ifs_vars_` is false, because stock zMod's `ANALOG_PRUTOK` runs always-on with no toggle
- the [#1247](https://github.com/prestonbrown/helixscreen/issues/1247) reporter's original
misexpectation ("stock zMod has no switchover") was refuted by a source read of
`zmod_ifs.py:cmd_ANALOG_PRUTOK` plus on-device confirmation from raza616. Once `_IFS_VARS`
answers, availability stays `Available`, `provider` names the plugin (`"lessWaste"` or
`"bambufy"`, from the detected variable prefix), `restriction` becomes `PluginReadOnly`, and
`enabled` mirrors `variable_backup` - including a genuine `Unknown` when the key was never
read, since flattening that to `Off` would promise the user that no swap will happen when we
simply did not read the setting. Editability stays `ReadOnly` + `PluginReadOnly`: `backup` is
never written. The only write path would be `write_ifs_var("backup", …)`, which is a bare
`_IFS_VARS` G-code whose failure surfaces only as the console "Unknown command" latch that
demotes `has_ifs_vars_` for the session - not something to drive a user-facing toggle from.

There is no per-slot relation either, so no backup dropdown appears. What the plugin *will*
switch to is answered instead by `is_endless_spool_backup_eligible()`, which IFS overrides
with the rule the firmware enforces: exact material **and** exact colour **and** the port
reporting filament present. It shares `backup_eligible_locked()` with
`find_backup_slot_locked()`, so the runout detail text and the eligibility answer cannot
drift apart.

These capabilities are the ONLY path this state takes to the UI. The AD5X-specific
`ams_ifs_plugin` / `ams_ifs_backup_enabled` subjects have been retired in favour of
`ams_endless_state` / `ams_endless_text`, which `AmsState` publishes from
`get_endless_spool_capabilities()` for every backend.

### Open Issues & Debugging Notes

> **No AD5X test device.** HelixScreen ships IFS support **blind** — there is no AD5X in the test fleet. Every IFS fix is field-validated through users, primarily **raza616** (the most active AD5X/IFS reporter). Treat live Discord console pastes and freshly-captured debug bundles as the ground truth, and prefer regression tests + the mock backend (`HELIX_MOCK_AMS=ifs`) for anything that can't be exercised on hardware.

**Stuck-purge on load — KNOWN OPEN, UNRESOLVED.** Loading a lane via the multi-filament screen can leave HelixScreen stuck displaying "purging" indefinitely. **No confirmed cause; do not ship a speculative fix.** Dead ends already ruled out:

- *Head-sensor-clobber theory* — **disproved.** raza's live `QUERY_FILAMENT_SENSOR` showed both `head_switch_sensor` and `ifs_motion_sensor` detecting filament when loaded. (Bundle snapshots show `head_switch=false` in all three captures, but that reconciles to "nothing at the head *at capture time*" — raza had already unloaded — not a sensor inversion.)
- *`BlockingIOError [Errno 11]` at `gcode.py:459 _respond_raw`* — **red herring.** It's present in the *not*-stuck bundle too, in a bed-mesh-dump context. It's console-flood backpressure (drops echoed text, not state).

To actually crack it, capture — **while stuck**:

1. A debug bundle whose `log_tail` is **verified fresh** (its last timestamp == the bundle timestamp; see caveat below).
2. A simultaneous live `QUERY_FILAMENT_SENSOR` for both `head_switch_sensor` and `ifs_motion_sensor`.
3. A live `IFS_STATUS`.
4. zmod's own `/var/log/messages` — **the "did the load actually finish inside zmod" answer lives here, NOT in the bundle.**

> **Debug-bundle caveat (cost a whole investigation):** the bundle's `log_tail` can be a **stale ring buffer that predates the incident**. In the stuck-purge bundles the `log_tail` ended *before* the reported event, so the `RUN_ZCOLOR` / `IN_ZCOLOR` / `ActionPrompt` lines "read from the log" were an **old session**, and `klipper_log` was just a 74-second idle window (config dump + bed-mesh table). **Always confirm the `log_tail`'s last timestamp matches the bundle timestamp before trusting any "from the log" claim.** When the bundle is stale, the only valid runtime evidence is live console pastes.

### Key Files

| File | Purpose |
|------|---------|
| `include/ams_backend_ad5x_ifs.h` | Backend class declaration |
| `src/printer/ams_backend_ad5x_ifs.cpp` | Full implementation |
| `tests/unit/test_ams_backend_ad5x_ifs.cpp` | Unit tests (16 cases, 100+ assertions) |
| `docs/devel/printer-research/FLASHFORGE_AD5X_IFS_ANALYSIS.md` | Protocol research |

### Automatic Setup

AD5X users running ZMOD firmware get automatic detection — no configuration needed. When HelixScreen connects to a Moonraker instance with IFS sensors, it:

1. Detects `filament_switch_sensor _ifs_port_sensor_*` in object list
2. Sets `AmsType::AD5X_IFS`
3. Subscribes to `save_variables` for filament state
4. Creates `AmsBackendAd5xIfs` backend
5. Queries initial state via `printer.objects.query`

Existing beta testers upgrading to a version with IFS support will see the filament panel populate automatically on next connection.

---

## CFS (Creality Filament System)

The `box` Klipper object is shared by several firmwares that agree on almost nothing. CFS support therefore has **two independent axes** — do not infer one from the other:

**Axis 1 — macro dialect** (`CfsMacroVariant`), latched at backend construction:

| Printer family | Stock firmware path | Macro dialect | Detection signal |
|----------------|--------------------|---------------|-----------------|
| K2, K2 Pro, K2 Plus (built-in CFS) | Creality K2 firmware | `CR_BOX_*` primitives + `BOX_SAVE_FAN`/`BOX_MODE_WAIT` envelope | `PrinterDetector::is_creality_k1() == false` |
| K1, K1C, K1 Max (official CFS upgrade ≥ v2.3.5.33) | Creality K1 CFS upgrade firmware | Plain `BOX_*` primitives, no fan-save/mode-wait | `PrinterDetector::is_creality_k1() == true` |
| K2 Plus on a community Kalico port | [`Jacob10383/kalico`](https://github.com/Jacob10383/kalico) + a reimplemented `box.py` | High-level bare `T<n>` / `BOX_UNLOAD` | `api_version == 1` in the box payload |

**Axis 2 — box schema** (`CfsSchema`), detected per-payload by `AmsBackendCfs::detect_schema()`:

| Schema | Shape | Parser |
|--------|-------|--------|
| `Stock` | `T1`–`T4` nested units, four parallel arrays each, material **codes** | `parse_stock_box_status()` |
| `Flat` | One `slots[]` array of self-describing objects, plain material names, `#RRGGBB` colors | `parse_flat_box_status()` |

Both axes are decided **from the payload, never from `PrinterDetector`** — a community port reports as stock K2 Plus hardware by every model signal, so model detection cannot see the firmware swap. `Stock` is the default for anything ambiguous.

The command dialect is selected by the explicit `api_version == 1` field rather than inferred from the `Flat` status layout, so another firmware can use the same layout without inheriting this one's commands. It also cannot be detected with `has_macro("BOX_LOAD")`: the Fork commands are registered in Python, so they are not gcode_macros and never appear in `printer.objects.list`.

A `Flat` box whose module we cannot identify still has its control paths refused by `reject_if_flat_schema()`. Full field mapping, command signatures and remaining gaps: `printers/CREALITY_K2_SUPPORT.md` § "Community Kalico port".

[`Jacob10383/kalico`](https://github.com/Jacob10383/kalico) is the Kalico (Danger-Klipper) fork the port builds on — it is the firmware *base*, and it does **not** contain the CFS modules. `box.py` and its siblings are dropped in by the port's installer and are not committed to any public repo, so the repo link is context rather than a source for the command surface. To read the modules themselves, fetch them from the port's content-addressed firmware store: `printers/CREALITY_K2_SUPPORT.md` § "Getting the module source".

### Firmware requirements

- **K2 series:** Stock firmware. Detection is automatic when the CFS unit is paired (RS-485, exposes `box` Klipper object).
- **K1 series:** Requires the **official Creality K1/K1C/K1 Max CFS upgrade firmware** (the reporter for #968 had `v2.3.5.33`). Stock K1/K1C/K1Max firmware without the CFS upgrade does not expose the `box` object and the backend stays disabled. Community open-source K1 firmwares (Guilouz, etc.) do not currently bundle the CFS macros — install Creality's signed CFS-aware image to use the upgrade.

### Macro dialect comparison

| Operation | K2 emission | K1 emission |
|-----------|-------------|-------------|
| Envelope open | `SAVE_GCODE_STATE` → `BOX_SAVE_FAN` → `BOX_GO_TO_EXTRUDE_POS` → `BOX_MODE_WAIT` | `SAVE_GCODE_STATE` → `BOX_GO_TO_EXTRUDE_POS` |
| Load slot N | `CR_BOX_PRE_OPT` → `CR_BOX_EXTRUDE TNN=…` → `CR_BOX_WASTE` → `CR_BOX_FLUSH TNN=…` → `CR_BOX_END_OPT` | `BOX_EXTRUDE_MATERIAL TNN=…` → `BOX_MATERIAL_FLUSH TNN=…` |
| Unload current | `CR_BOX_PRE_OPT` → `CR_BOX_CUT` → `BOX_MODE_WAIT` → `CR_BOX_RETRUDE` → `CR_BOX_END_OPT` | `BOX_CUT_MATERIAL` → `BOX_RETRUDE_MATERIAL` |
| Envelope close (with wipe) | `BOX_NOZZLE_CLEAN` → `BOX_RESTORE_FAN` → `BOX_MOVE_TO_SAFE_POS` → `RESTORE_GCODE_STATE` | `BOX_NOZZLE_CLEAN` → `BOX_MOVE_TO_SAFE_POS` → `RESTORE_GCODE_STATE` |
| Tool remap | `BOX_MODIFY_TN T<src>=T<dst>` | (same — assumed; needs field confirmation) |
| Color sync | `BOX_MODIFY_TN_DATA ADDR=… NUM=… PART=color_value DATA=0RRGGBB` | (same — assumed; needs field confirmation) |

The K1 envelope is intentionally shorter — `BOX_SAVE_FAN` / `BOX_RESTORE_FAN` / `BOX_MODE_WAIT` are not exposed by the K1 CFS firmware (verified absent in the public K1-Max box.cfg dump at [DieDutchman/K1-Max-KAMP-CFS-Fix](https://github.com/DieDutchman/K1-Max-KAMP-CFS-Fix/blob/main/Config_Files/box.cfg) and from the #968 reporter's gcode/help output). Emitting them on K1 would surface as `key61 Unknown command`.

### Implementation

| File | Role |
|------|------|
| `include/ams_backend_cfs.h` | `CfsMacroVariant` enum, `AmsBackendCfs` class, static `load_gcode/unload_gcode/swap_gcode(idx, variant)` helpers |
| `src/printer/ams_backend_cfs.cpp` | `wrap_with_park_k1` / `wrap_with_park_k2` envelopes, K1-vs-K2 body emission |
| `include/printer_discovery.h` | `box` object handler — enables CFS for both K1 and K2 (#968 gate flipped) |

`AmsBackendCfs::macro_variant_` is latched in the constructor by querying `PrinterDetector::is_creality_k1()`. All member operations (`load_filament`, `unload_filament`, `change_tool`) thread `macro_variant_` into the gcode helpers. Static call sites without an explicit variant default to `K2` to preserve existing test behavior.

### Endless spool (auto-refill)

CFS reports `Available` + `ReadOnly` + `FirmwareManaged`, with `enabled` from
`box.auto_refill` (stock) or `box.runout_swap_enabled` (flat fork) via
`AmsSystemInfo::endless_spool_enabled`. On and off are therefore distinguishable, which the
old two-bool struct could not express - it hardcoded `supported = true` and buried the real
state in an untranslated `description`.

`AmsBackendCfs` deliberately does **not** override `get_endless_spool_config()`. The box picks
the refill spool itself from its own `same_material` groups and exposes no per-slot mapping to
read, so the base's empty relation is the truthful answer, and it is what keeps the context
menu from drawing a backup dropdown that could only ever read "None". `box.same_material` is
parsed for one purpose only - a material-code-to-name lookup used when resolving a slot's
material name - and is not wired to endless spool.

The user-facing on/off control is the `toggle_auto_refill` device action, which emits
`BOX_ENABLE_AUTO_REFILL`; it is not an endless-spool *edit* in the
`set_endless_spool_backup()` sense, which is why editability stays `ReadOnly`.

### Bypass / external spool

`supports_bypass` is hardcoded `false` in three places - the constructor and both schema
parsers - and `enable_bypass()` / `disable_bypass()` return `not_supported` without consulting
`bypass_available_for()`. So the **Enable Bypass Controls** override renders the external-spool
node and its metadata menu, but the sidebar's bypass toggle still fails. See
[Bypass visibility and the force override](#bypass-visibility-and-the-force-override).

The `Flat` schema does carry an `external: true` entry in `slots[]` for the spool holder.
`parse_flat_box_status()` skips it (it is not a CFS bay, and counting it renders a phantom
fifth slot on a 4-bay unit) and bounds-checks `loaded_slot` against the resulting vector,
because that field indexes the payload's array and can therefore name the external entry.
Bypass stays unsupported anyway: the port's `box.py` is unpublished, so there is no verified
command that loads from the holder, and advertising bypass would put a button on screen that
cannot work.

### Known limitations on K1

- `BOX_MODIFY_TN` (tool remap) and `BOX_MODIFY_TN_DATA` (color sync) are emitted with the same syntax on K1 — neither has been field-validated.
- `BOX_LOAD_MATERIAL_WITH_MATERIAL` and `BOX_QUIT_MATERIAL` (K1 high-level orchestrators) are not used; HelixScreen drives the primitives directly to keep behavior parallel between the two backends.
- Bed-area shrink for the rear-mounted K1 CFS upgrade (~5 mm Y) is not yet applied via the printer database.
- Hardware validation for K1/K1C is pending — track via [#968](https://github.com/prestonbrown/helixscreen/issues/968).

---

## QIDI Box (QIDI PLUS4 / Q2 / MAX4)

> **Status: STUB** — The `AmsType::QIDI_BOX` enum value, factory wiring, and a no-op `AmsBackendQidi` scaffold exist so the type round-trips through the rest of the system. No real protocol is implemented. Every backend operation logs `spdlog::warn("... not yet implemented")` and returns `AmsErrorHelper::not_supported(...)`. Do **not** ship this as a user-facing feature until live hardware validation has happened.

The QIDI Box is QIDI's RFID-aware multi-material system: 4 slots per unit, chainable up to 4 units = 16 colors, with active drying up to 65°C and runout/tangle sensors. It is a **hub-style AMS** (like FlashForge IFS or Bambu AMS), not a lane-selector MMU — the closest in-tree analog is `AmsBackendAd5xIfs`, not Happy Hare or AFC.

### Compatible Hardware

| Printer | Supported | Notes |
|---------|-----------|-------|
| QIDI PLUS4 | Yes (per QIDI) | PLUS4 kit is not interchangeable with Q2/MAX4 — different hub board + data cable |
| QIDI Q2    | Yes (per QIDI) | Same kit as MAX4 |
| QIDI MAX4  | Yes (per QIDI) | Same kit as Q2 |
| Q1 Pro     | **No** | Unsupported by QIDI — different mainboard generation |
| X-Max 3    | **No** | Unsupported by QIDI — older MKSPI board |

The `assets/config/printer_database.json` entries for `qidi_plus_4` and `qidi_q2` carry an `"ams_type": "qidi_box"` capability tag. A `qidi_max_4` entry does not yet exist in the database — add one alongside the real protocol work.

### Detection

**Not yet wired.** The `ams_type` capability in the printer database is informational today — actual filament-system detection runs through heuristics in `include/printer_discovery.h` against `printer.objects.list`. Detection for QIDI Box will likely key off Klipper objects exposed by the Box's udev-identified USB-serial device (`QIDI_BOX_V1`) or the `_BOX_*` gcode macros. The exact object names need to be enumerated on a real PLUS4 / Q2 / MAX4.

### Firmware Openness

QIDI printers (Q1 Pro and newer) run forks of Klipper and Moonraker from [QIDITECH/klipper](https://github.com/QIDITECH/klipper) and [QIDITECH/moonraker](https://github.com/QIDITECH/moonraker). SSH is open by default (`mks` / `makerbase`), and KIAUH is pre-installed. QIDI discourages upstream Klipper updates because their board requires their fork.

**The Box firmware itself ships as obfuscated `.so` Python extension modules.** A community open-source reimplementation at [qidi-community/Plus4-Wiki customisable_qidibox_firmware](https://github.com/qidi-community/Plus4-Wiki/tree/main/content/customisable_qidibox_firmware) replaces six modules (`box_detect.py`, `box_rfid.py`, `box_stepper.py`, `box_extras.py`, `aht20_f.py`, `buttons_irq.py`) with editable Python. Maintainers label it "strongly WIP." This repo is the primary protocol reference for a HelixScreen integrator.

### Control Surface (expected)

All control runs through Klipper gcode macros — **no dedicated Moonraker endpoints**, no REST extension. State lives in printer objects and `save_variables`, same shape as AD5X IFS. Known macro names from the QIDI stock config:

| Command | Action |
|---------|--------|
| `BOX_CHANGE_FILAMENT` | Tool change |
| `_BOX_START` | Internal helper |
| `_BOX_*` | Additional internal macros |

Exact parameter shapes and the full macro list need to be confirmed against a real printer.

### Path Topology

```
  Slot 1 ──┐
  Slot 2 ──┤
            ├── Hub ── Toolhead
  Slot 3 ──┤
  Slot 4 ──┘
```

`PathTopology::HUB` — slots converge at a hub inside the Box before the toolhead. Chained boxes add units with their own hubs; the multi-unit addressing scheme is not publicly documented.

### RFID

Spools identify via MIFARE Classic RFID tags. Data lives in sector 1 block 0. Third-party read/write tools exist:

- [TinkerBarn/BoxRFID](https://github.com/TinkerBarn/BoxRFID) — Electron desktop app
- [n0cloud/qidi-box-rfid-manager](https://github.com/n0cloud/qidi-box-rfid-manager) — mobile
- [LexyGuru/Qidi_RFID_App](https://github.com/LexyGuru/Qidi_RFID_App)

### Do NOT Confuse With

- **Happy Hare "QuattroBox"** — listed in Happy Hare's supported hardware, but it is an unrelated DIY MMU by [Batalhoti](https://github.com/Batalhoti/QuattroBox). Happy Hare does **not** support the QIDI Box.
- **The `"box"` string alias in `ams_type_from_string()`** — already claimed by `CFS` (Creality K2 "box" terminology). QIDI Box requires the explicit `"qidi_box"` / `"QIDI Box"` / `"qidibox"` spelling.

### Capabilities (planned)

| Feature | Expected | Notes |
|---------|----------|-------|
| Endless Spool | Yes (auto-backup-spool) | Advertised by QIDI. The stub reports `Unsupported` today - it does not override `get_endless_spool_capabilities()`. Expect `FirmwareManaged` read-only if it turns out to work like AD5X IFS |
| Tool Mapping | Likely via `save_variables` | Matches AD5X IFS shape |
| Bypass Mode | Unknown | Need hardware inspection. Backend hardcodes `false`; [the force override](#bypass-visibility-and-the-force-override) shows the external spool for tracking only |
| Spoolman | Optional | Works through standard Moonraker `[spoolman]` |
| Auto-Heat on Load | Unknown | |
| Dryer | Yes (up to 65°C) | `aht20_f.py` owns humidity sensing |
| Device Actions | Unknown | |

### Key Files

| File | Purpose |
|------|---------|
| `include/ams_backend_qidi.h` | Backend class declaration (stub) |
| `src/printer/ams_backend_qidi.cpp` | Stub implementation — logs warn, returns not-supported |
| `include/ams_types.h` | `AmsType::QIDI_BOX` enum + string converters |

No dedicated unit tests yet — adding them is blocked on having real protocol behavior to test against.

### Follow-up Work (in order)

1. Get access to a PLUS4, Q2, or MAX4 with a Box attached.
2. SSH in (`mks` / `makerbase`), enumerate `printer.objects.list` and `save_variables` keys. Capture the stock gcode macro bodies.
3. Add detection to `PrinterDiscovery::parse_objects()` — key off whatever Klipper objects the Box exposes.
4. Implement `AmsBackendQidi` on top of `AmsSubscriptionBackend`, modeled on `AmsBackendAd5xIfs` (printer-object polling + macro invocation).
5. Add the `qidi_max_4` entry to `assets/config/printer_database.json`.
6. Add `assets/images/ams/qidi_box_64.png` (TODO comment exists in the stub).
7. Write unit tests against captured real-device fixtures.

---

## Dryer / Box-Heater Control

Some AMS backends include an integrated filament dryer — a heated chamber that removes moisture from hygroscopic filaments (Nylon, PA-CF, TPU, PETG, etc.) before or during a print. HelixScreen exposes a common dryer control UI across all backends that support it.

### Data Flow

```
AmsBackend::get_dryer_info()       populates DryerInfo (ams_types.h)
        │
        ▼
AmsState::sync_dryer_from_backend()  bridges to LVGL subjects
        │
        ▼
AmsEnvironmentOverlay              control UI (ui_ams_environment_overlay.cpp
  + ui_xml/ams_environment_overlay.xml)  target temp, duration, start/stop
```

`DryerInfo` (declared in `include/ams_types.h`) carries:

| Field | Type | Description |
|-------|------|-------------|
| `supported` | bool | Whether this backend has a dryer at all |
| `active` | bool | Dryer is currently running |
| `current_temp` | float | Current chamber temperature (°C) |
| `target_temp` | float | Target setpoint (°C) |
| `remaining_minutes` | int | Countdown to end of session (-1 = no timer) |
| `max_temp` | float | Hardware maximum for the target slider |

> **Humidity is not on `DryerInfo`.** It lives on `EnvironmentData` (per-unit, `AmsUnit::environment`) alongside the box temperature, because humidity is a per-enclosure reading from an environment sensor, not a property of the global dryer session. The dryer overlay and the AMS panel environment indicator both read `AmsUnit::environment`.

### Backend Virtual Interface

Declared in `include/ams_backend.h`. Default implementations return `supported=false` / `not_supported` so existing backends that don't have a dryer need no changes.

| Virtual | Default | Description |
|---------|---------|-------------|
| `get_dryer_info()` | `DryerInfo{.supported=false}` | Read current dryer state |
| `start_drying(temp, minutes)` | NOT_SUPPORTED | Begin a drying session |
| `stop_drying()` | NOT_SUPPORTED | End the active session |
| `update_drying(temp, minutes)` | NOT_SUPPORTED | Change temp/time mid-session |
| `get_drying_presets()` | empty vector | Return material-preset list |

### Backend Support Matrix

| Backend | Drying | Command |
|---------|--------|---------|
| ACE (Anycubic ACE Pro) | ✅ | `ACE_START_DRYING TEMP=<t> DURATION=<m>` / `ACE_STOP_DRYING` |
| Happy Hare | ✅ | `MMU_HEATER DRY=1 TEMP=<t> TIMER=<mins>` / `MMU_HEATER STOP=1` (TIMER is minutes; see [Happy Hare Specifics](#happy-hare-specifics)) |
| QIDI Box | ✅ | `ENABLE_BOX_DRY BOX=<n> TEMP=<t> END_TIME=<h>` / `DISABLE_BOX_DRY BOX=<n>`, with `SET_HEATER_TEMPERATURE` fallback when `box_extras` is absent. Write-path always enabled (commands verified vs QIDI firmware, #1030). |
| CFS (Creality K2) | ❌ | Not supported — CFS has no drying hardware |
| AFC (Box Turtle / OpenAMS) | ❌ | Not supported |
| AD5X IFS | ❌ | Not supported |
| Snapmaker U1 (SnapSwap) | ❌ | Not supported |
| Tool Changer | ❌ | Not applicable |

### QIDI Box Specifics

The QIDI Box dryer uses the printer's standard `heater_generic heater_box<N>` Klipper object — the same safety system (temperature limits, watchdog) that applies to any Klipper heater. The active session timer is tracked via `box_extras.box_drying_state.box<N>` (fields `dry_state` and `end_time`). Remaining time is computed as `(end_time - now) / 60` since there is no native remaining-minutes field.

See [QIDI_BOX_HEATER.md](QIDI_BOX_HEATER.md) for full reverse-engineering details: Klipper object schema, firmware command variants, config key spellings, and per-material drying tables.

### Happy Hare Specifics

Happy Hare's filament dryer is driven by the `MMU_HEATER` command and configured under `[mmu_machine]`. HelixScreen reads the dryer's object names from `configfile.settings` once at connect (`query_heater_config_from_config`), then tracks live state over the normal subscription push — **there is no polling**.

**What HelixScreen supports today:**

- **Commands.** `MMU_HEATER DRY=1 TEMP=<°C> TIMER=<minutes>` to start, `MMU_HEATER STOP=1` to stop. `TIMER` is **minutes** (float, `minval=0`), so sub-hour cycles are valid — the duration field in the overlay is a minutes field for this reason.
- **Box temperature.** Read from the `filament_heater` `heater_generic` object's live `temperature`.
- **Box humidity.** Happy Hare's `mmu` object deliberately does **not** republish temp/humidity (`mmu_environment_manager.get_status()` omits them by design); the client must read the environment sensor object directly. Humidity therefore comes from the **backing humidity chip** — `bme280` / `htu21d` / `sht3x` / `aht10` `<name>`, where `<name>` is the bare second token of the `environment_sensor` value (e.g. `temperature_sensor box` → `htu21d box`). Discovery subscribes these chips and requests the `humidity` field on all temperature sensors (only objects present in `objects.list` are subscribed, so this is safe for printers without them).
- **Per-unit / multi-MMU resolution.** Happy Hare has two mutually-exclusive enclosure forms: a **shared** enclosure (scalar `filament_heater` / `environment_sensor`) or **per-gate** hardware (plural `filament_heaters` / `environment_sensors`, one entry per gate, distributed across units for multi-MMU). `get_system_info()` resolves **each unit's** heater + sensor — the scalar form applies to every unit; the per-gate lists map each unit to the object at its first gate (`first_slot_global_index`). So a multi-MMU rig with distinct box sensors shows the correct temp/humidity per unit in the panel indicator and overlay.

**What is NOT yet supported (boundary):**

- **Per-gate / per-slot / per-lane *drying control*.** The control surface (`AmsEnvironmentOverlay`) and the `DryerInfo` model are **single-dryer-global**: start/stop drives the default heater (no `GATES=` selector), and the per-gate `drying_state` **array** is collapsed to a single "any gate active" boolean in `parse_mmu_state`. Independently drying specific gates (`MMU_HEATER … GATES=g1,g2`), per-gate countdowns, and the `HUMIDITY=` termination target are tracked post-1.0 in **#1026**.

> Note: the per-unit environment *readout* (above) is unverified on per-gate/EMU hardware — it is unit-tested (scalar + 2-unit per-gate) but we own no EMU rig. The QIDI Box (the common Happy-Hare-on-a-box case) is a single shared sensor.

### Adding Dryer Support to a New Backend

Override the five dryer virtuals in your `AmsBackend` subclass. At minimum implement `get_dryer_info()` — the UI polls this to drive the display. Implement `start_drying()` and `stop_drying()` to make the controls functional. `update_drying()` and `get_drying_presets()` are optional enhancements.

Dryer status flows into `AmsState::sync_dryer_from_backend()` the same way slot state does — no additional wiring is required in `AmsState`.

---

## Context Menu Actions

The `AmsContextMenu` (`ui_ams_context_menu.h`) provides per-slot operations:

| Action | Description | Availability |
|--------|-------------|------------|
| **Load** | Load filament from this slot | When slot has filament and not at toolhead |
| **Unload** | Unload filament from extruder | When filament is loaded to extruder |
| **Eject** | Eject filament from hub back to spool | When hub-loaded but not at toolhead, and `supports_lane_eject()` is true (AFC and Happy Hare) |
| **Spool Info** | View/edit slot properties (color, material, brand) | When slot has filament |
| **Spoolman** | Assign a Spoolman spool to this slot | Always |

The context menu also includes inline dropdowns for:

- **Tool Mapping**: Assign which tool number maps to this slot (if backend supports it)
- **Endless Spool Backup**: Set backup slot for runout (if backend supports it)

The tool dropdown is populated from `backend->get_tool_mapping()`.

The backup dropdown is populated from `backend->get_endless_spool_config()`, which is a
*group* relation, projected to this slot's single successor with
`helix::printer::endless_spool_backup_for()` - the same projection the panel's arrow renderer
uses (see [Endless Spool](#endless-spool-shared-model)). Its row visibility is the pure
predicate `AmsContextMenu::decide_show_backup_row(caps, has_relation)`:

- Not `available()` - hidden.
- `editable()` - shown, because there is something to write even before anything is set.
- Read-only **and** there is a relation to display - shown, but the dropdown gets
  `LV_STATE_DISABLED`.
- Read-only with no relation - hidden. This is the CFS and AD5X IFS case: the firmware picks
  the backup and publishes no mapping, so a visible dropdown could only ever read "None".

The "(incompatible)" suffix on backup options still comes from
`filament::are_materials_compatible()` directly in `build_backup_options()`; it does not yet
route through `AmsBackend::is_endless_spool_backup_eligible()`, so a backend that tightens the
rule (AD5X IFS) does not yet tighten this label.

---

## Device Operations Overlay

The `AmsDeviceOperationsOverlay` (`ui_ams_device_operations_overlay.h`) consolidates device-specific controls:

### Fixed Actions (all backends)

| Action | G-code (varies by backend) | Description |
|--------|---------------------------|-------------|
| Home | `MMU_HOME` / `AFC_RESET` | Reset to home position (label follows `reset_button_label()`; AFC sends `AFC_RESET`, not `AFC_HOME`) |
| Recover | `MMU_RECOVER` / `AFC_RESET` | Attempt error recovery |
| Abort | `cancel()` | Cancel current operation |
| Bypass Toggle | `enable_bypass()` / `disable_bypass()` | Toggle bypass mode (if supported) |

### Bypass visibility and the force override

Two pure predicates decide whether any bypass UI exists. Both had been inlined at four render
sites, where three of the four had already drifted apart, so neither may be re-derived locally:

| Predicate | Header | Rule |
|-----------|--------|------|
| `bypass_available(supports_bypass, force_override)` | `ams_bypass_policy.h` | `supports_bypass \|\| force_override` - folds the user's override into the firmware's report |
| `bypass_node_visible(supports_bypass, bypass_active, is_afc, always_show)` | `ui_bypass_spool_widget.h` | Additionally hides AFC's *virtual* bypass sensor while disengaged (#1229) unless `always_show` |

`bypass_available_for(bool)` and `bypass_node_visible_for(const AmsBackend*)` gather the live
inputs from `SettingsManager` and the backend; the render sites call the `_for` variants. The
firmware's own `supports_bypass` is never overwritten, so switching the override off restores
reality without a re-parse.

Settings keys (per-printer, under `df() + "ams/"`): `force_bypass_controls`,
`always_show_bypass_spool`. The **Enable Bypass Controls** row in `ams_device_operations.xml`
binds `hidden` to `ams_device_ops_fw_supports_bypass == 1`, so it self-hides on hardware that
already reports a bypass. Flipping it calls `AmsState::sync_from_backend()` +
`update_from_backend()` because both gating subjects are recomputed from the backend, not from
the setting, and neither moves on its own.

Where the override lands, by backend:

| Backend | `supports_bypass` | Override row shown | `enable_bypass()` with override on |
|---------|-------------------|--------------------|------------------------------------|
| AFC | `afc_defaults` caps, default `true` | no | Consults `bypass_available_for()` |
| AD5X IFS | `true` (`ams_backend_ad5x_ifs.cpp:85`) | no | Real command via `less_waste_external` |
| Happy Hare | Runtime from `[mmu_machine] has_bypass`; `false` until first status | Only when `has_bypass: 0` | Consults `bypass_available_for()`; `MMU_SELECT_BYPASS` runs |
| CFS | Hardcoded `false` in ctor + both parsers (`:397`, `:526`, `:917`) | yes | `not_supported`, unconditionally |
| ACE | Hardcoded `false` (`:43`) | yes | `not_supported` |
| Snapmaker | Hardcoded `false` (`:237`) | yes | `not_supported` |
| Tool Changer | Hardcoded `false` (`:31`) | yes | `not_supported` |
| QIDI Box | Hardcoded `false` (`:193`, stub backend) | yes | `not_supported` |

Happy Hare is the one backend where the override changes machine behavior rather than only the
UI: `cmd_MMU_SELECT_BYPASS` never checks `has_bypass`, it deselects the gear steppers and
reports gate -2 either way, while `has_bypass` defaults to `0` for `mmu_vendor: Other` (a QIDI
Box driven through Happy Hare reports exactly that) and is ANDed with the calibrated bypass
offset on type-A selectors.

On the bottom five rows the override is display-and-tracking only. Their `is_bypass_active()`
returns a literal `false`, so `bypass_node_visible()` reaches the `!is_afc` branch and renders
the node; tapping it opens `show_external_spool_menu()`, which writes HelixScreen-side slot
metadata (`AmsState::set_external_spool_info()`) and sends nothing to the printer. The sidebar
toggle, however, is gated on the same `ams_supports_bypass` subject, so it appears too and then
fails with the backend's `not_supported`.

> **Adding a backend:** if the override is meant to engage a command the firmware can actually
> run, guard `enable_bypass()` with `bypass_available_for(system_info_.supports_bypass)` rather
> than `system_info_.supports_bypass` directly - that is what AFC, Happy Hare and the mock do.
> A hardcoded `not_supported` is the correct answer only when no command exists at all.

### Dynamic Actions (backend-specific)

Each backend can expose dynamic device actions via `get_device_sections()` and `get_device_actions()`. The UI renders them as buttons, toggles, sliders, or dropdowns based on `ActionType`.

The section lists live in `src/printer/afc_defaults.cpp` (`afc_default_sections()`) and
`src/printer/hh_defaults.cpp` (`hh_default_sections()`):

| Backend | Sections |
|---------|----------|
| AFC | **Setup**, **Speed Settings**, **Toolhead**, **Maintenance**, **Hub & Cutter**, **Tip Forming**, **Purge & Wipe** (7) |
| Happy Hare | **Setup**, **Speed**, **Toolhead**, **Accessories**, **Maintenance** (5) |

There is no separate "Calibration" or "LED & Modes" section on AFC. The calibration
wizard, bowden length, LED toggles and quiet mode all live under **Setup**.
`AmsBackendAfc::get_device_sections()` drops **Tip Forming** whenever
`system_info_.tip_method != TipMethod::TIP_FORM`, which is the common case (the default
capability set is `TipMethod::CUT`), so a stock Box Turtle shows six.

Action counts are not fixed either. `afc_default_actions()` returns 26 static actions, and
`AmsBackendAfc::get_device_actions()` then adds one **Hub Distance** slider per lane and,
on a multi-extruder rig, swaps the single bowden / toolhead / toolhead-LED entries for
per-extruder ones. See the [AFC-Specific Features](#afc-specific-features) section for
details.

---

## Mock Mode for Testing

The `AmsBackendMock` simulates any of the supported backend types for UI development and testing.

### Activation

Mock mode is activated when `RuntimeConfig::should_mock_ams()` returns true (typically via the `--test` CLI flag). The factory method `AmsBackend::create()` automatically returns a mock backend in this case.

Pass `--real-ams` alongside `--test` to opt back out and drive a real backend (e.g. `AmsBackendHappyHare`) against the mock Moonraker client instead of `AmsBackendMock`. This is what makes backend-specific chokepoints reachable under `--test` — for example `AmsSubscriptionBackend::ensure_homed_then()`'s "Home printer first?" confirmation, which `AmsBackendMock` never goes near since it doesn't inherit `AmsSubscriptionBackend`. The mock Moonraker client only simulates a minimal, static `mmu` status for Happy Hare (`moonraker_client_mock_objects.cpp`'s `get_mock_mmu_status()`: 4 gates, a mix of loaded/empty, no operation state machine) — it is a plumbing harness for exercising backend code paths, not a UI development tool. Use plain `--test` + `HELIX_MOCK_AMS` (below) for that.

**`--real-ams` seeds Happy Hare only and does not compose with `HELIX_MOCK_AMS`.** The backend comes from mock hardware discovery, not from `HELIX_MOCK_AMS` — that variable is read inside `AmsBackend::create()`'s mock branch (`src/printer/ams_backend.cpp`), which `--real-ams` bypasses entirely. So `HELIX_MOCK_AMS=toolchanger` combined with `--real-ams` still swaps in a real `AmsBackendToolChanger`, but with zero seeded state — a silently empty panel, not a toolchanger simulation.

The seed also dispatches from the main thread (inside an `UpdateQueue` drain), while production delivers the same `mmu` payload from the libhv WebSocket event-loop thread. A threading bug in a backend's `handle_status_update` will not reproduce under `--real-ams`.

```bash
./build/bin/helix-screen --test --real-ams -vv
```

### Environment Variables

| Variable | Values | Default | Description |
|----------|--------|---------|-------------|
| `HELIX_AMS_GATES` | 1-16 | 4 | Number of simulated slots |
| `HELIX_MOCK_AMS` | `afc`, `box_turtle`, `boxturtle`, `toolchanger`, `tool_changer`, `tc`, `mixed`, `multi`, `ifs`, `ad5x`, `ad5x_ifs` | Happy Hare | AMS type to simulate |
| `HELIX_MOCK_AMS_STATE` | `idle`, `loading`, `error`, `bypass` | `idle` | Visual scenario to simulate |
| `HELIX_MOCK_DRYER` | `1`, `true` | Disabled | Simulate integrated dryer |
| `HELIX_MOCK_DRYER_SPEED` | Integer | 60 | Dryer speed multiplier (60 = 1 real sec = 1 sim min) |

### Mock AFC Mode

```bash
HELIX_MOCK_AMS=afc ./build/bin/helix-screen --test
```

When AFC mock mode is enabled:

- Reports `AmsType::AFC` with type name "AFC (Mock)"
- Uses `PathTopology::HUB` (4 lanes merge through hub)
- Configures 4 lanes with realistic filament data (PLA, PETG, ABS, ASA)
- Sets AFC-specific device sections: Calibration, Maintenance, Speed Settings, LEDs & Modes
- Includes mock device actions: calibration wizard, bowden length slider, speed multipliers, lane tests, blade change, park, brush, motor reset, LED toggle, quiet mode toggle
- Uses `TipMethod::CUT`
- Editable endless spool with pre-configured backup mapping
- Supports auto-heat on load

### Mock Mixed Topology Mode

```bash
HELIX_MOCK_AMS=mixed ./build/bin/helix-screen --test
```

Simulates a real-world 6-toolhead toolchanger with mixed AFC hardware (based on production data):

- **Unit 0**: Box Turtle "Turtle_1" — 4 lanes, PARALLEL, lanes 0-3 → T0-T3, TurtleNeck buffers, no hub sensor
- **Unit 1**: OpenAMS "AMS_1" — 4 lanes, HUB, lanes 4-7 all → T4, per-lane hubs (Hub_1-Hub_4), no buffers
- **Unit 2**: OpenAMS "AMS_2" — 4 lanes, HUB, lanes 8-11 all → T5, per-lane hubs (Hub_5-Hub_8), no buffers
- Total: 12 slots, 6 physical toolheads
- Per-unit topology via `get_unit_topology()`
- 23 regression tests in `tests/unit/test_ams_mock_mixed_topology.cpp` validate this setup

### Mock Tool Changer Mode

```bash
HELIX_MOCK_AMS=toolchanger ./build/bin/helix-screen --test
```

- Reports `AmsType::TOOL_CHANGER`
- Uses `PathTopology::PARALLEL`
- Disables bypass mode
- Labels slots as "T0", "T1", etc.

### Mock AD5X IFS Mode

```bash
HELIX_MOCK_AMS=ifs ./build/bin/helix-screen --test
```

- Reports `AmsType::AD5X_IFS` with type name "AD5X IFS"
- Uses `PathTopology::LINEAR`
- 4 slots with bypass support
- Tool mapping enabled; endless spool `Unsupported` - the scenario clears
  `endless_spool_supported_` / `endless_spool_editable_`, not just
  `system_info_.endless_spool_enabled`. Clearing only the bit left the mock AD5X with an
  editable backup dropdown and endless-spool arrows the real backend does not have. The
  Snapmaker scenario clears the same pair for the same reason.

### Mock Realistic Mode

```bash
HELIX_MOCK_AMS_STATE=loading ./build/bin/helix-screen --test
```

Enables multi-phase operation simulation with realistic timing:

- **Load**: HEATING -> LOADING (segment animation) -> IDLE
- **Unload**: HEATING -> CUTTING -> UNLOADING (animation) -> IDLE
- Timing respects `--sim-speed` flag with +/-20-30% variance

### Mock-Specific Test Methods

The mock backend exposes additional methods for unit testing:

| Method | Description |
|--------|-------------|
| `simulate_error(AmsResult)` | Trigger a specific error condition |
| `simulate_pause()` | Set PAUSED state (user intervention required) |
| `resume()` | Resume from PAUSED state |
| `set_operation_delay(ms)` | Set simulated operation delay |
| `force_slot_status(slot, status)` | Force a specific slot status |
| `set_has_hardware_bypass_sensor(bool)` | Toggle hardware vs virtual bypass sensor |
| `set_endless_spool_supported(bool)` | Availability: `Available` vs `Unsupported`. Also sets `system_info_.endless_spool_enabled`, which is what `caps.enabled` reads |
| `set_endless_spool_editable(bool)` | Editability: `PerSlot` vs `ReadOnly` + `FirmwareManaged` (the shape CFS and a multi-unit MMU have). When read-only, `set_endless_spool_backup()` is rejected by the base with the translated restriction reason, not by the mock |
| `set_device_sections(sections)` | Set custom device sections for testing |
| `set_device_actions(actions)` | Set custom device actions for testing |

---

## Developer Guide: Adding a New Backend

### 1. Define the AmsType

Add a new value to `AmsType` in `ams_types.h`:

```cpp
enum class AmsType {
    // ... existing values ...
    MY_SYSTEM = 5  // New system type
};
```

Update `ams_type_to_string()`, `ams_type_from_string()`, and the `is_filament_system()` / `is_tool_changer()` helpers as appropriate.

### 2. Add Detection in PrinterDiscovery

In `printer_discovery.h`, add detection logic in `parse_objects()`:

```cpp
else if (name == "my_system") {
    has_mmu_ = true;
    mmu_type_ = AmsType::MY_SYSTEM;
}
```

Add any component discovery (lane names, tool names, etc.) as needed.

### 3. Implement the Backend Class

Create `include/ams_backend_mysystem.h` and `src/printer/ams_backend_mysystem.cpp`. Implement all pure virtual methods from `AmsBackend`:

**Required overrides:**

- `start()`, `stop()`, `is_running()` -- Lifecycle
- `set_event_callback()` -- Event registration
- `get_system_info()`, `get_type()`, `get_slot_info()`, `get_current_action()`, `get_current_tool()`, `get_current_slot()`, `is_filament_loaded()` -- State queries
- `get_topology()`, `get_filament_segment()`, `get_slot_filament_segment()`, `infer_error_segment()` -- Path visualization
- `load_filament()`, `unload_filament()`, `select_slot()`, `change_tool()` -- Operations
- `recover()`, `reset()`, `cancel()` -- Recovery
- `set_slot_info()`, `set_tool_mapping()` -- Configuration
- `enable_bypass()`, `disable_bypass()`, `is_bypass_active()` -- Bypass mode

**Optional overrides (with default implementations):**

- `clear_fault()` -- Clear a latched fault, bookkeeping only (default: forwards to `cancel()`)
- `recover_lane_position()` -- Physical retract of a stranded lane (default: NOT_SUPPORTED)
- `get_dryer_info()`, `start_drying()`, `stop_drying()`, `update_drying()` -- Dryer control
- `get_endless_spool_capabilities()`, `get_endless_spool_config()` -- Endless spool state. `set_endless_spool_backup()` is **not** an override point: it is non-virtual and owns every rejection. Supply `apply_endless_spool_backup()` (protected, transport only), `endless_spool_slot_count()` (protected, only if `total_slots` is wrong for you), and `is_endless_spool_backup_eligible()` (only to tighten the default material-compatibility rule). `reset_endless_spool()` already works for any editable backend by looping the setter with -1 - override it only if your firmware has a real reset primitive. See § [Endless Spool](#endless-spool-shared-model).
- `get_tool_mapping_capabilities()`, `get_tool_mapping()` -- Tool mapping
- `get_device_sections()`, `get_device_actions()`, `execute_device_action()` -- Device-specific actions
- `set_discovered_lanes()`, `set_discovered_tools()` -- Discovery configuration
- `supports_auto_heat_on_load()` -- Auto-heat capability
- `supports_lane_eject()` + `eject_lane()` -- Cold retract of a lane's filament back to the spool. Without the predicate the context menu never offers Eject, whatever `eject_lane()` does.
- `has_per_slot_loaded_authority()` -- Return true only when the firmware reports load state **per slot**. Leave it false when your per-slot answer is derived from an aggregate "current slot" pointer, or a mid-toolchange null will drop the highlight.
- `reset_button_label()` -- Sidebar Reset button text (default `"Reset"`; Happy Hare uses `"Home"`)

**The error seam.** All optional, all defaulted to "nothing", and a backend that skips the
whole group gets **no error dialog at all**, silently. Read § [Two error channels](#two-error-channels)
before implementing any of them:

- `classify_error()` -- Channel A: claim one gcode-response line and return an `ErrorEvent`. The router applies **no line filtering**, so every override must gate itself (AFC and Happy Hare take only `!!` lines via `helix::is_bang_line`; CFS deliberately takes only non-`!!` lines). Return `nullopt` to defer to the generic classifier.
- `current_error()` -- Channel B: the current actionable fault derived from backend **status**, consulted only by `AmsErrorBridge` on the rising edge into `AmsAction::ERROR`. Independent of channel A, not an alternative to it: AFC overrides both.
- `build_recovery_actions()` (protected) -- The buttons the user can tap for the current fault. **The caller already holds `mutex_`**, which is non-recursive: an override that locks deadlocks. The base returns an empty vector deliberately: `decide_presentation()` keys off `recovery_actions.empty()` to pick MODAL vs MODAL_WITH_RECOVER, so recovery is strictly opt-in.

**The toolchange narration seam.** Also optional; leaving it empty falls back to the
sidebar's legacy `AmsAction`-driven hardcoded step list, which is a valid choice:

- `toolchange_phase_template(op)` -- The ordered phase list per `StepOperationType`. Order it by **first narration**, not by macro name: a phase whose line fires twice re-reports the earlier index, and the step bar has no notion of a repeated step.
- `match_narration_phase()` -- Map a `//` narration body to a phase id. Loose substring needles are fine here; the text came from a macro's own `respond_info`.
- `match_bare_narration_phase()` -- Map an **unprefixed** console line to a phase id. Must match anchored line shapes, never loose substrings. The open console carries user-controlled gcode filenames, so a `cut` needle fires on `haircut.gcode`.

**Runout and spool-assignment routing.** Both default to something reasonable; override
only if your hardware model diverges:

- `recovers_filament_on_resume()` -- True when Resume itself re-feeds filament (Snapmaker U1 runs `AUTO_FEEDING` then `RESUME`). Such backends present Resume as the runout dialog's primary action and demote manual Load/Unload/Purge. Default false, which keeps Load prominent. That is correct for AFC, Happy Hare, and every basic runout sensor.
- `supports_per_tool_spool_assignment()` -- Whether each tool owns its own spool assignment. Default is `is_tool_changer(get_type())`; no backend currently overrides it.

### 4. Wire into the Factory

In `src/printer/ams_backend.cpp`, add cases to both `create()` overloads:

```cpp
case AmsType::MY_SYSTEM:
    return std::make_unique<AmsBackendMySystem>(api, client);
```

### 5. Add Mock Support

In `src/printer/ams_backend.cpp`, extend the `HELIX_MOCK_AMS` environment variable handling:

```cpp
if (type_str == "mysystem") {
    mock->set_my_system_mode(true);
}
```

Add corresponding `set_my_system_mode()` to `AmsBackendMock` if the new system has unique UI characteristics that need simulation.

### 6. Update AmsState (if needed)

If the new backend has special discovery requirements, update `AmsState::init_backend_from_hardware()` accordingly. For example, ACE supports both object-list detection (`ace` in `printer.objects.list`) and a REST probe fallback.

### 7. Add Tests

Write tests for:
- State parsing from Moonraker JSON
- G-code command generation
- Error handling and recovery
- Tool/slot mapping
- Path segment computation

See `tests/unit/test_ams_backend_happy_hare.cpp`, `test_ams_tool_mapping.cpp`, `test_ams_endless_spool.cpp`, and `test_ams_device_actions.cpp` for patterns.

---

## Spoolman Management & Spool Wizard

Beyond slot assignment, HelixScreen provides full Spoolman spool management:

- **SpoolmanPanel overlay** — Browse, search, edit, and delete spools with virtualized list (20-row pool)
- **New Spool Wizard** — 3-step guided creation: Vendor → Filament → Spool Details
- **Context menu** — Per-spool actions: Set Active, Edit, Delete
- **Edit modal** — Update weight, price, lot number, notes via PATCH

### Spool Wizard Architecture

The wizard (`SpoolWizardOverlay`) is a 3-step overlay:

1. **Step 0 — Select Vendor**: Search/filter vendors from Spoolman server, or create a new one via modal (`create_vendor_modal.xml`)
2. **Step 1 — Select Filament**: Filter filaments by selected vendor (`vendor.id` API param), or create a new one via modal (`create_filament_modal.xml`) with material from `filament::MATERIALS[]` database, color picker, temp ranges, weight
3. **Step 2 — Spool Details**: Remaining weight, price, lot number, notes — compact 2-column layout

Key patterns:
- **Modal forms** for vendor/filament creation (not inline) — keeps list scroll area maximized
- **Vendor filtering**: Filament API uses `vendor.id=X` (Spoolman's dot-notation filter syntax)
- **Color picker**: HSV picker + preset swatches, launched from filament creation modal
- **Atomic creation**: Creates vendor → filament → spool in sequence with best-effort rollback on failure
- **Row selection**: `LV_STATE_CHECKED` with `selected_style` (primary left border + elevated bg)

### Key Files

| File | Purpose |
|------|---------|
| `include/ui_spool_wizard.h` | Wizard overlay class declaration |
| `src/ui/ui_spool_wizard.cpp` | Wizard logic, API calls, callbacks |
| `ui_xml/spool_wizard.xml` | 3-step wizard layout |
| `ui_xml/create_vendor_modal.xml` | New vendor modal form |
| `ui_xml/create_filament_modal.xml` | New filament modal form |
| `ui_xml/wizard_vendor_row.xml` | Selectable vendor row (lv_button with checked style) |
| `ui_xml/wizard_filament_row.xml` | Selectable filament row (lv_button with checked style) |
| `src/ui/ui_color_picker.cpp` | Color picker modal (used by filament creation) |

See `docs/devel/plans/2026-02-15-spool-wizard-status.md` for visual test plan.

---

## Troubleshooting

### Common Issues by Backend

#### Happy Hare

| Symptom | Cause | Fix |
|---------|-------|-----|
| "No multi-filament system detected" | `mmu` object not in Klipper | Verify Happy Hare is installed and `[mmu]` section exists in printer.cfg |
| Gate status all "Unknown" | Subscription not receiving updates | Check Moonraker connection, verify `printer.mmu` is subscribable |
| Tool mapping not updating | Stale TTG map | Try reset tool mappings (sends 1:1 mapping for all tools) |
| Bypass button disabled | Hardware bypass sensor detected | System auto-detects bypass via sensor, manual toggle not available |

#### AFC

| Symptom | Cause | Fix |
|---------|-------|-----|
| "No multi-filament system detected" | `AFC` object not in Klipper | Verify AFC-Klipper-Add-On is installed |
| Lane count wrong | Discovery mismatch | Check for both `AFC_stepper lane*` and `AFC_lane lane*` objects in `printer.objects.list` (OpenAMS uses `AFC_lane`) |
| Too many nozzles drawn | HUB unit map values treated as separate tools | Verify topology detection — HUB units always have tool_count=1 regardless of `map` field values |
| Hub sensors not updating | Hub name doesn't match unit name | OpenAMS uses per-lane hubs (Hub_1..Hub_N) — check hub-to-unit ownership in `unit_infos_` |
| No filament colors/materials | AFC version too old or no Spoolman | `lane_data` database requires v1.0.32+; assign spools in Spoolman |
| Device actions missing | Backend not returning sections | Verify AFC backend is connected (not mock) |
| Bowden length slider shows wrong range | Default 450mm being used | Hub data may not be received yet; wait for state sync |
| Quiet mode not toggling | G-code not recognized | Verify AFC firmware supports `AFC_QUIET_MODE` command |

#### ACE (Anycubic ACE Pro)

| Symptom | Cause | Fix |
|---------|-------|-----|
| ACE Pro not detected | Object not in list + REST probe failed | Verify a ValgACE/BunnyACE/DuckACE driver is installed; check `ace` in `printer.objects.list` and `/server/ace/info` endpoint |
| Stale state | Polling interval | ACE polls at 500ms; state may lag slightly |
| Dryer not controllable | Missing REST bridge | BunnyACE/DuckACE users must install ValgACE's `ace_status.py` Moonraker component |

#### Tool Changer

| Symptom | Cause | Fix |
|---------|-------|-----|
| No tools shown | `toolchanger` object missing | Verify klipper-toolchanger is installed |
| Wrong tool count | Discovery mismatch | Check that `tool T*` objects appear in `printer.objects.list` |
| "Uninitialized" status | Tools not homed | Run `T0` or `SELECT_TOOL TOOL=T0` to initialize |

#### AD5X IFS

| Symptom | Cause | Fix |
|---------|-------|-----|
| IFS not detected | Missing or outdated ZMOD firmware | Install ZMOD v1.7.0+ (v1.6.2 hard minimum). Verify `zmod_ifs.py` is installed and `_ifs_port_sensor_*` sensors appear in `printer.objects.list` |
| Colors/materials empty | `save_variables` not populated | Run IFS calibration wizard in ZMOD to initialize `less_waste_*` variables |
| Slots all EMPTY | Port sensors not subscribed | Check that `filament_switch_sensor _ifs_port_sensor_{1-4}` are present |
| Tool mapping wrong | Stale `less_waste_tools` | Check `save_variables.variables.less_waste_tools` — ports are 1-based, 5=unmapped |
| Bypass stuck on | `less_waste_external` = 1 | Set via ZMOD UI or `SAVE_VARIABLE VARIABLE=less_waste_external VALUE=0` |

### Debug Logging

Run with `-vv` (DEBUG) or `-vvv` (TRACE) to see backend-specific logging:

```bash
./build/bin/helix-screen --test -vv
```

All backends log with prefixes:

| Prefix | Backend |
|--------|---------|
| `[AMS Backend]` | Factory/creation |
| `[AMS Happy Hare]` / `[AmsBackendHappyHare]` | Happy Hare |
| `[AMS AFC]` | AFC |
| `[AMS ACE]` | ACE (Anycubic ACE Pro) |
| `[AMS ToolChanger]` | Tool Changer |
| `[AMS AD5X-IFS]` | AD5X IFS |
| `[AmsBackendMock]` | Mock |

### Error Result Codes

See `ams_error.h` for the full `AmsResult` enum. Key results:

| Result | Recoverable | Typical Cause |
|--------|-------------|---------------|
| `FILAMENT_JAM` | Yes | Filament stuck in path |
| `SLOT_BLOCKED` | Yes | Slot obstructed |
| `EXTRUDER_COLD` | Yes | Nozzle below load temp |
| `LOAD_FAILED` | Yes | Load did not complete |
| `UNLOAD_FAILED` | Yes | Unload did not complete |
| `BUSY` | No (wait) | Another operation in progress |
| `NOT_SUPPORTED` | No | Feature not available on this backend |
| `HOMING_FAILED` | Yes | Selector home failed |

`AmsErrorHelper` provides factory methods for creating user-friendly error messages with suggestions for each error type.
