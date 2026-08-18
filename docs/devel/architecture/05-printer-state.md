# 05 — Printer State & the Singleton Map

Every fact the UI shows about the printer lives in one object graph rooted at `PrinterState`, a Meyers singleton reached through `get_printer_state()`. It is not a god class: the header delegates to thirteen domain components (temperature, motion, print, capabilities, ...) held by value, each owning the LVGL subjects for exactly one concern. Around it orbit two satellites with different jobs and different access patterns — `ToolState` (a classic `::instance()` singleton for multi-tool tracking) and `TemperatureController` (owned by `SubjectInitializer`, and the only code allowed to send a heater target). This chapter covers the decomposition, the two satellites, and the regenerated map of every global in the tree: 76 `::instance()` singletons plus four other access shapes, and which of them register with the two shutdown registries.

Chapter 02 covered the subject machinery itself — init macros, observer factories, the `SubjectInitializer` phase ordering — so this chapter stays on the map: which class owns which data, and how you are allowed to reach it.

```mermaid
flowchart TB
    MR["Moonraker status JSON<br/>(already on the main thread — ch. 02)"]
    UI["Panels, home widgets, XML bindings"]

    PS["PrinterState — get_printer_state()<br/>orchestrator: state_mutex_,<br/>122 fixed subject declarations"]

    subgraph DOM["13 domain components, held by value"]
        D1["PrinterTemperatureState<br/>nozzle/bed/chamber + dynamic ExtruderInfo[]"]
        D2["PrinterMotionState<br/>position, speed/flow, live + persisted z-offset"]
        D3["PrinterPrintState<br/>progress, filename, ETA, print-start"]
        D4["PrinterCapabilitiesState<br/>22 subjects gating UI"]
        D5["fan - LED - calibration - network - versions<br/>excluded-objects - hardware-validation<br/>plugin-status - composite-visibility"]
    end

    TS["ToolState — ToolState::instance()<br/>ToolInfo[], AMS topology override,<br/>5 subjects, spool persistence"]
    TC["TemperatureController<br/>SubjectInitializer-owned, get_temperature_controller()<br/>the one heater-target send"]

    MR -->|"update_from_status() fan-out"| PS
    PS --> DOM
    MR -->|"update_from_status()"| TS
    DOM -->|"subjects"| UI
    TS -->|"subjects"| UI
    UI -->|"set_target(HeaterType, degC)"| TC
    TC -->|"gcode via JSON-RPC (M141 for chamber)"| MR
```

## Key files

| File | Role |
|------|------|
| `include/printer_state.h` | `PrinterState` orchestrator; the 13 domain members live at the bottom of the class |
| `src/printer/printer_state.cpp` | `init_subjects()` / `deinit_subjects()` fan-out, `update_from_status()`, setter marshalling |
| `include/tool_state.h` | `ToolState` singleton: `ToolInfo`, AMS topology override, Spoolman spool assignments |
| `include/temperature_controller.h` | The single authority for heater target sends |
| `include/app_globals.h` | Access to every published global: printer state, API/client, controller, histories |
| `src/application/subject_initializer.cpp` | Boot-time init phases; owns `TemperatureController` and `TemperatureService` |
| `include/static_subject_registry.h` | Subject-deinit ordering; mandates the self-registration pattern |
| `include/static_panel_registry.h` | Panel/overlay destruction ordering |
| `include/ui_panel_singleton_macros.h` | `DEFINE_GLOBAL_PANEL` — the global-panel idiom behind every `get_global_*_panel()` |

## How it works

### One orchestrator, thirteen domains

`PrinterState` (`include/printer_state.h:208`) keeps its historical public API but holds the implementation as thirteen domain components by value (`include/printer_state.h:2236`-2279): `temperature_state_`, `motion_state_`, `led_state_component_`, `fan_state_`, `print_domain_`, `capabilities_state_`, `plugin_status_state_`, `calibration_state_`, `hardware_validation_state_`, `composite_visibility_state_`, `network_state_`, `versions_state_`, `excluded_objects_state_`. Callers never touch a domain directly — `PrinterState` forwards. Each domain follows the same shape: `init_subjects(bool register_xml)`, `deinit_subjects()`, `update_from_status()`, change-gated setters.

| Domain | Owns (from its header) |
|--------|------------------------|
| `PrinterTemperatureState` | Nozzle/bed/chamber temps + targets (decidegrees), dynamic per-extruder `ExtruderInfo` map |
| `PrinterMotionState` | Position, speed/flow, homed axes, kinematic envelope, live + persisted z-offset |
| `PrinterPrintState` | Print progress, state, filename, layers, ETA, print-start phases (30 subjects — the largest) |
| `PrinterCapabilitiesState` | Hardware/firmware capability subjects gating UI feature visibility (22 subjects) |
| `PrinterFanState` | Fan speeds and wizard-configured fan role assignments |
| `PrinterCalibrationState` | PID / Z-offset calibration runs, bed mesh status, the Klippy-volatile subjects |
| `PrinterNetworkState` | Moonraker connectivity, Klippy state, hostname |
| `PrinterVersionsState` | Klipper, MCU, Moonraker software versions |
| `PrinterLedState` | LED channel/brightness/on-off subjects |
| `PrinterExcludedObjectsState` | Klipper `EXCLUDE_OBJECT` state |
| `PrinterHardwareValidationState` | Hardware health-check results (11 subjects) |
| `PrinterPluginStatusState` | HelixPrint plugin status gating |
| `PrinterCompositeVisibilityState` | The aggregate `has_any_preprint_options` visibility subject |

Across the fourteen headers (orchestrator + domains) there are 122 fixed `lv_subject_t` declarations, plus heap-allocated dynamic subjects created at runtime (per-extruder `ExtruderInfo`, rediscovered fans and sensors). The old singleton map's "~50 subjects" was undercounted by half even before counting dynamics.

Lifecycle is a fan-out, not fourteen registrations. `PrinterState::init_subjects()` (`src/printer/printer_state.cpp:193`) calls each domain's `init_subjects(register_xml)` in a fixed order, then self-registers **one** cleanup entry (`"PrinterState"`) with `StaticSubjectRegistry`. `deinit_subjects()` (`printer_state.cpp:130`) runs the mirror: invalidate the `AsyncLifetimeGuard` (drops setter callbacks still queued on the UpdateQueue), unregister the per-printer cache invalidator from `PrinterCacheRegistry`, flip the `SubjectLifetime` death token **before** tearing anything down (so surviving `ObserverGuard`s skip removal on soon-to-be-freed observer lists), then deinit all thirteen domains plus the orchestrator's own three subjects (`active_printer_name_`, `multi_printer_enabled_`, `z_offset_can_save_`). Domains never register themselves — the old monolithic ARCHITECTURE.md's claim that "each domain registers with StaticSubjectRegistry" was stale; the single orchestrator entry covers them.

### The satellites: ToolState and TemperatureController

`ToolState` (`include/tool_state.h:78`) is a standalone `::instance()` singleton because tools span domains: a tool has a temperature (extruder mapping), a filament source (AMS backend slot), and a Spoolman identity. It owns five subjects — `active_tool`, `tool_count`, `tools_version` (UI rebuild trigger), `tool_badge_text`, `show_tool_badge` — the last two feeding the `nozzle_icon` component's tool badge. Two facts trip contributors:

- `tool_count() != extruder_count()`. When an AMS backend pushes a `ToolTopology` override (`set_ams_topology()`), `tools_` expands to one entry per filament **slot**, so a 4-slot AMS on a single-hotend printer reports 4 tools and 1 extruder. `is_multi_tool()` answers "show multi-tool controls?"; `has_multiple_extruders()` answers "does this printer physically have several hotends?" — the badge test, not the controls test.
- Spool assignments are persisted by identity, not weight (`assign_spool()` → the tool_spools JSON + Moonraker DB); the whole weight-churn saga is documented in `07-filament-ams.md` ("Spool assignment: identity is durable, weight is cache").

Like `PrinterState`, `ToolState` is fed `update_from_status()` on the main thread (ch. 02) and hands out a `SubjectLifetime` via `get_subjects_lifetime()` (`include/tool_state.h:199`) that long-lived observers must pass to their `observe_*` call.

`TemperatureController` (`include/temperature_controller.h:65`) is deliberately **not** a singleton. `SubjectInitializer` constructs it in `init_panel_subjects()` (`src/application/subject_initializer.cpp:468`), holds the `unique_ptr`, and publishes the raw pointer as a shared resource on `PanelWidgetManager`; `get_temperature_controller()` (`include/app_globals.h:79`) looks it up and returns `nullptr` before init. It resolves Klipper heater names (`Nozzle` → active extruder, `Bed` → `heater_bed`, `Chamber` → discovered name), applies `configfile` `max_temp` limits to the keypad range and preset visibility, owns the preset model, and provides the standard failure toast. It holds no widgets or subjects — toasts go through `NOTIFY_*`, which keeps it unit-testable. The rule is absolute: any UI that sets a temperature calls `set_target()` (`temperature_controller.h:99`) — raw `MoonrakerAPI::set_temperature()` from view code fails the build via the lint gate in `tests/shell/test_code_lint.bats`. Chamber specifics (M141 routing, decidegree precision) are in `../MULTI_EXTRUDER_TEMPERATURE.md` § "Chamber Heating (M141)"; error-ownership of a failed send is in `../RPC_ERROR_OWNERSHIP.md`.

### The singleton census: five access shapes, 76 `::instance()` classes

"HelixScreen has 40+ singletons" (the old map) is now half the story. The tree has **76 classes with a `static X& instance()` (or pointer) declaration** in `include/`, plus four other shapes worth knowing before you grep:

1. **Meyers `::instance()` singletons** — the 76 in the table below.
2. **`get_printer_state()`** (`include/app_globals.h:149`) — same Meyers technique, free-function spelling; there is no `PrinterState::instance()`.
3. **Published pointers** — get/set pairs in `app_globals.h` for objects owned elsewhere and published as globals (nullable!): `MoonrakerManager` (Application-owned, `set_moonraker_manager()` in `src/application/application.cpp:2080`), `IMoonrakerClient` / `IMoonrakerAPI` (owned by MoonrakerManager behind interfaces), `JobQueueState`, `PrintHistoryManager`, `TemperatureHistoryManager`.
4. **`Config::get_instance()`** (`include/config.h:562`) — static-member-pointer spelling of the same idea.
5. **Not singletons at all** — `PrinterDetector` is a static utility class (`PrinterDetector::auto_detect()` etc.; no `instance()` exists), and panels/overlays are global **instances** behind `get_global_*_panel()` accessors created by `DEFINE_GLOBAL_PANEL` (`include/ui_panel_singleton_macros.h:74`) — each registering its destruction with `StaticPanelRegistry`.

| Singleton | Header | Role |
|-----------|--------|------|
| **Printer & job state** | | |
| `ToolState` | `tool_state.h` | Multi-tool tracking, AMS topology, spool assignments |
| `AmsState` | `ams_state.h` | Multi-backend filament-system state (ch. 07) |
| `TimelapseState` | `timelapse_state.h` | Timelapse recording + render progress |
| `PrintControlButtons` | `print_control_buttons.h` | Shared pause/resume/stop subjects + callbacks |
| `PowerDeviceState` | `power_device_state.h` | Moonraker power-device state |
| `PerformanceState` | `performance_state.h` | Per-metric ring buffers (~60 s history) |
| `SensorState` | `sensor_state.h` | Discovered Moonraker sensor metadata |
| **Sensor managers** | | |
| `TemperatureSensorManager` | `temperature_sensor_manager.h` | `temperature_sensor` / `temperature_fan` objects |
| `HumiditySensorManager` | `humidity_sensor_manager.h` | BME280, HTU21D, SHT3X, AHT10/20/20-F |
| `WidthSensorManager` | `width_sensor_manager.h` | Filament width sensors (TSL1401CL, Hall) |
| `ProbeSensorManager` | `probe_sensor_manager.h` | Native Klipper probe sensors |
| `AccelSensorManager` | `accel_sensor_manager.h` | ADXL345, LIS2DW, LIS3DH, MPU9250, ICM20948 |
| `ColorSensorManager` | `color_sensor_manager.h` | TD-1 color sensors |
| `FilamentSensorManager` | `filament_sensor_manager.h` | Filament sensor discovery + runout state |
| `DetectionManager` | `detection_manager.h` | Detection-source registry + policy dispatch |
| **Filament & spools** | | |
| `SpoolmanManager` | `spoolman_manager.h` | Spoolman polling, circuit breaker, identity cache |
| `FilamentConsumptionTracker` | `filament_consumption_tracker.h` | Per-spool filament consumption accounting |
| `PostOpCooldownManager` | `post_op_cooldown_manager.h` | Cooldown after load/unload/swap operations |
| **Settings & config** | | |
| `SettingsManager` | `settings_manager.h` | Persistent settings root |
| `SystemSettingsManager` | `system_settings_manager.h` | System-level settings slice |
| `AudioSettingsManager` | `audio_settings_manager.h` | Completion alerts, sound settings |
| `DisplaySettingsManager` | `display_settings_manager.h` | Time format, animations toggle |
| `InputSettingsManager` | `input_settings_manager.h` | Input/scroll settings |
| `SafetySettingsManager` | `safety_settings_manager.h` | Safety settings |
| `MaterialSettingsManager` | `material_settings_manager.h` | Preset materials |
| `LabelPrinterSettingsManager` | `label_printer_settings.h` | Label printer settings |
| `Config` † | `config.h` | JSON config, RFC 6901 pointers (`get_instance()`) |
| **Navigation & chrome** | | |
| `NavigationManager` | `ui_nav_manager.h` | Panel/overlay stack (ch. 08) |
| `ModalStack` | `ui_modal.h` | Dialog stacking |
| `KeyboardManager` | `ui_keyboard_manager.h` | Global keyboard handling |
| `NotificationManager` | `ui_notification_manager.h` | Active notifications, badge state |
| `NotificationHistory` | `ui_notification_history.h` | Notification history |
| `ToastManager` | `ui_toast_manager.h` | Toast lifecycle |
| `ScreensaverManager` | `screensaver.h` | Screensaver |
| `LockManager` | `lock_manager.h` | PIN storage, lock state, auto-lock |
| `LockScreenOverlay` | `ui_lock_screen.h` | Full-screen PIN entry |
| `FirstRunTour` | `first_run_tour.h` | First-run tour overlay |
| `EmergencyStopOverlay` | `ui_emergency_stop.h` | E-Stop overlay |
| `PrinterStatusIcon` | `ui_printer_status_icon.h` | Status icon state for XML |
| **Dev overlays** | | |
| `UiOverlayPerformance` | `ui_overlay_performance.h` | CPU/memory + per-MCU load overlay |
| `MemoryStatsOverlay` | `ui_panel_memory_stats.h` | Memory stats overlay |
| **Display & rendering** | | |
| `DisplayManager` ‡ | `display_manager.h` | LVGL display init + lifecycle |
| `ThemeManager` | `theme_manager.h` | Design tokens, breakpoints, themes |
| `LayoutManager` | `layout_manager.h` | Breakpoint detection (sm/md/lg) |
| `PrinterImageManager` | `printer_image_manager.h` | Printer model image cache |
| `ThumbnailProcessor` | `thumbnail_processor.h` | Background thumbnail pre-scaling |
| `CjkFontManager` | `cjk_font_manager.h` | CJK font loading |
| `PageScrollAutoInject` | `page_scroll_auto_inject.h` | Page-scroll chevron auto-attach |
| **Background work & caches** | | |
| `UpdateQueue` | `ui_update_queue.h` | Any-thread → main-thread bridge (ch. 02) |
| `MemoryMonitor` | `memory_monitor.h` | Memory sampling + pressure thresholds |
| `StreamingPolicy` | `streaming_policy.h` | When to use streaming operations |
| `MacroParamCache` | `macro_param_cache.h` | Macro parameter knowledge cache |
| `StandardMacros` | `standard_macros.h` | Semantic-op → printer macro mapping |
| `ThermalRateManager` | `thermal_rate_model.h` | EMA thermal heating-rate model |
| `SubjectDebugRegistry` | `subject_debug_registry.h` | Subject registry for debugging |
| `PrinterCacheRegistry` | `printer_cache_registry.h` | Per-printer cache invalidation on switch |
| **Network & remote** | | |
| `RemoteControlServer` | `remote_control_server.h` | `helix-screen ctl` Unix-socket JSON-RPC server |
| `RemotePointer` | `remote_pointer.h` | `ctl`-driven pointer input device |
| `BluetoothLoader` | `bluetooth_loader.h` | Bluetooth subsystem loader |
| **System, update & crash** | | |
| `UpdateChecker` | `system/update_checker.h` | Async release checks |
| `CrashReporter` | `system/crash_reporter.h` | Crash detection + delivery |
| `CrashHistory` | `system/crash_history.h` | Persistent crash-submission history |
| `CrashErrorLogSink` | `system/crash_error_log_sink.h` | spdlog sink capturing errors into crashes |
| `TelemetryManager` | `system/telemetry_manager.h` | Opt-in anonymous telemetry |
| `PendingStartupWarnings` | `pending_startup_warnings.h` | Warnings queued pre-UI, shown later |
| `UpgradeBanner` | `upgrade_banner.h` | Dismissible 1.0 upgrade banner |
| `UpgradeNudge` | `upgrade_nudge.h` | Upgrade nudge coordination |
| `TipsManager` | `tips_manager.h` | Printing tips |
| **Plugins** | | |
| `PluginRegistry` | `plugin_registry.h` | Service locator for plugin-to-plugin calls |
| `EventDispatcher` | `plugin_events.h` | Plugin event system |
| `InjectionPointManager` | `injection_point_manager.h` | Plugin UI injection points |
| **LED** | | |
| `LedController` | `led/led_controller.h` | LED hardware interface (5 backends) |
| `LedAutoState` | `led/led_auto_state.h` | Auto-state lighting rules |
| **Widget & lifecycle infrastructure** | | |
| `PanelWidgetManager` | `panel_widget_manager.h` | Home-widget registry + shared resources |
| `StaticSubjectRegistry` | `static_subject_registry.h` | Subject deinit ordering |
| `StaticPanelRegistry` | `static_panel_registry.h` | Panel destruction ordering |

† different spelling, same idea. ‡ `DisplayManager::instance()` returns a **pointer** (null before `Application` creates the display), unlike the reference-returning rest.

The eight singletons the old map missed entirely: `PostOpCooldownManager`, `RemoteControlServer`, `AudioSettingsManager`, `PrinterCacheRegistry`, `FilamentConsumptionTracker`, `CrashHistory`, `UpgradeBanner` — all verified `::instance()` — and `MoonrakerManager`, which is **not** `::instance()` at all (Application-owned, published pointer). The old map also listed `PrinterDetector` under capabilities; it is a static class, no instance exists.

### Registries: who cleans up what, and when

Two singletons exist to kill the others cleanly. `StaticSubjectRegistry` (`include/static_subject_registry.h:50`) holds `deinit` callbacks; `StaticPanelRegistry` (`include/static_panel_registry.h:34`) holds `destroy` callbacks. `Application::shutdown()` runs them in a fixed order — `StaticPanelRegistry::destroy_all()`, then `StaticSubjectRegistry::deinit_all()`, then `lv_deinit()` — so panels (and their observers) die before the subjects those observers point at.

Which registry a global joins is decided by what it owns, and the registration is always self-serve:

- **Subject-owning singletons** (`PrinterState`, `AmsState`, `ToolState`, `SettingsManager`, `TimelapseState`, `LedController`, `PrintControlButtons`, the sensor managers, ...) self-register `deinit_subjects()` in the last lines of their own `init_subjects()`. The registry header makes this mandatory: registration lives next to initialization so the pair cannot drift, and external registration (e.g. from `SubjectInitializer`) is called out as the fragile pattern that causes shutdown crashes.
- **Global panels and overlays** register a destroy callback (which resets their `unique_ptr`) at creation, inside the `get_global_*_panel()` accessor — that is exactly what `DEFINE_GLOBAL_PANEL` expands to. Reverse creation order destroys them while spdlog and LVGL are still alive; `clear()` wipes stale entries during soft restart (printer switch).
- **Everything else** — singletons with no LVGL subjects (`TemperatureController`, `CrashHistory`, `RemoteControlServer`, ...) — registers with neither and relies on plain destruction ordering.

Registration order is load-bearing: `SubjectInitializer` initializes `NavigationManager` **after** `PrinterState` precisely so reverse-order deinit clears NavigationManager's observers on PrinterState subjects before those subjects die (the comment at `src/application/subject_initializer.cpp:249`).

## Patterns & gotchas

- **Check the access shape before adding a `::instance()` call.** Five shapes exist (census above). In particular `get_moonraker_api()` and friends return `nullptr` early in boot — panels must tolerate that (the `DEFINE_GLOBAL_PANEL_WITH_STATE` doc notes panels fetch the API lazily, never cache it in a constructor).
- **`DisplayManager::instance()` is the odd pointer.** Reference-returning habit will write `DisplayManager::instance().foo()` and not compile — or worse, dereference without a null check before display creation.
- **Never send a heater target except through `TemperatureController::set_target()`.** Lint-enforced (`tests/shell/test_code_lint.bats`); the controller's own `->set_temperature()` is the sole sanctioned RPC.
- **Do not add `StaticSubjectRegistry` registration inside a domain class.** `PrinterState` registers once and its `deinit_subjects()` fans out to all thirteen. A second registration would deinit a domain twice.
- **`deinit_subjects()` expires the lifetime token first**, then unregisters the cache invalidator, then tears down — keep that order if you ever touch it; surviving observers depend on the token flipping before the observer lists free (ch. 03).
- **New global panel? Use the macros.** `DEFINE_GLOBAL_PANEL` / `DEFINE_GLOBAL_PANEL_WITH_STATE` (`include/ui_panel_singleton_macros.h`) get the `StaticPanelRegistry` wiring right by construction; hand-rolled globals are how shutdown crashes happen.
- **Counting rule for the census:** `rg 'static\s+\w+(&|\*)\s+instance\s*\(' include/ --glob '*.h'` → 76 at audit time. If you add singleton number 77, this chapter's count is stale — update it.

## Going deeper

- `02-subjects-dataflow.md` — the other half of this chapter: subject init macros, observer factories, the `SubjectInitializer` phases, UpdateQueue internals.
- `03-threading-lifetime.md` — the `SubjectLifetime` / `AsyncLifetimeGuard` contracts that `deinit_subjects()` and ToolState's spool callbacks rely on.
- `04-moonraker.md` — where the status JSON feeding `update_from_status()` comes from, and who owns the client/API pair.
- `../MULTI_EXTRUDER_TEMPERATURE.md` § "Chamber Heating (M141)" — chamber routing and decidegree precision; `../RPC_ERROR_OWNERSHIP.md` — who reports a failed heater send. ToolInfo and the tool lifecycle: the ToolState section above.
- `../TOOL_ABSTRACTION.md` — the ToolState deep dive: tool-to-backend mapping, DetectState semantics.
- `../MULTI_EXTRUDER_TEMPERATURE.md` — `ExtruderInfo` and dynamic extruder subjects.
- `../PRINT_STATE_MACHINE.md` — the print lifecycle state machine behind `PrinterPrintState`.

## Guided code tour

Read in this order; about 25 minutes total.

1. `include/printer_state.h:208` — the `PrinterState` class doc, then jump to `:2236` and read the thirteen domain members: plain by-value composition, no pointers, no inheritance.
2. `src/printer/printer_state.cpp:193` — `init_subjects()`: the ordered domain fan-out, and the single `StaticSubjectRegistry::register_deinit("PrinterState", ...)` at the end. Then `:130` `deinit_subjects()` for the mirror image — guard invalidation, cache unregistration, token expiry, reverse fan-out.
3. `include/printer_temperature_state.h:64` — a representative domain: 8 fixed subjects, the dynamic `ExtruderInfo` map (`:38`), and `update_from_status()` (`:88`).
4. `include/printer_motion_state.h:38` — a second domain: kinematic envelope, speed/flow, live + persisted z-offset subjects.
5. `include/tool_state.h:78` — ToolState: the five subjects, `ToolTopology` override (`:67`), `extruder_count()` vs `tool_count()` (`:124`), and `get_subjects_lifetime()` (`:199`) with its death-signal contract.
6. `include/temperature_controller.h:65` — the controller: `resolved_name()`, `keypad_range()`, `SendOptions` (`:43`), and `set_target()` (`:99`) — the one send.
7. `include/app_globals.h:76` — the published-global family: `get_temperature_controller()`, the get/set pairs, and `get_printer_state()` at `:149`.
8. `src/application/subject_initializer.cpp:467` — where the controller is constructed and registered as a `PanelWidgetManager` shared resource; scroll up to `:307` for the PrinterState init phase and its ordering comments.
9. `include/static_subject_registry.h:50` — the registry, and the header comment that makes self-registration mandatory.
10. `include/static_panel_registry.h:34` — the panel registry: reverse-order destroy, `is_destroying_all()`, `clear()` for soft restart.
11. `include/ui_panel_singleton_macros.h:74` — `DEFINE_GLOBAL_PANEL` and the `WITH_STATE` variant: the entire panel-singleton idiom in two macros.
