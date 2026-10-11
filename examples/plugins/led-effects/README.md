# led-effects

The permissioned HelixScreen Lua plugin: a home tile that turns one
klipper-led_effect effect on or off and shows its live state. The companion to
temp-spark for plugin authors who need to send G-code.

## What it demonstrates

- `helix.gcode` behind the `gcode` permission: enabling the plugin in Settings
  asks for consent to send G-code, and only the granted plugin can send it.
- `helix.moonraker.subscribe`: the tile's state is Klipper's own `enabled`
  value, delivered live, so a change made anywhere else reaches the tile too.
- Validating a setting before it reaches G-code: the effect name must match
  `^[%w_]+$` and stay short enough that `"led_effect " .. name` fits
  subscribe's 64-byte object-name cap, or the tile says "Invalid name" and
  nothing is sent.
- `helix.widget` `on_size`: at two cells wide the tile also names the effect.
- `bind_flag_if_eq` instead of `cond`: a plugin id containing a hyphen cannot
  use `cond`, so show/hide is a flag bound to a subject.
- `helix.ui.toast`: a refused toggle reports the failure instead of failing
  silently.

## Run it

```sh
HELIX_PLUGIN_DIR=examples/plugins ./build/bin/helix-screen --test -vv
```

Then Settings > Plugins > LED Effects > enable, approve the G-code permission,
and add the tile from the home panel's widget catalog (Plugins category; free
a cell first if the page is full). The `--test` printer has the effects
`breathing`, `fire_comet`, `rainbow` and `static_white`; the Effect name
setting in the plugin's settings screen picks which one the tile toggles.

## Layout

- `manifest.json` id, one 1x1 widget that grows to 2x1, the `gcode`
  permission, one string setting.
- `main.lua` subjects, the live subscription, the validated toggle.
- `ui/led-effects__tile.xml` two icons that swap on the state, plus the
  effect's name at two cells wide.

The old C++ demo toggled a hard-coded `chamber_light` with plain `SET_LED` and
reacted to print events. Plain light toggling lives in the built-in LED
widget, and the plugin API has no print events: watch
`helix.printer.watch("print_state", fn)` instead.
