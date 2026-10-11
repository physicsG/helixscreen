# Creality K1C Setup Guide

Get HelixScreen running on your Creality K1C (also works for K1 and K1 Max).

**Time required:** ~20 minutes

---

## What You Need

- Creality K1C with firmware **v1.3.3.5 or later** (check: Settings on the touchscreen)
- K1C connected to your WiFi network
- A computer on the same network with an SSH client

---

## Step 1: Enable Root Access

On the K1C touchscreen:

1. Go to **Settings** > **Root account information**
2. Read the disclaimer, **wait 30 seconds**, then confirm

That's it. Root is now enabled.

> **Newer K1C units (2025 hardware revision):** Some recent K1C units ship without the **Root Account Information** option in the Settings menu. If you don't see it, your unit needs a community rooting workaround; check the Simple AF or Guilouz project pages (Step 3) for the current method.

---

## Step 2: SSH Into the Printer

From your computer:

```bash
ssh root@<PRINTER_IP>
# Password: creality_2023
```

Find the IP address on the K1C touchscreen under Settings > Network, or check your router's DHCP client list.

---

## Step 3: Install Simple AF (Recommended) or Guilouz Helper Script

HelixScreen needs Moonraker (the Klipper API server) to be running on your K1C. Stock firmware doesn't include it, so you need community firmware.

### Option A: Simple AF (Recommended)

[Simple AF](https://github.com/pellcorp/creality) is the easiest path and has first-class HelixScreen support.

```bash
cd /usr/data
git clone https://github.com/pellcorp/creality.git pellcorp
cd pellcorp
sh installer.sh
```

Follow the prompts. This installs Klipper, Moonraker, and GuppyScreen. Once it's working (GuppyScreen appears on the touchscreen), proceed to Step 4.

### Option B: Guilouz Helper Script

If you prefer the [Guilouz Helper Script](https://github.com/Guilouz/Creality-Helper-Script-Wiki):

```bash
git clone --depth 1 https://github.com/Guilouz/Creality-Helper-Script.git /usr/data/helper-script
sh /usr/data/helper-script/helper.sh
```

From the menu, install **Moonraker** at minimum. Optionally install Fluidd or Mainsail for web access.

> **Note:** If git clone fails with an SSL error, run `git config --global http.sslVerify false` first.

---

## Step 4: Install HelixScreen

### Option A: Direct Install (Recommended)

If your K1C has internet access, install directly on the printer via SSH:

```bash
wget -O - http://dl.helixscreen.org/install.sh | sh
```

This works because `dl.helixscreen.org` serves over plain HTTP, which BusyBox wget supports. No need to download anything on your computer first.

### Option B: Two-Step Install

If your printer doesn't have internet access, download on your computer first, then copy to the printer.

**On your computer:**

Go to the [latest release page](https://github.com/prestonbrown/helixscreen/releases/latest) and download:
- `helixscreen-k1.zip` (the K1 release archive)
- `install.sh` (the installer script, under "Assets")

Or use the command line (replace `vX.Y.Z` with the actual version):
```bash
VERSION=vX.Y.Z  # Check latest at https://github.com/prestonbrown/helixscreen/releases/latest
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-k1.zip"
wget https://releases.helixscreen.org/install.sh
```

**Copy to the printer and install:**

```bash
scp helixscreen-k1.zip install.sh root@<PRINTER_IP>:/usr/data/
ssh root@<PRINTER_IP>    # password: creality_2023
sh /usr/data/install.sh --local /usr/data/helixscreen-k1.zip
```

The installer automatically:
- Detects your K1C platform
- Stops the stock Creality screen UI (or whatever screen UI is running, such as GuppyScreen on a pellcorp install)
- Installs to `/usr/data/helixscreen/`
- Creates the boot service (`/etc/init.d/S99helixscreen`)
- Configures Moonraker update manager for easy future updates

---

> **Creality Print and Creality Cloud keep working.**
> HelixScreen and the stock Creality screen UI cannot share the framebuffer, so the
> installer stops the stock screen UI, but it keeps the backend servers (`master-server`,
> `app-server`, `web-server`) running and starts them on boot, so Creality Print and the
> Creality Cloud app keep reaching the printer as before. (This applies to a rooted stock
> printer or Guilouz firmware; on a Simple AF install the stock stack is already disabled,
> and HelixScreen stops only GuppyScreen.)
>
> If Creality Print cannot reach the printer after installing, update HelixScreen and
> reboot: the backend startup script ships with current releases. See
> [Troubleshooting: Creality Print cannot connect](../TROUBLESHOOTING.md#creality-print-can-no-longer-find-or-connect-to-the-printer).

## Step 5: Complete Setup on the Touchscreen

HelixScreen starts automatically after install. The on-screen wizard walks you through:

1. **Language** selection
2. **Moonraker connection**: should auto-detect on `localhost:7125`
3. **Printer identification**: the K1C is in the printer database
4. **Hardware discovery**: heaters, fans, sensors, LEDs

---

## Managing HelixScreen

```bash
# Start/stop/restart/status
/etc/init.d/S99helixscreen start
/etc/init.d/S99helixscreen stop
/etc/init.d/S99helixscreen restart
/etc/init.d/S99helixscreen status

# Installed version
/usr/data/helixscreen/bin/helix-screen --version

# View logs (both written to disk under /usr/data)
tail -100 /usr/data/helixscreen-state/logs/helix.log # structured app log
tail -100 /usr/data/helixscreen/logs/launcher.log    # launcher / crash capture
tail -f /usr/data/helixscreen/logs/launcher.log      # live follow launcher
```

The installer stops and disables the previous screen UI automatically. To do it by hand on a rooted stock printer:

```bash
/etc/init.d/S99start_app stop
chmod -x /etc/init.d/S99start_app
```

On a Simple AF install the previous UI is GuppyScreen:

```bash
/etc/init.d/S99guppyscreen stop
chmod -x /etc/init.d/S99guppyscreen
```

---

## Updating

### From Fluidd/Mainsail (easiest)

If you have Fluidd or Mainsail installed, HelixScreen appears in the update manager. One-click update.

### From SSH

```bash
# On your computer (replace vX.Y.Z with actual version):
VERSION=vX.Y.Z
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-k1.zip"
scp helixscreen-k1.zip root@<PRINTER_IP>:/usr/data/

# On the printer (use the bundled install.sh - no need to download it again):
/usr/data/helixscreen/install.sh --local /usr/data/helixscreen-k1.zip --update
```

---

## Uninstalling

```bash
cp /usr/data/helixscreen/install.sh /tmp/install.sh && sh /tmp/install.sh --uninstall
```

(The installer refuses `--uninstall` run from inside the install directory, so the script is copied to `/tmp` first.)

This removes HelixScreen and restores the screen UI that was running before: the stock Creality screen, or GuppyScreen on a pellcorp / Simple AF install.

### Manual Uninstall

<details>
<summary>Rooted stock firmware</summary>

```bash
# Stop and remove the boot service
/etc/init.d/S99helixscreen stop
rm /etc/init.d/S99helixscreen

# Remove files
rm -rf /usr/data/helixscreen

# Re-enable the stock Creality screen UI
chmod +x /etc/init.d/S99start_app

# Reboot to restore the stock screen
reboot
```
</details>

<details>
<summary>Simple AF / pellcorp (GuppyScreen)</summary>

```bash
# Stop and remove the boot service
/etc/init.d/S99helixscreen stop
rm /etc/init.d/S99helixscreen

# Remove files
rm -rf /usr/data/helixscreen

# Re-enable GuppyScreen
chmod +x /etc/init.d/S99guppyscreen

# Reboot to restore GuppyScreen
reboot
```
</details>

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Can't SSH in | Make sure root is enabled (Step 1). Password is `creality_2023` |
| `curl` or SSL error | K1 doesn't support HTTPS downloads. Use the two-step install (Step 4): download on your computer, then `scp` to the printer |
| Installer warns that Moonraker "does not appear to be running" | Complete Step 3 first: Moonraker must be running |
| Blank screen after install | Check logs: `logread \| grep helix-screen \| tail -100`, `tail -100 /usr/data/helixscreen-state/logs/helix.log` and `tail -100 /usr/data/helixscreen/logs/launcher.log` |
| Touch not responding | Reboot: `reboot` |
| Creality Print can't find the printer after install | The stock Creality backend should still be running beside HelixScreen. Update to the current release and reboot, then see [Troubleshooting](../TROUBLESHOOTING.md#creality-print-can-no-longer-find-or-connect-to-the-printer) |

For more help: [Troubleshooting Guide](../TROUBLESHOOTING.md) | [Discord](https://discord.gg/RZCT2StKhr) | [GitHub Issues](https://github.com/prestonbrown/helixscreen/issues)

---

## Firmware Upgrade Path

If your K1C is on older firmware, you must upgrade in order:

| Starting Version | Upgrade To |
|-----------------|------------|
| v1.2.9.14 or earlier | v1.2.9.15 → v1.2.9.22 → v1.3.0.30 → v1.3.1.4 → latest |
| v1.2.9.17 - v1.2.9.21 | v1.2.9.22 → v1.3.0.30 → v1.3.1.4 → latest |
| v1.3.0.30 | v1.3.1.4 → latest |
| v1.3.1.4+ | Latest directly |

Download firmware from Creality's site, put the `.img` on a FAT32 USB drive, insert into the printer, and follow the on-screen prompts. **Factory reset from Settings before installing rooted firmware.**

> **After factory reset**, root access must be re-enabled (Step 1) and community firmware reinstalled (Step 3).
