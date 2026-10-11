# calibration-bridge

> **Proof of concept.** The screen half below is built and unit-tested, but
> no companion exists yet: the OrcaSlicer plugin that would send these
> events and receive the answers is still being developed by a community
> member, and Orca's plugin system is read-only today. The contract in this
> README is a proposal for that companion to code against - and the payload
> the unit tests post is exactly what it will look like.

Run slicer-side filament calibrations at the printer. A companion - an
OrcaSlicer plugin reading the loaded profile - posts the wizard context
through Moonraker agent events; the screen starts the pre-sliced calibration
print, asks which result is best, and posts the answer back to the
companion, which (once Orca plugins can write) lands it in the profile.

This is the screen half of that story, and the plugin system's first
**overlay-only** plugin: no home tile, because calibration is episodic. The
companion event is the front door; a settings action row is the manual one.

The author guide is [docs/devel/PLUGIN_DEVELOPMENT.md](../../docs/devel/PLUGIN_DEVELOPMENT.md).

## The contract (a companion implements the other side)

Events arrive as Moonraker agent events (websocket `notify_agent_event`,
filtered by event name; `agent` is the companion's name, `data` carries the
payload):

- `helixcal.available` - a calibration is ready to run.

  ```json
  {"wizard": "temp_tower", "filament": "Generic PLA - Voron Rapido",
   "printer": "Voron 2.4 300", "nozzle_mm": 0.4, "current": 220,
   "choices": [190, 195, 200, 205, 210, 215, 220, 225, 230],
   "file": "gcodes/cal_temp_tower.gcode"}
  ```

  `wizard` is one of `temp_tower`, `pa_pattern`, `flow_rate` - it picks the
  label and the number format (`%d°C`, `%.2f`, `%d%%`). `choices` and
  `current` are unit-free numbers; the plugin formats them. Whole numbers
  where the format is whole (`temp_tower`, `flow_rate`), and a fully valid
  event - known wizard, at least two numeric choices, numeric `current` - or
  nothing is applied and the status line says `bad event`. `file` is the
  pre-sliced print (skipped if empty).

- `helixcal.cancel` - `{}`. Resets to idle.

The answer goes to the plugin's **Companion result URL** setting as a POST:

```json
{"event": "chosen", "wizard": "temp_tower", "value": 205,
 "filament": "Generic PLA - Voron Rapido", "current": 220}
```

What the user picked in the dropdown is exactly what `value` carries - one
formatter renders both the label and the payload.

## What it demonstrates

- **Overlay-only plugins**: `widgets: []` in the manifest, the overlay is
  the whole UI. Entry points: the agent event (auto-open), the settings
  action row, and a toast when nothing is running.
- `helix.moonraker.on_agent_event`: the companion channel, filtered by
  event name, delivered on the main thread.
- **A dropdown fed from an event**: XML cannot build widgets at runtime, so
  the choice list rides in through a component prop (`<api><prop>`,
  newline-separated) filled by `helix.ui.overlay` attrs at open time - and
  the selection writes back through the engine's two-way
  `bind_value` subject binding, the one widget-to-Lua path that needs no
  widget access. **Next** accepts the held selection.
- `helix.moonraker.call("printer.print.start", ...)`: the `moonraker_write`
  permission starting the pre-sliced print, then `helix.printer.watch` on
  `print_state` handing over to the picker on completion (or back to
  available with the reason on failure).
- The quiet failure policy: a refused event, a failed print start or a
  failed POST is a status line, never a toast storm and never a fault.
- `i18n/de.xml`: the overlay, the state machine's words and the manifest.

## Run it

```sh
HELIX_PLUGIN_DIR=examples/plugins ./build/bin/helix-screen --test -vv
```

Then Settings > Plugins > Calibration Bridge > enable (it asks to connect to
servers on your network and the internet, and to change printer settings
through Moonraker - the `http` and `moonraker_write` permissions), set the
Companion result URL, and fire an event at it. There is no companion to send
events yet (see the note above): today the way to see the flow is the unit
tests, which post exactly the payloads in the contract, end to end.

## Layout

- `manifest.json` no widgets, the two permissions, the URL/auto-open/auto-ask
  settings, the action row.
- `main.lua` the state machine (idle / available / printing / asking / done),
  the agent-event and print-state wiring, the result POST, the status-key
  re-render on language switch.
- `ui/calibration-bridge__ask.xml` the overlay: context card, Start row,
  printing line, picker row (label + dropdown), result row, Cancel/Next.
- `i18n/de.xml` the translation pack.
