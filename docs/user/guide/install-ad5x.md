# Installing HelixScreen on the FlashForge Adventurer 5X (ZMOD)

End-to-end guide for the FlashForge Adventurer 5X running the ZMOD firmware modification: install, update, and uninstall.

> **Tested and working.** Prebuilt `ad5x` binaries are included in releases. Installation is handled through ZMOD.

## Prerequisites

- **Hardware:**
  - FlashForge Adventurer 5X
  - Built-in 4.3" touchscreen (800x480)
  - Network connection

- **Software:**
  - [ZMOD](https://github.com/ghzserg/zmod) firmware modification v1.7.0 or newer: ZMOD provides the Klipper, Moonraker, and SSH access that HelixScreen needs

## Install

ZMOD manages HelixScreen installation and updates through Moonraker's update manager. If you are setting up an AD5X from stock, install ZMOD first following its own instructions; HelixScreen then arrives and updates as an update-manager entry. That path "just works" and is the one most users should take.

<details>
<summary>Manual install from the command line (advanced)</summary>

Most users never need this. Use the manual route only if you're pinning a specific version, working from a `--local` zip, or recovering from a failed update.

ZMOD installs HelixScreen into a chroot rooted at `/usr/data/.mod/.zmod/`. When you SSH into the printer you land in the host filesystem, *not* the chroot, so a plain `curl ... | sh` writes into the squashfs base view that HelixScreen never sees. The installer detects this and refuses to run with a friendly message; the fix is to enter the chroot first:

```bash
ssh root@<printer-ip>
chroot /usr/data/.mod/.zmod
# now you're in the same view HelixScreen runs from:
curl -fsSL https://releases.helixscreen.org/install.sh | sh
```

</details>

## What the Installer Does Here

- Detects the ZMOD chroot and refuses to run outside it (see the manual-install section above for why)
- Installs to `/srv/helixscreen/` inside the chroot
- Auto-detects ZMOD firmware by recognizing ZMOD-specific Klipper device names, and applies ZMOD-optimized presets for display, input, and fan configuration: no manual configuration needed

## Service Control and Logs

Run service commands from inside the chroot:

```bash
chroot /usr/data/.mod/.zmod
/etc/init.d/S80helixscreen start|stop|restart|status
```

Both log streams live under `/opt/config/mod_data/log/` (paths as seen from inside the chroot), where ZMOD's support archive collects them:

```bash
tail -100 /opt/config/mod_data/log/helix.log        # structured app log (rotated)
tail -100 /opt/config/mod_data/log/helixscreen.log  # launcher stream
```

## Updating

Most upgrades happen automatically through Mainsail's Update Manager. Open Mainsail, go to **Machine > Update Manager**, and click **Update** next to HelixScreen.

If that fails (you see an error toast like `Error updating helixscreen: [Errno 93] Directory not empty`, or HelixScreen won't start after an update), run the CLI upgrade from a Mainsail shell or an SSH session:

```bash
ssh root@<printer-ip>
chroot /usr/data/.mod/.zmod
curl -fsSL https://releases.helixscreen.org/install.sh | sh -s -- --update
```

The `chroot` step is required. ZMOD installs HelixScreen inside `/usr/data/.mod/.zmod/`, and a plain `curl ... | sh` from outside the chroot writes to the wrong filesystem view; the installer detects this and refuses to run.

Offline variant (release zip already on the printer):

```bash
ssh root@<printer-ip>
chroot /usr/data/.mod/.zmod
sh /tmp/install.sh --local /tmp/helixscreen-ad5x.zip --update
```

Check the current version from inside the chroot:

```bash
/srv/helixscreen/bin/helix-screen --version
```

Your settings (`settings.json`), environment overrides (`helixscreen.env`), and custom files (custom printer images, etc.) are automatically preserved across updates.

## Uninstalling

Run the uninstaller from inside the chroot:

```bash
chroot /usr/data/.mod/.zmod
sh /tmp/install.sh --uninstall
```

The bundled copy at `/srv/helixscreen/install.sh` needs the same treatment: the installer refuses `--uninstall` from inside the install directory, so copy it out first (`cp /srv/helixscreen/install.sh /tmp/install.sh && sh /tmp/install.sh --uninstall`).

## Quirks and Notes

- IFS (the 4-channel filament system) is supported; see [Filament Management](filament.md)
- Random solid colors during screen sleep are a known AD5X quirk; see [Troubleshooting](../TROUBLESHOOTING.md#random-solid-colors-during-screen-sleep-ad5x)
- Changing a lane's color from the color menu can revert within a second once the lane has a Spoolman spool assigned. Change it in the lane's own editor (tap the lane, then edit it) or in Spoolman instead; see [Troubleshooting](../TROUBLESHOOTING.md#color-set-on-the-printers-own-screen-reverts-ad5x-with-spoolman)

## See Also

- [Supported Printers: FlashForge Adventurer 5X](supported-printers.md#flashforge-adventurer-5x)
- [Troubleshooting: update failed in Mainsail](../TROUBLESHOOTING.md#update-failed-in-mainsail--screen-wont-start-after-update-ad5x)
- [UPGRADING.md](../UPGRADING.md): version pinning and reset behavior
- [Installation overview](../INSTALL.md): all other platforms
