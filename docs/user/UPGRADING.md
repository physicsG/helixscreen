# Upgrading HelixScreen

This guide helps you upgrade HelixScreen to a newer version.

---

## Quick Upgrade

**Raspberry Pi / Creality K1:**
```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh -s -- --update
```

**Adventurer 5M** (no HTTPS support - two-step process):
```bash
# On your computer (replace vX.Y.Z with actual version):
VERSION=vX.Y.Z  # Check latest at https://github.com/prestonbrown/helixscreen/releases/latest
wget "https://github.com/prestonbrown/helixscreen/releases/download/${VERSION}/helixscreen-ad5m.zip"
# Windows users: use WSL, WinSCP (SCP protocol), or PuTTY's pscp instead of scp -O
scp -O helixscreen-ad5m.zip root@<printer-ip>:/data/

# On the printer (use the bundled install.sh):
# Forge-X:
/opt/helixscreen/install.sh --local /data/helixscreen-ad5m.zip --update
# Klipper Mod:
/root/printer_software/helixscreen/install.sh --local /data/helixscreen-ad5m.zip --update
```

**Adventurer 5X (ZMOD):**

Most AD5X upgrades happen automatically through Mainsail's Update Manager — open Mainsail, go to **Machine → Update Manager**, and click **Update** next to HelixScreen.

If that fails (you see an error toast like `Error updating helixscreen: [Errno 93] Directory not empty`, or HelixScreen won't start after an update), run the CLI upgrade from a Mainsail Shell or SSH session:

```bash
ssh root@<printer-ip>
chroot /usr/data/.mod/.zmod
curl -fsSL https://releases.helixscreen.org/install.sh | sh -s -- --update
```

The `chroot` step is required. ZMOD installs HelixScreen inside `/usr/data/.mod/.zmod/`, and a plain `curl … | sh` from outside the chroot writes to the wrong filesystem view — the installer detects this and refuses to run.

Offline variant (release zip already on the printer):

```bash
ssh root@<printer-ip>
chroot /usr/data/.mod/.zmod
sh /tmp/install.sh --local /tmp/helixscreen-ad5x.zip --update
```

Your settings (`settings.json`), environment overrides (`helixscreen.env`), and custom files (custom printer images, etc.) are automatically preserved across updates.

> **One thing that can change: your home screen arrangement.** If an update changes how the home grid is sized, saved widget positions no longer point at the right cells, so HelixScreen re-places every widget on the new grid. Which widgets you have and how they are configured is kept — only the positions and sizes are recomputed. See [What Happens on Upgrade](guide/home-panel.md#what-happens-on-upgrade).

---

## If the Setup Wizard Keeps Appearing

After upgrading, if HelixScreen keeps showing the setup wizard on every boot, your configuration file format may have changed in a way that's incompatible with the new version.

### Quick Fix

The easiest solution is to delete your config file and let the wizard create a new one:

**MainsailOS (Pi):**
```bash
sudo rm ~/helixscreen/config/settings.json
sudo systemctl restart helixscreen
```
> If HelixScreen was installed without a Klipper ecosystem present, the config is at the fallback location `/opt/helixscreen/config/settings.json` instead.

**Adventurer 5M (Forge-X):**
```bash
rm /opt/helixscreen/config/settings.json
/etc/init.d/S90helixscreen restart
```

**Adventurer 5M (Klipper Mod):**
```bash
rm /root/printer_software/helixscreen/config/settings.json
/etc/init.d/S80helixscreen restart
```

**Creality K1 (Simple AF):**
```bash
rm /usr/data/helixscreen/config/settings.json
/etc/init.d/S99helixscreen restart
```

After restarting, the wizard will guide you through setup again. Your printer settings (Klipper, Moonraker) are not affected - only HelixScreen's display preferences need to be reconfigured.

### Alternative: Factory Reset from UI

If you can access the settings panel before the wizard appears:

1. Navigate to **Settings** (gear icon in sidebar)
2. Scroll down to **Factory Reset**
3. Tap **Factory Reset** and confirm

This clears all HelixScreen settings and restarts the wizard.

---

## What's Preserved During Upgrades

The installer automatically preserves:

- **`settings.json`** — All your settings (printer connection, display preferences, sound, safety, etc.)
- **`helixscreen.env`** — Any environment variable overrides you've set
- **Custom files** — Custom printer images, user-added printer database entries, and other files in the `config/` directory

You should not need to reconfigure anything after a normal upgrade. If something does go wrong, see the troubleshooting sections below.

---

## What Settings Are Affected by a Reset

If you perform a factory reset or delete your config, you'll need to reconfigure:

- WiFi connection (if not using Ethernet)
- Moonraker connection (usually auto-detected)
- Display preferences (brightness, theme, sleep timeout)
- Sound settings
- Safety preferences (E-Stop confirmation)

Your Klipper configuration, Moonraker settings, print history, and G-code files are **not affected** - they're stored separately.

---

## Upgrade to Specific Version

**Raspberry Pi / Creality K1:**
```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh -s -- --update --version v1.2.0
```

**Adventurer 5M:** Download the specific version archive from [GitHub Releases](https://github.com/prestonbrown/helixscreen/releases), then use `--local` as shown above.

### Reinstall a Version with Fresh Settings

The commands above keep your existing `settings.json`. To reinstall a specific version **and** reset HelixScreen's settings to defaults at the same time, use `--clean` instead of `--update`:

```bash
curl -sSL https://raw.githubusercontent.com/prestonbrown/helixscreen/main/scripts/install.sh | sh -s -- --clean --version v1.2.0
```

`--clean` removes HelixScreen's settings and caches (it asks for confirmation first), then installs the version you specified. Your Klipper config, Moonraker settings, print history, and G-code files are **not** affected.

---

## Checking Your Version

**On the touchscreen:** Settings → scroll down → Version row shows current version

**Via SSH:**
```bash
# Pi:
/opt/helixscreen/bin/helix-screen --version

# K1:
/usr/data/helixscreen/bin/helix-screen --version

# AD5M (Forge-X):
/opt/helixscreen/bin/helix-screen --version

# AD5M (Klipper Mod):
/root/printer_software/helixscreen/bin/helix-screen --version

# AD5X (ZMOD — run from inside the chroot):
/srv/helixscreen/bin/helix-screen --version
```

---

## Getting Help

If you encounter issues after upgrading:

1. Ask in the [HelixScreen Discord](https://discord.gg/RZCT2StKhr) for quick help
2. Check [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common problems
3. View logs for error messages:
   - **Pi / x86 (systemd):** `sudo journalctl -u helixscreen -n 50`
   - **AD5M:** `tail -50 /opt/helixscreen/logs/launcher.log` and `grep helix-screen /var/log/messages | tail -50`
   - **K1 / K1C / K2 / AD5X (BusyBox):** `tail -50 /usr/data/helixscreen/logs/launcher.log` and `logread | grep helix-screen`
   - **Snapmaker U1:** `tail -50 /var/log/helixscreen/launcher.log` and `grep helix-screen /var/log/messages | tail -50`
   - **Pre-v0.99.62 installs (legacy):** add `tail -50 /tmp/helixscreen.log` as a fallback if `launcher.log` doesn't exist yet
4. Open an issue on [GitHub](https://github.com/prestonbrown/helixscreen/issues) with your version and any error messages

---

*Back to: [Installation Guide](INSTALL.md) | [User Guide](USER_GUIDE.md)*
