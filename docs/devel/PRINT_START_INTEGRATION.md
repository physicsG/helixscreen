# Print Start Phase Detection

HelixScreen detects when your print's preparation phase (heating, homing, leveling, etc.) is complete and actual printing begins. This document explains how the detection works and how to optimize it for your setup.

**Developer guide**: For adding profiles for new printers, see [PRINT_START_PROFILES.md](PRINT_START_PROFILES.md).

## How It Works

When a print starts, HelixScreen shows a "Preparing Print" overlay on the home panel with a progress bar showing the current phase (homing, heating, leveling, etc.). Once the preparation is complete, the UI transitions to show normal print status.

### When detection starts: two arming paths

The detection window opens at one of two moments, and which one it is matters for
everything below.

| Path | Opens when | Covers |
|---|---|---|
| **Commit-armed** | The user presses Print on the screen | Any host-side pre-start work *plus* the file's own `PRINT_START` |
| **Printer-edge** | `print_stats` transitions into `printing` | The file's own `PRINT_START` only |

A print started from Mainsail, Fluidd, or the printer's own panel has no commit for
HelixScreen to observe, so it always takes the printer-edge path. That path is
unchanged and must stay working - it is the only one an externally started print has.

The distinction exists because some pre-print work runs *in front of* the job rather
than inside it. An optional forced bed mesh, or a printer-level setup macro, is
dispatched by HelixScreen as a blocking `gcode_script` before `start_print` is ever
called. Klipper reports `print_stats=complete` (describing the *previous* job) for
that entire time. On a K2 Plus that window is around 455 seconds. Commit arming is
what lets the UI describe it instead of rendering stale job data.

Consequence for timing history: the two paths measure different quantities, so
recorded pre-print durations are bucketed by which window they came from and are
never averaged together. See [PREPRINT_PREDICTION.md](PREPRINT_PREDICTION.md).

HelixScreen uses a modular profile system with printer-specific signal matching. Known printers (like the FlashForge AD5M) get accurate, per-phase progress tracking. Unknown printers use generic pattern matching that works with standard G-code commands.

Detection uses a multi-signal system with these fallbacks for completion:

| Priority | Signal | How It Works |
|----------|--------|--------------|
| 1 | **Macro Variables** | Watches `gcode_macro _HELIX_STATE.print_started` or `gcode_macro _START_PRINT.print_started` |
| 2 | **G-code Console** | Parses console output for `HELIX:READY`, `LAYER: 1`, `;LAYER:1`, `First layer`, or `SET_PRINT_STATS_INFO CURRENT_LAYER=` |
| 3 | **Layer Count** | Monitors `print_stats.info.current_layer` becoming ≥1 |
| 4 | **Progress + Temps** | Print progress ≥2% AND temps within 5°C of target |
| 5 | **Timeout** | Adaptive; see "Completion Fallbacks". Never a flat 45s. |

## Making Your Setup HelixScreen-Friendly

### Option 1: Install HelixScreen Macros (Recommended)

The HelixScreen macro file provides:
- **Instant detection**: `HELIX_READY` signal at the end of your PRINT_START
- **Phase tracking**: Optional `HELIX_PHASE_*` macros for detailed progress display
- **Pre-print helpers**: `HELIX_BED_LEVEL_IF_NEEDED`, `HELIX_CLEAN_NOZZLE`, `HELIX_START_PRINT`

**Installation via Settings UI:**

1. Go to Settings → Advanced → HelixScreen Macros
2. Click "Install Macros"
3. Restart Klipper when prompted

**Manual Installation:**

1. Copy `assets/config/helix_macros.cfg` to your Klipper config directory
2. Add to your `printer.cfg`:

```ini
[include helix_macros.cfg]
```

3. Add `HELIX_READY` at the end of your PRINT_START macro:

```gcode
[gcode_macro PRINT_START]
gcode:
    # Your heating commands
    M190 S{BED_TEMP}
    M109 S{EXTRUDER_TEMP}

    # Your homing and leveling
    G28
    BED_MESH_CALIBRATE

    # Signal HelixScreen that prep is done
    HELIX_READY

    # Purge line or first layer starts here
```

### Option 1b: With Phase Tracking

For detailed progress display during preparation, add phase signals:

```gcode
[gcode_macro PRINT_START]
gcode:
    HELIX_PHASE_HOMING
    G28

    HELIX_PHASE_HEATING_BED
    M190 S{BED_TEMP}

    HELIX_PHASE_BED_MESH
    BED_MESH_CALIBRATE

    HELIX_PHASE_HEATING_NOZZLE
    M109 S{EXTRUDER_TEMP}

    HELIX_PHASE_PURGING
    # Your purge line code here

    # Signal preparation complete
    HELIX_READY
```

### Option 2: Add Variables to Existing Macro

If you have an existing `_START_PRINT` or `START_PRINT` macro, add state tracking:

```gcode
[gcode_macro _START_PRINT]
variable_print_started: False
gcode:
    # ... your heating, homing, leveling code ...

    # At the end, signal completion:
    SET_GCODE_VARIABLE MACRO=_START_PRINT VARIABLE=print_started VALUE=True
```

### Option 3: Use SET_PRINT_STATS_INFO (Slicer-Based)

If your slicer supports it, enable layer info in your G-code:

**PrusaSlicer/SuperSlicer:**
- Enable "Verbose G-code" in Printer Settings → General → Advanced
- The slicer will insert `SET_PRINT_STATS_INFO CURRENT_LAYER=N` commands

**Cura:**
- Use a post-processing script to add layer comments
- HelixScreen looks for `LAYER:N` or `LAYER: N` patterns

## Available Macros

When `helix_macros.cfg` is installed, these macros are available:

### Core Signals

| Macro | Purpose |
|-------|---------|
| `HELIX_READY` | Signal that print preparation is complete |
| `HELIX_ENDED` | Signal that print has ended (called in PRINT_END) |
| `HELIX_RESET` | Reset state (for cancel/error recovery) |

### Phase Tracking (Optional)

| Macro | Phase Displayed |
|-------|-----------------|
| `HELIX_PHASE_HOMING` | "Homing..." |
| `HELIX_PHASE_HEATING_BED` | "Heating Bed..." |
| `HELIX_PHASE_HEATING_NOZZLE` | "Heating Nozzle..." |
| `HELIX_PHASE_BED_MESH` | "Loading Bed Mesh..." |
| `HELIX_PHASE_QGL` | "Leveling Gantry..." |
| `HELIX_PHASE_Z_TILT` | "Z Tilt Adjust..." |
| `HELIX_PHASE_CLEANING` | "Cleaning Nozzle..." |
| `HELIX_PHASE_PURGING` | "Purging..." |

### Pre-Print Helpers

| Macro | Purpose |
|-------|---------|
| `HELIX_BED_LEVEL_IF_NEEDED` | Run bed mesh only if stale (configurable max age) |
| `HELIX_CLEAN_NOZZLE` | Nozzle cleaning sequence (configure brush position) |
| `HELIX_START_PRINT` | Complete start print sequence with all options |

## Controllable Pre-Print Operations

HelixScreen can analyze your `PRINT_START` macro to detect which operations can be toggled from the print details panel. This allows you to skip bed mesh, QGL, etc. on a per-print basis.

### Parameter Semantics

HelixScreen recognizes two styles of parameter control:

| Style | Example | When checkbox is unchecked |
|-------|---------|---------------------------|
| **Opt-IN** (recommended) | `PERFORM_BED_MESH=1` | Passes `PERFORM_BED_MESH=0` |
| **Opt-OUT** | `SKIP_BED_MESH=1` | Passes `SKIP_BED_MESH=1` |

**Opt-IN (PERFORM_*)**: Operation is skipped by default; checkbox enables it.
**Opt-OUT (SKIP_*)**: Operation runs by default; checkbox disables it.

### Recognized Parameter Names

HelixScreen detects these parameter patterns in your `PRINT_START` macro:

| Operation | Opt-IN Patterns | Opt-OUT Patterns |
|-----------|-----------------|------------------|
| Bed Mesh | `PERFORM_BED_MESH`, `DO_BED_MESH`, `FORCE_BED_MESH`, `FORCE_LEVELING` | `SKIP_BED_MESH`, `SKIP_MESH`, `NO_MESH` |
| QGL | `PERFORM_QGL`, `DO_QGL`, `FORCE_QGL` | `SKIP_QGL`, `NO_QGL` |
| Z-Tilt | `PERFORM_Z_TILT`, `DO_Z_TILT`, `FORCE_Z_TILT` | `SKIP_Z_TILT`, `NO_Z_TILT` |
| Nozzle Clean | `PERFORM_NOZZLE_CLEAN`, `DO_NOZZLE_CLEAN` | `SKIP_NOZZLE_CLEAN`, `SKIP_CLEAN` |

### Making Your Macro Controllable

Wrap operations in Jinja conditionals using recognized parameters:

**Opt-IN Style (recommended):**

```gcode
[gcode_macro PRINT_START]
gcode:
    {% set perform_bed_mesh = params.PERFORM_BED_MESH|default(0)|int %}
    {% set perform_qgl = params.PERFORM_QGL|default(0)|int %}

    G28  ; Always home

    {% if perform_qgl == 1 %}
        QUAD_GANTRY_LEVEL
    {% endif %}

    {% if perform_bed_mesh == 1 %}
        BED_MESH_CALIBRATE
    {% endif %}

    HELIX_READY
```

**Opt-OUT Style:**

```gcode
[gcode_macro PRINT_START]
gcode:
    {% set skip_bed_mesh = params.SKIP_BED_MESH|default(0)|int %}

    G28  ; Always home

    {% if skip_bed_mesh == 0 %}
        BED_MESH_CALIBRATE
    {% endif %}

    HELIX_READY
```

### Using HELIX_START_PRINT

The bundled `HELIX_START_PRINT` macro supports all controllable operations:

```gcode
; In your slicer's start G-code:
HELIX_START_PRINT BED_TEMP={first_layer_bed_temperature} EXTRUDER_TEMP={first_layer_temperature} PERFORM_BED_MESH=1 PERFORM_QGL=1
```

Available parameters:
- `BED_TEMP` - Bed temperature (default: 60)
- `EXTRUDER_TEMP` - Extruder temperature (default: 200)
- `PERFORM_QGL` - Run quad gantry level (0 or 1)
- `PERFORM_Z_TILT` - Run Z-tilt adjust (0 or 1)
- `PERFORM_BED_MESH` - Run bed mesh calibrate (0 or 1)
- `PERFORM_NOZZLE_CLEAN` - Run nozzle cleaning (0 or 1)

## Troubleshooting

### "Preparing" Stuck Forever

If the home panel stays on "Preparing Print" indefinitely:

1. **Check console output**: Run your print and look at the Klipper console. Do you see any layer markers?
2. **Verify macro variables**: Query `gcode_macro _HELIX_STATE` via Moonraker to see if `print_started` is being set
3. **Check whether the printer is still talking.** Every timeout except the 1800s
   absolute ceiling also requires the printer to have gone quiet for 90 seconds. A
   pre-print that is still narrating itself is not considered stuck, however long it
   runs - that is deliberate, and it is why a slow bed mesh no longer gets cut off
   mid-sequence.
4. **Check for a host-side pre-start block.** If the printer is commit-armed and a
   forced bed mesh or setup macro is running in front of the job, `print_stats` will
   read `complete` or `standby` for the whole block. That is expected, not a hang -
   look for the blocking `gcode_script` in the log and its elapsed time.

### Quick Detection Not Working

If detection falls all the way through to a timeout:

1. **Add HELIX_READY**: The most reliable option
2. **Check if your slicer sets layer info**: Look for `SET_PRINT_STATS_INFO` in your G-code
3. **Verify macro object subscriptions**: HelixScreen subscribes to `gcode_macro _HELIX_STATE`, `gcode_macro _START_PRINT`, and `gcode_macro START_PRINT`
4. **ForgeX users**: Detection should be instant via the Forge-X profile (`// State:` signals) and `START_PRINT.preparation_done` macro variable

## Technical Details

### Printer-Specific Profiles

HelixScreen uses a modular profile system to match different printer firmware. Known printers (like the FlashForge AD5M with Forge-X firmware) have custom profiles that map firmware-specific output to preparation phases with accurate progress tracking. Unknown printers use generic regex patterns that work with standard G-code commands.

Profiles are JSON files in `assets/config/print_start_profiles/`. Each profile can define:
- **Signal formats**: Exact prefix + value matching for structured firmware output
- **Response patterns**: Regex patterns for G-code console parsing
- **Progress mode**: `sequential` (known firmware) or `weighted` (generic heuristics)

For developer details on creating profiles for new printers, see [PRINT_START_PROFILES.md](PRINT_START_PROFILES.md).

### Phase Detection

During PRINT_START, HelixScreen detects these preparation phases:

| Phase | Description |
|-------|-------------|
| Initializing | PRINT_START macro detected |
| Homing | G28 / Home All Axes |
| Heating Bed | M190 / M140 |
| Heating Nozzle | M109 / M104 |
| QGL | Quad Gantry Level |
| Z Tilt | Z Tilt Adjust |
| Bed Mesh | BED_MESH_CALIBRATE / mesh load |
| Cleaning | Nozzle wipe / clean |
| Purging | Purge line / priming |
| Complete | Transition to printing |

### Detection Priority

G-code responses are checked in this order (first match wins):

| Priority | Signal | How It Works |
|----------|--------|--------------|
| 1 | **HELIX:PHASE signals** | Universal `HELIX:PHASE:HOMING` etc. from HelixScreen macros |
| 2 | **Profile signal formats** | Exact prefix matching (e.g., Forge-X `// State: HOMING...`) |
| 3 | **PRINT_START marker** | Detects `PRINT_START`/`START_PRINT` once per session |
| 4 | **Completion marker** | `SET_PRINT_STATS_INFO CURRENT_LAYER=`, `LAYER: 1`, `HELIX:READY` |
| 5 | **Profile regex patterns** | G-code command matching (G28, M190, etc.) |

### Completion Fallbacks

For printers that don't emit G-code layer markers, HelixScreen has additional fallback signals:

| Fallback | Condition |
|----------|-----------|
| **Macro Variables** | `_HELIX_STATE.print_started`, `_START_PRINT.print_started`, or `START_PRINT.preparation_done` becomes True |
| **Layer Count** | `print_stats.info.current_layer` becomes ≥ 1 |
| **Progress + Temps** | Print progress ≥ 2% AND temperatures within 5°C of target |
| **Timeout (adaptive)** | Elapsed > 1.5x the predicted total, **and** temps ≥ 90% of target, **and** the printer has said nothing for 90s |
| **Timeout (no history)** | Elapsed > 300s, with the same temp and quiet requirements |
| **Absolute ceiling** | Elapsed > 1800s. The only fallback that ignores the quiet requirement. |

### Files

| File | Purpose |
|------|---------|
| `src/print/print_start_collector.cpp` | Detection engine and fallback implementation |
| `src/print/print_start_profile.cpp` | Profile loading and signal/pattern matching |
| `assets/config/print_start_profiles/*.json` | Printer-specific profile definitions |
| `assets/config/helix_macros.cfg` | Klipper macros for detection and phase tracking |
| `src/printer/macro_manager.cpp` | Macro installation management |
| `src/api/moonraker_client.cpp` | Object subscription setup |
| `docs/devel/PRINT_START_PROFILES.md` | Developer guide for creating new profiles |
