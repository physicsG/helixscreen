# HelixScreen KIAUH Extension

This directory contains a KIAUH (Klipper Installation And Update Helper) extension for HelixScreen.

## Installation Methods

### Method 1: Use the Direct Installer (Recommended)

The simplest way to install HelixScreen is using our bundled installer:

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh
```

If KIAUH is detected at `~/kiauh/kiauh/extensions/`, the installer automatically
registers the HelixScreen KIAUH extension. Pass `--skip-kiauh-registration` to
opt out:

```bash
curl -sSL https://releases.helixscreen.org/install.sh | sh -s -- --skip-kiauh-registration
```

After install, restart KIAUH (`~/kiauh/kiauh.sh`) and HelixScreen will appear
in the **Extensions** menu.

### Method 2: Manually Register the Extension

If you already have HelixScreen installed and want to add the KIAUH extension
afterward, copy the extension files into KIAUH's `extensions/` directory:

```bash
# Release tarball ships the extension under the install dir:
cp -r ~/helixscreen/scripts/kiauh/helixscreen ~/kiauh/kiauh/extensions/

# Or fetch directly from the repo:
git clone --depth 1 https://github.com/prestonbrown/helixscreen.git /tmp/helixscreen
cp -r /tmp/helixscreen/scripts/kiauh/helixscreen ~/kiauh/kiauh/extensions/
rm -rf /tmp/helixscreen
```

Then restart KIAUH and open the Extensions menu — HelixScreen should be listed.

#### Using the Extension

From the KIAUH Extensions menu, you can:

- **Install**: Downloads and installs the latest HelixScreen release
- **Update**: Updates to the latest version (preserves configuration)
- **Remove**: Uninstalls HelixScreen and restores your previous screen UI

## Note on KIAUH v5 vs v6

This extension is designed for KIAUH v6 (Python-based). If you're using an older version of KIAUH, use the direct installer method instead.

## What the Extension Does

The KIAUH extension is a thin wrapper around our bundled installer. It provides:

- Menu integration with KIAUH
- Same installation process as the direct installer
- Consistent update and removal experience

Under the hood, it downloads and runs the same `install.sh` script that the direct installation uses.

## Troubleshooting

If installation fails through KIAUH:

1. Try the direct installer method (curl|sh above)
2. Check KIAUH logs: `~/kiauh/logs/`
3. Check HelixScreen logs: `journalctl -u helixscreen` (Pi) or `/tmp/helixscreen.log` (AD5M)

## Support

- GitHub Issues: https://github.com/prestonbrown/helixscreen/issues
- Documentation: https://github.com/prestonbrown/helixscreen/blob/main/docs/user/INSTALL.md
