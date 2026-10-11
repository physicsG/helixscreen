# Installing HelixScreen on the Creality Sonic Pad

End-to-end guide for the Creality Sonic Pad: install, update, and uninstall.

The Sonic Pad is a standalone 7" touchscreen that can run Klipper. It uses a 32-bit ARM userspace (armhf) despite having a 64-bit-capable processor (Allwinner H616).

> **Tested firmware: [SonicPad-Debian](https://github.com/Jpe230/SonicPad-Debian) only.**
> This is the only Sonic Pad firmware HelixScreen has been tested on. It replaces Creality's stock OpenWrt image with Debian 11 (bullseye), which is what gives the Pad a normal systemd + GNU userspace for HelixScreen to install into.
>
> HelixScreen is **not** tested on Creality's stock Sonic Pad firmware. The stock image is a heavily cut-down OpenWrt build, so the installer's assumptions about systemd, package tooling, and archive utilities do not hold there. If you are on stock firmware, flash SonicPad-Debian first.

## Prerequisites

HelixScreen requires Klipper and Moonraker to already be installed and working on the Pad, typically via [KIAUH](https://github.com/dw-0/kiauh) or a similar tool. HelixScreen replaces whatever touchscreen UI you're currently using (e.g., KlipperScreen).

- **Hardware:**
  - Creality Sonic Pad (7" 1024x600 capacitive touchscreen)
  - Network connection (Ethernet)

- **Software:**
  - [SonicPad-Debian](https://github.com/Jpe230/SonicPad-Debian) (Debian 11 bullseye): see the note above
  - Klipper and Moonraker installed and working (via KIAUH or similar)
  - SSH access (`sonic@<pad-ip>`)
  - About 100MB free disk space

## Install

The standard installer works on Sonic Pad:

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh
```

## What the Installer Does Here

- Detects the Pad as a 32-bit ARM platform and downloads the `pi32` release binary
- Installs to `~/helixscreen/` and sets HelixScreen up as a systemd service
- Stops and disables any competing touchscreen UI so it doesn't fight over the display

## Service Control and Logs

```bash
sudo systemctl start helixscreen      # or stop / restart / status
sudo journalctl -u helixscreen -f     # follow logs
```

## Updating

Use the bundled installer (no need to download it again):

```bash
~/helixscreen/install.sh --update
```

Add `--version vX.Y.Z` to pin a specific version. Check the current version with:

```bash
~/helixscreen/bin/helix-screen --version
```

## Uninstalling

```bash
cp ~/helixscreen/install.sh /tmp/install.sh && sh /tmp/install.sh --uninstall
```

This removes HelixScreen and restores your previous UI (e.g., KlipperScreen).

## Quirks and Notes

- The Pad has a Goodix GT9xx touchscreen controller; the touch calibration wizard runs automatically on first boot if needed
- Moonraker runs on `localhost:7125` (the default), so the wizard's connection step needs no changes
- The `display-sleep` service is automatically stopped to prevent backlight conflicts
- If the brightness slider or auto-dim do nothing, force Creality's `brightness` helper with `HELIX_BACKLIGHT_DEVICE=brightness` in `~/helixscreen/config/helixscreen.env`; see [Troubleshooting](../TROUBLESHOOTING.md#brightness-slider-or-screen-dimming-does-nothing)

## See Also

- [Supported Printers](supported-printers.md): the Sonic Pad's dedicated-build row
- [Troubleshooting](../TROUBLESHOOTING.md)
- [UPGRADING.md](../UPGRADING.md): version pinning and reset behavior
- [Installation overview](../INSTALL.md): all other platforms
