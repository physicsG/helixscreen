# Backend-neutral filament operations

_2026-10-17. Status: approved; phases 1-3 in progress on feat/filament-ops-neutral._

## Why

The AMS panel decides what Load and Unload mean from the backend's kind:

- **On the U1,** Unload opens a head picker.
- **On a toolchanger,** Load mounts the tool and Unload unmounts it.
- **Elsewhere,** Unload empties whichever head is active.

Six surfaces copy that logic: the sidebar, the context menu, the filament panel, the runout dialogs, the sensor tile and the batch modal. Some of them branch on `AmsType` directly. The mock animates filament for a toolchanger whose real backend mounts tools, so the tests never saw the mismatch.

## Rulings (Preston)

1. **Load and Unload mean filament on every printer.** They never mount or unmount a tool. Mounting is the toolhead tap. Park is its own action.
2. **Feeding a head never requires it to be mounted or active.** If a backend needs the tool on the carriage to feed it, it mounts the tool inside the load. The UI never asks whether a head is mounted for a filament op.
3. **The UI is backend-neutral.** It asks capability questions only. A lint fails any `get_type()` or `AmsType::` in `src/ui/`. The allowlist is the identify wizard.
4. **Multi-head printers:** Load/Unload open the head picker everywhere. The picker's DEFAULT selection is the selected/mounted head. This is a preselection only, never a precondition.
5. **Single-head printers:** the sidebar gains a Load button.
6. **Labels:** "Load…" / "Unload…". "Load Tools…" truncates on most panels. The picker's title names what it's choosing.
7. **A toolchanger with no feeder** can never load or unload filament itself. The sidebar's two main cells fall back by capability: Load→**Mount** (the selected tool), Unload→**Park** (the mounted tool). The context menu keeps disabled Load/Unload with the reason "Load and unload filament at the tool by hand". A toolchanger WITH a feeder shows Load…/Unload… plus a separate Park cell.
8. **The picker's source is the Assigned Tool slot.** On a system where several slots can feed one head (multiACE bays, AFC + toolchanger lanes), a picker row reads "Head 2 ← Slot 5". A head with no assigned slot is disabled with "Assign a slot first". Loading any other slot stays a slot tap.
9. **A mount-to-feed restores the carriage.** The backend mounts, feeds or empties, then remounts whatever was mounted before (or parks, if nothing was). This is refused while a print is running or paused.
10. **Terms:** Endless Spool / Filament Mapping / Assigned Tool (memory `feedback_filament_terminology`).

## Backend contract (include/ams_backend.h)

None of these read `current_tool` / `current_slot`:

```cpp
enum class FilamentOpSupport : uint8_t { Automatic, Manual, Unavailable };
struct HeadFilamentOps { FilamentOpSupport load, unload; std::string load_reason, unload_reason; };
virtual int filament_head_count() const;                        // default 1
virtual std::optional<int> head_for_slot(int slot) const;       // which head a source feeds
virtual HeadFilamentOps head_filament_ops(int head) const;
virtual std::optional<bool> head_has_filament(int head) const;  // filament, never "mounted"
virtual int max_heads_per_filament_op() const;                  // replaces supports_batch_filament_ops
bool supports_reset() const;                                     // trait; replaces ams_is_tool_changer on Reset
```

- `load_filament` / `unload_filament` keep their signatures. Their contract tightens to "filament only".
- `needs_mount_to_feed(head)` is a protected backend detail. The mount and the feed are reported as one LOADING action, with step names coming from `get_operation_step_model()`.
- Mount stays `ToolState::request_tool_change` (the tap and the Mount cell). Park stays `park_active_tool()`.

## UI decision

`include/filament_action_set.h` holds a pure `decide_filament_actions(const FilamentHeadView&)` plus `decide_slot_actions(view, head)`.

- **Inputs:** the head count, max heads per op, each head's ops and filament state, and the busy/print gating (precedence unchanged). There is no active-head input except the picker's default selection.
- **Outputs:** visible/enabled/reason for each of: load, unload, mount, park.

Every surface maps those outputs to its own widgets.

## Per backend

| Backend | Heads | Load / Unload | Notes |
|---|---|---|---|
| HH, AFC 1-ext, ACE, CFS, QIDI, AD5X, OpenAMS | 1 | Automatic | Unchanged |
| U1, multiACE | 4 | Automatic, parked heads included; batch of 4 | multiACE bays map to their routed head; an unrouted bay is disabled with a reason |
| Creator 5 (feed shim) | N | load Automatic, unload Manual | The Select-Slot-as-mount override is deleted |
| klipper-toolchanger + feeder | N | Automatic | Mounts inside the op if needed |
| klipper-toolchanger, no feeder (MedusaHC today) | N | Manual / Unavailable | The sidebar shows Mount / Park |
| AFC + toolchanger | N | Automatic | Load is `CHANGE_TOOL LANE=<lane>` and AFC swaps heads itself (#1824); hardware verification required |

## Phases (one review, scoped mutation and one gate across phases 2-4)

1. **Interfaces only, no behaviour change.** The queries, with defaults matching today, plus `supports_reset`. `decide_filament_actions` and its tests. The `get_type()` line in `read_backend_caps` and the Reset gate both go. The lint lands.
2. **Sidebar and picker unification.** Delete the batch branches and the active-head fallbacks. The picker becomes generic (multi-select up to max). The runout head comes from the event. Single-head systems get a sidebar Load. Do `ctl geom` at 800x480 and the smallest breakpoint.
3. **Toolchanger semantics, the user-visible change.** Per-head answers for toolchangers. `load_mounts_tool`, the Mount/Unmount relabel and `AlreadyMounted` are deleted. The Mount/Park fallback cells and the Park cell land. Mock parity. User and developer docs update in the same change.
4. **The hardware-verified pieces:** the AFC+toolchanger load verb and the MedusaHC feed provider.

## Behaviour changes users will notice

- Toolchanger Unload no longer unmounts (Park does).
- A toolchanger runout "Load" feeds the head or explains why it can't. It no longer mounts.
- AFC multi-extruder Unload opens the picker, preselected to the mounted head.
- Single-head printers get a sidebar Load.
- The Creator 5's Select Slot stops mounting.
- Translation strings change.

## Calls made in the design (not Preston's), for him to check

- **Manual vs Unavailable on screen.** Both show the button disabled with a reason. Manual's reason says how to do it by hand; Unavailable's says why it can't be done. No guidance flow for now. The enum stays, for the wording.
  - No-feeder toolchanger: load and unload are both Manual ("by hand").
  - MedusaHC: Manual until its feed command is known.
- **Mount and Park targets.** These are the only actions that read head state. `FilamentHeadView` carries `selected_head`, which is the panel's selected slot mapped through `head_for_slot`, and `mounted_head`.
  - Mount targets the selected head. It is disabled with "Already mounted" when the selected head is already mounted, and "Select a tool" when nothing is selected.
  - Park targets the mounted head, and is hidden when none is mounted.
  - Filament actions never read either field.
- **Phase 1 lint scope.** The lint covers the filament-op code only: the sidebar, the context menu, the filament panel, the runout handlers, filament_op_dispatch/execute and the batch modal. buffer_status_modal, ui_ams_detail, ui_wizard_summary and ui_panel_ams stay out of scope for now, with a tracking note in the lint's comment.
- **MedusaHC feed.** Phase 4 waits on the command and a hardware tester. A question goes to MedusaHC users; until there's an answer, MedusaHC is a no-feeder toolchanger (Mount/Park cells).

## Left to the builder

The widget layout of the Mount/Park cells, the reason strings (each needs translation) and the picker title wording, all within the rulings above.

## Implementation plan

### Phase 1: interfaces, the decision, the lint (no behaviour change)

`include/ams_backend.h`:

```cpp
enum class FilamentOpSupport : uint8_t { Automatic, Manual, Unavailable };
struct HeadFilamentOps {
    FilamentOpSupport load = FilamentOpSupport::Automatic;
    FilamentOpSupport unload = FilamentOpSupport::Automatic;
    std::string load_reason;   // translated; empty when Automatic
    std::string unload_reason; // translated; empty when Automatic
};
[[nodiscard]] virtual int filament_head_count() const;                  // 1
[[nodiscard]] virtual std::optional<int> head_for_slot(int slot) const; // 0 for a valid slot on one head
[[nodiscard]] virtual HeadFilamentOps head_filament_ops(int head) const;   // Automatic, or Manual from unload_is_manual()
[[nodiscard]] virtual std::optional<bool> head_has_filament(int head) const; // is_filament_loaded() on one head
[[nodiscard]] virtual int max_heads_per_filament_op() const;           // 1
[[nodiscard]] virtual std::optional<int> head_source_slot(int head, const AmsSystemInfo& info) const; // the Assigned Tool slot: mapped_tool == head and head_for_slot(slot) == head
[[nodiscard]] bool supports_reset() const;                              // BackendTraits::supports_reset, default true
```

- `BackendTraits::supports_batch_filament_ops` is deleted; `supports_batch_filament_ops()` stays one phase as `max_heads_per_filament_op() > 1`. Snapmaker (and multiACE through it) answer 4; the mock answers what its persona's backend answers.
- `BackendTraits::supports_reset`: false on the tool changer and the Snapmaker, which is exactly where `ams_is_tool_changer` hid Reset.
- Snapmaker: `filament_head_count()` = 4, `head_for_slot(s)` = s; multiACE: a bay maps through `head_for_global_locked()`. Tool changer: one head per tool. Mock: per persona.

`include/filament_action_set.h` (new, pure, `helix::ui`):

```cpp
struct HeadState {
    HeadFilamentOps ops;
    std::optional<int> source_slot;     // head_source_slot(head)
    std::optional<bool> source_present; // slot_presence() of the source
    std::optional<bool> at_nozzle;      // filament already fed: nothing to load
    std::optional<bool> has_filament;   // head_has_filament(head): something to unload
};
struct FilamentHeadView {
    bool backend_present = false;
    int max_heads_per_op = 1;
    std::vector<HeadState> heads;
    bool heads_dock = false;     // carriage_can_be_empty(): heads mount and park
    bool supports_park = false;  // supports_tool_park()
    int selected_head = -1;      // the picker's preselection only
    int mounted_head = -1;
    bool system_busy = false;
    bool print_blocks_op = false;
};
struct ActionState { bool visible = false; bool enabled = false; std::string reason; };
struct FilamentActionSet {
    ActionState load, unload, mount, park;
    bool unload_opens_picker = false; // more than one head
};
struct HeadPickRow { int head; int slot; bool enabled; std::string reason; bool preselected; };

[[nodiscard]] FilamentActionSet decide_filament_actions(const FilamentHeadView& view);
[[nodiscard]] FilamentActionSet decide_slot_actions(const FilamentHeadView& view, int head);
[[nodiscard]] std::vector<HeadPickRow> decide_head_picker_rows(const FilamentHeadView& view, bool for_load);
```

Rules: a sidebar with no head whose load or unload is Automatic, on a machine whose heads dock, falls back to Mount/Park. Otherwise Load is visible, enabled when some head can load (Automatic, an assigned slot, a source not known empty, filament not already at the nozzle); Unload likewise for some Automatic head holding filament. A direction no head can do automatically is disabled with the first head's reason. Park is visible where the heads dock and the backend parks, enabled with a head mounted. Busy and print gating disable every action, with no reason (the stepper and the print say why). Mount and Park are the only outputs that read `selected_head` / `mounted_head`.

`include/filament_op_execute.h`: `FilamentHeadView read_filament_head_view(AmsBackend*, PrinterState&)` fills the view off a live backend. `read_backend_caps()` reads `load_mounts_tool()` instead of `get_type()`; `BackendCaps::is_tool_changer` becomes `load_mounts_tool`.

Sidebar Reset binds `ams_sidebar_supports_reset` (from `supports_reset()`), not `ams_is_tool_changer`.

Lint: `scripts/check_filament_ops_neutral.py` fails on `get_type()` or `AmsType::` in the filament-op files (sidebar, context menu, filament panel, runout handler, the sensor and print-status tiles' dispatch, filament_op_dispatch / execute, batch modal, filament_action_set). `tests/shell/test_code_lint.bats` runs it and a meta-test proves it fires on each shape.

Tests (`tests/unit/test_filament_action_set.cpp`, `[filament][actions]`):
- `filament actions: one automatic head shows Load and Unload`
- `filament actions: Load needs an assigned slot`
- `filament actions: Unload needs filament in some head`
- `filament actions: busy and print gating disable everything without a reason`
- `filament actions: a docking machine with no automatic op falls back to Mount and Park`
- `filament actions: Park is hidden with nothing mounted`
- `filament actions: a Manual direction is disabled with its reason`
- `filament actions: multi-head unload opens the picker`
- `slot actions: Mount targets the tapped head and refuses the mounted one`
- `head picker: rows follow the source slot, preselect the selected or mounted head`
- `head picker: a head with no assigned slot is disabled with a reason`
- backend defaults, `[ams][filament_ops]`: `backend filament ops: defaults answer one automatic head`, `backend filament ops: Snapmaker answers four heads, batch of four`.

### Phase 2: sidebar and picker

- `BatchFilamentModal` becomes the head picker: rows from `decide_head_picker_rows()`, "Tool 2 · Slot 5" labels (the text fonts carry no arrow glyph), disabled rows carry their reason, selection capped at `max_heads_per_op` (one: picking a row clears the others), and it hands the chosen slots to an `on_confirm` callback instead of dispatching. Title names the choice ("Load into which tool?" / "Unload which tool?" / "Mount which tool?"). `UiMultiselect` gains `MultiSelectItem::enabled`.
- The sidebar: `btn_load` ("Load…", every printer with a filament system) and `btn_unload` ("Unload" on one head, "Unload…" on several). Both read `decide_filament_actions()`. One chosen head runs the per-slot path (`handle_load_with_preheat` / `handle_unload(slot)`); several run `load_filament_batch` / `unload_filament_batch`. The batch-only buttons, `ams_sidebar_supports_batch` and the `supports_batch_filament_ops()` branches go. A reason line under the grid shows a disabled action's reason.
- The runout dialog and the sensor tile take their head from the event: `FilamentSensorManager::real_runout_lane()`, mapped through `head_for_slot()` and back to `head_source_slot()`, the current slot only when no lane is named.
- `supports_batch_filament_ops()` is deleted.
- Tests: `[ams][batch]` picker cases rewritten over `decide_head_picker_rows`; `[ams][sidebar]` a single-head printer shows Load; `[runout]` the runout lane names the head.

### Phase 3: tool changer semantics

- `AmsBackendToolChanger`: `head_filament_ops()` answers Automatic load from the feed command where one exists, Manual otherwise, with the "by hand" reasons; unload is Manual (no feed reverses). `do_load_filament()` refuses rather than mounts without a feed; `do_unload_filament()` refuses rather than unmounts. `load_mounts_tool`, `kTraitsWithFeed`'s `supports_gate_select` and the `select_gate()` override go; `head_has_filament()` is nullopt (nothing senses filament).
- `FilamentRefusal::AlreadyMounted` and `BackendCaps::load_mounts_tool` go; `plan_load` loses its tool-changer arm. The context menu's Mount/Unmount relabel goes; it shows Load/Unload with reasons and, where neither is automatic, Mount (the tapped tool) or Park (the mounted one).
- Sidebar Mount and Park cells: Mount opens the picker in mount mode and calls `offer_tool_change()`; Park calls `park_active_tool()` behind the same refusals the tool switcher applies.
- Mock: the TOOL_CHANGER persona answers `head_filament_ops()` from the same `AmsBackendToolChanger` static the real backend uses, and refuses load/unload the way it does.
- Docs: `docs/devel/FILAMENT_MANAGEMENT.md` (the contract), `docs/user/guide/filament.md` (what the buttons mean), translations in all 8 locales.
- Tests: `[toolchanger]` load/unload refuse without a feed and never send SELECT_TOOL / UNSELECT_TOOL; `[ams][mock]` parity; `[filament][actions]` C5 shape (feed load, manual unload, Park cell).

### Scope change (approved): MedusaHC loads through the carriage

MedusaHC is no longer a no-feeder changer. `toolchanger_addon::feeds_through_mount()` marks
it, its heads answer Automatic with `needs_mount_to_feed()` true, and
`include/mount_feed_sequence.h` runs one LOADING/UNLOADING operation: mount, heat, an
"Insert filament into Tool N" prompt (Klipper action prompt; the extruder grabs the strand),
slow extrude and purge (or the printer's `LOAD_FILAMENT T=` macro), restore the carriage;
unload retracts fully (or `UNLOAD_FILAMENT T=`) and restores, with no `OPEN`. Refused while
printing or paused. Feeder buttons send plain `OPEN` / `CLOSE`. Tests:
`tests/unit/test_toolchanger_mount_feed.cpp` (`[mount_feed]`).
