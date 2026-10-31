# QIDI Plus 5 — Real-Device Analysis

**Date**: 2026-10-13
**Status**: Real device data analyzed (stock firmware 01.01.01.07)
**Source**: Live stock QIDI Plus 5 with SSH, Moonraker `info` / `objects/list` /
`configfile` JSON, `journalctl`, and the on-device `qidiclient` install.

> The Plus 5 is the newest QIDI and shares the **new-generation board** class with
> the Q2 and Max 4 (Rockchip host + STM32 main MCU + Linux framebuffer display),
> but with a **sideways 480×800** panel and a **~333×345 mm** stepper travel
> (~325 mm print area). This note records
> what the hardware exposes, so the database entry and preset key off real
> signals rather than assumptions.

---

## 0. Host / OS

| | |
|---|---|
| SoC | Rockchip **RK3308B-S**, 4× Cortex-A35 @ 1.1 GHz, **no GPU** |
| RAM / storage | ~498 MB / 29 GB eMMC (one ext4 root partition) |
| OS | Debian 11 (bullseye), glibc 2.31, kernel **5.10.160** (QIDI vendor) |
| **Virtual terminals** | **none** — `CONFIG_VT=n`, no `/dev/ttyN`, console is the serial `ttyFIQ0` |
| MCUs | **STM32F407** (main) + **STM32F103xe** (toolhead, `[mcu THR]` on `/dev/ttyS4`) |

Because there are no VTs, Xorg/KlipperScreen cannot start normally (`parse_vt_settings:
Cannot open /dev/tty0`). HelixScreen is unaffected: its Pi/aarch64 build renders
through **LVGL's DRM/fbdev backends directly** (SDL is a desktop-only backend) and
needs no display server. (For the record, the no-VT Xorg workaround is `Xorg :0 vt08
-sharevts` with `/dev/tty8 -> /dev/null` — see the companion community project.)

## 1. Display + stock UI

- **Panel**: 480×800 RGB panel on the SoC, exposed as `/dev/fb0` (`/proc/fb` →
  `rockchipdrmfb`). Touch: **Goodix** capacitive on `/dev/input/event0` (I2C),
  ABS range X 0..479 / Y 0..799, no calibration needed.
- **Orientation**: native framebuffer is portrait 480×800, but the panel is
  mounted **sideways**. `display.rotate = 90` (or `270`) renders it as landscape
  800×480 — the usable orientation, and what the preset sets.
- **Stock UI**: a native **LVGL** application, `/home/qidi/QIDI_Client/bin/qidiclient`
  (90 MB, systemd service **`qidi-client`**), assets in `QIDI_Client/access/*`.
  It is a closed binary (the community only re-themes its LVGL icons/fonts), and it
  is what the on-device install replaces. The host talks to Moonraker as WebSocket
  agent `QD_PLUS5` (`WebSocket++`).

## 2. Klipper fingerprint (what detection sees)

- **Kinematics**: corexy; 2 Z steppers (`stepper_z`, `stepper_z1`) with `z_tilt`.
- **Build volume**: stepper travel **X ≈ 333 mm** (`position_min -7` … `position_max 326`),
  **Y ≈ 345 mm** (`-5` … `340`). This is the model's discriminator inside the
  framebuffer family — Q2 ≈ 275 mm, Max 4 ≈ 390 mm. The `bed_mesh` probed area is
  290×290.
- **Heaters/sensors**: `heater_bed`, `extruder` (max 375 °C), active
  `heater_generic chamber` (0–70 °C), `temperature_sensor
  Chamber_Thermal_Protection_Sensor`.
- **Fans (6)**: `fan_generic cooling_fan`, `heater_fan hotend_fan`,
  `controller_fan chamber_fan`, `fan_generic chamber_circulation_fan`,
  `controller_fan board_fan`, **`fan_generic auxiliary_cooling_fan`** (single aux —
  the Max 4 carries a second `auxiliary_cooling_fan2`).
- **Lights**: `output_pin caselight` (+ `smart_output_pin caselight`), `neopixel RGB`.
- **Filament**: `filament_switch_sensor filament_switch_sensor`; QIDI Box host
  (`multi_color_controller`, `ai_func_manager`), compatible with the hub-style
  4-slot box chainable to 16.
- **Macros** (stock `klipper-macros-qd/*`): `M4029`/`M4030`/`M4031`/`M4032`
  calibration, `CLEAR_NOZZLE`, `M141`/`M191` chamber, `M603`/`M604` load/unload,
  `CUT_FILAMENT_1`, `EXTRUSION_AND_FLUSH`.
- 174 objects total; `mcu` reports `stm32f407xx`, `mcu THR` reports `stm32f103xe`.

### Detection caveat: hostname is not the host name

HelixScreen's detector hostname is Moonraker's **`machine_name`**, which QIDI sets
to the literal **`QIDI`** — *not* the real hostname (`qidi-plus5`), and not the
`/printer/info` hostname. So `hostname_match` patterns collapse to `qidi` on stock
firmware and cannot separate the models; the Plus 5 is identified by its **bed
size** instead (see § 3).

## 3. Database entry rationale

The `qidi_plus_5` entry identifies by the **`build_volume_range` window 324–342 ×
336–354 mm** (`"identifying": true`): the bed size is the one signal that names the
model among the QIDI framebuffer family (Q2 ≈ 275, Max 4 ≈ 390), and it never matches
those siblings. Because a bare corexy with the same travel would otherwise be saved
as a Plus 5, the entry also carries a **`sensor_required` on
`Chamber_Thermal_Protection`** (a QIDI config marker): a machine without it is
excluded outright, so bed size alone never identifies a Plus 5. The remaining
QIDI-shared signals (chamber heater, aux fan, `multi_color_controller`, `probe_air`,
`M4029`, `CLEAR_NOZZLE`, STM32F407) are marked **`corroborating`** — support only.

> A stricter shape suggested in review — bed size as a `"separator"` inside an
> already-identified family (the K2 pattern) — needs the whole QIDI family's
> identifying evidence normalised so the siblings tie before the bed breaks the tie.
> That is a larger change than this entry and is left for a follow-up.

The behaviour is pinned by `tests/fixtures/printers/qidi_plus5.json` (with a corpus
row in `detection_cases.json`) and by the negative test that a nameless corexy at the
same bed size is not saved as a Plus 5.

The preset (`assets/config/presets/qidi_plus5.json`) carries the hardware mappings,
`display.rotate = 90`, and the CLEAR_NOZZLE / M4029 quick macros.

## 4. On-device install notes

- Stock firmware runs standard **Moonraker on `:7125`**. HelixScreen's installer
  disables the stock screen at install time through `competing_uis.sh`, which takes
  down the `qidi-client` unit **by name** (it is in `COMPETING_UIS`). The boot-time
  `hooks-qidi.sh` platform hook does **not** fire here: its `_is_qidi_class_sbc`
  fingerprint requires `/etc/hostname` to be `linaro-alip`, and the Plus 5's is
  `qidi-plus5`. The `ExecStart` arm does not match either — the unit runs
  `bash /home/qidi/QIDI_Client/bin/start.sh`, and the `qidiclient` binary is `exec`ed
  from inside `start.sh` — and `QIDI_STOCK_UI_BINS` lists only `…/QD_Q2/bin/client`.
  The screen app and QIDI cloud/camera stop while HelixScreen runs; uninstalling
  brings them back. (Extending the fingerprint/bins, with a bats test, is a proposed
  follow-up.)
- HelixScreen auto-selects its **DRM** backend and the Goodix touch device; with
  rotation set it transparently uses the framebuffer path for software rotation.
  `display.rotate = 90` was confirmed correct on hardware.
- RAM is fine: HelixScreen ran at ~49 MB RSS alongside Klipper/Moonraker on the
  498 MB host.
