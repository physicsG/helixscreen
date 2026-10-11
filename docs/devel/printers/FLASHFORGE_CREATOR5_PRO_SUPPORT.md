# FlashForge Creator 5 and Creator 5 Pro Support

The FlashForge Creator 5 line is served by the **unified MIPS32 target**: `make creator5`
is an alias of `make mips`, so its binary, toolchain and release asset are the K1/AD5X
one. The board is told apart at runtime, never at compile time (see the unified-block
comment in `mk/cross.mk`). This page covers what is specific to the printers: a 4-tool
toolchanger running a FlashForge Klipper fork on an Ingenic X2000 MIPS host. HelixScreen
carries two entries for the line: the **Creator 5 Pro**, which adds a chamber heater and
chamber fans over the base machine, and the heater-free **Creator 5**. Each is detected
on its own and gets its own preset (see "Printer detection and preset"); everything else
on this page applies to both models.

**Status: the UI runs on the printer** under both firmware families: Z-Mod
(`ghzserg/z_c5pro`) starts it in place of the stock UI (`DISPLAY_OFF HELIX=1`;
prestonbrown/helixscreen#1714), and Reforge (`Klipper4FlashForge/firmware`) ships without
`firmwareExe`, so nothing competes for the framebuffer. `helix-screen --version` answers
over SSH with the NaN2008 toolchain; the kernel refuses legacy-NaN executables with
ENOEXEC (see "NaN encoding").

**Status: the toolchanger is implemented and verified against the `creator5_zmod` mock**:
the four heads are driven by the tool changer backend through Z-Mod's own objects, with
mount/unmount and per-head colour and material (see "Z-Mod tool changer support" below).
A field bundle from a real Creator 5 Pro on Z-Mod 1.7.4 (`5WSPTZBL`, Oct 2026) confirms the
wire contract end to end: `zmod_color.get_status()` publishes `slots` (with per-head
`hasFilament`), `palette` and a live `active_tool_id` — the export
[ghzserg/z_c5pro#1](https://github.com/ghzserg/z_c5pro/pull/1) was upstreamed for, so the
#1714 boot-time and write-through dependencies are satisfied on zmod ≥ 1.7.4. Still open on
hardware: the swap-marker text the step bar parses (below) — if a build echoes different
wording, the bar stays silently suppressed until the parser learns it.

## Hardware (from the stock `firmwareExe` ELF and the device rootfs)

| Spec | Value |
|------|-------|
| SoC | Ingenic X2000 (XBurst2, MIPS32r2, 2x 1.2 GHz) - same family as the Creality K1's X2000E |
| FPU / ABI | Hard float, 64-bit FPU (`-mfp64`), **NaN2008** (`/lib/ld-linux-mipsn8.so.1`) |
| RAM | 128-256 MB |
| Display | 800x480 panel; the framebuffer is exposed **portrait (480x800)**, so the preset sets `display.rotate = 90` (verified on the printer); fbdev `/dev/fb0`, no X11/Wayland/DRM |
| Touch | evdev, `/dev/input/event2` (auto-detected; override with `HELIX_TOUCH_DEVICE`) |
| Stock UI | `firmwareExe` - LVGL 8 on fbdev. **Also** hosts the port-8898 REST API, the MQTT cloud link, and the tool-pickup orchestration for UI-started prints |
| C library | glibc 2.33, built with Ingenic "MIPS Linux Tools GCC12.1 Release6.0.1 xburst2" (`mips-gcc1210-glibc233`), min kernel 3.10.14 |
| Stock libs on device | libcurl, OpenSSL 1.0.0, zlib, OpenCV 4.2, FFmpeg (libav* 56/58), libzip 5, libstdc++ (GCC 12) |
| Python | 3.8.2 at `/usr/prog/Python-3.8.2` |
| Klipper | FlashForge fork at `/usr/prog/klipper`, API socket `/tmp/uds`, config `/usr/data/config/printer.cfg` |
| Moonraker | Present (Mainsail works against the printer), default port 7125 |

### Filesystem layout

FlashForge layout, same family as the AD5X: `/usr/prog/` (programs), `/usr/data/` (user data,
large partition), `/usr/data/config/` (Klipper/Moonraker config), `/usr/data/firmwareRes/`
(stock UI resources, per-unit tool offsets in `config/device_config.conf` - back that up).

### Platform key and self-update

The unified build defines only `HELIX_PLATFORM_MIPS`, so `UpdateChecker::get_platform_key()`
returns `"mips"` and self-update fetches the unified `mips` asset, the same bytes this
board runs. No creator5-specific asset exists or is needed.

The installer keeps the **ad5x** platform key for these boards: `detect_platform()`
(`scripts/lib/installer/platform.sh`) classifies any MIPS box with `/usr/data` +
`/usr/prog` that way, which downloads the unified mips build under the ad5x alias
(correct binary, correct paths). The board's own name comes from the `MACHINE=` line in
the stock `<app_startup.sh>` (`ff_machine_id()`), or, inside the Z-Mod chroot where that
file is unreachable, from the first word of `VERSION_CODENAME` in the chroot's
os-release; so `install.sh` reports a Creator 5 or Creator 5 Pro as itself and only uses
"ad5x" as the install-package label.

## NaN encoding

**This kernel refuses legacy-NaN executables.** Verified on the device: a legacy-NaN build
fails with `execve(...) = -1 ENOEXEC (Exec format error)`, while every stock binary carries
the `nan2008` ELF flag (busybox: `e_flags 0x70001405, noreorder, cpic, nan2008, o32,
mips32r2`; the rejected build was `0x70001007`, identical except for that bit). `-mnan=2008`
alone does not rescue a legacy-NaN toolchain: its `libc.a` is legacy-NaN and `ld` refuses to
link mixed NaN encodings.

That is exactly the ABI the unified target mandates (`-mnan=2008 -mfp64 -march=mips32r2`,
baked into the `helixscreen/toolchain-mips` toolchain and explicit in `mk/cross.mk`), so
the Creator 5 Pro rides it without a dedicated toolchain. The build is fully static (musl),
so the device's glibc 2.33 rootfs is irrelevant to it.

## Building

```bash
make creator5              # alias of: make PLATFORM_TARGET=mips
make creator5-docker       # alias of mips-docker (helixscreen/toolchain-mips image)
file build/mips/bin/helix-screen
# ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped
readelf -h build/mips/bin/helix-screen | grep Flags
# Flags: 0x70001407, noreorder, pic, cpic, nan2008, o32, mips32r2   <- nan2008 is the point
```

Output lands in `build/mips/`, the K1/AD5X build directory, because it is the same build.
The docker image is shared with K1/AD5X (`docker/Dockerfile.mips`), so nothing new is built
on first use if the unified image is already present.

### Release package

The unified binary ships as the `mips` release asset: `make release-mips` builds
`releases/helixscreen-mips-v<ver>.tar.gz` (+ `.zip`, with k1/ad5x transition aliases), and
the release workflow's mips matrix entry publishes it. Same layout as every other platform
tarball (`bin/`, `ui_xml/`, `assets/`, `config/`, `certs/`, `install.sh`); needs the
prerendered images first (`make venv-setup && make gen-all-images`, as `release.yml` does).
No preset is baked in: first boot runs the hardware wizard, whose detection applies
`assets/config/presets/creator5_pro.json` on a Pro and
`assets/config/presets/creator5.json` on a heater-free Creator 5.

The binary locates its data root relative to itself (`<root>/bin/helix-screen` ->
`<root>/ui_xml`), so unpack the tarball as a whole (e.g. to `/usr/data/helixscreen/`)
and run `bin/helix-screen` from there; a bare binary fails with "Could not find
HelixScreen data root". `HELIX_DATA_DIR` overrides the lookup.

### Build configuration

| Setting | Value |
|---------|-------|
| `PLATFORM_TARGET` | `mips` (`creator5` is an alias) |
| Toolchain image | `helixscreen/toolchain-mips`: GCC 13.2 + musl 1.2.4, `mipsel-k1-linux-musl-`, nan2008/fp64 defaults |
| Architecture | `-march=mips32r2 -mtune=mips32r2 -mnan=2008 -mfp64`, little-endian, hard float |
| Linking | Fully static (musl), `-Os`, `-flto=auto`, gc-sections |
| Display backend | fbdev (`/dev/fb0`), 480x800 portrait framebuffer; the preset's `display.rotate = 90` presents it landscape |
| Input | evdev (auto-detect; `HELIX_TOUCH_DEVICE=/dev/input/event2` to pin) |
| SSL | Enabled (static OpenSSL in the toolchain image) |
| Platform defines | `HELIX_PLATFORM_MIPS` only, no creator5 define |
| Sound | Compiled in, same as K1/AD5X (`HELIX_HAS_SOUND` + tracker via the jz_pwm backend); whether this board has `/dev/jz_pwm` hardware is unverified |
| Output | `build/mips/bin/helix-screen`, `helix-splash`; CA bundle in `build/mips/certs/` |

## Printer detection and preset

`assets/config/printer_database.json` carries two entries: `flashforge_creator_5_pro`
(preset `assets/config/presets/creator5_pro.json`, rotate 90) and `flashforge_creator_5`
(preset `assets/config/presets/creator5.json`, rotate 90, no chamber heater or chamber
fans). The chamber heater separates the models the way the chamber light separates the
AD5M pair: the Pro requires `heater_generic chamber_heater` once objects are reported,
while the Creator 5 excludes on its presence, so exactly one entry can score on a given
machine. The heater carries no confidence of its own: its config name is shared with
other enclosed printers (the K2 Plus ships one), so on its own it would name the Pro on
hardware that is not a Creator 5 at all. Two firmware families run on this hardware: the FlashForge fork (K4C5,
with `ff_*` printer objects) and Z-Mod (`ghzserg/z_c5pro`, no `ff_*` objects but
`gcode_button extruder_grab1..4`). The family fingerprints are shared across both
entries: `ff_toolchange` names the K4C5 firmware, `gcode_button extruder_grab1` names
the changer on either firmware, and `zmod_color` is corroborating-only because AD5X
Z-Mod carries it too. Z-Mod 1.7.4+ adds a second corroborator, the `zmod_afclite`
object (the AFC shim's own extra, field-confirmed in bundle `5WSPTZBL`), which the AD5X
never carries. The disambiguation against the AD5X runs both ways: both Creator 5
entries exclude on `zmod_ifs`/`SET_EXTRUDER_SLOT` (AD5X IFS), and the AD5X entry
excludes on `gcode_button extruder_grab1` and `zmod_afclite`. The directional pairs are pinned in
`tests/unit/test_printer_detector.cpp` (`[creator5]`).

Preset macro buttons for macros a firmware does not ship (e.g. `TOOLCHANGE_PARK` on Z-Mod)
render greyed out, not dead: `ControlsPanel::update_macro_button` keeps the button visible
and disabled when the macro is absent.

## Z-Mod tool changer support

On Z-Mod the four heads are driven by the tool changer backend through the Z-Mod row of the
`toolchanger_addon` provider table: mounting is `_T_IN T=<n>`, unmounting is `_T_OUT`, and
the mounted head is read from `zmod_color.active_tool_id` (0..3 mounted, -1 empty carriage,
-2 dock and carriage sensors disagree). Each head's colour and material are read from
`zmod_color.slots` and written back with `CHANGE_ZCOLOR SLOT=<n> HEX=<RRGGBB> TYPE=<type>
SILENT=1`, the colour snapped to Z-Mod's palette first. The full contract, including the
sensor-error and write-through rules, lives in
[`FILAMENT_BACKEND_TOOLCHANGER.md`](../FILAMENT_BACKEND_TOOLCHANGER.md#z-mod-on-the-creator-5-pro).

**Z-Mod 1.7.4's AFC shim must not win the machine.** The firmware publishes an
AFC-shaped surface (`zmod_afclite` registering `AFC`, `AFC_lane E0..E3`, `AFC_unit C5`) so
Fluidd and Mainsail draw their AFC panel for the C5's heads. Discovery yields that claim to
the tool changer (`PrinterDiscovery::parse_objects`, the carve-out beside the PAXX AFC-Lite
rule): the shim reports no extruders and no hubs, so an AFC backend would draw a hub
topology for four independent heads and send Happy Hare macros the firmware does not have.
The `zmod_afclite` object also corroborates both Creator 5 database entries and is an
exclude on the AD5X entry.

**The swap routine and its step bar.** A tool change cuts the filament at a station and
wipes the nozzle on rubber instead of purging bulk filament (FlashForge's own wiki: the
cutter "eliminates purging during filament switches"; a small prime tower still catches
ooze). Z-Mod's per-material numbers live in zmod's /mod_data/filament.json: `temp` /
`temp_manual` / `temp_wait` (load-heat / manual-purge / idle), `filament_drop_length`
(50 mm purge into the trash), `filament_tube_length` (295 mm feed), with the trash bin at
`trash 275/254/10` and the rubber wiper at `wiper 266.5/13.8/1`. Grab and unload retry up
to 5 times (`[zmod_color] retry:`). The macros narrate the swap through respond_info lines
— `Grab attempt for T<n>: i of N...`, `Unload attempt for T<n>: ...`, and the per-head
`X/Y/Z-Offset: ... _T_IN` / `_T_OUT` echoes — and the tool changer backend parses exactly
those markers (`toolchanger_addon::parse_zmod_marker`) to drive the operation step bar:
a mount is [Pick up tool], an unmount is [Dock tool], a change is [Dock tool, Pick up
tool]. The marker text is field-verified (bundle `5WSPTZBL`); a firmware build that stops
echoing it degrades to no step bar, never a wrong one.

Z-Mod's C5 "endless spool mode" (on runout, switch to the next head whose colour AND type
both match) is firmware-managed and always on; the backend reports it the same way the
AD5X IFS backend reports stock zMod (Available / FirmwareManaged / ReadOnly, provider
"zmod").

**Beyond the swap itself, three more Z-Mod surfaces are consumed** (details in the
toolchanger backend doc's Z-Mod section): `T_INFO` — the firmware's read-only tool-state
query — is sent at connect and after `FIRMWARE_RESTART`, and its answer clears the stale
boot carriage reading on firmware older than the `zmod_color` export; the afclite shim's
fault flag (`error_state` + `message`) surfaces as this backend's own fault dialog, while
its activity word and lane map are logged for field visibility and acted on for nothing
(vocabulary unverified — a debug bundle captured during a real load shows both); and
`zmod_color.slots[].hasFilament` stamps a docked head without filament as Empty.

**The filament-feed macros are deliberately not wired.** `LOAD_FILAMENT`,
`UNLOAD_FILAMENT`, `LOAD_MATERIAL`, `TOOL_UNLOAD` and `LANE_UNLOAD` all exist on this
firmware and all move hardware (heat, feed, cut, wipe), but no capture shows their
parameter shapes — which of `T=`, `SLOT=`, `LANE=` or bare-operates-on-active each takes —
and a guessed parameter can drive the wrong head's feed motor. The afclite state logging
above is the instrument that settles it: once a capture shows the words and the
parameters, wiring them is a provider-row change. Until then the macros remain reachable
through the Macros panel, which is where they are safe.

One upstream dependency: [ghzserg/z_c5pro#1](https://github.com/ghzserg/z_c5pro/pull/1)
adds `slots`, `palette` and a live `active_tool_id` to `zmod_color.get_status()`. Without
it a Z-Mod C5 boots showing a dock sensor error until the first `_T_IN` / `_T_OUT` /
`GET_ZCOLOR`, and colour or material edits stay local to HelixScreen instead of reaching
filament.json on the printer. **zmod ≥ 1.7.4 ships the export** (field-confirmed in bundle
`5WSPTZBL`); older Z-Mod builds keep the degraded behaviour. Hardware verification of the
step-bar markers on a live swap remains open (prestonbrown/helixscreen#1714).

### The `creator5_zmod` mock persona

`HELIX_MOCK_PRINTER=creator5_zmod ./build/bin/helix-screen --test` runs the real
`AmsBackendToolChanger` against mock Z-Mod hardware: the persona publishes `zmod`,
`zmod_color`, `save_variables`, the 1-based `gcode_button extruder_pos1..4` /
`extruder_grab1..4`, Z-Mod 1.7.4's afclite shim objects (`zmod_afclite`, `AFC`,
`AFC_lane E0..E3`, `AFC_unit C5`) and the `fd_ex`/`fm_ex` sensor pairs, answers
`_T_IN` / `_T_OUT` / `CHANGE_ZCOLOR` — narrating the swap markers the step bar parses —
and implies `--real-ams` so the mock AMS gate does not stand a mock backend in front of
it. The `creator5` persona models the Reforge side only. Details in
[`MOCK_ENVIRONMENT_VARIABLES.md`](../MOCK_ENVIRONMENT_VARIABLES.md).

## On-device bring-up

Each item below says whether it is done or still open.

1. **Sanity on the printer** (done): `bin/helix-screen --version` runs. (`readelf -h` on
   the binary must list `nan2008` in Flags; without it the kernel answers ENOEXEC.)
   `install.sh` names the board from its `MACHINE=` line (see "Platform key and
   self-update" above) and installs the unified mips payload.
2. **Stock UI coexistence** (done): on the stock firmware, unlike the K1's
   `display-server`, `firmwareExe` is not just a UI: stopping it also kills the 8898 REST
   API, the cloud link and the stock print orchestration (tool grab/release for
   UI-started prints), and two processes drawing to `/dev/fb0` will fight. The other
   firmwares sidestep it: Z-Mod (`ghzserg/z_c5pro`) starts HelixScreen in place of the
   stock UI (`DISPLAY_OFF HELIX=1`), and Reforge (`Klipper4FlashForge/firmware`) removes
   `firmwareExe` entirely.
3. **Touch** (done): `/dev/input/event2`; if auto-detect picks another device, pin it
   with `HELIX_TOUCH_DEVICE`. The capacitive Goodix controller declares an 800x480 ABS
   range on the 480x800 portrait framebuffer: transposed, not mismatched. HelixScreen
   scales such a range by the display size and leaves rotation to LVGL
   (`has_transposed_abs_range()`, prestonbrown/helixscreen#1450; versions without that
   handling force a bad calibration, so they need the axes swapped by hand,
   prestonbrown/helixscreen#1714). With `display.rotate = 90` the touch input is
   auto-rotated to match the display, so `HELIX_TOUCH_SWAP_AXES` must NOT be set: it
   would swap already-correct axes.
4. **Moonraker** (done): HelixScreen talks to Moonraker (not `/tmp/uds`). The Moonraker
   instance that Mainsail uses is the one to point at (port 7125 unless `moonraker.conf`
   says otherwise).
5. **Detection + preset** (done): `printer_database.json` entries
   `flashforge_creator_5_pro` (fingerprint: `ff_toolchange` / `gcode_button
   extruder_grab1`, 4 extruders, requires `heater_generic chamber_heater`) and
   `flashforge_creator_5` for the heater-free model (same fingerprints, excluding on the
   chamber heater). Presets: `creator5_pro.json` (4 hotends, chamber heater,
   part/chamber fans, LED, `fd_ex*` switches as per-head runout sensors, rotate 90) and
   `creator5.json` (the same minus the chamber heater and chamber fans). Without the
   entries the detector resolves the AD5X, which matches on hostname, MIPS and 4 tools;
   the entries are what tell them apart, with the AD5X side excluding on
   `gcode_button extruder_grab1`. Each entry carries its own image:
   `flashforge-creator-5-pro.png` / `flashforge-creator-5.png`.
6. **Toolchanger model** (done): 4 extruders (`extruder`, `extruder1..3`), 4 filament
   switch + 4 motion sensors (`fd_ex0..3`, `fm_ex0..3`), `heater_generic
   chamber_heater`, `fan_generic fanM106` (part), `heater_fan heat_fan*`, `fan_generic
   chamber_*_fan`, `led chamber_led`. The chamber heater and the `chamber_*_fan`s are
   the Pro's hardware; the heater-free Creator 5 reports neither, and its preset drops
   both.
7. **Memory** (open): 128-256 MB shared with Klipper, Moonraker, `firmwareExe`.
   HelixScreen's ~15 MB footprint is fine, but check `free` with the stock stack
   running.
8. **Init** (open): no systemd; the stock stack is started from BusyBox init scripts. An
   init.d script modeled on the AD5X/ZMOD `S80guppyscreen` pattern is the likely shape.
9. **Per-head runout** (hw-verify): the presets give `fd_ex0..3` the runout role and
   `"lane": N`, because the names encode no head. Z-Mod holds every `fd_ex`/`fm_ex` sensor
   disabled in standby and enables only the active head's pair while printing
   (`_ENABLE_SENSOR` keys on `zmod_color.active_tool_id`), so a runout alert only ever
   comes from the printing head: a sensor the firmware holds disabled raises none (#1714).
   The pre-print check reads the heads the file uses, and their sensors, even while
   stood down: `AmsBackendToolChanger` answers `slot_status_tracks_filament()` false,
   so `FilamentSensorManager#scan_required_lanes` takes each required head's `fd_ex`
   reading as that head's filament state. Not yet confirmed on a real C5: whether a
   stood-down switch keeps reporting live.
