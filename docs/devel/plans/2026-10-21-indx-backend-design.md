# Bondtech INDX backend: design

Issue: prestonbrown/helixscreen#1333. Branch: `feature/indx-backend`.
Status: design. Preston ruled on D1-D7 (section 13); the body follows those rulings.

## 1. What INDX is

Sources: Bondtech's wiki (`BondtechAB/INDX` README, read 2026-10-21 at `ae64179b1`), its
macros (`macros/indx.cfg`, `macros/indx-tc-macros.cfg`, `macros/indx-cal.cfg`,
`macros/homing.cfg`), the mainline host module (`BondtechAB/indx_klipper`, `host/`), and the
#1333 bundle analysis.

- **One Smart Head** on the gantry: induction coil, contactless IR temperature sensor, the
  DX extruder (one stepper, TMC2240), a load cell, an accelerometer. All electronics live
  here, on its own MCU (`indxmcu`).
- **Passive tools** (2 to "ten", the reporter runs 6) parked in a dock. Each is a nozzle and
  a filament path, nothing else: no heater, thermistor or wiring. The head picks one up,
  heats it by induction, and parks it to cool passively.
- **Filament path: confirmed per tool.** The README states it outright: "each tool carries
  its own filament directly. There is no shared filament path and no purge block required."
  Each tool has its own Bowden from its own spool, filament can be loaded with the tool
  docked or mounted, and no purge is needed between tools. How the head's DX gears engage a
  docked tool's strand on pickup is not documented; nothing below depends on it.
- **Klipper sees one extruder.** The README: "INDX passive tools are not separate Klipper
  extruders - there is only ever one `[extruder]`". That extruder has
  `heater_pin: indx:heater`, `sensor_type: indx`, and watermark control. `[indx]` is the
  toolboard host module (Kalico built-in, or `indx_klipper` on mainline).
- **Tool state is macro state.** No `toolchanger` or `tool <name>` objects exist. The tool on
  the head, the latch, offsets and counters live in `save_variables` and `gcode_macro`
  variables written by Bondtech's macros.
- **Heating is guarded in the macros.** INDX overrides `M104`/`M109`: with no tool on the head,
  or a tool listed in `no_heat_tools`, they do not heat ("INDX heats each tool at pickup").
  `CHANGE_TOOL` turns the coil off before docking and `M109`s the incoming tool after pickup.

Presented in HelixScreen as a **tool changer** (ruling): `AmsType::INDX`, tool cards, a change
is a swap, each tool owns its filament and spool.

## 2. The macro contract

Bondtech's upstream macros are the contract (ruling). Everything HelixScreen reads or sends is
listed here with its source file. Nothing outside this table is read or sent.

### Read

| What | Where | Upstream file | Meaning |
|---|---|---|---|
| Tool on the head | `save_variables.variables.active_tool` | `indx-tc-macros.cfg` (`_INDX_SET_ACTIVE_TOOL`) | `0..N-1`, `-1` empty head. Absent until `INIT_STATE` runs 5 s after boot: no news, not "empty" |
| Half-seated fault | `save_variables.variables.seat_suspect` | `indx-tc-macros.cfg` (`_PICKUP_SEAT_FAIL`), cleared by `indx-cal.cfg` (`MANUAL_TOOL_*`) | `>= 0` names a tool that may be half on the head; `CHANGE_TOOL`, `PARK_TOOL` and `G28 Z` refuse until cleared. Survives restarts |
| Toolchange count | `save_variables.variables.toolchange_count` | `indx-tc-macros.cfg` (`_RECORD_TOOLCHANGE`) | Diagnostic only |
| Latch | `gcode_macro _TOOLCHANGE_VARS.toolhead_state` | `indx-tc-macros.cfg` | `0` locked, `2` open. Diagnostic only |
| Speed mode | `gcode_macro _TOOLCHANGE_VARS.mode` | `indx-tc-macros.cfg` (`_SET_MODE`) | `normal` / `stealth` / `sport`. Phase 3 only |
| Tool count | `gcode_macro TOOL_POSITIONS.tool_count` | `indx.cfg` | Slot count |
| Tools built | `gcode_macro TOOL_POSITIONS.tools` | `indx.cfg`, filled by `_INDX_BUILD_TOOLS` | Its length; `[]` until `INIT_STATE` |
| Tools without a hotend | `gcode_macro TOOL_POSITIONS.no_heat_tools` | `indx.cfg` | Pen, cutter, camera: never heated, no filament ops |
| Docks measured | `gcode_macro TOOL_POSITIONS.docks_calibrated` | `indx.cfg` | `False` makes `CHANGE_TOOL`/`PARK_TOOL` refuse |
| Load presets | `gcode_macro LOAD_FILAMENT.filament_presets` | `indx-cal.cfg` | `{TYPE: {temp, density, hc[, speed]}}` |
| Extruder temperature | `extruder.temperature` / `.target` | Klipper | Already subscribed app-wide; it is the mounted tool's nozzle |

Discovery also reads, from `configfile.config`: the `[indx]` section (detection) and
`gcode_macro TOOL_POSITIONS` `variable_tool_count` (to size slots before the first status
frame). Klipper keeps section-name case in `configfile.config`, so the lookup matches
`gcode_macro tool_positions` case-insensitively.

### Send

| Command | Upstream file | Used for |
|---|---|---|
| `T<n>` when `gcode_macro T<n>` exists, else `CHANGE_TOOL TOOL=<n>` | `indx.cfg`, `indx-tc-macros.cfg` | Change tool. `T<n>` is what a slicer sends, so a user's wrapper (the reporter's sets the Spoolman spool) runs for screen changes too. Upstream `T<n>` is exactly `CHANGE_TOOL TOOL=<n>`. Both home first when needed and re-heat the incoming tool to the outgoing target |
| `PARK_TOOL` | `indx-tc-macros.cfg` | Park, leaving the head empty |
| `LOAD_FILAMENT TOOL=<n> TYPE=<preset>` or `... TEMP=<c>` | `indx-cal.cfg` | Load: picks the tool up, sets the heater model's filament, heats, feeds and primes (`INDX_LOAD_FILAMENT`). Leaves the tool mounted and hot |

Later phases add `TOOL_STATUS`, `MODE_STEALTH`/`MODE_NORMAL`/`MODE_SPORT`, `TC_CYCLE_ALL`,
`MANUAL_TOOL_REMOVE`, `MANUAL_TOOL_SEAT TOOL=<n>`, `SET_TOOL_FILAMENT`, `INDX_CALIBRATE`,
`INDX_FAN_CALIBRATE` (section 6).

### Recognized, or not

`indx::check_contract()` (section 4) requires, in `printer.objects.list`: `save_variables`,
`gcode_macro TOOL_POSITIONS`, `gcode_macro _TOOLCHANGE_VARS`, `gcode_macro CHANGE_TOOL`,
`gcode_macro PARK_TOOL`. Missing any one: the backend still registers (detection was the
`[indx]` module, section 3) but enters **macros not recognized**: a unit with no slots, a
`current_error()` naming the missing objects, every op refused with that text, nothing sent.
It never guesses. `LOAD_FILAMENT` with `filament_presets` is optional: without it Load is
Manual.

**Not in the contract, deliberately.** The #1333 bundle analysis lists `INDX_LOCK`,
`INDX_UNLOCK`, `INDX_FORCE_STATE`, `VERIFY_TOOL_PRESENT` and `gcode_macro T<n>.spool_id`. None
of them exists in `BondtechAB/INDX` at HEAD, and `git log -S` over `macros/` finds none of
them at any point in its history, so they are the reporter's own (or a pre-public) macro set.
The reporter's config does share every name in the contract above, so one contract serves
both. `T<n>.active` (Mainsail/Fluidd tool buttons) arrived upstream on 2026-10-09 and older
installs lack it, so it is not read: `active_tool` is the one source. Upstream is moving fast
(commits this week), which is the reason the contract is as small as it is.

## 3. Detection

INDX registers no object `PrinterDiscovery` keys on today. Two signals, either one enough:

- `indx` in `printer.objects.list`. The mainline module's `IndxToolboard` has a `get_status`
  (`last_dock_measurement`), so it lists.
- An `indx` key in `configfile.config`. The reporter's Kalico build showed no `indx` object
  (bundle `NYWQDT7V`), which this catches. Not confirmed whether Kalico's module lacks
  `get_status` or the bundle missed it; the design does not need to know.

Changes, all in `src/printer/printer_discovery_parse.cpp`:

- `PrinterDiscovery#parse_objects` sets `has_indx_` on `indx`.
- `PrinterDiscovery#parse_config_keys` sets `has_indx_` on the `indx` key and records
  `variable_tool_count` from the `TOOL_POSITIONS` section. When it flips `has_indx_` it calls
  `register_detected_ams_systems()` again: that runs at the end of `parse_objects`, which is
  before the configfile query (step 4 of `MoonrakerDiscoverySequence`), and both still run
  before the subscription and before backends are built.
- `PrinterDiscovery#register_detected_ams_systems` gains an arm after `has_snapmaker_` and
  before the tool-changer arm: `has_indx_` registers `{AmsType::INDX, "INDX"}`. A real MMU
  still wins, as everywhere else. No conflict with the tool-changer arm: INDX has one
  extruder and no `tool` objects, so that arm never fires on it.
- Accessors `has_indx()` and `indx_tool_count()` follow the `has_medusahc()` precedent
  (detection flags are the one place discovery names hardware). `detect_printer_cmd` and the
  mock's `parse_config_keys` simulation pick this up unchanged.

Macros alone (`TOOL_POSITIONS`, `CHANGE_TOOL`) never detect INDX: without the `[indx]` module
they are someone else's macros.

## 4. Shape of the code (reuse first)

What exists and is extended rather than forked:

| Existing | Reused for |
|---|---|
| `AmsBackendToolChanger` (`include/ams_backend_toolchanger.h`) | Base class. Slots per tool, PARALLEL topology, the `FilamentSlotOverrideStore` with `T<n>` lane_data keys, Spoolman fields, optimistic SELECTING + ack + status confirmation (`finalize_dispatch_after_macro`), `RemapStrategy::GcodeRewrite` when the machine has its own `T<n>`, park support, tool-card UI |
| `toolchanger_addon::ToolReading` / `ToolCommands` (`include/toolchanger_addon.h`) | The tool reading and the swap commands. The base already lets a reading override the tool number (`apply_tool_sensor_locked`) and lets `ToolCommands` replace `SELECT_TOOL`/`UNSELECT_TOOL` |
| `AmsSubscriptionBackend` | Subscription, status delivery, dispatch |
| `ToolState` + `ToolTopology` (`include/tool_state.h`) | Tool list and active tool from the backend's topology, with one new field (section 7) |
| `MacroParamCache` (`include/macro_param_cache.h`) | Template scan for `SET_ACTIVE_SPOOL`, same pattern as `stops_printer` |
| `FilamentConsumptionTracker` fallback | Per-tool usage through `get_current_slot()` |
| `z_offset_persistence` shape (`include/z_offset_persistence.h`) | Pure-function module layout for the contract |

Not a `toolchanger_addon` provider row: the ruling gives INDX its own `AmsType` and backend,
and the INDX-specific answers (contract state, heat gating, load command, manual unload) do not
fit the row's fields. The subclass gets the row's machinery anyway, because it feeds the same
`ToolReading`/`ToolCommands` types into the base.

Not the Snapmaker backend: its state machine (`AUTO_FEEDING`, `print_task_config`, extruder
pins) is U1-specific and shares nothing with macro state.

### New files (the one vendor module)

- `include/indx_contract.h`, `src/printer/indx_contract.cpp`: pure functions, no I/O, the only
  place that knows INDX's object names, variable names and commands.
- `include/ams_backend_indx.h`, `src/printer/ams_backend_indx.cpp`: the backend.
- `docs/devel/FILAMENT_BACKEND_INDX.md`: the leaf doc, written in phase 1 from this plan
  (this plan is deleted when phase 1 lands).

`include/indx_contract.h` (passed `scripts/syntax_check.py`):

```cpp
namespace helix::indx {

struct FilamentPreset {
    float temp_c = 0.0f;
    float density = 0.0f;
    float heat_capacity = 0.0f;
};

/// What one status frame said. nullopt = the frame did not carry it (no news).
struct Reading {
    std::optional<int> active_tool;    ///< -1 = empty head
    std::optional<int> seat_suspect;   ///< -1 = clear
    std::optional<int> toolhead_state; ///< 0 locked, 2 open
    std::optional<int> toolchange_count;
    std::optional<int> tool_count;
    std::optional<int> tools_built;
    std::optional<std::set<int>> no_heat;
    std::optional<bool> docks_calibrated;
    std::optional<std::string> speed_mode;
    std::optional<std::map<std::string, FilamentPreset>> presets; ///< keyed upper-case
    std::map<int, int> tool_spool_ids; ///< T<n>.spool_id where declared (decision D4)
};

[[nodiscard]] std::optional<Reading> read_status(const nlohmann::json& status);
[[nodiscard]] bool present(const PrinterDiscovery& hw);

struct Contract {
    bool recognized = false;
    std::vector<std::string> missing;
    bool has_load_filament = false;
    bool macros_set_active_spool = false;
};

[[nodiscard]] Contract check_contract(const PrinterDiscovery& hw);
[[nodiscard]] nlohmann::json required_status_objects(const PrinterDiscovery& hw);
[[nodiscard]] std::string change_tool_gcode(const PrinterDiscovery& hw, int tool);
[[nodiscard]] std::string load_gcode(int tool, const std::string& material, int temp_c,
                                     const std::map<std::string, FilamentPreset>& presets);
[[nodiscard]] std::string heat_blocked_reason(int active_tool, const std::set<int>& no_heat);

} // namespace helix::indx
```

`include/ams_backend_indx.h` (passed `scripts/syntax_check.py` except for `AmsType::INDX`,
which phase 1 adds):

```cpp
class AmsBackendIndx : public AmsBackendToolChanger {
  public:
    AmsBackendIndx(IMoonrakerAPI* api, helix::IMoonrakerClient* client);
    void set_discovery(const helix::PrinterDiscovery& discovery) override;
    [[nodiscard]] AmsType get_type() const override { return AmsType::INDX; }
    [[nodiscard]] static nlohmann::json required_status_objects(const helix::PrinterDiscovery& hw);
    [[nodiscard]] bool manages_active_spool() const override;
    [[nodiscard]] std::optional<int> slot_for_extruder(int) const override { return std::nullopt; }
    [[nodiscard]] HeadFilamentOps head_filament_ops(int head) const override;
    [[nodiscard]] bool unload_is_manual() const override { return true; }
    [[nodiscard]] std::optional<helix::ErrorEvent> current_error() const override;
  protected:
    void handle_status(const nlohmann::json& status) override;
    AmsError do_change_tool(int tool_number) override;
    AmsError do_park_active_tool() override;
    AmsError do_load_filament(int slot_index) override;
  private:
    helix::indx::Contract contract_;
    helix::indx::Reading state_;
};
```

Base-class seams the subclass needs (phase 1, in `AmsBackendToolChanger`):

- `apply_tool_sensor_locked()` moves from private to protected, so `handle_status` can feed it
  a `ToolReading` built from `active_tool` (`current_tool`) and `tool_count`. The base's own
  `handle_status` finds no `toolchanger` object on INDX and does nothing harmful; the
  subclass calls it first, then applies its reading, so the reading wins within a frame.
- `set_discovered_tools()` is called from `AmsBackendIndx::set_discovery` with `T0..T<n-1>`
  from `indx_tool_count()`, and again from `handle_status` if `TOOL_POSITIONS.tool_count`
  disagrees (the status frame wins; the config value only avoids a zero-slot first paint).
- `set_tool_commands({present=true, provider_name="INDX", select_prefix=..., unselect="PARK_TOOL"})`.
  `select_prefix` cannot express "`T<n>` when it exists, else `CHANGE_TOOL TOOL=<n>`", so
  `do_change_tool` is overridden to send `indx::change_tool_gcode()` and reuse the base's
  dispatch and ack path for everything after the send.

## 5. Every `AmsBackend` virtual

Grouped. "Base" means `AmsBackendToolChanger`'s answer stands; "Default" means
`AmsBackend`'s.

**Lifecycle, events.** `start`, `stop`, `is_running`, `release_subscriptions`,
`before_disconnect`, `request_resync`, `set_event_callback`, `set_gcode_response_callback`:
base. `additional_start_checks` (base) refuses with no tools; on an unrecognized contract the
subclass starts with zero tools and skips that check so the error state can render.

**Identity and traits.** `get_type`: `INDX`. `traits()`: base `kTraits` with
`supports_reset = false`, `supports_auto_heat_on_load = true` (LOAD_FILAMENT heats),
`load_feeds_filament = true`, `filament_ops_self_home = true` (CHANGE_TOOL homes).
`get_klipper_object_name`: `"indx"`. `toolhead_style_hint`: default. `lane_noun`/`tool_noun`:
base (Tool). `manages_active_spool`: section 9. `printer_retains_spool_info`: default false.

**State.** `get_system_info`, `get_slot_info`, `get_current_tool`, `get_current_slot`,
`is_filament_loaded`: base, fed by the reading. `get_current_action`: base (IDLE, SELECTING
while a change is in flight, ERROR on a fault). `carriage_can_be_empty`: base true.
`slot_for_extruder`: **nullopt** (the base's identity would charge every millimetre to tool 0;
section 7). `firmware_default_routing`: default lane-per-tool. `tag_reading`: default.

**Errors and narration.** `current_error`: contract not recognized; `seat_suspect >= 0`
("T<n> may be half seated on the head. Check it by hand."); docks not calibrated (only when a
change is attempted, as a refusal, not a standing error). `classify_error`,
`duplicates_firmware_prompt`, `build_recovery_actions`: default in phase 1 (INDX raises with
`action_raise_error`, which reaches the console and the generic error path).
`toolchange_phase_template`, narration matchers, `get_operation_step_model`: default/base,
no step bar (INDX publishes no phase).

**Topology and path.** `get_topology` (PARALLEL), `get_unit_topology`, segments,
`infer_error_segment`, `slot_has_filament_at_toolhead`, `slot_is_actively_loaded`,
`slot_filament_parked_in_toolhead`: base. Sensors (`slot_has_prep_sensor`,
`slot_has_load_sensor`, insert identity/presence, `upstream_slot`, `get_bowden_progress`):
default (none). `spool_display_number`: default.

**Filament ops.** `head_filament_ops(h)`: Load Automatic when the contract has
`LOAD_FILAMENT` and `h` is not in `no_heat_tools`, else Unavailable ("T<n> has no hotend") or
Manual ("This printer has no INDX LOAD_FILAMENT macro"); Unload Manual (decision D2).
`unload_is_manual`: true. `do_load_filament(s)`: `indx::load_gcode(s, material, temp)` with the
slot's material, the material table's temperature or 220, refused while printing (base
gate). `do_unload_filament`: base refusal through `unload_is_manual`. `filament_head_count`,
`head_for_slot`, `head_source_slot`, `head_has_filament`: base. `max_heads_per_filament_op`:
1 (each LOAD_FILAMENT leaves its tool mounted and hot; a batch is phase 3).
`load_filament_batch`/`unload_filament_batch`/`filament_batch_in_flight`: default.
`needs_mount_to_feed`: false (the macro mounts). `requires_slot_selection_for_load`,
`needs_unload_before_load`, `slot_op_eligibility`, `can_unload_from_toolhead`,
`slot_unloads_to_toolhead`, `unload_blocked_reason`, `eject_unavailable_reason`,
`unload_lane_candidates`, `toolhead_filament_unaccounted`, `unload_heats_hotend`,
`auto_unloads_after_print`, `delegates_homing_to_printer`: base or default.

**Tool ops.** `change_tool`/`select_slot` -> `do_change_tool`: `indx::change_tool_gcode()`.
Refused with a reason when: contract not recognized, `seat_suspect >= 0`,
`docks_calibrated == false`, tool out of range, already mounted (no-op success).
`park_active_tool` -> `PARK_TOOL`, refused on the same gates; `supports_tool_park`: true.
`set_feeder_open`/`supports_feeder_control`/`feeder_open`: base (no feeder).

**Recovery.** `recover`: NOT_SUPPORTED. `reset`: NOT_SUPPORTED (`supports_reset = false`).
`cancel`: base (clears an optimistic action; a running macro cannot be stopped from here).
`clear_fault`, `recover_with_state`, lane recovery, `eject_lane`, `preload_lane`,
`select_gate`, `move_selector`, `check_gate`, `check_all_gates`, `reset_button_label`,
`prepare_for_resume`, `is_stuck_motion_sensor_runout`, `clear_message_queue`: default.

**Identity and spools.** `apply_user_edit`, `sync_external_identity`, weights, own-write echoes,
`repaint_slot_from_lane`, `clear_slot_override`, `firmware_stores_color_and_material`
(false), `get_supported_materials`, `normalize_material`: base. Per-tool spool assignment:
`supports_per_tool_spool_assignment` default (`is_tool_changer`) is true.

**Mapping.** `can_set_tool_mapping`, `set_tool_mapping_impl`, `get_tool_mapping`,
`reset_tool_mappings`, `owns_tool_mapping_table` (true), `get_remap_strategy`
(GcodeRewrite, from `tool_commands_.present`), `remap_ready`, `tool_mapping_origin`,
`last_print_tool_mapping`, routing questions, `reports_firmware_tool_mapping`,
`build_preprint_gcode`, `check_preprint_remap`, `note_print_start_from_here`: base or default.

**Endless spool, dryer, bypass, chute, environment.** `get_endless_spool_capabilities`:
base (Unsupported). Dryer, environment zones, chute calibration,
`publish_external_spool_lane`: default (none). Bypass: base (none).

**Device actions.** `get_device_sections`/`get_device_actions`/`execute_device_action`: none
in phase 1; section 6.

## 6. Actions in scope

| Action | Phase | Command | Notes |
|---|---|---|---|
| Change tool (tool card tap, tool switcher) | 1 | `T<n>` / `CHANGE_TOOL TOOL=<n>` | Blocks until the swap and its `M109` finish; the ack plus the next `active_tool` frame complete it |
| Park | 1 | `PARK_TOOL` | Coil off before docking (the macro does it) |
| Assign spool to tool | 1 | none (HelixScreen store, `T<n>` lane_data) | Plus D4 for the reporter's `spool_id` |
| Load | 1 | `LOAD_FILAMENT TOOL=<n> TYPE=...` | Material to preset match is case-insensitive through `normalize_material`; unmatched sends `TEMP=` |
| Unload | 1 | none | Manual with a reason (D2) |
| Speed mode | 3 | `MODE_STEALTH` / `MODE_NORMAL` / `MODE_SPORT` | A device-action choice reading `_TOOLCHANGE_VARS.mode` |
| Cycle every tool | 3 | `TC_CYCLE_ALL` | Confirm modal; refused while printing |
| Seat-fault recovery | 3 | `MANUAL_TOOL_REMOVE`, `MANUAL_TOOL_SEAT TOOL=<n>` | Hands-on steps; D5 |
| Heater calibration | 3 | `INDX_CALIBRATE`, `INDX_FAN_CALIBRATE` | Long-running, tool mounted, `SAVE_CONFIG` after |
| Filament model on edit | 3 | `SET_TOOL_FILAMENT TOOL=<n> DENSITY= HEAT_CAPACITY=` | D3 |
| Tool offsets display | 3 | none (read `t<n>_offset_x/y/z`, `global_z_offset`) | A reader in `helix::tool_offsets`, read-only |
| Dock, XY/Z offset, load-cell calibration | out | `CAL_*`, `CALIBRATE_DOCK_X`, `CALIBRATE_LOAD_CELL*` | Hand steps, a camera, physical tool seating. Mainsail console territory |

## 7. One heater: consequences across the app

The rule (ruling): one nozzle temperature, following the mounted tool. Every place the app
assumes a tool changer has per-tool extruders or heaters, and what INDX does there:

1. **`ToolState` heads** (`src/printer/tool_state.cpp#physical_heads`). Heads group by
   `extruder_name`. If every INDX tool names `extruder`, the six tools collapse into one head
   (the hub model) and the tool switcher shows one head. If none names it, `heated_heads()`
   is empty and every heater surface goes blank. Neither is right.
   **Change:** `ToolTopology` gains `bool heads_share_heater = false`, copied from a new
   `AmsBackend::heads_share_heater()` (default false, INDX true) in
   `src/printer/ams_state_tool_mapping.cpp#build_ams_topology`. With it set, every tool
   names the one extruder and `physical_heads(tools, heads_share_heater)` gives one head per
   tool, while `ToolState::heated_heads()` returns one entry per distinct extruder, the
   head holding the active tool standing for it (else the lowest). Result: tool switcher and
   `printer_manager_chips` see 6 heads; `extruder_count()` is 1;
   `has_multiple_extruders()` false; `has_multiple_toolheads()` true. Generic name, no vendor.
2. **Active tool.** With an AMS topology active, `ToolState::update_from_status` already ignores
   `toolhead.extruder` and takes the backend's tool. No change; `toolhead.extruder` is always
   `extruder` on INDX and must stay ignored.
3. **Nozzle temperature UI and its label** (D1). The heater surfaces (`TemperatureService`,
   temp panel, temp graph, `nozzle_temps_widget#distinct_extruder_names`,
   `print_status_widget`) key on Klipper extruders, so they show one readout with no change.
   The label follows the tool changer convention: "Nozzle 3" while T2 is mounted
   (`lane_number_text` numbering, or the tool's own name when its owner named it), plain
   "Nozzle" with the head empty. No "(T2)" form.

   `ToolState::nozzle_label()` (`src/printer/tool_state.cpp#ToolState/nozzle_label`) already
   produces that spelling, but gates it on `has_multiple_extruders()`, which is 1 heated head
   on INDX and so false. That gate must stay false for lane-fed single-hotend printers (AFC,
   Happy Hare, AD5X IFS), where naming the loaded lane says nothing. **Change:** a new
   predicate `ToolState::names_mounted_nozzle()`: `has_multiple_extruders() ||
   (heads_share_heater_ && has_multiple_toolheads())`. It answers "more than one nozzle can
   be the one this readout means". `has_multiple_extruders()` keeps its meaning (count
   heaters) for the heater consumers in items 5 and 10 and the print-status nozzle picker.
   A lane backend never sets `heads_share_heater`, so its label is unchanged by
   construction.

   The label producers, and what each says on INDX with T2 mounted:

   | Producer | Today | INDX | Change |
   |---|---|---|---|
   | `ToolState::nozzle_label()` (controls panel `controls_nozzle_label`, filament panel) | "Nozzle N" gated on `has_multiple_extruders()` | "Nozzle 3"; "Nozzle" when empty | Gate on `names_mounted_nozzle()` |
   | Tool badge (`src/ui/ui_ams_tool_text.cpp#update_tool_badge`) | digit beside the nozzle glyph, same gate | "3"; hidden when empty | Gate on `names_mounted_nozzle()` |
   | Print status widget (`print_status_widget.cpp#DetailedFormatter/update_multi_tool`, `#DetailedFormatter/update_tool_label`) | tool label gated on `has_multiple_extruders()`; nozzle picker the same | Tool label shows the mounted tool, as on a U1; the nozzle picker stays hidden (one heater, nothing to pin) | Label and `multi_tool` subject on `names_mounted_nozzle()`; picker keeps `has_multiple_extruders()` |
   | `nozzle_temps_widget#create_extruder_row` | short name `display_label_for_extruder()`, long name from `tool_name_for_extruder()` or the extruder's `display_name` | One row. Without a change it reads "Tool 1" / "Nozzle" whatever is mounted, because both lookups return the FIRST tool naming `extruder` | `display_label_for_extruder()` and `tool_name_for_extruder()` resolve a shared extruder to the active tool (empty with the head empty, so the row falls back to "Nozzle"); the long name of a shared extruder is `nozzle_label()`. Row rebuilt on the active-tool subject |
   | `PrinterTemperatureState` `extruder_display_name()` (`src/printer/printer_temperature_state.cpp`) | "Nozzle" with one extruder, "Nozzle N" with several | "Nozzle": one extruder | None. It names the heater, and on INDX is read only where the heater is meant |
   | Temp graph overlay selector (`src/ui/ui_overlay_temp_graph.cpp`) and temp graph widget series | selector shown only with `extruder_count() > 1`; series named from `display_name` | Selector hidden; the series is "Nozzle" | None. The trace is the one heater across every tool change, so it carries no tool's name: "Nozzle 3" on a line that was T0 an hour ago would be wrong |

   So every surface that names the nozzle a user is looking at now says "Nozzle 3", and the
   only "Nozzle" left is the heater history, which is not one nozzle (D7).
4. **Heating an empty head.** HelixScreen sends `SET_HEATER_TEMPERATURE HEATER=extruder`
   (`src/ui/ui_temperature_utils.cpp#build_heater_gcode`), which bypasses INDX's `M104`
   guard. Upstream refuses that state on purpose, and the heater module shuts Klipper down on
   a model divergence (`indx_klipper` `heater.py`, `max_model_error`); what the coil does with
   no tool is not documented, so it is treated as unsafe. **Change:** `AmsBackend` gains
   `std::string nozzle_heat_blocked_reason() const` (default empty; INDX returns
   `indx::heat_blocked_reason()`: "No tool on the head" or "T<n> has no hotend").
   `TemperatureController::set_target` refuses a nozzle target above 0 while it is non-empty
   and toasts the reason; 0 always goes through. An `ams_nozzle_heat_blocked` int subject
   lets the nozzle preset and keypad controls render disabled declaratively, and a tap on a
   disabled control shows the reason ("No tool on the head", "T3 has no hotend") as a toast
   (D1), through the same `nozzle_heat_blocked_reason()` the controller refuses with, so the
   two can never disagree.
5. **Preheat widget** (`src/ui/panel_widgets/preheat_widget.cpp`). One heated head, one
   target. Blocked by item 4 with no tool mounted.
6. **Mount-feed sequence** (`src/printer/mount_feed_sequence.cpp#heater_name`). Names
   `extruder<n>` per head and preheats the next head of a batch. INDX does not use it
   (Load is `LOAD_FILAMENT`, Unload is Manual, `needs_mount_to_feed` false). If D2 later picks
   a built-in unload, `heater_name` must take the shared heater and the batch preheat must
   be skipped when `heads_share_heater`.
7. **Swap preheat latch** (FILAMENT_MANAGEMENT.md, "Swap Preheat"). The latch is per extruder,
   meant to purge the previous material from the same melt zone. On INDX the next tool is a
   different nozzle, so the latch would heat a PLA tool to the previous tool's ABS
   temperature. Load is unaffected (auto-heat skips the UI preheat), but purge/extrude on a
   mounted tool would floor at the wrong value. **Change:** when the active tool changes on a
   backend with `heads_share_heater()`, `AmsState` calls `clear_load_latch()`.
8. **Filament consumption** (`src/printer/filament_consumption_tracker.cpp`). The base
   tool-changer answer `slot_for_extruder(i) = i` would charge all extrusion to tool 0.
   INDX answers nullopt, so the tracker uses aggregate usage plus `get_current_slot()`, the
   mounted tool.
9. **Pressure advance.** `apply_saved_pressure_advance` resolves each tool's
   `effective_heater()`, all `extruder`, so a spool's saved PA applies to the one extruder
   when that spool goes active, which on INDX follows the mount. No change; per-tool PA
   follows the spool for free.
10. **PA tuning / calibration overlays** (`ui_overlay_pa_tuning`, `ui_panel_calibration_pa`).
    One heated head: single-tool flow. Correct, since Klipper has one extruder.
11. **Filament mapping and preflight.** Tool cards and Filament Mapping come from AMS slots
    (`is_tool_changer`). Remap is `GcodeRewrite` (needs the HelixPrint plugin), same as any
    changer with its own `T<n>`. No preflight check assumes per-tool heaters. A print using a
    `no_heat_tools` tool is fine (the macro never heats it).
12. **Telemetry, wizard, chips.** `telemetry_manager.cpp` (two switches),
    `ui_wizard_ams_identify.cpp`, `printer_manager_chips.cpp` (via `is_tool_changer`) gain the
    INDX case; `-Wswitch` finds any missed.

## 8. Subscriptions

`AmsBackendIndx::required_status_objects(hw)`, merged in
`src/printer/ams_backend.cpp#AmsBackend/required_status_objects`, answers only when
`hw.mmu_type() == AmsType::INDX`:

```
save_variables:                 ["variables"]
gcode_macro TOOL_POSITIONS:     ["tool_count", "tools", "no_heat_tools", "docks_calibrated"]
gcode_macro _TOOLCHANGE_VARS:   ["toolhead_state", "mode"]
gcode_macro LOAD_FILAMENT:      ["filament_presets"]          (when listed)
gcode_macro T<n>:               ["spool_id"]                  (D4, each listed T<n>)
```

`save_variables.variables` is one field holding every saved variable, so each `SAVE_VARIABLE`
republishes all of it; parsing is O(variables) per change, negligible. `load_cell_probe` is
not subscribed (seat checks are the macros' job). `extruder` is already in the app-wide set.
A fixture `tests/fixtures/subscription_parity/indx.json` pins the map.

## 9. Active spool (feeds the HelixPrint plugin design)

`manages_active_spool()` is true when the printer's own macros switch the Spoolman spool: any
`gcode_macro T<n>` or `CHANGE_TOOL` template that calls `SET_ACTIVE_SPOOL` or
`spoolman_set_active_spool`. `MacroParamCache` already parses every template at discovery;
`CachedMacroInfo` gains `bool sets_active_spool`, found the way `stops_printer` is. Stock
Bondtech macros: false. The reporter's: true.

- **False** (stock): `AmsState::sync_current_loaded_from_backend` posts the mounted tool's
  `spoolman_id` as the active spool on each mount, as it does for every non-managing backend.
  The plugin covers slicer-driven changes while the screen is off; both post the same id, so
  the overlap is idempotent.
- **True**: HelixScreen posts nothing; the user's macros own it, and the plugin stands down by
  the same template test.

## 10. Mock profile

`HELIX_MOCK_AMS=indx`: **mock hardware, real backend**, like `medusahc` and `multiace`.
It implies `--real-ams` in `src/system/cli_args.cpp`, so real discovery builds
`AmsBackendIndx` under `--test` (direction of #958). Default persona pairing:
`HELIX_MOCK_PRINTER=voron_trident` (the reporter's machine); INDX is an add-on, not a printer
type, so no new persona. The state lives in a new `src/api/moonraker_client_mock_indx.cpp`
beside the other `moonraker_client_mock_*.cpp` parts.

Published:

- `printer.objects.list`: `indx`, `save_variables`, `gcode_macro TOOL_POSITIONS`,
  `gcode_macro _TOOLCHANGE_VARS`, `gcode_macro CHANGE_TOOL`, `gcode_macro PARK_TOOL`,
  `gcode_macro LOAD_FILAMENT`, `gcode_macro SET_TOOL_FILAMENT`, `gcode_macro TOOL_STATUS`,
  `gcode_macro MANUAL_TOOL_REMOVE`, `gcode_macro MANUAL_TOOL_SEAT`, `gcode_macro T0..T<n-1>`,
  one `extruder`, `load_cell_probe`.
- `configfile.config`: an `indx` section and `gcode_macro TOOL_POSITIONS` with
  `variable_tool_count`.
- Status: the shapes in section 2, starting with T0 mounted, presets copied from upstream's
  `filament_presets`, six tools, `no_heat_tools: [5]` (a pen, so the no-hotend path is live).

Gcode handlers (sim-speed scaled):

- `CHANGE_TOOL TOOL=<n>` and `T<n>`: target to 0, latch 2, `active_tool` -1 (park), latch 0,
  `active_tool` n, `toolchange_count` +1, then the extruder target back to the outgoing
  target unless `n` is a no-heat tool. About 14 s at 1x, matching the README. Refuses with
  Klipper's error text on `seat_suspect >= 0`, `docks_calibrated` false, or out of range.
- `PARK_TOOL`: the first half of the above.
- `M104`/`M109`/`SET_HEATER_TEMPERATURE HEATER=extruder` with no tool or a no-heat tool:
  logs the upstream refusal line and leaves the target at 0, so a missing UI gate shows up
  in a mock run as a target that never moves.
- `LOAD_FILAMENT TOOL=<n> TYPE=|TEMP=`: change, heat, respond lines, done.
- `MANUAL_TOOL_REMOVE` / `MANUAL_TOOL_SEAT TOOL=<n>`: clear `seat_suspect`.

Knobs: `HELIX_MOCK_INDX_TOOLS` (2-10, default 6); `HELIX_MOCK_INDX_FAULT` = `seat_suspect`
| `docks_uncalibrated` | `unrecognized` (drops `CHANGE_TOOL` from the list) |
`kalico` (drops `indx` from the objects list, config key only); `HELIX_MOCK_INDX_SPOOL_MACROS=1`
(the reporter's shape: `T<n>` declares `spool_id` and its template calls `SET_ACTIVE_SPOOL`).
Documented in `docs/devel/MOCK_ENVIRONMENT_VARIABLES.md`.

## 11. Test plan

Tags: `[ams][indx]` on every new case, sub-tags below. New test files:
`tests/unit/test_indx_contract.cpp`, `tests/unit/test_ams_backend_indx.cpp`,
`tests/unit/test_mock_indx.cpp`. The fixtures are synthetic, built from the upstream shapes
in section 2; the #1333 bundle is never read or committed.

| Tag | Cases |
|---|---|
| `[indx][detect]` | `indx` object alone registers INDX; `indx` config key alone registers INDX (after `parse_objects` registered nothing); macros without `[indx]` register nothing; an MMU plus `[indx]` keeps the MMU; `variable_tool_count` read case-insensitively; `has_indx` with one extruder never also registers TOOL_CHANGER |
| `[indx][contract]` | Each required object missing in turn gives `recognized=false` and names it; `LOAD_FILAMENT` without presets gives `has_load_filament=false`; a `T<n>` template with `SET_ACTIVE_SPOOL` sets `macros_set_active_spool` |
| `[indx][status]` | `active_tool` -1 and n; absent `active_tool` is no news (tool unchanged); `variables` frame without `active_tool` keeps it; `seat_suspect` raises and clears the error; `tool_count` change re-sizes slots; `tools: []` before INIT_STATE does not zero the count; non-int values ignored |
| `[indx][commands]` | `T<n>` sent when the macro exists, `CHANGE_TOOL TOOL=<n>` when not; park sends `PARK_TOOL`; change refused (nothing sent) on unrecognized contract, `seat_suspect`, `docks_calibrated` false, out of range; Load sends `TYPE=PETG` for `petg`, `TEMP=` for an unmatched material, nothing for an unsafe material string; Load refused on a no-heat tool; Unload Manual |
| `[indx][heat]` | `heat_blocked_reason` for -1, a no-heat tool, a printing tool; `TemperatureController::set_target` refuses nozzle >0 while blocked and sends 0; bed/chamber unaffected; a backend without the capability unaffected (`[temp_controller]` regression) |
| `[tool_state][shared_heater]` | `heads_share_heater` topology: 6 heads, `extruder_count()` 1, `heated_heads()` one entry standing for the active tool; without the flag the hub grouping is unchanged (`[ToolState][ams-topology]` regression) |
| `[tool_state][nozzle_label]` | Shared heater, T2 mounted: `nozzle_label()` "Nozzle 3", a user-named tool keeps its name, head empty "Nozzle", `names_mounted_nozzle()` true, `has_multiple_extruders()` false; a 4-lane single-hotend AMS topology still reads "Nozzle" and `names_mounted_nozzle()` false; `display_label_for_extruder("extruder")` and `tool_name_for_extruder("extruder")` follow the active tool and are empty with the head empty |
| `[tool_switcher]`, `[print_status]` labels | Tool badge "3" and hidden on an empty head; print-status tool label follows the mounted tool while the nozzle picker stays hidden |
| `[indx][spool]` | `manages_active_spool` false on stock, true with the wrapper; `slot_for_extruder` nullopt so the tracker charges the mounted tool |
| `[indx][mock]` | Mock persona discovered as INDX through real discovery; a change walks the latch and lands on `active_tool`; `kalico` mode still detects; `unrecognized` mode yields the error state; heating with no tool leaves the target at 0 |
| `[subscription_parity]` | `indx.json` fixture |

Hand-break proof per CLAUDE.md, one `make t` cycle each while writing: delete the
`parse_config_keys` re-registration (the config-only detect case goes red); make
`heat_blocked_reason` return empty (the `[indx][heat]` refusal goes red); drop the
`heads_share_heater` branch from `physical_heads` (the 6-heads case goes red); gate
`nozzle_label()` back on `has_multiple_extruders()` (the "Nozzle 3" case goes red) and drop
the clause instead of the gate (the lane-AMS "Nozzle" case goes red); swap the
`T<n>`/`CHANGE_TOOL` choice (the commands case goes red); drop the `slot_for_extruder`
override (the tracker case goes red).

Tags touched by the change, to run while building: `[ams][indx]`, `[toolchanger]`,
`[ToolState]`, `[tool-state]`, `[temp_controller]`, `[preheat]`, `[tool_switcher]`,
`[printer_discovery]`, `[ams][multi-backend]`, `[macro_param_cache]`, `[spoolman_sync]`,
`[subscription_parity]`, `[filament][tracker]`, `[ams][traits]`, `[mock][persona]`.
Finish, once per batch: one review, `make mutate-diff` over those tags, one screenshot each of
the filament panel's tool cards, the tool switcher and the nozzle temperature control under
`HELIX_MOCK_AMS=indx` (headless), one `make land-gate`.

## 12. Phasing

1. **Phase 1, visible and safe.** `AmsType::INDX` and every switch site; detection; the
   contract module; `AmsBackendIndx` with change, park, Load, spool assignment, the error
   states; `heads_share_heater` and `names_mounted_nozzle()` in ToolState with the label
   producers of section 7 item 3; the heat gate and its tap toast; `slot_for_extruder`; latch
   reset; `sets_active_spool`; the mock; the leaf doc. Then a build for the #1333 reporter
   (haznat) to run read-only first (does it detect, are the tools and the mounted tool
   right), then one supervised tool change.
2. **Phase 2, reporter feedback.** Whatever phase 1 shows on real hardware: the Kalico object
   question, D4's write path, timing of the change ack against the `active_tool` frame.
3. **Phase 3, device actions.** Speed mode, `TC_CYCLE_ALL`, seat-fault recovery, heater
   calibrations, `SET_TOOL_FILAMENT` on edit, offsets display, batch Load.

## 13. Decisions (ruled by Preston, 2026-10-21)

- **D1. Nozzle label: tool changer convention.** "Nozzle 3" while T2 is mounted
  (`lane_number_text`, or the tool's own name), plain "Nozzle" with the head empty, exactly
  as klipper-toolchanger and the U1 read. No "(T2)" form. The empty-head heat controls are
  disabled through `ams_nozzle_heat_blocked`, and a tap toasts the reason. Design: section
  7 item 3 (`names_mounted_nozzle()`, the producer table) and item 4 (the toast).
- **D2. Unload: Manual in phase 1.** `head_filament_ops()` answers Unload Manual with "Pick
  up the tool, heat it, and pull the filament out". A built-in mount-heat-retract waits for
  the reporter to confirm how a strand leaves an INDX tool.
- **D3. Material edits: phase 3 offers `SET_TOOL_FILAMENT`.** When an edited material matches
  a `LOAD_FILAMENT` preset, a confirm offers "Update the INDX heater model too?" and sends
  `SET_TOOL_FILAMENT TOOL=<n> DENSITY=<d> HEAT_CAPACITY=<hc>` on yes. Phase 1 and 2 never
  touch the model outside Load.
- **D4. `T<n>.spool_id`: read now, write later.** Read where the variable exists (phase 1,
  section 8 subscription). Writing it back waits for the reporter to confirm their macro
  shape; until then HelixScreen's store is where an assignment made on the screen lives.
- **D5. Half-seated fault: no buttons in phase 1.** The fault shows with the macro's own
  instructions. `MANUAL_TOOL_REMOVE` / `MANUAL_TOOL_SEAT` buttons are phase 3, behind a
  confirm that says to hold the tool.
- **D6. Change command: `T<n>` when the macro exists, else `CHANGE_TOOL TOOL=<n>`.** A screen
  tap then runs the user's wrapper exactly as a slicer change does.
- **D7. Temp graph series: plain "Nozzle".** The series is the one heater's history across
  every tool change, so it names no tool. `TempGraphWidget` is unchanged.

### Ruled by the orchestrator, 2026-10-21, for Preston's review

- **O1. No swap under a paused print.** A tool change or park while a print is paused is
  refused with its own reason ("Resuming would print with a different nozzle. Change tools
  after the print."). Load and Unload of the MOUNTED tool stay allowed while paused (runout
  recovery); a Load of a docked tool is a swap and is refused the same way.
  `filament_ops_self_home` stays false: CHANGE_TOOL's home is an ordinary G28 Layer 1 sees.
- **O2. Docked nozzles show no temperature.** On a shared heater only the mounted nozzle
  reads the heater (`ToolState::head_reads_heater()`); the tool picker's docked cells show
  material only and are never tinted hot.
- **O3. Telemetry reports the real type.** `ams.type` comes from one slug table
  (`ams_type_telemetry_slug()`): every existing value is unchanged, the U1 now reports
  "snapmaker" (it reported "unknown"), INDX reports "indx".
- **O4. The heat refusal covers every nozzle-heating start.** PA tuning and PID/MPC
  calibration of the extruder refuse to start with the backend's reason, and a refused
  preheat announces no success.
- **O5. An empty carriage selects no tool.** The filament panel's Tool dropdown reads "None"
  after a park, on every tool changer.

## 14. Not confirmed

- Whether Kalico's `[indx]` lists in `printer.objects.list`; detection handles both.
- How the head engages a docked tool's filament, and whether a retract can unload one (D2).
- What the induction heater does with no tool on the head. Treated as unsafe because upstream
  refuses it.
- Where the reporter's `INDX_FORCE_STATE` / `VERIFY_TOOL_PRESENT` / `spool_id` macros come
  from, and how `spool_id` is persisted (D4).
- Whether a `T<n>` added by the user without `variable_active` behaves differently from
  upstream's (the design does not read `active`).
