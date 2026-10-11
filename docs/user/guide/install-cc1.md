# Installing HelixScreen on the Elegoo Centauri Carbon

How to get HelixScreen onto an Elegoo Centauri Carbon: flash the community [OpenCentauri COSMOS](https://docs.opencentauri.cc/klipper-conversion/cosmos/cosmos/) firmware first, then install HelixScreen on top of it.

## Tested With

> **Tested and working.** Prebuilt binaries ship in releases and the installer has auto-detection support. Requires the community [OpenCentauri COSMOS firmware](https://docs.opencentauri.cc/klipper-conversion/cosmos/cosmos/); stock Elegoo firmware is not supported (no SSH, no Klipper, no Moonraker).
>
> **Minimum COSMOS version: 26.07.0.** Tested on COSMOS 26.08.0. COSMOS 0.0.7 and earlier are not supported: they lack the calibration and filament macros HelixScreen drives.

## Prerequisites

- **Hardware:**
  - Elegoo Centauri Carbon (4.3" 480×272 touchscreen, Allwinner R528, armv7l)
  - Network connection (WiFi or Ethernet)
  - A USB stick formatted FAT32 (for the firmware flash in Step 1)
- **Software:**
  - [OpenCentauri COSMOS firmware](https://github.com/OpenCentauri/cosmos/releases) **26.07.0 or newer** installed (replaces stock Elegoo firmware; ships Klipper + Moonraker + grumpyscreen/atomscreen/guppyscreen)
  - SSH access: `root` / default password `OpenCentauri` (change it after install)

## Installation

### Step 1: Install COSMOS firmware

OpenCentauri COSMOS is a full firmware replacement for the Centauri Carbon. It ships with Klipper, Moonraker, Mainsail, and a `gui-switcher` that lets you pick which touch UI to run.

1. Download the latest `update.swu` from https://github.com/OpenCentauri/cosmos/releases
2. Copy it to the root of a FAT32-formatted USB stick
3. Insert the USB stick into the printer, power on
4. From the stock Elegoo UI, navigate to the firmware-update menu and apply the update
5. **First boot takes 5-10 minutes** while it reflashes the toolhead and bed boards; be patient
6. After reboot, connect to WiFi from the COSMOS UI and note the printer's IP address

Already running COSMOS? Check the version before installing HelixScreen:

```bash
curl -s http://<ip>/printer/info | grep -o '"software_version": *"[^"]*"'
# "software_version": "Release - 26.08.0"
```

Mainsail shows the same string on its Machine page. Anything older than `Release - 26.07.0` needs the COSMOS update above first.

If the update fails or the device won't boot, consult the OpenCentauri [install guide](https://docs.opencentauri.cc/klipper-conversion/cosmos/install/) and [emergency USB recovery](https://docs.opencentauri.cc/software/updates/) docs.

### Step 2: Install HelixScreen

SSH into the printer (replace `<ip>` with your printer's IP):

```bash
ssh root@<ip>
# Default password: OpenCentauri
```

Then run the installer:

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh
```

### Step 3: Switch back to another UI (optional)

COSMOS's `config-manager` tool lets you switch between installed UIs without uninstalling HelixScreen:

```bash
config-manager ui screen_ui grumpyscreen   # or atomscreen, guppyscreen - any of the three launches HelixScreen via its wrapper
/etc/init.d/gui-switcher restart
```

## What the Installer Does on This Printer

The installer auto-detects COSMOS, installs HelixScreen to `/user-resource/helixscreen/`, and registers it with `gui-switcher` through an allowlist wrapper (see Quirks below). It stops the currently active UI (grumpyscreen, atomscreen, or guppyscreen) and starts HelixScreen in its place.

- Install directory: `/user-resource/helixscreen/` (`/` is read-only squashfs on COSMOS)
- Settings, logs and caches live beside it in `/user-resource/helixscreen-state/`, so a Moonraker web update, which replaces the install directory wholesale, leaves them untouched
- Init script: `/etc/init.d/helixscreen`; the watchdog also publishes its PID at `/var/run/gui.pid` so `gui-switcher` can stop it

## Service Control and Logs

```bash
/etc/init.d/helixscreen restart

# Structured app log (written to disk; the in-memory syslog only has early startup)
tail -100 /user-resource/helixscreen-state/logs/helix.log

# Launcher / crash capture (startup, crash output)
tail -100 /user-resource/helixscreen/logs/launcher.log
```

## Updating

Check the installed version on the touchscreen (**Settings > Help & About > About**), or from SSH with `/user-resource/helixscreen/bin/helix-screen --version`.

Re-run the installer with `--update`; it preserves your settings:

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --update
```

To pin a specific version add `--version vX.Y.Z`, or swap `--update` for `--clean` to reinstall with fresh settings. See [Updating HelixScreen](../INSTALL.md#updating-helixscreen) for the universal details.

## Uninstalling

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --uninstall
```

The uninstaller reverses the `gui-switcher` registration, including the allowlist wrapper described below, and restores the UI that was running before.

## Quirks and Notes

- **Slice with the COSMOS printer profile.** Elegoo's stock Centauri Carbon start G-code calls `M729`, and COSMOS turns `M729` into an emergency stop (26.08 shows "Use COSMOS OrcaSlicer profile (or COSMOS start/end machine gcode)"; 26.07 shows "Use COSMOS start/end machine gcode"). A file sliced with the stock profile halts the printer as soon as it starts, and Klipper needs a `FIRMWARE_RESTART`. Re-slice with the COSMOS OrcaSlicer profile, whose start G-code is a single `PRINT_START EXTRUDER=... BED=... CHAMBER=...` line
- **Moonraker listens on port `80`** on COSMOS directly (no nginx). Enter `80` as the port at the wizard's connection step; the `7125` default does not apply on this printer
- **Factory white-balance calibration**: the `cc1` preset ships with per-channel panel gain so colors look neutral out of the box on the Centauri Carbon's 4.3" panel. No manual tuning needed
- **The `config-manager` allowlist**: COSMOS's `config-manager` has a fixed allowlist for the `screen_ui` slot. The installer handles this automatically via an init-script wrapper so HelixScreen can be selected without patching COSMOS itself; the uninstaller fully reverses it

**Testing on this printer?** Please report your results via [GitHub Issues](https://github.com/prestonbrown/helixscreen/issues) or [Discord](https://discord.gg/RZCT2StKhr).

## See Also

- [Supported printers](supported-printers.md#other-dedicated-builds): where the Centauri Carbon sits in the support matrix
- [Troubleshooting](../TROUBLESHOOTING.md): log collection and common problems
- [UPGRADING.md](../UPGRADING.md): version pinning, resets, migrations
