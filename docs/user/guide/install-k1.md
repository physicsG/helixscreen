# Installing HelixScreen on the Creality K1 Series

> **Status:** the easy installer currently handles a K1 that already runs Simple AF or is rooted with Moonraker, and updates. The stock-firmware path described under "Stock Creality software" is coming; until it ships, use the [K1C guide](creality-k1c-setup.md).

How to get HelixScreen onto a Creality K1, K1C or K1 Max, either with the [HelixScreen easy installer](https://helixscreen.org/installer/) or by hand. HelixScreen replaces the printer's screen program and keeps Creality Print and Creality Cloud reaching the printer.

> **Once the stock path ships, you turn on root access from the printer's screen and the installer does the rest.** The by-hand steps are in the [Creality K1C Setup Guide](creality-k1c-setup.md); this page covers what the installer does and what it needs.

## What the Installer Needs

- A K1, K1C or K1 Max on your network, switched on, and a computer on the same network
- For a printer still on Creality's stock software: firmware **1.3.3.5 or newer** (check it on the printer's screen under Settings)
- Root access turned on, for a stock printer. The installer walks you through it

The installer recognizes the model by the name the printer reports (`CR-K1`, `K1C` or `CR-K1 Max`) and stops on anything else.

## Where Your Printer Starts

The installer looks at the printer first and picks the path.

### Stock Creality software

1. The installer asks whether the printer's Settings has an entry called **Root account information**. If it does not, the installer stops: some 2025 K1C units ship without it and need a community rooting method first (see the note in [Step 1 of the K1C guide](creality-k1c-setup.md#step-1-enable-root-access)).
2. You turn on root access: **Settings > Root account information**, read the notice, wait for the countdown, press OK, then press Next in the installer.
3. The installer signs in as `root` with the factory password `creality_2023`, and checks the model and the firmware version.
4. It checks the printer for leftovers of an earlier mod: a `helper-script` folder, Creality's own Fluidd or Mainsail script, or files in `/opt`. Simple AF installs only on a printer fresh from a factory reset, so if anything is found the installer stops and asks you to reset the printer from its Settings and run it again.
5. It installs [Simple AF](https://github.com/pellcorp/creality), the community firmware that gives the printer Klipper and Moonraker, by cloning it to `/usr/data/pellcorp` and running its K1 installer. This is the long step: the installer gives up only if it hears nothing from the printer for 30 minutes, then waits for Moonraker to come up. Do not switch the printer off during either.
6. It installs HelixScreen into `/usr/data/helixscreen` and waits for the service to start. The setup wizard then runs on the printer's screen.

Two things to know before you start:

- Simple AF's installer turns off certificate checking for git on the printer, and it stays off.
- Going back to Creality's own software later takes a factory reset or Creality's recovery firmware (see [Going Back](#going-back)).

### Already running Simple AF or the Guilouz Helper Script

If Moonraker is already running on the printer, the installer leaves your firmware mod alone. It signs in as `root` (password `creality_2023`), checks the model, and installs HelixScreen into `/usr/data/helixscreen`. The only thing it refuses here is a model that is not a K1, K1C or K1 Max.

The installer stops the screen program that was running, such as Simple AF's own screen, and starts HelixScreen in its place. The backend services Creality Print and Creality Cloud use stay up, as in the [manual install](creality-k1c-setup.md#step-4-install-helixscreen).

### HelixScreen is already installed

The installer updates it. It follows the channel you are on, so a beta install is offered the newest beta or stable, and it never installs an older version over a newer one. When nothing newer exists it says HelixScreen is up to date and changes nothing.

## When the Installer Stops

| What it says | What to do |
|--------------|------------|
| No root account entry in Settings | Your unit needs a community rooting method first; see the K1C guide, Step 1 |
| Firmware older than 1.3.3.5 | Update the printer from its own screen, then run the installer again. The [upgrade path](creality-k1c-setup.md#firmware-upgrade-path) lists the steps for very old firmware |
| Something from an earlier mod is on the printer | Factory reset the printer from its Settings, then run the installer again |
| Another model than K1, K1C or K1 Max | The K1 recipe does not cover it. See [Supported Printers](supported-printers.md) |
| The installer can't sign in | Check root access is on. The factory password is `creality_2023`; if you changed it, type yours when asked |
| The printer's screen shows an error while Simple AF installs | Leave the printer on until the installer finishes or gives up. If it gives up, factory reset the printer and run the installer again |
| HelixScreen doesn't appear after the installer finishes | Follow the log steps in [Managing HelixScreen](creality-k1c-setup.md#managing-helixscreen) |

## Going Back

**Remove only HelixScreen.** The installer app does not remove HelixScreen yet. Use the [uninstall steps](creality-k1c-setup.md#uninstalling) in the K1C guide: they take HelixScreen off and bring back the screen program the printer had before (Simple AF's screen, or the one your Helper Script setup used). This is quick.

**Back to Creality's own software.** HelixScreen's installer can't do this. Factory reset the printer from its Settings, or flash Creality's recovery firmware: download it from Creality's site, put the `.img` on a FAT32 USB drive, insert it into the printer and follow the on-screen prompts. The [K1C guide](creality-k1c-setup.md#firmware-upgrade-path) has the details. A factory reset removes root access, so turn it on again before reinstalling anything.

## See Also

- [Creality K1C Setup Guide](creality-k1c-setup.md): every step done by hand, plus troubleshooting
- [Installation overview](../INSTALL.md): all other platforms
