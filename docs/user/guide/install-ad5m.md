# Installing HelixScreen on the Flashforge Adventurer 5M / 5M Pro

Everything you need to run HelixScreen on a Flashforge Adventurer 5M or 5M Pro (AD5M): install, update, service control, and uninstall. HelixScreen runs directly on the printer's stock 4.3" touchscreen, replacing GuppyScreen (on Forge-X), KlipperScreen (on Klipper Mod) or the stock screen (on ZMOD).

> **Easy installer:** the [HelixScreen easy installer](https://helixscreen.org/installer/) can update HelixScreen on a Forge-X Adventurer 5M from a desktop app. Installing the firmware mod and a first install are still done by hand with this guide.

> **Tested versions:** Most thoroughly tested on Forge-X 1.4.0 with Flashforge firmware 3.1.5. Other versions may work fine.

---

## Prerequisites

- **Hardware:**
  - Flashforge Adventurer 5M or 5M Pro
  - Stock 4.3" touchscreen (800x480)
  - Network connection

- **Software:**
  - A firmware mod that gives you Klipper, Moonraker and root access. Stock Flashforge firmware cannot run HelixScreen. Pick one:
    - [Forge-X](https://github.com/DrA1ex/ff5m) (**recommended**): the setup HelixScreen is tested on most
    - [ZMOD](https://github.com/ghzserg/zmod): turns HelixScreen on with one command, no SSH needed
    - [Klipper Mod](https://github.com/xblax/flashforge_ad5m_klipper_mod): still works, but no longer actively developed
  - SSH access to the printer (usually `root@<printer-ip>`), except on ZMOD
  - About 100MB free disk space

### Firmware Variants

The installer automatically detects which firmware you're running and configures paths accordingly:

| Firmware | Replaces | Install Location | Init Script |
|----------|----------|------------------|-------------|
| **Forge-X** | GuppyScreen | `/opt/helixscreen/` | `S90helixscreen` |
| **Klipper Mod** | KlipperScreen | `/root/printer_software/helixscreen/` | `S80helixscreen` |
| **ZMOD** | Stock screen | `/srv/helixscreen/` | `S80helixscreen` |

> Klipper Mod **v00.06 and newer** installs to `/opt/helixscreen/` (still `S80helixscreen`); `/root/printer_software/helixscreen/` is the v00.05-and-older location.

On Klipper Mod, swapping KlipperScreen for HelixScreen frees a substantial chunk of the AD5M's limited RAM; see [Memory Constraints](#memory-constraints) below.

### Step 0: Install a Firmware Mod

Every mod installs the same way stock firmware updates do: a file on a FAT32 USB stick, inserted before you power the printer on. Each mod lists the stock firmware versions it supports, so check yours first (on the printer: **Settings > About**).

**Forge-X (recommended).** Supports stock firmware 2.6.5 through 5.1.x. Follow the [Forge-X install guide](https://github.com/DrA1ex/ff5m/blob/main/docs/INSTALL.md), including its notes on downgrading and the `-Factory` image if your version needs one. Then set it up for HelixScreen as described in [Forge-X: Set Up GUPPY Mode First](#forge-x-set-up-guppy-mode-first) below, and install HelixScreen with [Option 1](#option-1-automated-install-recommended).

**ZMOD.** Supports stock firmware 2.7.5 and newer. Follow the [ZMOD install guide](https://wiki.zmod.link/Setup/), then turn HelixScreen on as described in [ZMOD: Turn HelixScreen On](#zmod-turn-helixscreen-on). There is no SSH step.

> **Already running the old `prestonbrown/ff5m` image?** It is Forge-X with HelixScreen already installed, and it is no longer maintained. HelixScreen keeps working and updates normally (see [Updating](#updating)). For newer Forge-X versions, follow [upstream Forge-X](https://github.com/DrA1ex/ff5m)'s update instructions.

### Forge-X: Set Up GUPPY Mode First

**Important:** Forge-X must be installed and configured for GuppyScreen mode **before** installing HelixScreen. HelixScreen uses Forge-X's infrastructure (Klipper, Moonraker, backlight control) but replaces the GuppyScreen UI.

1. Install Forge-X following [their instructions](https://github.com/DrA1ex/ff5m)
2. Configure Forge-X with `display = 'GUPPY'` in variables.cfg
3. Verify GuppyScreen works on the touchscreen
4. Then run the HelixScreen installer

For everything the installer then changes on Forge-X, see [What the Installer Does](#what-the-installer-does).

---

## Installation

> **Important:** Installing HelixScreen replaces your current screen UI (GuppyScreen on Forge-X, KlipperScreen on Klipper Mod, the stock screen on ZMOD). Make sure you have a backup method to access your printer (SSH, Mainsail/Fluidd web interface).

### ZMOD: Turn HelixScreen On

ZMOD installs and updates HelixScreen itself, through Moonraker's update manager. It takes three steps. Open Fluidd or Mainsail in a browser (`http://<printer-ip>`) and use the console for the commands.

1. Run `ENABLE_EXTRA_PLUGINS` in the console. This adds ZMOD's list of third-party plugins to Moonraker, which includes the `helixscreen` update-manager entry, and reboots the printer. It does not install HelixScreen yet.
2. When the printer is back, open the update manager in Fluidd or Mainsail and update **helixscreen**. ZMOD starts it at version v0.0.1, so the current release shows as an update; applying it downloads `helixscreen-ad5m.zip` into `/srv/helixscreen`.
3. Run `DISPLAY_OFF HELIX=1` in the console. This turns the stock screen off and selects HelixScreen as the replacement, then reboots. Do this only after step 2: if no HelixScreen files are installed yet, the printer comes up with no screen at all.

From then on, HelixScreen updates through the same update manager entry. Because ZMOD manages HelixScreen, update and remove it through ZMOD rather than with the installer commands on this page. To go back to the stock screen, run `DISPLAY_ON`.

> **Read ZMOD's warning first.** With the stock screen off, the printer no longer runs the stock screen's print start and end routines, so bed leveling, Z-offset and your slicer's start and end G-code work differently. See ZMOD's [DISPLAY_OFF documentation](https://github.com/ghzserg/zmod/blob/main/docs/en/System.md) before you switch.

### Option 1: Automated Install (Recommended)

The AD5M uses BusyBox, which doesn't support HTTPS downloads directly. This is a **two-step process**:
1. Download on your local computer (Steps 1-2)
2. SSH into the printer as root and run the installer (Step 3)

**Step 1: Download on your computer**

Go to the [latest release page](https://github.com/prestonbrown/helixscreen/releases/latest) and download:
- `helixscreen-ad5m.zip` (the AD5M release archive)
- `install.sh` (the installer script, under "Assets")

Or use the command line (replace `vX.Y.Z` with the actual version):
```bash
VERSION=vX.Y.Z  # Check latest at https://github.com/prestonbrown/helixscreen/releases/latest
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
wget https://releases.helixscreen.org/install.sh
```

**Step 2: Copy to your printer**

```bash
# AD5M requires -O flag for scp (BusyBox lacks sftp-server)
# Note: Use /data/ not /tmp/ - AD5M's /tmp is a tiny tmpfs (~54MB)
scp -O helixscreen-ad5m.zip install.sh root@<printer-ip>:/data/
```

> **Windows users:** The `-O` flag is not supported by Windows 11's built-in OpenSSH.
> Use one of these alternatives instead:
> - **WSL** (recommended): open a WSL terminal and run all commands as shown (Linux tools work natively)
> - **[WinSCP](https://winscp.net/)** (free, GUI): set the protocol to **SCP**, then drag and drop files to `/data/` on the printer
> - **[PuTTY pscp](https://www.chiark.greenend.org.uk/~sgtatham/putty/latest.html)** (free, command-line):
>   `pscp helixscreen-ad5m.zip install.sh root@<printer-ip>:/data/`

**Step 3: SSH into the printer and run the installer**

```bash
# From your local computer, SSH into the printer as root
ssh root@<printer-ip>

# Now on the printer, run the installer
sh /data/install.sh --local /data/helixscreen-ad5m.zip
```

The install script automatically detects your firmware (Forge-X or Klipper Mod) and installs to the correct location.

### Option 2: Manual Install (Advanced)

<details>
<summary>Forge-X Manual Installation</summary>

```bash
# Download on your computer (replace vX.Y.Z with actual version)
VERSION=vX.Y.Z
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"

# Copy to printer (AD5M requires scp -O for legacy protocol)
# Note: Use /data/ not /tmp/ - AD5M's /tmp is a tiny tmpfs (~54MB)
# Windows users: use WinSCP (SCP protocol) or PuTTY's pscp instead - see note above
scp -O helixscreen-ad5m.zip root@<printer-ip>:/data/

# SSH into printer
ssh root@<printer-ip>

# Extract to /opt/helixscreen (Forge-X location). The zip has no folder
# inside it, so give unzip the destination folder.
mkdir -p /opt/helixscreen
unzip -q /data/helixscreen-ad5m.zip -d /opt/helixscreen

# Stop GuppyScreen
/opt/config/mod/.root/S80guppyscreen stop 2>/dev/null || true
chmod -x /opt/config/mod/.root/S80guppyscreen

# Install init script
cp /opt/helixscreen/config/helixscreen.init /etc/init.d/S90helixscreen
chmod +x /etc/init.d/S90helixscreen

# Start HelixScreen
/etc/init.d/S90helixscreen start

# Clean up
rm /data/helixscreen-ad5m.zip
```

</details>

<details>
<summary>Klipper Mod Manual Installation</summary>

> **Note:** Klipper Mod's `/tmp` is a small tmpfs (~54MB). The package is ~70MB, so we must use `/mnt/data` instead.

```bash
# Download on your computer (replace vX.Y.Z with actual version)
VERSION=vX.Y.Z
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"

# Copy to printer's data partition (NOT /tmp - it's too small!)
# Windows users: use WinSCP (SCP protocol) or PuTTY's pscp instead - see note above
scp -O helixscreen-ad5m.zip root@<printer-ip>:/mnt/data/

# SSH into printer
ssh root@<printer-ip>

# Extract to /root/printer_software/helixscreen (Klipper Mod location). The
# zip has no folder inside it, so give unzip the destination folder.
mkdir -p /root/printer_software/helixscreen
unzip -q /mnt/data/helixscreen-ad5m.zip -d /root/printer_software/helixscreen

# Stop KlipperScreen
/etc/init.d/S80klipperscreen stop 2>/dev/null || true
chmod -x /etc/init.d/S80klipperscreen

# Install init script (S80 to match KlipperScreen's boot order)
cp /root/printer_software/helixscreen/config/helixscreen.init /etc/init.d/S80helixscreen
chmod +x /etc/init.d/S80helixscreen

# Update the install path in the init script
sed -i 's|DAEMON_DIR=.*|DAEMON_DIR="/root/printer_software/helixscreen"|' /etc/init.d/S80helixscreen

# Start HelixScreen
/etc/init.d/S80helixscreen start

# Clean up
rm /mnt/data/helixscreen-ad5m.zip
```

</details>

> **Note:** AD5M runs as root, so `sudo` is not needed.
> **Note:** AD5M uses BusyBox utilities. Use `unzip` to extract `.zip` archives.
> **Note:** AD5M uses SysV init (BusyBox), not systemd.

### Reboot and Complete Setup

```bash
reboot
```

After reboot, HelixScreen will start automatically on the touchscreen. Use the touchscreen to complete the setup wizard; the printer should auto-detect since it's running locally. At the wizard's Moonraker Connection step, use `localhost`.

---

## What the Installer Does

Before proceeding, the installer checks that Klipper and Moonraker appear to be running and warns if either is missing; a piped install (`curl ... | sh`) continues after the warning, while an interactive one asks you to confirm.

**On Forge-X:**
- Sets Forge-X's display mode to `GUPPY` if it is not already (Forge-X itself must be installed and configured first; see [Prerequisites](#prerequisites))
- Stops and disables GuppyScreen (`chmod -x` on init scripts)
- Disables stock Flashforge UI in `/opt/auto_run.sh`
- Patches `/opt/config/mod/.shell/screen.sh` to skip backlight commands when HelixScreen is running (prevents Forge-X's delayed_gcode from dimming the screen)
- Installs HelixScreen to `/opt/helixscreen/`
- Creates init script at `/etc/init.d/S90helixscreen`

**On Klipper Mod:**
- Stops Xorg and KlipperScreen
- Disables their init scripts (`chmod -x`)
- Installs HelixScreen to `/root/printer_software/helixscreen/`
- Creates init script at `/etc/init.d/S80helixscreen`

On uninstall, all of this is reversed and your previous UI is restored; see [Uninstalling](#uninstalling).

---

## Service Control and Logs

AD5M uses SysV init (BusyBox), not systemd. The service script name depends on your firmware variant.

**Forge-X:**
```bash
/etc/init.d/S90helixscreen start|stop|restart|status
tail -100 /opt/helixscreen/logs/launcher.log    # launcher / crash capture
tail -100 /data/.helixscreen/logs/helix.log      # structured app log
```

**Klipper Mod:**
```bash
/etc/init.d/S80helixscreen start|stop|restart|status
tail -100 /root/printer_software/helixscreen/logs/launcher.log
tail -100 /data/.helixscreen/logs/helix.log
```

There are two log streams; collect both when reporting an issue:

- **Launcher / crash capture** (startup, crash output): `/opt/helixscreen/logs/launcher.log` on Forge-X, `/root/printer_software/helixscreen/logs/launcher.log` on Klipper Mod
- **Structured app log:** `/data/.helixscreen/logs/helix.log` on both firmwares. The app writes this file directly to flash; the system syslog only captures the earliest startup output, before the app's own logging takes over

### Disabling the Previous UI Manually

The installer does this automatically. If you ever need to do it by hand:

**Forge-X (SysV init):**
```bash
# Disable GuppyScreen
/opt/config/mod/.root/S80guppyscreen stop
chmod -x /opt/config/mod/.root/S80guppyscreen
```

**Klipper Mod (SysV init):**
```bash
# Disable KlipperScreen
/etc/init.d/S80klipperscreen stop
chmod -x /etc/init.d/S80klipperscreen
```

---

## Updating

### Check Your Current Version

On the touchscreen: **Settings** → scroll down to the bottom of the page to find the version number.

Or via SSH:
```bash
# Forge-X:
/opt/helixscreen/bin/helix-screen --version

# Klipper Mod:
/root/printer_software/helixscreen/bin/helix-screen --version
```

### Update to the Latest Version

Same two-step process as installing (the AD5M has no HTTPS support), but you only need to copy the zip and you use the `install.sh` bundled with your existing install:

```bash
# On your computer (replace vX.Y.Z with actual version):
VERSION=vX.Y.Z  # Check latest at https://github.com/prestonbrown/helixscreen/releases/latest
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
# Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
scp -O helixscreen-ad5m.zip root@<printer-ip>:/data/

# On the printer (use the bundled install.sh - no need to download it again):
# Forge-X:
/opt/helixscreen/install.sh --local /data/helixscreen-ad5m.zip --update
# Klipper Mod:
/root/printer_software/helixscreen/install.sh --local /data/helixscreen-ad5m.zip --update
```

This preserves your configuration and updates to the latest version. For the Windows transfer alternatives, see the note in [Step 2 of the automated install](#option-1-automated-install-recommended).

### Update to a Specific Version

Download the specific version archive from [GitHub Releases](https://github.com/prestonbrown/helixscreen/releases), then use `--local` as shown above. To reinstall a version with fresh settings instead of keeping your existing ones, see [UPGRADING.md](../UPGRADING.md) for `--version` and `--clean`.

---

## Uninstalling

> **On ZMOD:** switch back to the stock screen by running `DISPLAY_ON` (see [ZMOD: Turn HelixScreen On](#zmod-turn-helixscreen-on)). The commands below are for Forge-X and Klipper Mod.

### Automated Uninstall (Recommended)

The bundled installer's `--uninstall` removes HelixScreen and restores your previous UI (GuppyScreen on Forge-X, KlipperScreen on Klipper Mod):

```bash
# Forge-X:
cp /opt/helixscreen/install.sh /tmp/install.sh && sh /tmp/install.sh --uninstall
# Klipper Mod:
cp /root/printer_software/helixscreen/install.sh /tmp/install.sh && sh /tmp/install.sh --uninstall
```

> **Why copy it out?** The installer refuses to run `--uninstall` from inside the install directory: it would delete the script that is running it. Copying it to `/tmp` first avoids that.

On Forge-X, the uninstaller reverses everything listed in [What the Installer Does](#what-the-installer-does), including unpatching `screen.sh` and restoring backlight control.

### Manual Uninstall

<details>
<summary>AD5M Forge-X</summary>

```bash
# Stop and remove service
/etc/init.d/S90helixscreen stop
rm /etc/init.d/S90helixscreen

# Remove files
rm -rf /opt/helixscreen

# Re-enable GuppyScreen
chmod +x /opt/config/mod/.root/S80guppyscreen 2>/dev/null || true
chmod +x /opt/config/mod/.root/S35tslib 2>/dev/null || true

# Restore stock Flashforge UI in auto_run.sh (if it was disabled)
sed -i 's|^# Disabled by HelixScreen: /opt/PROGRAM/ffstartup-arm|/opt/PROGRAM/ffstartup-arm|' /opt/auto_run.sh 2>/dev/null || true

# Remove HelixScreen patch from screen.sh (restores backlight control)
# The automated uninstaller handles this; for manual removal, edit:
# /opt/config/mod/.shell/screen.sh and remove the helixscreen_active check

# Reboot to restore GuppyScreen
reboot
```

> **Note:** The automated uninstaller (`install.sh --uninstall`) handles all Forge-X restoration automatically, including unpatching `screen.sh`.
</details>

<details>
<summary>AD5M Klipper Mod</summary>

```bash
# Stop and remove service
/etc/init.d/S80helixscreen stop
rm /etc/init.d/S80helixscreen

# Remove files
rm -rf /root/printer_software/helixscreen

# Re-enable KlipperScreen
chmod +x /etc/init.d/S80klipperscreen 2>/dev/null || true

# Reboot to restore KlipperScreen
reboot
```
</details>

---

## Quirks and Notes

### Memory Constraints

The AD5M has limited RAM (~108MB total, with only ~24MB free after Klipper, Moonraker, and screen UI). HelixScreen is built with static linking and memory optimization for this environment.

**Measured memory comparison (VmRSS):**
| Component | KlipperScreen | HelixScreen |
|-----------|---------------|-------------|
| Screen UI | ~50 MB (Python + X Server) | **~15 MB** (C++) |
| **Total** | ~50 MB | **~10 MB** |

On Klipper Mod systems, switching from KlipperScreen to HelixScreen frees approximately **35 MB** of RAM, a significant improvement on a memory-constrained device!

> **Note:** The ~15 MB footprint includes the full LVGL widget tree, draw buffers for UI elements (gradients, color pickers, AMS spool icons), and runtime state for all panels. Images are loaded on-demand, not pre-cached.

If you experience memory issues:
- Reduce print history retention in Moonraker
- Avoid keeping many G-code files on the printer
- Consider disabling the camera stream if not needed

### Screen Dims After a Few Seconds

If the screen dims to ~10% brightness shortly after boot (about 3 seconds after Klipper starts), the Forge-X backlight patch didn't get applied; this can happen on manual installs. See [Troubleshooting: screen dims after a few seconds](../TROUBLESHOOTING.md#screen-dims-after-a-few-seconds).

---

## See Also

- [Troubleshooting](../TROUBLESHOOTING.md): generic problems (connections, displays, touch input), including a dedicated [Flashforge Adventurer 5M section](../TROUBLESHOOTING.md#flashforge-adventurer-5m-issues)
- [Supported Printers: FlashForge Adventurer 5M / 5M Pro](supported-printers.md#flashforge-adventurer-5m--5m-pro): what works on the AD5M
- [Upgrading HelixScreen](../UPGRADING.md): universal migrations (fixing a setup wizard that keeps appearing, factory reset, pinning versions)

---

*Back to: [Installation Guide](../INSTALL.md) | [User Guide](../USER_GUIDE.md)*
