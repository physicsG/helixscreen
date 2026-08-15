# Architecture Guide

This document explains HelixScreen's system design, data flow patterns, and architectural decisions.

> **Visual diagrams:** See [`architecture/`](architecture/README.md) for D2 diagrams covering system overview, data flow, threading, UI layer, startup sequence, and singleton map.

## Overview

HelixScreen uses a modern, declarative approach to embedded UI development that completely separates presentation from logic:

```
XML Layout Definitions (ui_xml/*.xml)
    ↓ bind_text / bind_value / bind_flag
Reactive Subject System (lv_subject_t)
    ↓ lv_subject_set_* / copy_*
C++ Application Logic (src/*.cpp)
```

**Key Innovation:** The entire UI is defined in XML files. C++ code only handles initialization and reactive data updates—zero layout or styling logic.

## Architectural Principles

### 1. Declarative UI Definition

All layout, styling, and component structure is defined in XML:

```xml
<!-- Complete panel definition in XML -->
<component>
  <view extends="lv_obj" style_bg_color="#bg_dark" style_pad_all="20">
    <lv_label text="Nozzle Temperature" style_text_color="#text_primary"/>
    <lv_label bind_text="temp_text" style_text_font="montserrat_28"/>
  </view>
</component>
```

**Benefits:**
- UI changes don't require recompilation
- Visual designers can modify layouts without C++ knowledge
- Complete separation of concerns between presentation and logic

### 2. Reactive Data Binding

LVGL 9's Subject-Observer pattern enables automatic UI updates:

```cpp
// C++ is pure logic - zero layout code
nozzle_panel.init_subjects();
lv_xml_create(screen, "nozzle_panel", NULL);
nozzle_panel.set_temp(210);  // All bound widgets update automatically
```

**Benefits:**
- No manual widget searching or updating
- Type-safe data updates
- One update propagates to multiple UI elements
- Clean separation between data and presentation

### 3. Custom HelixScreen Theme

HelixScreen uses a custom LVGL theme that wraps the default theme for enhanced styling:

**Architecture:** XML → C++ → Custom Theme → LVGL Default Theme

```xml
<!-- ui_xml/globals.xml - Single source of truth for theme values -->
<consts>
  <color name="primary_color" value="..."/>
  <color name="secondary_color" value="..."/>

  <!-- Theme-specific color variants for light/dark mode -->
  <color name="app_bg_color_light" value="..."/>
  <color name="app_bg_color_dark" value="..."/>
  <color name="card_bg_light" value="..."/>
  <color name="card_bg_dark" value="..."/>
  <color name="text_primary_light" value="..."/>
  <color name="text_primary_dark" value="..."/>

  <str name="font_body" value="..."/>
  <str name="font_heading" value="..."/>
</consts>
```

```cpp
// src/helix_theme.c - Custom theme wrapper
static void helix_theme_apply_cb(lv_theme_t* theme, lv_obj_t* obj) {
    // Apply default theme first
    lv_theme_apply(helix_theme->default_theme, obj);

    // Override input widgets with computed background color
    if(lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &helix_theme->input_bg_style, 0);
    }
    // Similar for dropdown, roller, spinbox...
}
```

```cpp
// src/ui/ui_theme.cpp - Initializes custom theme
void ui_theme_init(lv_display_t* display, bool dark_mode) {
    // Read colors from XML (NO hardcoded colors!)
    lv_color_t card_bg = parse_color(dark_mode ? card_bg_dark : card_bg_light);

    // Initialize custom HelixScreen theme (wraps default)
    lv_theme_t* theme = helix_theme_init(display, primary, secondary,
                                          dark_mode, font, screen_bg, card_bg, grey);
    lv_display_set_theme(display, theme);
}
```

**Key Features:**
- ✅ **No recompilation needed** - Edit `globals.xml` to change theme colors
- ✅ **Automatic styling** - Input widgets get computed backgrounds automatically
- ✅ **Computed colors** - Input backgrounds are lighter/darker than cards based on mode
- ✅ **Responsive** - Scales padding/sizing for different screen resolutions
- ✅ **Maintainable** - Uses LVGL public API, no fragile private structure patching
- ✅ **Dark/Light mode** - Runtime theme switching support
- ✅ **State-based styling** - Automatic pressed/disabled/checked states

**Theme Customization:**
- **Colors:** `primary_color`, `secondary_color`, `text_primary`, `text_muted` defined in globals.xml
- **Fonts:** `font_heading`, `font_body`, `font_small` for manual widget styling when needed
- **Mode:** Dark/light mode controlled via config file or command-line flags

**Config Persistence:**
Theme preference saved to `settings.json` and restored on next launch:
```json
{
  "dark_mode": true,
  ...
}
```

## Namespace Organization

All HelixScreen code lives under the `helix::` namespace:

| Namespace | Contents | Example |
|-----------|----------|---------|
| `helix::` | Core singletons, state, managers | `helix::PrinterState`, `helix::Config` |
| `helix::ui::` | UI functions, update queue | `helix::ui::queue_update()` |

**Rules:**
- **No `using` declarations in headers** — always use fully-qualified names in `.h` files
- **`using namespace helix;`** is acceptable in `.cpp` files only
- **Enum classes** — all enums use `enum class` within `helix::` (e.g., `helix::PanelId`, `helix::PrintState`)
- **JSON alias** — consolidated in `json_fwd.h` (`#include "json_fwd.h"` for forward declaration)

## ⚠️ CRITICAL: Reactive-First Principle - "The HelixScreen Way"

**ALL UI control MUST be reactive via subjects. Direct widget manipulation is an anti-pattern.**

### ✅ The Correct Way: Reactive UI Control

Control UI elements by updating subjects in C++, binding to them in XML:

```cpp
// C++ - Pure data updates, zero widget manipulation
lv_subject_t connection_test_passed;
lv_subject_init_int(&connection_test_passed, 0);  // Button starts disabled

// Later: Update subject when connection succeeds
lv_subject_set_int(&connection_test_passed, 1);  // Button becomes enabled automatically
```

```xml
<!-- XML - Reactive bindings control UI state -->
<lv_button name="next_button">
  <!-- Button automatically updates when subject changes -->
  <lv_obj-bind_flag_if_eq subject="connection_test_passed" flag="clickable" ref_value="0" negate="true"/>
  <lv_obj-bind_flag_if_eq subject="connection_test_passed" flag="user_1" ref_value="0"/>
</lv_button>
<lv_style selector="LV_STATE_USER_1" style_opa="128"/>  <!-- Disabled style -->
```

**Benefits:**
- ✅ UI automatically stays in sync with application state
- ✅ Zero manual widget searching/updating
- ✅ Testable - can verify subject values without UI
- ✅ Reusable - multiple widgets can bind to same subject

###  ❌ ANTI-PATTERN: Direct Widget Manipulation

**DO NOT** search for widgets by name/ID and manipulate them from C++:

```cpp
// ❌ WRONG - Direct widget manipulation (ANTI-PATTERN)
lv_obj_t* button = lv_obj_find_by_name(screen, "next_button");
lv_obj_add_state(button, LV_STATE_DISABLED);      // Manual state management
lv_obj_set_style_opa(button, 128, 0);              // Manual styling

// ❌ WRONG - Searching for labels to update text
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");
lv_label_set_text(label, "210°C");                 // Manual text update
```

**Why this is wrong:**
- ❌ Couples C++ code to specific widget names/structure
- ❌ Breaks if XML layout changes
- ❌ Difficult to test (requires UI to exist)
- ❌ Fragile - easy to forget updates, cause inconsistent state
- ❌ Violates separation of concerns

### Reactive Patterns for Common UI Tasks

| UI Task | ❌ Anti-Pattern | ✅ Reactive Way |
|---------|----------------|----------------|
| Update text | `lv_label_set_text(label, "...")` | `bind_text` in XML, `lv_subject_set_string()` in C++ |
| Enable/disable | `lv_obj_add_state(obj, DISABLED)` | `bind_flag_if_eq` in XML, update subject in C++ |
| Show/hide | `lv_obj_add_flag(obj, HIDDEN)` | `bind_flag_if_eq` for `hidden` flag |
| Update value | `lv_slider_set_value(slider, val)` | `bind_value` in XML, `lv_subject_set_int()` in C++ |
| Visual feedback | Manual style changes | `bind_flag_if_eq` + conditional styles |

### When Direct Access IS Acceptable

The ONLY acceptable use of `lv_obj_find_by_name()` is during **initialization** for special cases:

```cpp
// ✅ OK - One-time initialization during panel setup
void ui_panel_init() {
    lv_obj_t* dropdown = lv_obj_find_by_name(panel, "hardware_dropdown");
    ui_dropdown_populate(dropdown, get_available_hardware());  // One-time setup
}
```

**After initialization, ALL updates must be reactive.**

## Component Hierarchy

```
app_layout.xml
├── navigation_bar.xml      # 6-button vertical navigation
└── content_area            # 6 panels + 25+ overlays (see ui_xml/*.xml)
```

**Design Patterns:**
- **App Layout** - Root container with navigation + content area
- **Panel Components** - Self-contained UI screens with reactive data
- **Sub-Panel Overlays** - Motion/temp controls that slide over main content
- **Global Navigation** - Persistent 6-button navigation bar

## ⚠️ PREFERRED: Class-Based Architecture

**All new code should use class-based patterns.** This applies to:
- **UI panels** (PanelBase for main panels, OverlayBase for overlays)
- **Modals** (Modal)
- **Backend managers** (WiFiManager, EthernetManager, MoonrakerClient)
- **Domain state classes** (Printer*State decomposition)
- **Services and utilities**

### Why Class-Based?

| Benefit | Description |
|---------|-------------|
| **RAII** | Resources acquired in constructor, released in destructor - no leaks |
| **Encapsulation** | State and behavior together, clear ownership |
| **Testability** | Mock via interface inheritance, isolate dependencies |
| **Lifecycle** | Explicit init/start/stop/destroy - no hidden state |

**For implementation examples, see [DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md#class-patterns).**

### Panel & Overlay Base Classes

All main panels inherit from `PanelBase`, overlays from `OverlayBase`:

```cpp
class MyPanel : public PanelBase {
public:
    MyPanel();
    ~MyPanel() override;

    void init_subjects() override;   // Create subjects + self-register cleanup
    void setup(lv_obj_t* panel, lv_obj_t* parent) override;
    void on_activate() override;
    void on_deactivate() override;

private:
    lv_subject_t my_subject_{};
    char buf_[128]{};         // Static storage for string subjects
};
```

**Key features:**
- Two-phase init: `init_subjects()` first, then `setup()` after XML creation
- `register_observer()` tracks all observers via `ObserverGuard` for RAII cleanup
- Lifecycle hooks: `on_activate()` / `on_deactivate()` called by `NavigationManager`
- Dependency injection: receives `PrinterState&` and `MoonrakerAPI*`
- Self-registration: `init_subjects()` MUST register cleanup with `StaticSubjectRegistry`

> **`on_deactivate()` vs `cleanup()` — put dismiss-time teardown in `on_deactivate()`.**
> `NavigationManager::go_back()` calls **`on_deactivate()`** when an overlay is dismissed —
> it does **not** call `cleanup()`. For a persistent/singleton overlay (one created once and
> re-shown, registered via `StaticPanelRegistry`), `cleanup()` runs only at app shutdown.
> So any side effect a `show()` turns on and must turn back off when the user leaves
> (re-enabling a temporarily disabled input transform, restoring global state, releasing a
> resource) belongs in `on_deactivate()`. Putting it only in `cleanup()` means it never runs
> on a normal dismiss and the state stays wrong for the rest of the session. This was the root
> cause of #943: the touch-calibration overlay disabled the affine transform in `show()` but
> re-enabled it only in `cleanup()`, so an aborted recalibration left touch uncalibrated until
> the next reboot. Make `on_deactivate()` the authoritative teardown and keep `cleanup()`
> idempotent so it's safe if both run.

### ❌ AVOID: Function-Based Patterns

The old C-style wrapper APIs (`ui_panel_*_init()`, `ui_panel_*_show()`) have been **removed**. All panels and overlays use class-based patterns:

```cpp
// ✅ Current pattern - class-based with lifecycle
auto& motion = MotionPanel::instance();
motion.init_subjects();
// ... XML creation ...
motion.on_activate();
```

## Domain Decomposition: PrinterState

The central `PrinterState` class was decomposed into 13 focused domain classes, each owning LVGL subjects for a specific concern. `PrinterState` delegates to these via composition, keeping its public API but distributing the implementation.

```
PrinterState (orchestrator)
├── PrinterTemperatureState      # Nozzle, bed, chamber temps + targets
│   └── ExtruderInfo[]           # Per-extruder: name, temp/target subjects (heap-allocated)
├── PrinterMotionState           # Position, speed, homed axes
├── PrinterFanState              # Fan speeds, types
├── PrinterPrintState            # Print progress, filename, layers, ETA
├── PrinterCalibrationState      # PID, Z-offset, bed mesh status
├── PrinterCapabilitiesState     # QGL, probe, firmware features
├── PrinterExcludedObjectsState  # Object exclusion during prints
├── PrinterNetworkState          # WiFi, Ethernet, hostname
├── PrinterVersionsState         # Klipper, MCU, Moonraker versions
├── PrinterLedState              # LED controls and effects
├── PrinterHardwareValidationState  # Hardware health checks
├── PrinterPluginStatusState     # Plugin system state
└── PrinterCompositeVisibilityState # UI visibility rules
```

### Why Domain Decomposition?

| Concern | Before (God Class) | After (Domains) |
|---------|---------------------|------------------|
| **Lines** | 1514 in one file | ~100-200 per domain |
| **Subjects** | All mixed together | Grouped by concern |
| **Testing** | Hard to isolate | Test each domain independently |
| **Threading** | One big mutex | Domain-scoped thread safety |
| **Navigation** | Scroll 1500 lines | Find the right 150-line file |

### Domain Class Pattern

Each domain class follows a consistent structure:

```cpp
class PrinterTemperatureState {
public:
    void init_subjects();                     // Initialize LVGL subjects
    void deinit_subjects();                   // Clean shutdown
    void reset_for_testing();                 // Reset for unit tests

    // Subject accessors (for binding in XML or observers)
    lv_subject_t* nozzle_temp_subject();
    lv_subject_t* bed_temp_subject();

    // Setters (called from WebSocket thread via helix::ui::queue_update)
    void set_nozzle_temp(int temp);
    void set_bed_temp(int temp);

private:
    lv_subject_t nozzle_temp_{};
    lv_subject_t bed_temp_{};
    bool initialized_ = false;
};
```

**Key rules:**
- Domain classes own their subjects (init/deinit lifecycle)
- `PrinterState` forwards public API calls to the appropriate domain
- Callers never need to know about domain classes directly
- Each domain registers with `StaticSubjectRegistry` for shutdown cleanup

**Files:** `include/printer_*_state.h`, `src/printer/printer_*_state.cpp`

### ToolState Singleton

`ToolState` (`include/tool_state.h`) manages tool information for multi-tool printers (tool changers, multi-extruder setups). It is a standalone singleton, separate from `PrinterState`, because tool tracking spans both temperature and filament management domains.

**Lifecycle:**

```
init_subjects()                    Register LVGL subjects (active_tool, tool_count, tools_version)
    ↓
init_tools(PrinterDiscovery)       Populate ToolInfo vector from discovered "tool T*" objects
    ↓
update_from_status(json)           Called on each Moonraker status update (tool states, offsets)
    ↓
deinit_subjects()                  Clean shutdown before lv_deinit()
```

**Key types:**

```cpp
enum class DetectState { PRESENT, ABSENT, UNAVAILABLE };

struct ToolInfo {
    int index;                          // Tool number (0, 1, 2, ...)
    std::string name;                   // "T0", "T1", etc.
    std::optional<std::string> extruder_name;  // Associated extruder
    std::optional<std::string> heater_name;    // Override heater
    float gcode_x_offset, gcode_y_offset, gcode_z_offset;
    bool active, mounted;
    DetectState detect_state;
    int backend_index;                  // AMS backend index (-1 = direct drive)
    int backend_slot;                   // Slot in that backend (-1 = dynamic)
};
```

**Subjects:**

| Subject | Type | Description |
|---------|------|-------------|
| `active_tool` | int | Currently active tool index (0-based) |
| `tool_count` | int | Number of discovered tools |
| `tools_version` | int | Bumped when tool list changes (UI rebuild trigger) |

**Single-tool fallback:** On non-toolchanger printers, `ToolState` holds a single implicit T0 entry. UI code can always query `ToolState::tool_count()` to decide whether to show multi-tool controls.

**Files:** `include/tool_state.h`, `src/printer/tool_state.cpp`

---

### Centralized Temperature Sends: TemperatureController

**Every nozzle / bed / chamber target update routes through one `helix::TemperatureController` (`include/temperature_controller.h`, `src/ui/temperature_controller.cpp`).** It is the single authority for heater target control, so the heater-send logic lives in exactly one place instead of being copy-pasted across every panel, overlay, and widget that can set a temperature.

The controller:

- **Resolves Klipper heater object names** — `Nozzle` → the active extruder, `Bed` → `"heater_bed"`, `Chamber` → the discovered chamber heater name (never the bare default). Callers pass a `HeaterType`, not a raw object name.
- **Applies configured-max limits** — fetches `configfile` `max_temp` per heater via `ensure_limits()` and exposes the effective keypad input range (`keypad_range()`) and preset visibility (`preset_visible()`).
- **Owns the preset model** — Off / PLA / PETG / ABS targets derived from the filament database (same derivation as `temperature_service.cpp`), so views reading presets from the controller match what the service produced.
- **Provides standard error toasts** — on a failed RPC (or a heater that resolves empty), it emits the standard `NOTIFY_ERROR` toast and fires optional `on_success` / `on_error` hooks. Toasts are suppressible (`SendOptions{.toast = false}`) for silent sends like AMS slot-preheat / cooldown. It holds no LVGL widgets or subjects — it uses the `NOTIFY_*` notification system, so the logic is unit-testable.

**The one send:** `set_target(HeaterType, celsius, opts)` (or the explicit-name overload), plus `apply_material(nozzle, bed, chamber, opts)` for a material profile in a single call.

**Accessor and ownership:** Reach the controller via `get_temperature_controller()` (`include/app_globals.h`). It is **not** a classic `::instance()` singleton — `SubjectInitializer` owns the `unique_ptr` and registers a raw pointer as a shared resource on `PanelWidgetManager`; the accessor looks it up via `PanelWidgetManager::shared_resource<TemperatureController>()` and returns `nullptr` before init.

> **⚠️ MANDATORY:** New temperature-setting UI must call `TemperatureController::set_target()` — **never** the raw `MoonrakerAPI::set_temperature()`. The migrated view files (`ui_overlay_temp_graph.cpp`, `ui_panel_controls.cpp`, and others) are **lint-enforced** by `tests/shell/test_code_lint.bats`: the gate fails the build on any `api_->set_temperature(` call, or any `->set_temperature(` call whose receiver is not a `controller`. The controller's own `->set_temperature()` is the sole sanctioned send.

For the chamber-specific details — M141 routing, decidegree precision, and the `chamber_effective_target` / `chamber_mode` synthesis — see **[MULTI_EXTRUDER_TEMPERATURE.md](MULTI_EXTRUDER_TEMPERATURE.md)** § "Chamber Heating (M141)".

**Files:** `include/temperature_controller.h`, `src/ui/temperature_controller.cpp`, `src/application/subject_initializer.cpp` (ownership), `src/app_globals.cpp` (accessor)

---

## Panel Widget System

The home panel exposes a row of modular "widgets" — small cards that each display one aspect of printer state: fan speeds, temperatures, LED, power, network, thermistors, and more. Each widget is a self-contained C++ object that owns its own XML component and observer lifecycle.

### What Are PanelWidgets?

PanelWidgets live in `src/ui/panel_widgets/` (implementations) and headers alongside each `.cpp`. Current widgets:

| Widget ID | Class | Displays |
|-----------|-------|---------|
| `fan_stack` | `FanStackWidget` | Part / hotend / aux fan speeds with spinning icon animations |
| `temp_stack` | `TempStackWidget` | Nozzle and bed temperatures |
| `temperature` | `TemperatureWidget` | Single temperature display |
| `thermistor` | `ThermistorWidget` | Temperature sensor readings |
| `led` | `LedWidget` | LED on/off toggle with brightness-reactive icon |
| `power_device` | `PowerDeviceWidget` | Power device toggle |
| `network` | `NetworkWidget` | Network connection status |
| `active_spool` | `ActiveSpoolWidget` | Currently loaded Spoolman spool — color, material, brand, weight |

Widgets that are pure XML data binding (filament, probe, humidity, etc.) do NOT need a `PanelWidget` subclass — they work via subject bindings defined in their XML component alone.

### PanelWidget Base Class

`include/panel_widget.h` defines the interface:

```cpp
class PanelWidget {
public:
    virtual void init_subjects() {}       // Create subjects before lv_xml_create()
    virtual void set_config(const nlohmann::json& config) {}  // Per-widget config
    virtual std::string get_component_name() const;           // XML component to create
    virtual void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) = 0;
    virtual void detach() = 0;            // Reset observers, clear pointers
    virtual bool supports_reuse() const { return true; }   // Instance survives rebuilds
    virtual void on_activate() {}         // Panel became visible
    virtual void on_deactivate() {}       // Panel went offscreen
    virtual void on_size_changed(int colspan, int rowspan, int width_px, int height_px) {}  // Adapt to cell size
    virtual const char* id() const = 0;  // Stable widget ID string
};
```

### Tile Marking and the User-Flag Ledger

Every widget tile root is tagged with `helix::PANEL_WIDGET_TILE_FLAG`
(`include/panel_widget.h`), set at the one place a tile is created in
`src/ui/panel_widget_manager.cpp`, immediately after `lv_obj_set_name()`. One
set site is enough: the reuse map recycles `PanelWidget` C++ instances, not LVGL
objects, and the object tree is always cleaned and rebuilt.

The mark exists so that tree walks which only make sense at page level can stop
at a tile. Its first consumer is `PageScrollAutoInject` - see
[PAGE_SCROLL_BUTTONS.md](PAGE_SCROLL_BUTTONS.md) for why a chevron gutter inside
a grid-sized tile is the wrong affordance at the wrong scale.

LVGL gives the application four user flag bits. Three are claimed. **Check this
table before taking the fourth:**

| Flag | Owner | Meaning |
|------|-------|---------|
| `LV_OBJ_FLAG_USER_1` | `src/ui/ui_dialog.cpp` | "inside a dialog", read by `theme_manager.cpp` for elevated-surface input styling |
| `LV_OBJ_FLAG_USER_2` | *free* | reachable from XML, so prefer it for anything a binding should toggle |
| `LV_OBJ_FLAG_USER_3` | `include/panel_widget.h` | `PANEL_WIDGET_TILE_FLAG`, home widget tile root |
| `LV_OBJ_FLAG_USER_4` | `src/ui/ui_sound_preview_overlay.cpp` | suppress the button tap sound, read in `ui_button.cpp` |

`USER_3` was chosen over `USER_2` deliberately. helix-xml's `flag_to_enum()`
maps `user_1` and `user_2` for `<bind_flag_if_*>` but stops there, so `USER_3`
is the bit XML cannot reach and therefore cannot clear by accident.

> **A flag is a global namespace. Treat a spare bit as a last resort.**
> `src/ui/ui_ams_detail.cpp` once reused `USER_1` as a private "draw callback
> already attached" guard on the AMS slot grid. Because the theme walk in
> `theme_manager.cpp` looks up the parent chain for `USER_1` to answer "am I
> inside a dialog", that made an AMS slot grid read as a dialog, and any input
> placed inside it would have quietly picked up dialog styling. The guard is now
> an idempotent `lv_obj_remove_event_cb()` before `lv_obj_add_event_cb()`, which
> needs no bit at all.
>
> Before claiming `USER_2`, check whether you need a flag. An "already did this
> once" guard usually does not: removing the callback before adding it is
> idempotent by construction, and it cannot collide with anyone.

### Widget Factory Pattern

Each widget registers a factory function at startup via `register_widget_factory()`. The registry (`include/panel_widget_registry.h`) pairs an ID string with a factory lambda:

```cpp
// From fan_stack_widget.cpp — called once at startup
void register_fan_stack_widget() {
    register_widget_factory("fan_stack", []() {
        auto& ps = get_printer_state();
        return std::make_unique<FanStackWidget>(ps);
    });

    // XML event callbacks must be registered before any XML is parsed
    lv_xml_register_event_cb(nullptr, "on_fan_stack_clicked", FanStackWidget::on_fan_stack_clicked);
}
```

`PanelWidgetManager` (`include/panel_widget_manager.h`) is the central coordinator:
- `init_widget_subjects()` — calls each widget's `init_subjects()` before XML creation
- `populate_widgets(panel_id, container, reuse={})` — creates XML components and calls `attach()` for each enabled widget; accepts an optional `WidgetReuseMap` to reuse existing C++ instances
- `setup_gate_observers(panel_id, rebuild_cb)` — observes hardware availability subjects; rebuilds the widget row when capabilities change
- `notify_config_changed(panel_id)` — triggers a rebuild after config changes (e.g., widget reorder)

### HomePanel Integration

`HomePanel::populate_widgets()` (`src/ui/ui_panel_home.cpp`) delegates entirely to `PanelWidgetManager`:

```cpp
void HomePanel::populate_widgets() {
    lv_obj_t* container = lv_obj_find_by_name(panel_, "widget_container");

    // Extract reusable instances before destroying LVGL tree
    WidgetReuseMap reuse;
    for (auto& w : active_widgets_) {
        w->detach();
        if (w->supports_reuse())
            reuse[w->id()] = std::move(w);
    }
    lv_obj_clean(container);
    active_widgets_.clear();

    // Manager reuses existing instances or creates new ones via factory
    active_widgets_ = PanelWidgetManager::instance().populate_widgets(
        "home", container, std::move(reuse));
}
```

Gate observers call `populate_widgets()` automatically when hardware capabilities or klippy state change, so the widget row adapts to the connected printer without any manual dispatch.

### Widget Instance Reuse Across Rebuilds

When gate observers trigger a rebuild, the LVGL widget tree is destroyed and recreated. However, PanelWidget **C++ instances** are preserved across rebuilds to avoid stopping and restarting expensive resources (e.g., camera MJPEG streams).

**How it works:**

1. `HomePanel::populate_widgets()` calls `detach()` on all active widgets, then extracts instances that return `true` from `supports_reuse()` into a `WidgetReuseMap`
2. The LVGL tree is destroyed (`lv_obj_clean`), and non-reusable C++ instances are destroyed
3. `PanelWidgetManager::populate_widgets()` receives the reuse map. For each widget, it checks the map first before invoking the factory — reused instances skip allocation entirely
4. Reused instances get `attach()` called with the fresh LVGL objects from the new XML tree

**Contract for reusable widgets:**

- `supports_reuse()` returns `true` (the default; override to `false` to opt out)
- `detach()` is lightweight: clears LVGL pointers and observers only. Does NOT destroy expensive state
- The destructor handles full cleanup (stream stop, alive guard invalidation, etc.)
- `attach()` must work correctly on a previously-detached instance

**Thread safety during the detach→reattach gap:**

Background threads (e.g., camera stream) may still deliver callbacks. The pattern is:
- LVGL pointers are null after `detach()`, so queued `ui_queue_update` callbacks that check `camera_image_` etc. become safe no-ops
- Alive guards remain valid (not invalidated in `detach()`), so `weak_ptr` locks succeed but find null LVGL pointers
- After `attach()`, the next callback finds valid LVGL pointers and resumes normal operation

```cpp
// In HomePanel::populate_widgets():
WidgetReuseMap reuse;
for (auto& w : active_widgets_) {
    w->detach();
    if (w->supports_reuse())
        reuse[w->id()] = std::move(w);
}
// ... drain queue, destroy LVGL tree ...
active_widgets_ = mgr.populate_widgets("home", container, std::move(reuse));
```

### Version-Observer Self-Binding Pattern

The key architectural pattern used by interactive widgets: instead of HomePanel notifying each widget when hardware discovery completes, widgets observe a **version subject** that bumps whenever the relevant hardware list changes. On each bump, the widget calls its `bind_*()` method to reset and recreate all per-item observers.

**Why this approach:**
- Widgets are fully self-contained — no external dispatch needed
- The version subject fires immediately on `observe_int_sync()` registration, giving an initial bind with no special init path
- Reconnection / rediscovery automatically re-binds to the new hardware

**FanStackWidget example:**

```cpp
void FanStackWidget::attach_stack(lv_obj_t* /*widget_obj*/) {
    // ...cache label pointers...

    // Observe fans_version to re-bind when fans are discovered or change
    version_observer_ = helix::ui::observe_int_sync<FanStackWidget>(
        printer_state_.get_fans_version_subject(), this,
        [weak_alive](FanStackWidget* self, int /*version*/) {
            if (weak_alive.expired()) return;
            self->bind_fans();  // Reset observers, read current fans, create new observers
        });
}

void FanStackWidget::bind_fans() {
    // 1. Reset existing per-fan observers
    part_observer_.reset();
    hotend_observer_.reset();
    aux_observer_.reset();

    // 2. Read current hardware config
    const auto& fans = printer_state_.get_fans();
    if (fans.empty()) return;

    // 3. Create new per-fan observers using SubjectLifetime for dynamic subject safety
    SubjectLifetime lifetime;
    lv_subject_t* subject = printer_state_.get_fan_speed_subject(part_fan_name_, lifetime);
    part_observer_ = helix::ui::observe_int_sync<FanStackWidget>(
        subject, this, [weak_alive](FanStackWidget* self, int speed) {
            if (weak_alive.expired()) return;
            self->update_label(self->part_label_, speed);
        }, lifetime);
}
```

**LedWidget example** — observes `led_config_version_` on `LedController`:

```cpp
void LedWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    // ...

    // Fires immediately on add, triggering initial bind_led() — no separate init call needed
    led_version_observer_ = helix::ui::observe_int_sync<LedWidget>(
        led_ctrl.get_led_config_version_subject(), this,
        [weak_alive](LedWidget* self, int /*version*/) {
            if (weak_alive.expired()) return;
            self->bind_led();
        });
}
```

**PowerDeviceWidget** uses the same pattern but observes `power_device_count` instead of a version counter — the count itself changes whenever devices are discovered.

The old pattern (removed) was a `HomePanel::reload_from_config()` dispatch loop that called `PanelWidget::reload_from_config()` on each widget. The version-observer pattern replaced it: each widget is responsible for its own rebinding.

---

## Data Flow Architecture

### Subject Initialization Pattern

**Critical:** Subjects must be initialized BEFORE creating XML:

```cpp
// 1. Register XML components
lv_xml_register_component_from_file("A:/ui_xml/globals.xml");
lv_xml_register_component_from_file("A:/ui_xml/home_panel.xml");

// 2. Initialize subjects (BEFORE XML creation) — each self-registers cleanup
helix::NavigationManager::instance().init_subjects();
home_panel.init_subjects();

// 3. NOW create UI - bindings will find initialized subjects
lv_xml_create(screen, "app_layout", NULL);
```

**Why this order matters:**
- XML bindings look up subjects by name during creation
- If subjects don't exist, bindings fail silently with empty values
- C++ initialization creates subjects with proper default values

### Reactive Update Flow

```cpp
// Business logic updates subject
lv_subject_set_string(temp_text_subject, "210°C");

// LVGL automatically:
// 1. Notifies all observers (bound widgets)
// 2. Updates widget properties (text, values, flags)
// 3. Triggers redraws as needed

// Zero manual widget management required
```

### Event Handling Pattern

XML defines event bindings, C++ implements handlers:

```xml
<!-- XML: Declarative event binding -->
<lv_button>
    <event_cb trigger="clicked" callback="on_temp_increase"/>
</lv_button>
```

```cpp
// C++: Pure business logic
void on_temp_increase(lv_event_t* e) {
    int current = get_target_temp();
    set_target_temp(current + 5);

    // UI updates automatically via subject binding
    lv_subject_set_int(temp_target_subject, current + 5);
}
```

## Memory Management

### Subject Lifecycle

- **Creation:** During each class's `init_subjects()` method (e.g., `PrinterState::init_subjects()`, `HomePanel::init_subjects()`)
- **Lifetime:** Persistent throughout application runtime (static subjects) or until hardware changes (dynamic subjects — see below)
- **Updates:** Via `lv_subject_set_*()` functions from the main thread only; background threads must use `helix::ui::queue_update()`
- **Cleanup:** Each `init_subjects()` self-registers its `deinit_subjects()` with `StaticSubjectRegistry` or `StaticPanelRegistry`; explicit cleanup runs before `lv_deinit()`

### Dynamic Subject Safety (SubjectLifetime)

Per-fan, per-sensor, and per-extruder subjects are **dynamic** — they are destroyed and
recreated when hardware is rediscovered after a disconnect/reconnect. Observing one without a
`SubjectLifetime` token is a use-after-free: `lv_subject_deinit()` frees the subject's observer
list, but `ObserverGuard` still holds a pointer into it.

What matters is that the token is **passed to the `observe_*` factory** — its `lifetime`
parameter defaults to `{}`, so omitting it compiles silently and leaves the guard with no way
to learn the subject died. Whether the token itself lives in a local or a member does not
decide correctness: the accessors hand out a copy of a token the *owner* keeps, and death is
signalled by writing `*lifetime = false`, not by the refcount reaching zero. See
[`THREADING.md`](THREADING.md) §5 for the full reasoning, the dynamic-subject source table, the
collection pattern, and when declaration order does start to matter. Real usage:
`FanStackWidget::bind_fan_observer()` in `src/ui/panel_widgets/fan_stack_widget.cpp`.

### Klippy-Volatile Subjects

Moonraker sends **delta** status updates — changed fields only. A subject fed by a delta-only
field keeps its last value across a Klipper restart, because the field is simply absent from
later payloads until it next changes. When such a stale value *gates behaviour*, it is a live
bug: a cached `idle_timeout.state == "Printing"` from mid-`G28` made the app treat a
freshly-restarted, idle printer as busy and wedge the LED in-flight counter for a whole
session (#1129).

Declare those subjects with `INIT_SUBJECT_INT_VOLATILE` rather than `INIT_SUBJECT_INT`
(`include/state/subject_macros.h`); `PrinterState::set_klippy_state_internal()` — the single
chokepoint for every Klippy state change — resets them on a genuine transition, in both
directions. See [`THREADING.md`](THREADING.md) §5 for the membership rules, why the reset is
edge-triggered rather than a live `klippy != READY` predicate, and why `motors_enabled` is
deliberately excluded. Real usage: `src/printer/printer_calibration_state.cpp`.

### Widget Management

- **Creation:** Automatic during `lv_xml_create()`
- **Lifetime:** Managed by LVGL parent-child hierarchy
- **Updates:** Automatic via subject-observer bindings
- **Cleanup:** Automatic when parent objects are deleted

### LVGL Memory Patterns

LVGL uses automatic memory management:
- Widget memory allocated during creation
- Parent widgets automatically free child widgets
- No manual `free()` calls needed for UI elements
- Use LVGL's built-in reference counting for shared resources

### ⚠️ **REQUIRED:** RAII for Custom Widget Memory

**MANDATORY PATTERN for all custom widgets that allocate memory:**

Custom widgets must use RAII (Resource Acquisition Is Initialization) for exception-safe memory management. Manual `lv_malloc/lv_free` is **forbidden** due to leak risks from exceptions or early returns.

**Required header:** `#include "ui_widget_memory.h"`

**Pattern 1: Widget user_data (most common):**

```cpp
// Widget state structure
struct MyWidgetState {
    int value;
    lv_obj_t* button;
};

// ✅ REQUIRED: Delete callback uses RAII wrapper
static void my_widget_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    // Transfer ownership to RAII wrapper - automatic cleanup
    lvgl_unique_ptr<MyWidgetState> state(
        (MyWidgetState*)lv_obj_get_user_data(obj)
    );
    lv_obj_set_user_data(obj, nullptr);

    // Even if cleanup code throws exceptions, state is freed
    cleanup_resources();
}

// ✅ REQUIRED: Widget creation uses RAII helper
lv_obj_t* my_widget_create(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    if (!obj) return nullptr;

    // Allocate using RAII helper
    auto state_ptr = lvgl_make_unique<MyWidgetState>();
    if (!state_ptr) {
        lv_obj_delete(obj);
        return nullptr;
    }

    // Get raw pointer for initialization
    MyWidgetState* state = state_ptr.get();
    state->value = 0;
    state->button = nullptr;

    // Transfer ownership to LVGL widget
    lv_obj_set_user_data(obj, state_ptr.release());

    // Register cleanup
    lv_obj_add_event_cb(obj, my_widget_delete_cb, LV_EVENT_DELETE, nullptr);

    return obj;
}
```

**Pattern 2: Standalone widget structures (e.g., ui_temp_graph):**

```cpp
// ✅ REQUIRED: Creation returns unique_ptr ownership
ui_temp_graph_t* ui_temp_graph_create(lv_obj_t* parent) {
    auto graph_ptr = std::make_unique<ui_temp_graph_t>();
    if (!graph_ptr) return nullptr;

    // Initialize...
    graph_ptr->chart = lv_chart_create(parent);
    if (!graph_ptr->chart) {
        return nullptr; // graph_ptr auto-freed
    }

    // Transfer ownership to caller
    return graph_ptr.release();
}

// ✅ REQUIRED: Destruction uses RAII wrapper
void ui_temp_graph_destroy(ui_temp_graph_t* graph) {
    if (!graph) return;

    // Transfer ownership to RAII wrapper
    std::unique_ptr<ui_temp_graph_t> graph_ptr(graph);

    // Cleanup LVGL widgets
    if (graph_ptr->chart) {
        lv_obj_del(graph_ptr->chart);
    }

    // graph_ptr automatically freed via ~unique_ptr()
}
```

**Pattern 3: Nested allocations (e.g., ui_step_progress):**

```cpp
struct WidgetData {
    char** label_buffers;  // Array of strings
    int count;
};

// Allocate nested arrays using RAII
auto data_ptr = lvgl_make_unique<WidgetData>();
auto label_buffers_ptr = lvgl_make_unique_array<char*>(count);

// Initialize...
data_ptr->label_buffers = label_buffers_ptr.get();
data_ptr->count = count;

for (int i = 0; i < count; i++) {
    auto label = lvgl_make_unique_array<char>(128);
    data_ptr->label_buffers[i] = label.release();
}

// Release ownership
label_buffers_ptr.release();
lv_obj_set_user_data(obj, data_ptr.release());
```

**Why RAII is mandatory:**
1. **Exception Safety:** If code between malloc and free throws, memory leaks
2. **Early Returns:** Manual free is skipped if function returns early
3. **Maintenance:** RAII is self-documenting and enforces cleanup
4. **Future-Proof:** Adding exception-throwing code won't introduce leaks

**❌ FORBIDDEN ANTI-PATTERN:**

```cpp
// ❌ WRONG: Manual malloc/free is NOT ALLOWED
lv_obj_t* my_widget_create(lv_obj_t* parent) {
    MyWidgetState* state = (MyWidgetState*)lv_malloc(sizeof(MyWidgetState));
    if (!state) return nullptr;

    // If this throws exception, state leaks!
    do_something_that_might_throw();

    lv_obj_set_user_data(obj, state);
    return obj;
}

static void my_widget_delete_cb(lv_event_t* e) {
    MyWidgetState* state = ...;

    // If this throws exception, state leaks!
    cleanup_resources();

    lv_free(state);  // ❌ Never reached if exception thrown
}
```

**See also:** `include/ui_widget_memory.h` for full API documentation

**Examples:**
- `src/ui/ui_jog_pad.cpp` - Simple widget user_data
- `src/ui/ui_step_progress.cpp` - Complex nested allocations
- `src/ui/ui_temp_graph.cpp` - Standalone structure with custom destroy

### Memory Monitoring & Pressure Detection

`MemoryMonitor` (singleton, background thread) samples `/proc/self/status` every 5 seconds and evaluates device-tier-aware pressure thresholds:

- **`MemoryPressureLevel`**: `none` → `elevated` → `warning` → `critical`
- **`MemoryThresholds::for_device()`**: Returns thresholds tuned for constrained (<256 MB), normal (256-512 MB), or good (>512 MB) devices
- **Growth tracking**: Circular buffer of 10 RSS samples at 30s intervals detects leaks (>N MB growth over 5 minutes)
- **`SmapsRollup`**: Reads `/proc/self/smaps_rollup` for cheap heap vs shared lib breakdown without per-VMA cost

On threshold breach, fires a rate-limited `WarningCallback` (max 1 per level per 5 minutes) which is wired to `TelemetryManager::record_memory_warning()` in `Application::init()`. The `MemoryStatsOverlay` (M key) reads `MemoryMonitor::pressure_level()` for real-time display.

**Key files:**
- `include/memory_monitor.h` — `MemoryMonitor`, `MemoryPressureLevel`, `MemoryThresholds`, `MemoryWarningEvent`
- `include/memory_utils.h` — `MemoryInfo` (device tiers), `SmapsRollup`, `read_smaps_rollup()`
- `src/system/memory_monitor.cpp` — threshold evaluation, growth tracking, callback firing
- `src/system/telemetry_manager.cpp` — `memory_warning` and enriched `memory_snapshot` events

See `docs/audits/MEMORY_ANALYSIS.md` § "Proactive Memory Monitoring" for threshold values and verification steps.

### Static Object Destructors and Logging

**Problem:** Static/global objects are destroyed during `exit()` in undefined order across translation units (static destruction order fiasco). If your destructor tries to use spdlog, it may crash because spdlog's global logger might already be destroyed.

**Solution:** Use `fprintf(stderr, ...)` instead of spdlog in destructors of static/global objects:

```cpp
MyManager::~MyManager() {
    // Use fprintf - spdlog may be destroyed during static cleanup
    fprintf(stderr, "[MyManager] Shutting down\n");
    cleanup_resources();
}
```

**When this applies:**
- Destructors of objects stored in static/global variables (e.g., `static std::unique_ptr<WiFiManager>`)
- Any destructor that might run during `exit()` or program termination

**Reference implementations:**
- `src/api/wifi_manager.cpp:71-72`
- `src/api/ethernet_manager.cpp:38-41`
- `src/api/ethernet_backend_*.cpp` (all backend destructors)

**Note:** This is separate from the weak_ptr pattern used for async callback safety - that protects against managers being explicitly destroyed via `.reset()` while async operations are queued.

### ⚠️ Timer Lifecycle Management

**LVGL timers are NOT automatically cleaned up.** Timers created with `lv_timer_create()` continue running until explicitly deleted with `lv_timer_delete()`. If the object passed as `user_data` is destroyed without deleting the timer, the timer will fire with a dangling pointer causing use-after-free crashes.

**Recommended: Use LvglTimerGuard RAII wrapper**

```cpp
#include "ui_timer_guard.h"

class MyPanel {
    LvglTimerGuard update_timer_;

    void start_updates() {
        // Timer automatically deleted when MyPanel is destroyed
        update_timer_.reset(lv_timer_create(update_cb, 1000, this));
    }

    void stop_updates() {
        update_timer_.reset();  // Explicitly stop timer
    }
};
```

**Alternative: Manual cleanup with lv_is_initialized() guard**

For panels/classes that manage timers manually:

```cpp
MyPanel::~MyPanel() {
    // Check LVGL is still running (avoids crash during static destruction)
    if (lv_is_initialized()) {
        if (my_timer_) {
            lv_timer_delete(my_timer_);
            my_timer_ = nullptr;
        }
    }
}
```

**Timer patterns:**

| Pattern | Safe? | Notes |
|---------|-------|-------|
| One-shot with `lv_timer_delete(t)` in callback | YES | Timer self-destructs |
| One-shot with `lv_timer_set_repeat_count(t, 1)` | YES | LVGL auto-deletes |
| LvglTimerGuard member | YES | RAII cleanup |
| Manual delete in destructor with `lv_is_initialized()` check | YES | Explicit cleanup |
| Timer stored in member, no cleanup | **NO** | Use-after-free risk |

**See also:** `include/ui_timer_guard.h` for full API documentation

### ⚠️ Shutdown Order: StaticPanelRegistry & StaticSubjectRegistry

**Problem:** the "static destruction order fiasco" — C++ doesn't guarantee destruction order of
statics across translation units. When `lv_deinit()` runs it deletes widgets, which try to
remove their observers from subjects. If singleton subjects (PrinterState, AmsState,
SettingsManager) haven't been deinitialized first, that corrupts a linked list and crashes in
`lv_observer_remove`.

**Solution:** two self-registration registries enforce the order.

| Registry | Purpose | What it cleans up |
|----------|---------|-------------------|
| **StaticPanelRegistry** | UI panels/overlays with widgets | EmergencyStopOverlay, StatusBar, Keypad, Wizard subjects |
| **StaticSubjectRegistry** | Core state singletons with subjects | PrinterState, AmsState, SettingsManager, FilamentSensorManager |

**Shutdown order (in `Application::shutdown()`):**

```
1. StaticPanelRegistry::destroy_all()     ← Panels destroy their own subjects
2. StaticSubjectRegistry::deinit_all()    ← Core singleton subjects deinitialized
3. lv_deinit()                            ← Now safe - all observers disconnected
```

Registration is **mandatory and always self-registration**: each component's `init_subjects()`
registers its own `deinit_subjects()`, never an external caller. See
[`THREADING.md`](THREADING.md) §7 for the registration and `deinit_subjects()` patterns, LIFO
ordering, and idempotency rules.

**Reference implementations:**
- `src/application/static_subject_registry.cpp` - Core singleton registry
- `src/application/static_panel_registry.cpp` - Panel/overlay registry
- `src/application/subject_initializer.cpp` - Registration during init
- `src/application/application.cpp:shutdown()` - Correct call order

## Thread Safety

**LVGL is not thread-safe.** All widget creation and modification must happen on the main
thread — and `lv_subject_set_*()` counts, because a subject update fires its observers, which
call widget APIs. A background-thread subject write landing mid-render trips LVGL's
`!disp->rendering_in_progress` assertion, which on embedded targets is an infinite loop, not a
clean abort.

`UpdateQueue` is the single safe bridge. Background threads enqueue lambdas with
`helix::ui::queue_update()`; they drain on the main thread at the start of each
`lv_timer_handler()` cycle, before rendering begins:

```
1. UpdateQueue::process_pending()  ← drains all queued lambdas (highest priority)
2. LVGL timers (input polling, animations)
3. process_notifications()         ← dequeue Moonraker JSON
4. lv_refr_now()                   ← render to framebuffer
```

This ordering is why `queue_update()` exists instead of LVGL's native `lv_async_call()`, which
can fire *during* the render phase.

```
MAIN THREAD              LIBHV THREAD           UTILITY THREADS
─────────────            ─────────────          ───────────────
lv_timer_handler()       libhv Event Loop       UpdateChecker
  ├ process_pending()      ├ WebSocket conn     TelemetryManager
  ├ LVGL timers            ├ JSON-RPC parse     CrashReporter
  ├ process_notifs()       ├ Auto-reconnect     ───────────────
  └ lv_refr_now()          └ HTTP transfers            │
         ▲                        │                     │
         │                        │ queue_update(λ)     │ queue_update(λ)
         │                        ▼                     ▼
         └──────────────── UpdateQueue (mutex) ◄────────┘
```

Beyond libhv's event loop, background work runs on the `HttpExecutor` fast (4 workers) and slow
(1 worker) pools, `BusThread` for BlueZ DBus calls, and a few long-lived utility threads.
Everything that touches UI funnels back through `UpdateQueue`.

Two RAII guards cover object lifetime across that boundary: `AsyncLifetimeGuard`
(`include/async_lifetime_guard.h`) for callbacks whose owner may be dismissed before they fire,
and `SubjectLifetime` (`include/ui_observer_guard.h`) for observers on subjects that are
destroyed and recreated when hardware is rediscovered.

> **The rules for writing this code live in [`THREADING.md`](THREADING.md)** — which API to
> call from where, what is banned inside a queued callback, how to guard a background callback,
> shutdown ordering, testing patterns, and a symptom index. Every rule there cost a production
> crash to learn. Read it before writing cross-thread code.

### Multi-Backend AmsState Coordination

`AmsState` uses a `std::recursive_mutex` to protect the `backends_` vector and all backend operations. When a backend emits an event on a background thread, the handler acquires the mutex, reads backend state, then posts subject updates via `helix::ui::queue_update()`. Each backend's event callback captures its index at registration time, so events are routed to the correct per-backend subject storage without ambiguity.

For secondary backends (index 1+), slot subjects live in `BackendSlotSubjects` structs rather than the flat `slot_colors_[]`/`slot_statuses_[]` arrays. The `sync_backend(int)` and `update_slot_for_backend(int, int)` methods handle this routing. All subject writes happen on the LVGL thread via `helix::ui::queue_update()`, so no additional synchronization is needed for the subject values themselves.

## Sensor Framework

HelixScreen's sensor framework provides a unified architecture for discovering,
configuring, and displaying sensor data from multiple sources.

### Discovery Architecture

Sensors come from three different sources:

| Source | Discovery Method | Example Sensors |
|--------|------------------|-----------------|
| Klipper objects | `printer.objects.list` | Humidity, Probe, Width, Switch |
| Klipper config | `configfile.config` | Accelerometers |
| Moonraker API | Moonraker endpoints | Color sensors (TD-1) |

### Manager Pattern

Each sensor category has a singleton manager implementing `ISensorManager`.
Managers implement only the discovery methods for their data source.

### Threading Model

⚠️ Moonraker callbacks run on libhv's thread, NOT the main LVGL thread.

- `discover*()`, `load_config()`, `set_sensor_*()` → Main thread only
- `update_from_status()` → Thread-safe (mutex + helix::ui::queue_update)
- `save_config()` → Thread-safe (read-only with mutex)

## LVGL Configuration

### Required Features

Key settings in `lv_conf.h` for XML support:

```c
#define LV_USE_XML 1                           // Enable XML UI support
#define LV_USE_SNAPSHOT 1                      // Enable screenshot API
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS 1     // Required by XML parser
#define LV_FONT_MONTSERRAT_16 1                // Text fonts
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
```

### Display Driver Integration

**SDL2 Simulator:**
- Uses `lv_sdl_window_create()` for desktop development
- Automatic event handling via SDL2 backend
- Multi-display positioning support via environment variables

**Future Embedded Targets:**
- Framebuffer driver for direct hardware rendering
- Touch input via evdev integration
- Same XML/Subject code runs unchanged

## Design Decisions & Trade-offs

### Why XML Instead of Code?

**Advantages:**
- ✅ Rapid iteration without recompilation
- ✅ Designer-friendly declarative syntax
- ✅ Complete separation of presentation and logic
- ✅ Global theming capabilities
- ✅ Reduced C++ complexity

**Trade-offs:**
- ❌ XML support is experimental in LVGL 9
- ❌ Additional layer of abstraction
- ❌ Limited debugging tools for XML issues
- ❌ Requires UTF-8 encoding for all files

**Verdict:** The benefits outweigh the trade-offs for a touch UI where visual design changes frequently.

### Why Subject-Observer Instead of Direct Widget Updates?

**Advantages:**
- ✅ One data change updates multiple UI elements
- ✅ Type-safe data binding
- ✅ Automatic UI consistency
- ✅ Easier to test business logic separately

**Trade-offs:**
- ❌ Additional conceptual complexity
- ❌ Indirect relationship between data and UI
- ❌ Subject name string matching (not compile-time checked)

**Verdict:** The reactive pattern scales better as UI complexity grows and provides cleaner separation of concerns.

### Why LVGL 9 Instead of Native Platform UI?

**Advantages:**
- ✅ Single codebase for all platforms
- ✅ Embedded-optimized (low memory, no GPU required)
- ✅ Touch-first design patterns
- ✅ Extensive widget library
- ✅ Active development and community

**Trade-offs:**
- ❌ Custom look-and-feel (not native platform appearance)
- ❌ Learning curve for LVGL-specific patterns
- ❌ Limited platform integration (no native menus, etc.)

**Verdict:** Perfect for embedded touch interfaces where native platform UI isn't available or suitable.

## Test Mode Architecture

Test mode (`--test` flag) enables hardware mocking for development without a real printer. Key architectural principle: **production builds NEVER fall back to mocks** - mocks require explicit opt-in.

**Factory Pattern:** Backend factories check `RuntimeConfig::should_mock_*()` before creating implementations:
```cpp
if (config.should_mock_wifi()) {
    return std::make_unique<WifiBackendMock>();  // Test mode
}
return std::make_unique<WifiBackendMacOS>();     // Production
```

### Mock-facing interfaces and drift protection

Six mock boundaries are protected at compile time against silent drift: `AmsBackend`, `EthernetBackend`, `UsbBackend`, `WifiBackend` (already pure virtual today), plus `IMoonrakerAPI` (`include/i_moonraker_api.h`) and `helix::IMoonrakerClient` (`include/i_moonraker_client.h`) — narrow interfaces that mirror only the currently-virtual methods on the corresponding concrete class. Concrete classes inherit the interfaces; mocks still inherit the concretes (so they continue to reuse non-virtual helpers and sub-API composition). Drift protection is enforced via compile-only tests in `tests/unit/test_interface_drift_*.cpp` (`[compile][drift]` tag) that `static_assert(is_base_of_v<Interface, Mock> && !is_abstract_v<Mock>)`. Adding a pure virtual to an interface breaks the concrete's build first, which cascades into the mock.

For the backend interfaces (`AmsBackend`, `EthernetBackend`, `UsbBackend`, `WifiBackend`), callers continue to use the concrete types — those interfaces exist to enforce mock-parity at build time. The Moonraker interfaces went further: `IMoonrakerAPI`, `helix::IMoonrakerClient`, and the sub-API interfaces are the only types consumers may name; the concretes live behind `MoonrakerManager` and the rule is lint-enforced. See `MOONRAKER_ARCHITECTURE.md` for the full contract.

### Test-fixture isolation

`HelixTestFixture` (`tests/helix_test_fixture.h`) is the base for every test fixture. Its constructor and destructor call `reset_all()` which drains `UpdateQueue`, resets `SystemSettingsManager` language, and clears `ModalStack`. `LVGLTestFixture` inherits it. `XMLTestFixture` owns per-instance `PrinterState`, `MoonrakerClient`, and `MoonrakerAPI` members — the previous `static` state was removed so each test starts with fresh printer state. XML subjects still register into LVGL's global scope; per-test LVGL component scopes were attempted but blocked by LVGL internals (see the design doc at `docs/superpowers/specs/2026-04-18-test-mock-decoupling-design.md`). Each test's `init_subjects(true)` overwrites global entries with fresh pointers, and the destructor tears the screen down before deinitializing subjects so no widget dereferences stale pointers. The `helix::xml::ScopedSubjectRegistryOverride` infrastructure (`include/helix/xml/scoped_subject_registry.h`) is in place for future per-component scope work (modals, wizards).

**For usage and CLI flags:** See [DEVELOPMENT.md § Test Mode Development](DEVELOPMENT.md#test-mode-development).

## Critical Implementation Patterns

### Component Instantiation Names

**Always add explicit `name` attributes** to component instantiations:

```xml
<!-- app_layout.xml -->
<lv_obj name="content_area">
  <controls_panel name="controls_panel"/>  <!-- Explicit name required -->
  <home_panel name="home_panel"/>
</lv_obj>
```

**Why:** Component names in `<view name="...">` definitions do NOT propagate to `<component_tag/>` instantiations. Without explicit names, `lv_obj_find_by_name()` returns NULL.

**See [DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md#component-names-critical) for quick syntax reference.**

### Widget Lookup by Name

Use `lv_obj_find_by_name()` instead of index-based child access:

```cpp
// In XML: <lv_label name="temp_display" bind_text="temp_text"/>
// In C++:
lv_obj_t* label = lv_obj_find_by_name(panel, "temp_display");
if (label != NULL) {
    // Safe to use widget
}
```

**Benefits:**
- Robust against XML layout changes
- Self-documenting code
- Explicit error handling when widgets don't exist

### Image Scaling in Flex Layouts

**When scaling images immediately after layout changes**, call `lv_obj_update_layout()` first:

```cpp
// WRONG: Container reports 0x0 size
lv_coord_t w = lv_obj_get_width(container);  // Returns 0
ui_image_scale_to_cover(img, container);     // Fails

// CORRECT: Force layout calculation first
lv_obj_update_layout(container);
lv_coord_t w = lv_obj_get_width(container);  // Returns actual size
ui_image_scale_to_cover(img, container);     // Works correctly
```

**Why:** LVGL uses deferred layout calculation for performance. Immediate size queries after layout changes return stale values.

### Navigation History Stack

Use the navigation system for consistent overlay management:

```cpp
auto& nav = NavigationManager::instance();

// When showing overlay — register first, or on_deactivate() never fires on dismiss
nav.register_overlay_instance(overlay_root, this);
nav.push_overlay(overlay_root);  // Pushes current to history, shows overlay

// In back button callback
if (!nav.go_back()) {
    // Fallback: manual navigation if history is empty
    nav.set_active(helix::PanelId::Home);
}
```

**Benefits:**
- Automatic history management
- Consistent back button behavior
- State preservation when navigating back

**For common implementation patterns and code snippets, see [DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md).**

## Performance Characteristics

### XML Parsing Performance

- **One-time cost** during application startup
- Component registration is fast (simple file parsing)
- Widget creation is standard LVGL performance
- **No runtime XML parsing** after initialization

### Subject Update Performance

- **O(n) complexity** where n = number of bound widgets
- Optimized for small numbers of observers per subject
- **Batched updates** - multiple subject changes before next redraw
- **Efficient for typical UI** with 10-50 bound elements per panel

### Memory Footprint

- **Minimal XML overhead** - parsed structure discarded after creation
- **Subject storage** - ~100 bytes per subject (reasonable for 50-100 subjects)
- **Widget memory** - standard LVGL allocation patterns
- **Total overhead** - estimated <10KB for XML/Subject systems

## Extended Systems

These systems extend the core architecture for specific features:

### Modal System (ui_dialog)

All modal dialogs use the `ui_dialog` XML component for consistent theming and layout. The system standardizes dialog creation across the codebase:

- **`ui_dialog`** - Custom XML widget registered via `ui_dialog_register()`. Uses LVGL's built-in button grey for background, automatically adapting to light/dark mode. Used as the base container in all modal XML files.
- **`modal_button_row`** - Reusable XML component for consistent button layouts (OK/Cancel, Yes/No, etc.) at the bottom of modals.
- **Modal pattern** - Class-based modals inherit lifecycle management. Dynamically created modal buttons are wired imperatively since they are generated at runtime (see Legitimate Exception #11).

**XML usage:**
```xml
<ui_dialog>
  <text_heading text="Confirm Action"/>
  <text_body bind_text="dialog_message"/>
  <modal_button_row name="buttons"/>
</ui_dialog>
```

**Files:** `include/ui_dialog.h`, `ui_xml/modal_button_row.xml`, `ui_xml/*_modal.xml`, `ui_xml/*_dialog.xml`

### Spoolman Management

Full spool lifecycle management via Spoolman integration:

- **SpoolmanPanel** (`ui_panel_spoolman.cpp`) — Virtualized spool list with search, context menu, edit modal
- **SpoolWizardOverlay** (`ui_spool_wizard.cpp`) — 3-step creation wizard (Vendor → Filament → Spool Details)
- **SpoolmanContextMenu** — Right-click/long-press actions per spool row
- **SpoolEditModal** — Inline editing of spool properties

API calls route through `server.spoolman.proxy` JSON-RPC via Moonraker. The wizard supports dual-source vendor/filament data (server + SpoolmanDB external catalog) with deduplication. Creation is atomic with best-effort rollback.

Spoolman is now decoupled from AMS backends — spool assignments are tracked per-tool via `ToolState` with persistence to `config/tool_spools.json`. This allows Spoolman to work independently of any filament changer hardware.

### Config Migration System

Versioned schema migration for `settings.json` that automatically upgrades configuration between releases. The `Config` class tracks a `config_version` integer (currently `CURRENT_CONFIG_VERSION = 21`) and applies migrations sequentially on load:

- **Version tracking** - Each config file stores its schema version; missing version implies v0
- **Sequential migrations** - Migrations run in order (each version step transforms the JSON structure) on startup
- **Key consolidation** - Example: flat keys (`display_rotate`, `display_sleep_sec`, `touch_calibrated`) migrated into nested objects (`/display/rotate`, `/input/calibration/valid`)
- **Non-destructive** - Existing new-format values are preserved; only old-format keys are migrated and removed
- **Auto-save** - Config is saved after migration completes

**Files:** `include/config.h`, `src/system/config.cpp`
**Tests:** 150+ migration assertions in `tests/unit/test_config.cpp`

### Exclude Objects System

Allows users to exclude individual objects from the current print (Klipper's `exclude_object` module):

- **PrinterExcludedObjectsState** - Domain class managing excluded/defined object lists with a version subject that increments on changes. Uses `SubjectManager` for subject lifecycle and `std::unordered_set` for excluded object names (sets are not natively supported by LVGL subjects).
- **PrintExcludeObjectManager** - Coordinates the confirmation flow for excluding objects, preventing accidental exclusion.
- **ExcludeObjectSideList** - Side list showing all defined objects with status indicators (current/idle/excluded), tap-to-exclude, and optional G-code object thumbnails via `GCodeObjectThumbnailRenderer`.
- **ExcludeObjectMapView** - Object map with 3D selection brackets, paired with the side list.
- **ExcludeObjectModal** - Confirmation modal before excluding an object.

**Files:** `include/printer_excluded_objects_state.h`, `include/ui_exclude_object_side_list.h`, `include/ui_exclude_object_map_view.h`, `include/ui_print_exclude_object_manager.h`, `ui_xml/exclude_object_modal.xml`

### Frequency Response Chart Widget

Custom chart widget for visualizing accelerometer frequency response data during input shaper calibration:

- **Platform-adaptive rendering** - Configures for EMBEDDED (50 points, simplified), BASIC (50 points), or STANDARD (200 points with animations) tiers
- **Multi-series support** - Separate data series for X/Y axis measurements with independent visibility toggle
- **Peak marking** - Vertical markers at detected resonance frequencies
- **Auto-downsampling** - Data exceeding the platform's max points is downsampled while preserving frequency range endpoints
- **Shaper overlay chips** - Interactive toggles to show/hide recommended shaper frequency bands

**Files:** `include/ui_frequency_response_chart.h`, `src/ui/ui_frequency_response_chart.cpp`

### Markdown Viewer Widget (ui_markdown)

Theme-aware markdown rendering widget registered as an XML custom component:

- **XML integration** - Used as `<ui_markdown bind_text="subject_name"/>` or `<ui_markdown text="# Static content"/>`
- **Theme-aware** - Reads colors from design tokens for headings, body, links, and code blocks
- **Subject binding** - Supports `bind_text` for reactive markdown content updates
- **RAII cleanup** - Automatic resource management via user data pattern
- **lv_markdown backend** - Wraps the `lv_markdown` library (git submodule)

**Files:** `include/ui_markdown.h`, `src/ui/ui_markdown.cpp`

### Update System

Async update checking and in-place installation system:

- **UpdateChecker** - Singleton that checks GitHub releases API and R2 CDN for newer versions. Rate-limited to 1 check per hour. Background thread for HTTP, results marshaled to LVGL thread via `helix::ui::queue_update()`.
- **Three update channels** - Stable (GitHub releases), Beta (pre-releases), Dev (custom URL)
- **Download pipeline** - Confirm -> Download -> Verify (SHA-256 + ELF architecture validation) -> Install (`install.sh`)
- **Safety guards** - Downloads blocked during active prints, explicit user confirmation required
- **Version dismissal** - Users can dismiss specific versions (persisted to config)
- **Auto-check** - 15-second initial delay, then 24-hour periodic checks
- **Notification modal** - Uses `ui_markdown` widget for release notes display
- **LVGL subjects** - Status, progress, version text all bound to UI via subjects

**Files:** `include/system/update_checker.h`, `src/system/update_checker.cpp`, `ui_xml/update_notify_modal.xml`, `ui_xml/update_download_modal.xml`

### Print Start Phase Detection

Modular system for detecting preparation phases (homing, heating, leveling) during PRINT_START:

- **PrintStartCollector** - Monitors G-code responses for phase detection with priority chain: HELIX:PHASE signals → profile signal formats → PRINT_START marker → completion marker → profile regex → built-in fallback
- **PrintStartProfile** - JSON-driven printer-specific profiles with signal format matching and regex patterns. Supports `sequential` (known firmware) and `weighted` (generic) progress modes
- **PreprintPredictor** - Weighted-average predictor using historical timing data to estimate remaining preparation time. Tracks last 3 entries (FIFO) with per-phase weighting and 15-minute anomaly rejection

**See [PRINT_START_PROFILES.md](PRINT_START_PROFILES.md) for the full developer guide.**

### Moonraker Plugin

Optional plugin for enhanced print phase tracking. See [moonraker-plugin/README.md](../../moonraker-plugin/README.md) for installation and details.

- **helix_print.py** - Tracks print phases (heating, mesh, purge, etc.)
- **HelixPluginInstaller** - Auto-detects and installs plugin (local) or shows install command (remote)

### LED Control System

Unified LED management across five backends with automatic state-based lighting:

- **LedController** - Singleton orchestrating five backends: `NativeBackend` (Klipper neopixel/dotstar/led), `LedEffectBackend` (led_effect plugin animations), `WledBackend` (WLED network strips via Moonraker HTTP bridge), `MacroBackend` (user-configured macro devices), `OutputPinBackend` (output_pin brightness-only or on/off devices)
- **LedAutoState** - Observes printer state subjects (print status, klippy state, extruder target) and automatically applies LED actions for six states: idle, heating, printing, paused, error, complete
- **PrinterLedState** - Domain class tracking one LED strip for home panel display (RGBW subjects)
- **LedControlOverlay** - Full control overlay with color presets, effects, WLED presets, macro buttons
- **LedSettingsOverlay** - Configuration overlay for strip selection, auto-state mapping, macro device management

**Files:** `include/led/`, `src/led/`, `include/printer_led_state.h`, `include/ui_settings_led.h`, `ui_xml/led_*.xml`

**See [LED_CONTROL.md](LED_CONTROL.md) for the full developer guide.**

### Print History Management

Centralized caching shared between history panels:

- **PrintHistoryManager** - Single source of truth for job history, avoids duplicate API calls
- **FileHistoryStatus** - Enum for print status indicators (Completed, Cancelled, Error, etc.)
- **Observer pattern** - Panels register for change notifications, cleanup on destruction

### Timelapse State

`TimelapseState` is a singleton that handles timelapse event dispatch and state management. It subscribes to WebSocket `notify_timelapse_event` notifications, tracks frame capture counts and render progress via LVGL subjects, and emits throttled toast notifications during rendering.

**Files:** `include/timelapse_state.h`, `src/printer/timelapse_state.cpp`
**See [TIMELAPSE.md](TIMELAPSE.md) for the full developer guide.**

### Active Print Media

Handles async file operations during print selection:

- **ActivePrintMediaManager** - Manages file streaming state
- **StreamingPolicy** - Prevents conflicting async operations
- **BusyOverlay** - Visual feedback during long-running operations

### Post-Operation Cooldown

`PostOpCooldownManager` is a singleton that schedules heater cooldown after filament operations
complete. All filament operation paths (FilamentPanel, AMS sidebar, AMS backends) funnel through
this single manager instead of implementing their own cooldown timers.

- `schedule()` — starts a configurable delay timer (default 120s, setting: `/filament/cooldown_delay_seconds`)
- `schedule()` is also the single opt-out point: it returns early when `/filament/auto_cooldown`
  is false (Settings > Safety toggle — filament systems like AFC run their own cooldown) or when
  the delay is `<= 0`. Both read config directly, not the SettingsManager subject, because
  `schedule()` is callable from any thread.
- `cancel()` — cancels pending cooldown (called when user manually heats or new op starts)
- At fire time, checks: extruder target > 0 AND print state != PRINTING/PAUSED
- Calling `schedule()` resets any existing pending timer
- Thread-safe: defers to LVGL main thread via `queue_update()`

### Watchdog (Embedded)

Crash recovery for embedded deployments:

- **helix_watchdog** - Separate process, minimal dependencies
- **Crash dialog** - Renders directly to framebuffer (no LVGL)
- **Auto-restart** - Monitors heartbeat, restarts on crash/hang

## Legitimate Exceptions to UI Patterns

During code audits, the following patterns may appear to violate declarative UI guidelines but are **acceptable exceptions**. Future audits should not flag these:

### 1. DELETE Event Handlers
`lv_obj_add_event_cb(obj, cb, LV_EVENT_DELETE, ...)` is required for RAII cleanup of widget user_data. Cannot be done in XML.

### 2. Canvas/Drawing Code
Files like `nozzle_renderer_*.cpp`, `ui_filament_path_canvas.cpp`, `ui_bed_mesh.cpp` use hardcoded colors for physical/material rendering (brass nozzle, charcoal frame). These are not theme colors.

### 3. Dynamic Widget Creation
Widgets created at runtime (step progress indicators, AMS mini status, keyboard overlays) cannot use XML bindings since they're generated programmatically.

### 4. Gesture State Machines
Keyboard long-press detection, jog pad touch handling require imperative state management for gesture recognition.

### 5. Bootstrap Components
Fatal error screen (`ui_fatal_error.cpp`) runs before theme is loaded - must use hardcoded colors.

### 6. Fallback Color Pattern
```cpp
const char* str = lv_xml_get_const("color_token");
lv_color_t c = str ? ui_theme_parse_hex_color(str) : lv_color_hex(0xFALLBACK);
```
This is correct - `str` contains an actual hex value from XML, not a token name.

### 7. fprintf in Destructors
spdlog may be destroyed during static destruction. Use `fprintf(stderr, ...)` in destructors of static/global objects.

### 8. Async Context new/delete
`helix::ui::queue_update()` with the `unique_ptr` overload handles memory automatically. The raw lambda overload captures by value or move, avoiding manual `new`/`delete`.

### 9. CLI printf
Code in `cli_args.cpp` runs before logging infrastructure is initialized - printf is acceptable.

### 10. Experimental Test Code
Test scaffolding in `experimental/src/*.cpp` uses printf for test output - not production code.

### 11. Modal Dialog Buttons
Dynamically created modals (confirmation dialogs, warnings) wire buttons imperatively since they're created at runtime.

### 12. snprintf with sizeof()
```cpp
char buf[256];
snprintf(buf, sizeof(buf), "format...", args);
```
This is safe - truncates rather than overflows. Not a security issue.

### 13. StreamingPolicy State Management
`StreamingPolicy` uses imperative state (`start()`/`complete()`) to track async operations. Cannot be declarative since it manages operation lifecycle, not UI state.

### 14. BusyOverlay Show/Hide
`BusyOverlay::show()`/`hide()` are imperative because they provide async feedback during operations. The overlay exists temporarily, not as persistent UI state.

---

## Related Documentation

This document focuses on system design, patterns, and architectural decisions ("why"). For implementation details:

- **[DEVELOPER_QUICK_REFERENCE.md](DEVELOPER_QUICK_REFERENCE.md)** - Code snippets, common patterns, quick lookups ("how")
- **[LVGL9_XML_GUIDE.md](LVGL9_XML_GUIDE.md)** - Complete XML syntax reference
- **[DEVELOPMENT.md](DEVELOPMENT.md)** - Build system and daily workflow
- **[DEVELOPMENT.md#contributing](DEVELOPMENT.md#contributing)** - Code standards and git workflow
- **[BUILD_SYSTEM.md](BUILD_SYSTEM.md)** - Build configuration and patches