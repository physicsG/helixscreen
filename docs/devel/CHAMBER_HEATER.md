# Chamber Heaters (Developer Guide)

How HelixScreen models heated printer chambers: the backend abstraction that keeps vendor knowledge in one place, how a chamber is discovered and wired into subjects, the ceiling rules that cap what the UI will send, and the arbitration semantics when something else is driving the heater.

Chamber heaters come in two very different shapes, and the backend interface is what lets the rest of the app treat them identically:

- **Integrated style** (K2 Plus): the chamber is part of the printer — a `heater_generic` + a `temperature_fan` cooling pair, scripted by the printer's own `M141`/`M191` macros. Diagnostics are just the standard Klipper status fields.
- **External appliance** (DragonBreath, Panda Breath): a separate heated-filtration box with its own firmware, exposing a vendor status object (faults, PTC element temp, filter fan) and vendor commands. These need a backend that speaks their schema.

Part of issue #1290.

---

## Architecture Overview

```
Moonraker objects/list
  |
  |  PrinterDiscovery::parse_objects  (include/printer_discovery.h)
  |    try_set_chamber_heater() consults helix::chamber::match()
  v
ChamberHeaterBackend (registry, fixed priority)
  |  generic            — keyword-tier fallback (CHAMBER 100 > ENCLOSURE 90 > CAVITY 85 > BOX 60)
  |  dragonbreath       — appliance, name match at 95
  |  panda_breath       — appliance, name match at 95
  v
PrinterState::set_hardware  (src/printer/printer_state.cpp)
  |  gates on "resolved chamber heater == discovery pick", then:
  |    temperature_state_.set_chamber_diagnostics_source(id, diag_object, filter_pin)
  |    TemperatureController::set_chamber_actions(reset_gcode, filter_pin, conservative_max)
  v
  +-> Subscription builder adds backend surfaces
  |     (src/api/moonraker_discovery_sequence.cpp — chamber::required_status_objects)
  |
  +-> Status frames -> printer_temperature_state.cpp parse block
  |     backend->parse_diagnostics() -> generic ChamberHeaterDiagnostics
  |     -> subjects chamber_heater_* / chamber_filter_fan_*
  |
  +-> TemperatureController::ensure_limits() applies the ceiling
        configfile max_temp  >  backend conservative_max  >  heater default (80)
  |
  v
ui_xml/components/chamber_diagnostics_card.xml  (temp graph overlay, chamber-mode-gated)
```

The invariant: **vendor JSON schemas are translated to the generic `ChamberHeaterDiagnostics` struct at the backend border** — subjects, UI, and controllers never see a vendor field name. Adding a brand means one new `.cpp` file and one registry line; nothing else in the tree changes.

---

## Key Files

| File | Purpose |
|------|---------|
| `include/chamber_heater_backend.h` | Backend interface + registry API: `ChamberHeaterDiagnostics` (the only shape subjects/UI see), `match()`, `backend_by_id()`, `required_status_objects()` |
| `src/printer/chamber_heater_backend_generic.cpp` | Generic keyword backend + the registry itself (`registry()`, `match()`, `backend_by_id()`) and the keyword-confidence tiers |
| `src/printer/chamber_heater_backend_dragonbreath.cpp` | DragonBreath appliance backend: 24-field `dragonbreath` status parse, `DRAGONBREATH_RESET`, filter pin, 60°C conservative cap. Schema verified live on the U1 rig 2026-08 |
| `src/printer/chamber_heater_backend_panda_breath.cpp` | Panda Breath backend: heater + 60°C ceiling only (schema not yet hardware-verified), documents stock Auto mode via `device_autonomous_control()` |
| `include/printer_discovery.h` | `try_set_chamber_heater` lambda in `parse_objects()` — first registry consult, records backend id + diagnostics object + filter pin on discovery |
| `src/api/moonraker_discovery_sequence.cpp` | Subscription-builder block adding the backend's diagnostics object + filter pin (`chamber::required_status_objects`) |
| `src/printer/printer_temperature_state.cpp` | Diagnostics parse block: translates backend output to subjects; also owns all `chamber_heater_*` / `chamber_filter_fan_*` subject registration and display-string formatters |
| `src/printer/printer_state.cpp` | The wiring block: gates both the diagnostics source and the TemperatureController action surface on resolved-heater == discovery-pick |
| `src/ui/temperature_controller.cpp` | `set_chamber_actions()`, `reset_chamber_fault()`, `set_chamber_filter_fan()`, and the `ensure_limits()` ceiling fallback |
| `ui_xml/components/chamber_diagnostics_card.xml` | The card: fault/inhibited banner + Reset, element temp + filter fan %, filter-fan toggle. Structural portrait branch renders a compact variant (single info row with an inline icon-button fan toggle, one-line banner) so 272x480 chamber mode fits with no scroll. Instantiated in `temp_graph_overlay.xml` behind `<if cond="printer_has_chamber_heater_diagnostics and temp_graph_mode eq 3">` |
| `src/api/moonraker_client_mock.cpp` | Mock chamber backend shape (`HELIX_MOCK_OBJECTS` dragonbreath trio), registry-based chamber-status key |
| `tests/unit/test_chamber_*.cpp` | Backend match/parse, subjects, ceiling, actions, discovery, mock — tags under `[chamber]` |

---

## The Two Styles

### Integrated style (K2 Plus)

The chamber ships with the printer. Klipper exposes the pair directly:

- `heater_generic chamber_heater` — the heating element (HEATING setpoint)
- `temperature_fan chamber_fan` — chamber cooling (MAINTAINING setpoint)

The printer's own `M141`/`M191` macros split the setpoint across the pair: above 40°C the target lands on the heater, at or below 40°C it parks on the cooling fan with the heater at 0. `M141 S0` resets the fan to its **configured resting target** (`target_temp` in the fan's config section — 35°C on our K2), not to literal 0. HelixScreen mirrors this split in `chamber_effective_setpoint()` (`include/ui_temperature_utils.h`) and routes chamber sets through `M141` when the printer defines the macro (`chamber_uses_m141()`), falling back to raw `SET_HEATER_TEMPERATURE` / `SET_TEMPERATURE_FAN_TARGET` otherwise.

These printers need no diagnostics backend — the generic backend matches by keyword and provides no diagnostics surface; the standard status fields are the diagnostics.

### External appliance style (DragonBreath, Panda Breath)

A separate box with its own firmware bolts onto the printer. The Klipper side is a thin glue module exposing:

- a vendor status object (e.g. `dragonbreath` — 24 fields: `ptc_temp`, `fault`, `inhibited`, `fan_percent`, `fan_reason`, `mode`, `source`, `lease_owned`, ...)
- vendor commands (`DRAGONBREATH_RESET`) and a filter-fan `output_pin`

The backend translates that schema to the generic struct. Vendor names appear **only** in `chamber_heater_backend_*.cpp` — anywhere else is a vendor-leak bug (see the VENDOR_OK rule in the root `CLAUDE.md`).

---

## Discovery and Arbitration

`chamber::match(object_name)` runs every discovered `heater_generic` / `temperature_fan` bare name past the registry:

- Every backend scores the name; highest confidence wins, ties resolve by registry order.
- Appliance backends claim their own names at **95** — they always beat the generic keyword tiers on the object that is actually theirs.
- The generic backend carries the keyword tiers: `CHAMBER` 100 > `ENCLOSURE` 90 > `CAVITY` 85 > standalone `BOX` 60; -1 for compound names, -40 for air-quality tokens (`TVOC`, `CO2`, `HUMIDITY`, ...), floored at 1.
- `try_set_chamber_heater` additionally breaks ties by object type: a settable `heater_generic` (weight 2) beats a `temperature_fan` (weight 1) at equal keyword confidence. The losing `temperature_fan` is still recorded as the **chamber cooling fan** so the integrated-style Maintaining readout works.

The matched backend id survives on `PrinterDiscovery` (`chamber_heater_backend_id()`) and is re-consulted in `PrinterState::set_hardware`: the diagnostics source and the action surface apply **only while the resolved chamber heater is the discovery pick** — a manual override to another heater (or "none") detaches both, and the actions revert to no-ops.

---

## Subjects

All registered by `PrinterTemperatureState`; display strings are formatter subjects (XML has no deci/percent formatter).

| Subject | Type | Meaning |
|---------|------|---------|
| `chamber_heater_fault` | int 0/1 | Latched fault |
| `chamber_heater_inhibited` | int 0/1 | Heater refusing commands (e.g. post-fault cooldown) |
| `chamber_heater_fault_reason` | string | Raw vendor fault code, "" when none — log-only, never bound by UI |
| `chamber_heater_fault_reason_text` | string | Translated phrase for the backend's generic `FaultReason` kind ("" when none) — what the banner binds |
| `chamber_heater_externally_controlled` | int 0/1 | Another controller is driving the heater (display-only, see below) |
| `chamber_heater_element_temp` / `..._text` | int / string | Heating-element temp ("-1"/"--" = unknown) |
| `chamber_filter_fan_percent` / `..._text` | int / string | Filtration-fan speed ("-1"/"--" = unknown) |
| `chamber_filter_fan_reason` | string | Why the firmware chose that fan speed |
| `chamber_filter_fan_on` / `..._text` | int / string | Binary filter-fan pin state |
| `chamber_filter_fan_icon` | string | Toggle icon name ("fan"/"fan_off") — bind_icon source for the compact portrait card |
| `printer_has_chamber_heater_diagnostics` | int 0/1 | Capability: diagnostics card is built at all |
| `printer_has_chamber_filter_fan` | int 0/1 | Capability: filter-fan toggle row |

Capability setters round-trip through `PrinterCapabilitiesState`; `set_hardware` raises them exactly when the backend provides the corresponding surface.

---

## Ceiling Rules

What the chamber keypad and presets will offer, in precedence order:

1. **`configfile` `max_temp`** for the resolved heater section — the printer's own limit (read via `query_configfile` in `TemperatureController::ensure_limits()`). Example: the K2's `heater_generic chamber_heater` declares `max_temp: 80`.
2. **Backend `conservative_max_temp()`** — used only when configfile is silent. DragonBreath and Panda Breath both return 60 (their firmware hard-caps targets; DragonBreath's config usually says 75 — the backend stays below it on purpose).
3. **Heater default** (`keypad_max_default`, 80 for chamber) — generic backend returns 0 = no clamp, so an unconfigured generic chamber keeps the default.

The fallback snapshot is taken **before** the configfile query fires — the parse callback runs on the WebSocket thread and must not read `this` members (see `THREADING.md`).

---

## Arbitration: Who Is Driving the Heater?

Two distinct mechanisms, both informational in v1 — HelixScreen never fights another controller for the chamber:

- **DragonBreath lease semantics.** SET_HEATER_TEMPERATURE commands round-trip with a lease: while our lease is held the status reports `lease_owned` / `source: klipper`. When another controller (device web UI, physical button) takes over, the firmware invalidates our lease and the status keeps reporting authoritative state. `parse_diagnostics()` derives `externally_controlled = (mode == power_on) && !(lease_owned || source == klipper)` — heating that is neither ours nor Klipper's. The UI shows this as an annotation only.
- **Stock-firmware autonomous mode (Panda Breath).** `device_autonomous_control()` returns true: the device's own Auto mode follows the bed temperature with no host involvement. Documented, not acted on — a policy (e.g. dimming the setpoint UI) is deliberately deferred.

---

## Adding a Chamber-Heater Backend

One file + one registry line + one test + one mock shape. The vendor-abstraction test: adding a second appliance must touch exactly the files below — if you find yourself editing the status parser, the subscription builder, *and* the panel, the abstraction is missing something.

1. **Create `src/printer/chamber_heater_backend_<name>.cpp`** — subclass `ChamberHeaterBackend` (see `chamber_heater_backend_panda_breath.cpp` for the minimal shape, `chamber_heater_backend_dragonbreath.cpp` for the full diagnostics parse). Implement `id()`, `discovery_confidence()`, and the capability questions; return `std::nullopt` from `parse_diagnostics()` until the status schema is hardware-verified. Give the file a `// VENDOR_OK:` header comment.

2. **Register it** in the `REG` vector in `chamber_heater_backend_generic.cpp` (`registry()`), plus the `<name>_backend_instance()` free-function pattern the existing backends use. The Makefile picks the new `.cpp` up by wildcard; also add it to `components/helixapp/app_srcs.txt` (ESP32 manifest — the pre-commit gate fails the link otherwise).

3. **Add a `parse_diagnostics` test** in `tests/unit/test_chamber_heater_backend.cpp` — a live nominal payload, a faulted/edge variant, and a foreign-payload rejection (mirror the three DragonBreath cases).

4. **Mock shape** — for appliances with a status object, extend the `HELIX_MOCK_OBJECTS` handling in `moonraker_client_mock.cpp` if a new object shape is needed (the dragonbreath trio is the reference: `HELIX_MOCK_OBJECTS="heater_generic dragonbreath dragonbreath output_pin dragonbreath_filter"` — heater object, status object, filter pin).

Verify with `./build/bin/helix-tests "[chamber]"` and a mock run:

```bash
HELIX_MOCK_OBJECTS="heater_generic dragonbreath dragonbreath output_pin dragonbreath_filter" \
  ./build/bin/helix-screen --test -vv
```

---

## Testing

```bash
./build/bin/helix-tests "[chamber]"          # the whole feature
./build/bin/helix-tests "[chamber][backend]" # registry match + per-backend parse
./build/bin/helix-tests "[chamber][subjects]"# diagnostics subjects + pin mapping
./build/bin/helix-tests "[chamber][ceiling]" # configfile > conservative > default
./build/bin/helix-tests "[chamber][actions]" # fault reset + filter fan gcode
```

Per-file: `test_chamber_heater_backend.cpp` (match/parse), `test_chamber_heater_discovery.cpp` (discovery hook), `test_chamber_diagnostics_subjects.cpp`, `test_chamber_ceiling_actions.cpp`, `test_chamber_mock_dragonbreath.cpp`, `test_chamber_panel_diagnostics.cpp`, `test_chamber_temperature.cpp`, `test_chamber_mode_icon_label_parity.cpp`.

---

## Verification Log

### DragonBreath on the U1 rig (2026-08, live)

- `SET_HEATER_TEMPERATURE HEATER=<bare> TARGET=...` round-trips with **lease semantics**: the `dragonbreath` status object reports our lease while we hold it; another controller taking over invalidates it and the object keeps reporting authoritative state.
- The glue module's `M141` is **module-registered, not a macro** — invisible to Moonraker's `gcode_macro` object list. Chamber routing therefore falls back to raw `SET_HEATER_TEMPERATURE` (`chamber_uses_m141()` returns false because no `gcode_macro M141` object exists).
- `configfile` exposes `max_temp: 75` for the heater section — readable, and it wins over the backend's conservative 60.
- The `dragonbreath` status object carries **24 fields**; the backend parse (`chamber_heater_backend_dragonbreath.cpp`) is written against a captured live payload.

### K2 Plus, integrated style (2026-08-20, live, non-invasive)

Printer idle-checked first (`print_stats.state: complete`) before any gcode was sent.

| Check | Result |
|-------|--------|
| Chamber objects present | **PASS** — verbatim: `heater_generic chamber_heater`, `temperature_fan chamber_fan`; also `heater_fan chamber_fan` (PTC fan on the heater itself) and `temperature_sensor chamber_temp` |
| `M141`/`M191` macro visibility | **PASS** — `gcode_macro M141` and `gcode_macro M191` are listed objects (macro bodies not exposed via `configfile`; `gcode_macro SET_CHAMBER_FAN` and `gcode_macro CANCEL_CHAMBER_FAN_SWITCH` also present) |
| `M141 S35` readback (cooling range) | **PASS** — heater target stayed 0.0; the setpoint parks on `temperature_fan chamber_fan` (target 35.0). Note: 35 equals this unit's configured resting `target_temp`, so the parking is confirmed by the mode model plus the `S0` behavior below rather than a visible target change |
| `M141 S0` reset | **PASS** (with nuance) — heater 0; the cooling fan returns to its configured resting target (35.0), **not** literal 0. This is exactly the resting semantics `chamber_effective_setpoint()` implements; a literal `SET_TEMPERATURE_FAN_TARGET ... TARGET=0` is a setpoint *below* resting, not the firmware's off state |
| Left clean | **PASS** — restored to the config-defined idle (heater target 0 / power 0, fan target at resting 35.0, fan speed 0) |

Config facts captured: `heater_generic chamber_heater` — `max_temp: 80`, watermark control, `verify_heater`; `temperature_fan chamber_fan` — `max_temp: 80`, `target_temp: 35` (resting), watermark, `max_delta: 0.5`.

---

## Deferred: Dryer Mode

Chamber dryer mode (Panda Breath's `PANDA_BREATH_DRY_START`/`STOP` passthrough, DragonBreath's hardware drying with no Klipper surface, a generic hold-N°C-for-M-hours loop) is **deliberately out of scope** for v1 — tracked in [#1299](https://github.com/prestonbrown/helixscreen/issues/1299). It should land as a generic backend capability question reusing the existing dryer UX (Happy Hare dryer panels, AMS environment overlay), not as per-vendor UI.
