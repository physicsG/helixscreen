# Installation Testing Guide

This document provides step-by-step procedures for verifying HelixScreen installation across all supported platforms. Use this to validate releases before publishing or to troubleshoot installation issues.

---

## Table of Contents

- [Overview](#overview)
- [Test Environment Requirements](#test-environment-requirements)
- [Platform Test Matrix](#platform-test-matrix)
- [Test Procedures](#test-procedures)
  - [MainsailOS (Pi)](#mainsailos-pi)
  - [KIAUH Pi](#kiauh-pi)
  - [Manual Pi](#manual-pi)
  - [AD5M/ForgeX](#ad5mforgex)
  - [AD5M/Klipper Mod](#ad5mklipper-mod)
- [Verification Checklist](#verification-checklist)
- [Known Issues](#known-issues)

---

## Overview

HelixScreen is an **add-on** to existing Klipper installations. We don't ship custom OS images - we assume Klipper/Moonraker is already working.

**Common requirement for all platforms:** Moonraker API accessible (default: `http://localhost:7125`)

### What the Installer Does

1. Detects platform (Pi vs Pi32 vs AD5M vs K1) and firmware variant (ForgeX vs Klipper Mod)
2. Downloads the correct release archive from GitHub
3. Stops competing UIs (GuppyScreen, KlipperScreen, etc.)
4. Extracts to the appropriate install directory
5. Installs and enables the service (systemd or SysV init)
6. Starts HelixScreen

### Install Paths by Platform

| Platform | Firmware | Install Dir | Init Script | Service Type |
|----------|----------|-------------|-------------|--------------|
| Pi 64-bit (aarch64) | - | `/opt/helixscreen` | `/etc/systemd/system/helixscreen.service` | systemd |
| Pi 32-bit (armv7l) | - | `/opt/helixscreen` | `/etc/systemd/system/helixscreen.service` | systemd |
| AD5M | ForgeX | `/opt/helixscreen` | `/etc/init.d/S90helixscreen` | SysV |
| AD5M | Klipper Mod | `/root/printer_software/helixscreen` | `/etc/init.d/S80helixscreen` | SysV |
| K1 | Simple AF | `/usr/data/helixscreen` | `/etc/init.d/S99helixscreen` | SysV |

> **Note:** Both 32-bit and 64-bit Pi use the same install paths and systemd service. The installer auto-detects architecture via `uname -m` and downloads the matching binary (`helixscreen-pi.zip` for 64-bit, `helixscreen-pi32.zip` for 32-bit).

---

## Test Environment Requirements

### MainsailOS (Most Common)

- **Hardware:** Pi 3/4/5 with touchscreen
- **Software:** Working MainsailOS with Klipper/Moonraker
- **Verify ready:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  # Should output: "ready" or "standby"
  ```

### KIAUH Pi

- **Hardware:** Pi 3/4/5 with touchscreen
- **Software:** Raspbian/Debian + KIAUH-installed Klipper/Moonraker
- **Verify KIAUH:**
  ```bash
  ls ~/kiauh/kiauh.sh
  ```
- **Verify Moonraker:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  ```

### Manual Pi

- **Hardware:** Pi 3/4/5 with touchscreen
- **Software:** Any Linux with manually installed Klipper/Moonraker (no KIAUH)
- **Verify Moonraker:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  ```

### AD5M/ForgeX

- **Hardware:** FlashForge Adventurer 5M/5M Pro
- **Software:** ForgeX firmware with GuppyScreen mode enabled
- **Verify ForgeX:**
  ```bash
  grep display /opt/config/mod_data/variables.cfg
  # Should contain: display = 'GUPPY'
  ```
- **Verify Moonraker:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  ```

### AD5M/Klipper Mod

- **Hardware:** FlashForge Adventurer 5M/5M Pro
- **Software:** Klipper Mod firmware
- **Verify Klipper Mod:**
  ```bash
  ls /root/printer_software/
  # or
  ls /mnt/data/.klipper_mod/
  ```
- **Verify Moonraker:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  ```

### K1/Simple AF

- **Hardware:** Creality K1, K1C, K1 Max, or similar
- **Software:** Simple AF installed and working
- **Verify Simple AF:**
  ```bash
  ls /usr/data/pellcorp/
  # Should exist
  ```
- **Verify GuppyScreen running:**
  ```bash
  ps | grep -i guppy
  ```
- **Verify Moonraker:**
  ```bash
  curl -s http://localhost:7125/printer/info | jq -r '.result.state'
  ```

---

## Platform Test Matrix

> **Note:** This matrix is a representative subset of the supported platforms, chosen to exercise the distinct install mechanisms (systemd Pi, framebuffer init scripts, BusyBox). For the full list of supported printers and their install paths, see [INSTALL.md](INSTALL.md).

| Test Case | MainsailOS | KIAUH Pi | Manual Pi | AD5M/ForgeX | AD5M/Klipper Mod | K1/Simple AF |
|-----------|:----------:|:--------:|:---------:|:-----------:|:----------------:|:------------:|
| Clean install | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Upgrade install | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Uninstall + reinstall | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Reboot persistence | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Moonraker connection | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Touch input works | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Display renders | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Wizard completes | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Panel navigation | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Competing UI disabled | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |

---

## Test Procedures

### MainsailOS (Pi)

#### Clean Install Test

1. **SSH into Pi:**
   ```bash
   ssh pi@mainsailos.local
   # or: ssh pi@<ip-address>
   ```

2. **Verify no existing HelixScreen:**
   ```bash
   ls /opt/helixscreen 2>/dev/null && echo "EXISTS - uninstall first" || echo "CLEAN"
   systemctl is-active helixscreen 2>/dev/null || echo "Service not running (good)"
   ```

3. **Run installer:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh
   ```

4. **Verify installation:**
   ```bash
   # Binary exists and runs
   /opt/helixscreen/bin/helix-screen --version

   # Service is enabled
   systemctl is-enabled helixscreen
   # Expected: enabled

   # Service is running
   systemctl is-active helixscreen
   # Expected: active
   ```

5. **Reboot test:**
   ```bash
   sudo reboot
   ```
   After reboot, verify HelixScreen appears on the touchscreen automatically.

6. **Post-reboot verification:**
   ```bash
   # Service running after reboot
   systemctl is-active helixscreen

   # Check logs for errors
   journalctl -u helixscreen -n 20 --no-pager | grep -i error || echo "No errors"
   ```

#### Upgrade Install Test

1. **With existing HelixScreen installed, run:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --update
   ```

2. **Verify config preserved:**
   ```bash
   # settings.json should still exist with your settings
   cat /opt/helixscreen/config/settings.json | head -5
   ```

3. **Service should restart automatically and UI should appear.**

#### Uninstall Test

1. **Run uninstaller:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --uninstall
   ```

2. **Verify removal:**
   ```bash
   ls /opt/helixscreen 2>/dev/null && echo "FAILED - still exists" || echo "REMOVED"
   systemctl is-active helixscreen 2>/dev/null && echo "FAILED - still running" || echo "STOPPED"
   ```

3. **If KlipperScreen was installed, verify it restarts.**

---

### KIAUH Pi

Same procedures as MainsailOS - the curl|sh installer works identically.

**Additional check:** Verify KIAUH extension works:
```bash
# Copy extension into KIAUH's extensions directory
cp -r ~/helixscreen/scripts/kiauh/helixscreen ~/kiauh/kiauh/extensions/
~/kiauh/kiauh.sh
# Navigate to Extensions menu - HelixScreen should appear
```

---

### Manual Pi

Same procedures as MainsailOS - the curl|sh installer works on any systemd Linux.

**Pre-check:** Ensure systemd is the init system:
```bash
ps --no-headers -o comm 1
# Should output: systemd
```

---

### AD5M/ForgeX

#### Clean Install Test

1. **SSH into AD5M:**
   ```bash
   ssh root@<printer-ip>
   ```

2. **Verify ForgeX and no existing HelixScreen:**
   ```bash
   # ForgeX in GUPPY mode
   grep display /opt/config/mod_data/variables.cfg
   # Expected: display = 'GUPPY'

   # No existing HelixScreen
   ls /opt/helixscreen 2>/dev/null && echo "EXISTS - uninstall first" || echo "CLEAN"
   ```

3. **Download and copy from your computer** (AD5M lacks HTTPS support):
   ```bash
   # On your computer (replace vX.Y.Z with actual version):
   VERSION=vX.Y.Z
   wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
   wget https://releases.helixscreen.org/install.sh
   # Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
   scp -O helixscreen-ad5m.zip install.sh root@<printer-ip>:/data/
   ```

4. **Run installer on printer:**
   ```bash
   sh /data/install.sh --local /data/helixscreen-ad5m.zip
   ```

5. **Verify installation:**
   ```bash
   # Binary exists and runs
   /opt/helixscreen/bin/helix-screen --version

   # Init script installed
   ls -la /etc/init.d/S90helixscreen

   # Service running
   /etc/init.d/S90helixscreen status
   # or check for process
   ps | grep helix-screen | grep -v grep

   # GuppyScreen disabled
   ls -la /opt/config/mod/.root/S80guppyscreen
   # Should NOT have 'x' (execute) permission
   ```

6. **Verify backlight patch:**
   ```bash
   grep helixscreen_active /opt/config/mod/.shell/screen.sh
   # Should find the patch
   ```

7. **Reboot test:**
   ```bash
   reboot
   ```
   After reboot:
   - HelixScreen should appear on touchscreen
   - Backlight should be at full brightness (not dimmed)

8. **Post-reboot verification:**
   ```bash
   # Service running
   /etc/init.d/S90helixscreen status

   # Check logs (both streams: structured app log + launcher capture)
   { grep helix-screen /var/log/messages | tail -20; tail -20 /opt/helixscreen/logs/launcher.log 2>/dev/null; } | grep -i error || echo "No errors"

   # Active flag exists
   ls /tmp/helixscreen_active
   ```

#### Upgrade Install Test

1. **Download new release and copy** (same two-step process):
   ```bash
   # On your computer (replace vX.Y.Z with actual version):
   VERSION=vX.Y.Z
   wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
   wget https://releases.helixscreen.org/install.sh
   # Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
   scp -O helixscreen-ad5m.zip install.sh root@<printer-ip>:/data/
   ```

2. **Run with --update** (use bundled install.sh):
   ```bash
   /opt/helixscreen/install.sh --local /data/helixscreen-ad5m.zip --update
   ```

3. **Verify config preserved:**
   ```bash
   cat /opt/helixscreen/config/settings.json | head -5
   ```

#### Uninstall Test

1. **Run uninstaller** (use bundled install.sh):
   ```bash
   /opt/helixscreen/install.sh --uninstall
   ```

2. **Verify removal and GuppyScreen restoration:**
   ```bash
   # HelixScreen removed
   ls /opt/helixscreen 2>/dev/null && echo "FAILED" || echo "REMOVED"

   # GuppyScreen re-enabled
   ls -la /opt/config/mod/.root/S80guppyscreen
   # Should have 'x' (execute) permission

   # Backlight patch removed
   grep helixscreen_active /opt/config/mod/.shell/screen.sh && echo "FAILED - patch still present" || echo "Patch removed"
   ```

3. **Reboot and verify GuppyScreen appears.**

---

### AD5M/Klipper Mod

#### Clean Install Test

1. **SSH into AD5M:**
   ```bash
   ssh root@<printer-ip>
   ```

2. **Verify Klipper Mod and no existing HelixScreen:**
   ```bash
   # Klipper Mod indicator
   ls /root/printer_software/ || ls /mnt/data/.klipper_mod/

   # No existing HelixScreen
   ls /root/printer_software/helixscreen 2>/dev/null && echo "EXISTS - uninstall first" || echo "CLEAN"
   ```

3. **Download and copy from your computer** (AD5M lacks HTTPS support):
   ```bash
   # On your computer (replace vX.Y.Z with actual version):
   VERSION=vX.Y.Z
   wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
   wget https://releases.helixscreen.org/install.sh
   # Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
   scp -O helixscreen-ad5m.zip install.sh root@<printer-ip>:/mnt/data/
   ```

4. **Run installer on printer:**
   ```bash
   sh /mnt/data/install.sh --local /mnt/data/helixscreen-ad5m.zip
   ```

5. **Verify installation:**
   ```bash
   # Binary exists (note different path!)
   /root/printer_software/helixscreen/bin/helix-screen --version

   # Init script installed (S80, not S90)
   ls -la /etc/init.d/S80helixscreen

   # Service running
   /etc/init.d/S80helixscreen status

   # KlipperScreen disabled
   ls -la /etc/init.d/S80klipperscreen 2>/dev/null
   # Should NOT have 'x' (execute) permission, or may not exist

   # Xorg disabled (if was running)
   ls -la /etc/init.d/S40xorg 2>/dev/null
   # Should NOT have 'x' permission
   ```

6. **Reboot test:**
   ```bash
   reboot
   ```
   After reboot, HelixScreen should appear on touchscreen.

7. **Post-reboot verification:**
   ```bash
   # Service running
   /etc/init.d/S80helixscreen status

   # Check logs (both streams: structured app log + launcher capture)
   { grep helix-screen /var/log/messages | tail -20; tail -20 /root/printer_software/helixscreen/logs/launcher.log 2>/dev/null; } | grep -i error || echo "No errors"
   ```

#### Uninstall Test

1. **Run uninstaller** (use bundled install.sh):
   ```bash
   /root/printer_software/helixscreen/install.sh --uninstall
   ```

2. **Verify removal and KlipperScreen restoration:**
   ```bash
   # HelixScreen removed
   ls /root/printer_software/helixscreen 2>/dev/null && echo "FAILED" || echo "REMOVED"

   # KlipperScreen re-enabled (if it existed)
   ls -la /etc/init.d/S80klipperscreen 2>/dev/null
   # Should have 'x' (execute) permission
   ```

3. **Reboot and verify KlipperScreen appears.**

---

### K1/Simple AF

#### Clean Install Test

1. **SSH into K1:**
   ```bash
   ssh root@<printer-ip>
   ```

2. **Verify Simple AF and no existing HelixScreen:**
   ```bash
   # Simple AF indicator
   ls /usr/data/pellcorp/
   # Should exist

   # No existing HelixScreen
   ls /usr/data/helixscreen 2>/dev/null && echo "EXISTS - uninstall first" || echo "CLEAN"
   ```

3. **Run installer:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh
   ```

4. **Verify installation:**
   ```bash
   # Binary exists
   /usr/data/helixscreen/bin/helix-screen --version

   # Init script installed (S99)
   ls -la /etc/init.d/S99helixscreen

   # Service running
   /etc/init.d/S99helixscreen status
   # or check for process
   ps | grep helix-screen | grep -v grep

   # GuppyScreen disabled
   ls -la /etc/init.d/S99guppyscreen
   # Should NOT have 'x' (execute) permission
   ```

5. **Reboot test:**
   ```bash
   reboot
   ```
   After reboot, HelixScreen should appear on touchscreen.

6. **Post-reboot verification:**
   ```bash
   # Service running
   /etc/init.d/S99helixscreen status

   # Check logs (both streams: in-RAM syslog + launcher capture)
   { logread | grep helix-screen | tail -20; tail -20 /usr/data/helixscreen/logs/launcher.log 2>/dev/null; } | grep -i error || echo "No errors"
   ```

#### Upgrade Install Test

1. **Run with --update:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --update
   ```

2. **Verify config preserved:**
   ```bash
   cat /usr/data/helixscreen/config/settings.json | head -5
   ```

#### Uninstall Test

1. **Run uninstaller:**
   ```bash
   curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --uninstall
   ```

2. **Verify removal and GuppyScreen restoration:**
   ```bash
   # HelixScreen removed
   ls /usr/data/helixscreen 2>/dev/null && echo "FAILED" || echo "REMOVED"

   # GuppyScreen re-enabled
   ls -la /etc/init.d/S99guppyscreen
   # Should have 'x' (execute) permission
   ```

3. **Reboot and verify GuppyScreen appears.**

---

## Verification Checklist

Run through this checklist after installation on each platform:

### Basic Functionality

- [ ] **Binary runs:** `/path/to/helix-screen --version` shows version
- [ ] **Service enabled:** systemctl is-enabled (Pi) or init script exists (AD5M)
- [ ] **Service running:** Active process, no crash loop
- [ ] **Reboot persists:** After reboot, service starts automatically
- [ ] **Display renders:** UI visible on touchscreen
- [ ] **Touch input works:** Can tap buttons, scroll lists

### Moonraker Integration

- [ ] **Connects to Moonraker:** Home screen shows printer status (not "Disconnected")
- [ ] **Receives updates:** Temperature values update in real-time
- [ ] **Can send commands:** Toggle a fan or heater from the UI

### Wizard & Configuration

- [ ] **Wizard appears on first run:** (Delete settings.json to test)
- [ ] **Wizard completes:** All steps finish without error
- [ ] **Settings persist:** Reboot and verify settings retained

### UI Navigation

- [ ] **Home panel:** Shows temperatures, printer status
- [ ] **Controls panel:** Fans, macros, power controls
- [ ] **Motion panel:** Jog controls, home buttons
- [ ] **Temperature panels:** Nozzle and bed temp controls
- [ ] **Print Select:** Lists G-code files
- [ ] **Settings:** Can change theme, view about info

### Platform-Specific

**MainsailOS/Pi:**
- [ ] systemd journal logs accessible: `journalctl -u helixscreen`
- [ ] No KlipperScreen conflicts (if installed)

**AD5M/ForgeX:**
- [ ] Backlight at full brightness (not dimmed after boot)
- [ ] `/tmp/helixscreen_active` flag exists while running
- [ ] GuppyScreen init script disabled (no execute permission)
- [ ] Stock FlashForge UI disabled in auto_run.sh

**AD5M/Klipper Mod:**
- [ ] Installed to `/root/printer_software/helixscreen` (not /opt)
- [ ] Uses S80 init script (not S90)
- [ ] Xorg disabled (framebuffer access)
- [ ] KlipperScreen disabled

**K1/Simple AF:**
- [ ] Installed to `/usr/data/helixscreen`
- [ ] Uses S99 init script
- [ ] GuppyScreen disabled (no execute permission on init script)
- [ ] Moonraker update_manager configured

---

## Known Issues

### All Platforms

| Issue | Workaround |
|-------|------------|
| Wizard keeps appearing | Check settings.json exists and has valid JSON |
| Touch offset | Use Settings → Touch & Input → Touch Calibration |

### Pi-Specific

| Issue | Workaround |
|-------|------------|
| Pi 5 black screen | Try `drm_device: "/dev/dri/card1"` or `card2` in config |
| Low memory on Pi 3 | Disable other services, reduce Moonraker cache |

### AD5M/ForgeX-Specific

| Issue | Workaround |
|-------|------------|
| Screen dims after boot | Re-run installer to apply backlight patch |
| GuppyScreen still appears | Verify `/opt/config/mod/.root/S80guppyscreen` has no execute permission |

### AD5M/Klipper Mod-Specific

| Issue | Workaround |
|-------|------------|
| Install fails "disk full" | Installer should use /mnt/data; verify manually |
| KlipperScreen still appears | Verify init script disabled: `chmod -x /etc/init.d/S80klipperscreen` |

### K1/Simple AF-Specific

| Issue | Workaround |
|-------|------------|
| GuppyScreen still appears | Verify `/etc/init.d/S99guppyscreen` has no execute permission |
| Display not working | Verify Simple AF is properly installed first |

---

## Diagnostic Commands Quick Reference

### Pi (systemd)

```bash
# Service status
systemctl status helixscreen

# Logs
journalctl -u helixscreen -f          # Follow live
journalctl -u helixscreen -n 100      # Last 100 lines
journalctl -u helixscreen -p err      # Errors only

# Restart
sudo systemctl restart helixscreen
```

### AD5M (SysV init)

```bash
# Service status (ForgeX)
/etc/init.d/S90helixscreen status

# Service status (Klipper Mod)
/etc/init.d/S80helixscreen status

# Logs (AD5M has persistent /var/log/messages syslog; launcher capture on disk)
grep helix-screen /var/log/messages | tail -100      # structured app log
tail -100 /opt/helixscreen/logs/launcher.log         # launcher / crash capture
tail -f  /opt/helixscreen/logs/launcher.log          # follow launcher live

# Restart (ForgeX)
/etc/init.d/S90helixscreen restart

# Restart (Klipper Mod)
/etc/init.d/S80helixscreen restart
```

### K1 (SysV init, BusyBox in-RAM syslog)

```bash
# Service status
/etc/init.d/S99helixscreen status

# Logs
logread | grep helix-screen | tail -100              # structured app log (RAM ring)
tail -100 /usr/data/helixscreen/logs/launcher.log    # launcher / crash capture
tail -f  /usr/data/helixscreen/logs/launcher.log     # follow launcher live

# Restart
/etc/init.d/S99helixscreen restart
```

### Moonraker Check (All Platforms)

```bash
# Test Moonraker connection
curl -s http://localhost:7125/printer/info | jq '.result.state'

# List files
curl -s http://localhost:7125/server/files/list | jq '.result | length'
```

---

*Related: [INSTALL.md](INSTALL.md) | [TROUBLESHOOTING.md](TROUBLESHOOTING.md)*
