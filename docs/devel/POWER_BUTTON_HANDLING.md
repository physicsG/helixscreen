# Power Button Handling Research

This document captures research on how power buttons are handled across different 3D printer screen UIs and firmware configurations.

## Overview

Power button handling varies significantly across different setups:
- Some systems use Linux kernel/systemd to handle KEY_POWER events
- Some use Klipper's `gcode_button` to monitor GPIO pins
- Some have no software handling (hardware power cutoff)

The key insight: **there is no standard**. Each firmware/mod handles this differently.

The sections below capture the original research. The confirmation-dialog flow described in that research has since **shipped** — see **As Built** immediately below.

---

## As Built (Shipped)

HelixScreen ships the UI-initiated confirmation dialog — **Option C (Shutdown Panel, like KlipperScreen)** from the research in section 5. It is a UI shutdown/reboot flow, **not** a hardware power-button interceptor (Options B/D's evdev listener was not built).

**Entry points** — both open the same dialog:
- **Advanced panel Power row** — `AdvancedPanel::handle_power_clicked()` calls `helix::show_shutdown_dialog(...)` (`src/ui/ui_panel_advanced.cpp:244-246`).
- **Home-panel "shutdown" widget** — the `ShutdownWidget` panel widget (`src/ui/panel_widgets/shutdown_widget.{h,cpp}`), registered as `"shutdown"` / "Shutdown/Reboot" in the panel widget registry (`src/ui/panel_widget_registry.cpp:59`, `register_shutdown_widget()`). Its click handler also routes through `show_shutdown_dialog(...)`.

**Flow:**
1. User taps the Power row (Advanced panel) or the home-panel Shutdown widget.
2. `helix::show_shutdown_dialog(api, modal, lifetime, parent_screen)` (`src/ui/panel_widgets/shutdown_widget.h:53`, impl `shutdown_widget.cpp:329`) configures and shows a `ShutdownModal` confirmation dialog.
3. Scope depends on host topology:
   - **Same host as Moonraker:** single-scope Shutdown/Reboot. When Moonraker is connected these run `machine.shutdown` / `machine.reboot` via `MoonrakerAPI`; when it isn't, they fall back to a local `SystemPower` shutdown/reboot so the user isn't forced to the hardware switch.
   - **Different host (screen-only setup):** dual-scope — separate "printer", "screen", or "both" actions. The "both" flows defer the local `SystemPower` call until the printer-side ack (hence the `AsyncLifetimeGuard`).
4. Confirming executes the chosen action; cancelling dismisses the modal.

Both entry points share one dialog implementation, so behavior is identical regardless of where it was launched.

---

## 1. Linux Kernel / Systemd Handling

On systems with systemd-logind, pressing a power button typically:
1. Generates KEY_POWER event on `/dev/input/eventX`
2. systemd-logind receives the event
3. Configured action runs (default: `poweroff`)

**Configuration:** `/etc/systemd/logind.conf`
```ini
HandlePowerKey=poweroff    # or ignore, suspend, hibernate
```

**Note:** Embedded systems (like AD5M with BusyBox) don't have systemd, so this doesn't apply.

---

## 2. KlipperScreen Handling

KlipperScreen has a dedicated **Shutdown Panel** (panels/shutdown.py) that shows confirmation dialogs:

```python
def reboot_poweroff(self, widget, method):
    label.set_label(_("Are you sure you wish to shutdown the system?"))
    buttons = [
        {"name": _("Accept"), "response": Gtk.ResponseType.ACCEPT},
        {"name": _("Cancel"), "response": Gtk.ResponseType.CANCEL},
    ]
    self._gtk.Dialog(title, buttons, label, self.reboot_poweroff_confirm, method)
```

**Key points:**
- This is a **UI panel** users navigate to, not a hardware button interceptor
- KlipperScreen does NOT intercept hardware power button events
- The physical power button bypasses this entirely

---

## 3. Klipper gcode_button Handling

Klipper can monitor GPIO pins directly via `gcode_button`:

```cfg
[gcode_button btn_power]
pin: !PC15
press_gcode:
    SHUTDOWN_MACRO
```

**How it works:**
1. Klipper polls the GPIO pin
2. On state change, executes `press_gcode` (and optionally `release_gcode`)
3. Runs immediately - no confirmation dialog by default

**Pros:** Works at firmware level, reliable, no Linux dependencies
**Cons:** No built-in UI confirmation mechanism

---

## 4. AD5M / ForgeX Implementation (Example)

### Hardware
- Power button connected to GPIO PC15
- Touch controller (USB) also reports KEY_POWER on `/dev/input/event3`
- Touchscreen is separate device on `/dev/input/event0`

### ForgeX Configuration

**`macros/headless.cfg`:**
```cfg
[gcode_button btn_power]
pin: !PC15
press_gcode:
    _SHUTDOWN_BUTTON_TRIGGER

[gcode_macro _SHUTDOWN_BUTTON_TRIGGER]
variable_trigger_allowed: False
gcode:
    {% if trigger_allowed %}
        RESPOND TYPE=error MSG="Shutdown triggered by button"
        SHUTDOWN
        SET_GCODE_VARIABLE MACRO=_SHUTDOWN_BUTTON_TRIGGER VARIABLE=trigger_allowed VALUE=False
    {% endif %}

# Debounce guard - prevents false triggers during boot
[delayed_gcode _PRO_BUTTON_BOUNCE_GUARD]
initial_duration: 1
gcode:
    SET_GCODE_VARIABLE MACRO=_SHUTDOWN_BUTTON_TRIGGER VARIABLE=trigger_allowed VALUE=True
```

**`macros/base.cfg`:**
```cfg
[gcode_macro SHUTDOWN]
description: Turn off the printer
gcode:
    BED_MESH_CLEAR
    M400
    SET_PIN PIN=clear_power_off VALUE=1
    WAIT TIME=500
    SET_PIN PIN=clear_power_off VALUE=0
    SET_PIN PIN=power_off VALUE=0
    RUN_SHELL_COMMAND CMD=sync
    RUN_SHELL_COMMAND CMD=poweroff
```

### Current Behavior
1. Power button pressed
2. Klipper's gcode_button sees GPIO change
3. `_SHUTDOWN_BUTTON_TRIGGER` runs
4. If debounce guard passed, `SHUTDOWN` macro runs
5. System powers off immediately - **NO CONFIRMATION DIALOG**

### Why HelixScreen Doesn't See It
- HelixScreen opens `/dev/input/event0` (touchscreen)
- Power button KEY_POWER goes to `/dev/input/event3` (USB controller)
- Nobody in userspace has event3 open
- Klipper handles it at GPIO level before any evdev event matters

---

## 5. Options for Adding Confirmation Dialog

### Option A: Klipper action:prompt Protocol

Modify the Klipper macro to use `action:prompt` instead of immediate shutdown:

```cfg
[gcode_macro _SHUTDOWN_BUTTON_TRIGGER]
variable_trigger_allowed: False
gcode:
    {% if trigger_allowed %}
        RESPOND TYPE=command MSG="action:prompt_begin Shutdown"
        RESPOND TYPE=command MSG="action:prompt_text Shut down the printer?"
        RESPOND TYPE=command MSG="action:prompt_button_group_start"
        RESPOND TYPE=command MSG="action:prompt_button Cancel|RESPOND TYPE=command MSG=action:prompt_end"
        RESPOND TYPE=command MSG="action:prompt_button Shut Down|SHUTDOWN"
        RESPOND TYPE=command MSG="action:prompt_button_group_end"
        RESPOND TYPE=command MSG="action:prompt_show"
    {% endif %}
```

**Pros:**
- Works with existing ActionPromptManager in HelixScreen
- No changes needed to HelixScreen code
- Firmware-level solution, works with any screen UI

**Cons:**
- Requires ForgeX/firmware modification (not our code)
- Not all firmwares will adopt this

### Option B: HelixScreen Listens to Power Button evdev

Have HelixScreen also open `/dev/input/event3` (or auto-detect KEY_POWER device):

```cpp
// In input handling code
if (key_code == KEY_POWER) {
    show_shutdown_confirmation_dialog();
    return;  // Consume the event
}
```

**Pros:**
- Works regardless of Klipper config
- We control the behavior

**Cons:**
- Race condition with Klipper's gcode_button (which also sees GPIO)
- Need to use EVIOCGRAB to exclusively grab the device
- Device path varies across hardware

### Option C: Shutdown Panel (Like KlipperScreen)

Add a dedicated shutdown panel with Shutdown/Reboot/Cancel buttons:

**Pros:**
- Simple to implement
- Clear user experience
- Works for UI-initiated shutdowns

**Cons:**
- Doesn't intercept hardware power button
- Users can still accidentally shut down via button

### Option D: Hybrid Approach (Recommended)

1. **Shutdown Panel** - For intentional shutdowns via UI
2. **Detect gcode_button at startup** - Query Klipper for `btn_power` or similar
3. **Document recommended Klipper config** - Provide example macro using action:prompt
4. **Optional: evdev power button listener** - For systems without Klipper GPIO handling

---

## 6. Discovery: Detecting Power Button Configuration

HelixScreen could query Klipper at startup to understand power button setup:

```
# Query gcode_buttons
GET /printer/objects/query?gcode_button

# Look for btn_power or similar
```

This would let us:
- Show appropriate UI (e.g., "Shutdown" button if power button exists)
- Warn if power button will cause immediate shutdown
- Suggest macro modifications for confirmation

---

## 7. Recommendations

### Short Term
1. Add a **Shutdown Panel** (like KlipperScreen) for UI-initiated shutdowns
2. Document the action:prompt approach for firmware authors

### Medium Term
3. Query Klipper for gcode_button configuration at startup
4. Display power button status in settings/about panel

### Long Term
5. Consider evdev listener for hardware power button (with EVIOCGRAB)
6. Work with ForgeX/firmware authors to standardize on action:prompt

---

## References

- KlipperScreen shutdown panel: KlipperScreen/panels/shutdown.py
- ForgeX power config: `ad5m-forgex/macros/headless.cfg`
- Klipper gcode_button docs: https://www.klipper3d.org/Config_Reference.html#gcode_button
- Klipper action:prompt docs: https://www.klipper3d.org/G-Codes.html#action-commands
- Linux KEY_POWER: <linux/input-event-codes.h> on the target device (code 116)
