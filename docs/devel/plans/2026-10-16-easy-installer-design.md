# Easy Installer: Design

A cross-platform desktop app (Windows, macOS, Linux) that puts HelixScreen on a commercial
printer for an owner who has never used SSH. It identifies the printer and what is already on
it, fetches the right firmware, walks the owner through the steps only they can do on the
printer's own screen, and finishes the install itself.

## Who it is for

Someone who bought a K1, an AD5M or a QIDI and wants a better screen. The command line is not
their problem to solve. Owners of hand-built printers (Vorons, any MainsailOS/FluiddPi/KIAUH
host) are covered too, because that path costs almost nothing once the SSH engine exists.

## Decisions already made

| Question | Decision |
|----------|----------|
| How HelixScreen reaches a modded printer | Flash the mod's own unmodified file, then install HelixScreen once Moonraker answers (over SSH, or through the mod's own switch where it has one). We never repack or host anyone's firmware, and we maintain no firmware image of our own |
| Where firmware comes from | Each mod's own GitHub releases. We host only a signed manifest naming URL + sha256 |
| When the app is unsure of hardware revision or firmware version | Refuse and link the manual guide. Never write a stick on a guess |
| Framework | Tauri: Rust core, web UI. Qt is the fallback if Linux WebKitGTK proves painful |
| License | GPL-3.0-or-later, so Apache-2.0 and GPL code from flashing tools can be reused |
| Repo | New `prestonbrown/helix-installer`; the manifest lives there too. This repo gains only `install.sh --progress` |
| Formatting drives | Never. A stick must already be FAT32; the app checks and refuses otherwise |

## Starting states

Every printer is in one of three states when the app finds it, and each needs a different
recipe:

| State | Meaning | Detected by |
|-------|---------|-------------|
| `stock` | Vendor firmware, no Klipper access for us | No Moonraker on 7125, vendor discovery answers (FlashForge UDP 48899), or the user says so |
| `modded:<mod>` | A community firmware is already on it (Simple AF, Helper Script, Forge-X, Klipper Mod, ZMOD) or stock firmware that already runs Moonraker (K2, QIDI) | Moonraker answers; `machine.system_info` plus a recipe-defined SSH probe names the mod |
| `helix:<version>` | HelixScreen already installed | Its service or init script exists (`S99helixscreen`, `helixscreen.service`, ZMOD's plugin), or Moonraker's update_manager lists it |

`helix` state offers **upgrade** (newer release over the existing install), **reinstall**,
or **restore stock**. Upgrade uses whichever path the printer already updates through:
`install.sh --update` over SSH, or ZMOD's update_manager entry. An AD5M still running the
old `prestonbrown/ff5m` image is `modded:forge-x` with HelixScreen installed, and upgrades
over SSH like any other.

A `modded` printer skips straight to the steps after the mod: a K1 that already has Simple AF
or Helper Script goes directly to `helix_install`, and an AD5X with ZMOD goes directly to
`DISPLAY_OFF HELIX=1` (an AD5M already on ZMOD takes the same step). The manifest maps `(printer, state, version)` to a recipe; anything it
does not map is refused.

## Architecture

```
 Signed manifest (helixscreen.org)          Tauri app
 ┌──────────────────────────┐    ┌──────────────────────────────────────────────┐
 │ printers: K1, K2, AD5M…  │    │ UI (web): printer picker -> guided steps     │
 │  ├ identify rules        │───>│ ────────────────────────────────────────────  │
 │  ├ (state,version)->     │    │ Rust core:                                   │
 │  │   recipe map          │    │  discovery  mDNS, Moonraker :7125 probe,     │
 │  ├ recipes (step lists)  │    │             FlashForge UDP 48899             │
 │  ├ restore recipes       │    │  identify   pure fn: facts + rules -> recipe │
 │  └ artifacts: url+sha256 │    │  fetch      download, sha256 verify, cache   │
 │ step screenshots (CDN)   │    │  stick      removable drives, FAT32 check,   │
 └──────────────────────────┘    │             copy, eject                      │
                                 │  ssh        russh: PTY exec, upload, stream  │
                                 │  moonraker  HTTP: info, gcode/script         │
                                 │  runner     executes steps, state on disk    │
                                 └──────────────────────────────────────────────┘
```

- **discovery** finds candidates and reports what each says it is. It never picks a recipe.
- **identify** is a pure function: discovery and probe facts plus the manifest's rules in,
  `(printer, state, recipe)` or *refuse* out. It is where "refuse when unsure" lives, and it
  is unit-tested against recorded probes.
- **fetch** refuses any file whose sha256 is not in the signed manifest.
- **stick** offers removable drives only; detection and system-disk safety are borrowed from
  Raspberry Pi Imager (Apache-2.0) or the Rust port of Etcher's `drivelist`.
- **ssh** requests a PTY and feeds the login password to `sudo`, because QIDI and MainsailOS
  users are not root and their sudo is not passwordless.
- **runner** walks a recipe, saving state after every step.

## Manifest

One JSON file, signed with ed25519 (minisign); the public key is compiled into the app. It
carries `manifest_version` and `min_app_version`, so an old app refuses a manifest it cannot
read.

```json
{
  "manifest_version": 1, "min_app_version": "1.0.0",
  "printers": [{
    "id": "ad5x", "name": "FlashForge Adventurer 5X", "photo": "img/ad5x.jpg",
    "identify": [{ "flashforge_udp.model": "Adventurer 5X" }],
    "recipes": [
      { "when": { "state": "stock", "stock_version": ">=1.1.7 <=1.2.3" }, "recipe": "ad5x_zmod" },
      { "when": { "state": "modded:zmod" },                               "recipe": "ad5x_enable_helix" },
      { "when": { "state": "helix" },                                     "recipe": "ad5x_helix_update" }
    ],
    "restore": "ad5x_restore",
    "fallback": { "refuse": true, "doc": "https://helixscreen.org/docs/install-ad5x" },
    "verified": { "by": "helixscreen", "date": "", "firmware": "" }
  }],
  "artifacts": { "zmod_1.7.4": { "url": "https://github.com/ghzserg/zmod/releases/...", "sha256": "..." } },
  "recipes": { "ad5x_zmod": [ ] }
}
```

The decision language is facts and `when` ranges, nothing more: no loops, no scripting.
Anything more complicated becomes two recipes.

## Step types

Thirteen, and every recipe is built from them.

| Step | Does | Fails with |
|------|------|------------|
| `info` | Text and screenshot, Next | - |
| `user_action` | "Do this on the printer" with a screenshot; user confirms; optional `verify` probe | verify failed: retry or help |
| `ask` | Question with picture answers ("What version does the About screen show?"); sets a fact | - |
| `probe` | Reads facts: Moonraker `printer.info` / `machine.system_info`, an SSH command, FlashForge UDP | unreachable |
| `require` | Checks a fact against a range; refuses with a doc link otherwise | refuse |
| `download` | Fetch artifact, verify sha256, cache | network, hash mismatch |
| `write_stick` | Copy artifacts to a chosen FAT32 removable drive, eject | wrong filesystem, no space |
| `wait_for` | Port, Moonraker, SSH or a service comes up, with timeout and message | timeout: guided help |
| `ssh_login` | Try the recipe's default logins in order, else ask; record whether sudo is needed | auth failed |
| `ssh_run` | Run a command from the signed recipe, detached, streaming its log | non-zero exit |
| `moonraker_gcode` | Send a macro through `/printer/gcode/script` | Moonraker error |
| `moonraker_update` | Ask Moonraker's update manager to upgrade a named entry (ZMOD-managed HelixScreen) | Moonraker error |
| `helix_install` | Upload the release and `install.sh` to a recipe-chosen directory, run `install.sh --local <zip> --progress` (`--update` for upgrades), check the installer reports the expected platform and success | installer step failed |

`ssh_run` takes commands only from the signed manifest, never from the network or user input;
the trust boundary is the signing key.

## v1 printers

v1 is the minimum. Adding a printer later is a manifest entry, a recipe and screenshots.

| Printer | `stock` | `modded` | `helix` |
|---------|---------|----------|---------|
| Generic Klipper host (Voron, MainsailOS, FluiddPi, KIAUH) | n/a | discover -> probe Moonraker -> `ssh_login` (ask: Imager sets the login) -> `helix_install --platform pi` | `--update` |
| QIDI Q2 / Max 4 / Plus 5 (stock Moonraker counts as `modded`) | n/a | discover -> probe -> `info` (QIDI Studio box sync and cloud stop) -> `ssh_login` tries `qidi`/`qiditech` then `mks`/`makerbase` -> `helix_install` | `--update` |
| Creality K2 / Plus / Pro (stock Moonraker) | n/a | `user_action` root toggle (verify: SSH open) -> `ssh_login` `creality_2024`, else the password the screen shows -> `helix_install` | `--update` |
| Creality K1 / K1C / K1 Max | `ask` root option visible (no: 2025 K1C, refuse) -> `info` restoring stock later means a factory reset -> `user_action` root toggle -> `ssh_login` `creality_2023` -> `require` fw >= 1.3.3.5, no Helper Script remnants -> `ssh_run` Simple AF `installer.sh --install <probe>` -> `helix_install` | Simple AF or Helper Script present: `helix_install` | `--update` |
| FlashForge AD5M / Pro | discover (UDP model) -> `ask` stock version and Pro or not -> `require` 2.6.5-5.1.x, excluding versions Forge-X says need a `-Factory` image first (refused in v1) -> `download` upstream Forge-X -> `write_stick` renamed for the model (Forge-X requires `Adventurer5MPro-ForgeX-x.y.z.tgz` on a Pro) -> `user_action` flash -> `wait_for` Moonraker -> `ssh_login` (`root`/`root`) -> `helix_install` (the installer sets Forge-X's GUPPY display mode itself) | Forge-X / Klipper Mod: `ssh_login` (`root`/`root`, `root`/`klipper`) -> `helix_install`. ZMOD: `moonraker_gcode DISPLAY_OFF HELIX=1` -> `wait_for` HelixScreen | Forge-X / Klipper Mod `--update`; ZMOD's update_manager |
| FlashForge AD5X | discover -> `ask` stock version -> `require` ZMOD window (1.1.7-1.2.3, 3.0.3, 3.0.9, 3.1.0) -> `download` ZMOD -> `write_stick` -> `user_action` flash -> `wait_for` Moonraker (40 min, remove stick after) -> `moonraker_gcode DISPLAY_OFF HELIX=1` -> `wait_for` HelixScreen | ZMOD present: `moonraker_gcode` step onward | ZMOD's update_manager |

AD5X stock above 3.1.0 needs a factory-downgrade recipe. It ships in v1 only after a
supervised run on our unit.

FlashForge UDP discovery gives model and serial but no firmware version, so both FlashForge
recipes `ask` with a picture of the About screen.

### Open until a supervised hardware run

| Printer | Question | Device |
|---------|----------|--------|
| K1 | Simple AF run time; which `--install` probe a stock K1 needs (likely `loadcells`) | ours |
| K2 | Fixed `creality_2024` or a per-unit password on current firmware; does Moonraker answer before root | ours |
| AD5X | Does `DISPLAY_OFF HELIX=1` through the API complete; does it reboot mid-way; screen-switch-back macro for restore | ours, unplugged |
| AD5M | Forge-X root login (`root`/`root` assumed); does ZMOD's `DISPLAY_OFF HELIX=1` work on the AD5M as on the AD5X | tester |
| Generic | sudo over PTY on MainsailOS | pi5, pi3 |
| Max 4 / Plus 5 | Logins (Plus 5 unconfirmed); ships "community-verified" | none |

## Errors and recovery

- **Point of no return.** Each recipe marks the first step that changes the printer. A failure
  before it says nothing was changed and offers retry or the manual guide. A failure after it
  enters recovery: the exact state the printer is in and the next action, never "something
  went wrong".
- **Installs outlive the app.** `ssh_run` and `helix_install` start the job detached
  (`setsid` and a log file on the printer). The runner saves state after every step; reopening
  the app resumes ("installing on your K1, step 6 of 9") by reading that log.
- **Timeouts get help.** Every `wait_for` has a "what does your printer screen show?" picture
  screen whose answers lead to wait, re-insert, or the recovery guide. Long waits show "do not
  power off".
- **Re-identify after the point of no return.** A printer that stops answering like the
  expected model stops the recipe.
- **Manifest trouble.** Offline, the cached manifest is used only while its signature is valid
  and it is under 30 days old.
- **Support bundle.** One button exports the run log: every step, its output and the facts,
  with passwords stripped and IPs and serials masked.

### Restore stock

A `restore` recipe per printer. The UI states how far back each goes before the user starts:

| Printer | Restores to | How |
|---------|-------------|-----|
| Generic / QIDI / K2 | Stock screen | `install.sh --uninstall` |
| K1 | Simple AF's GrumpyScreen; full stock needs a factory reset or Creality's recovery | uninstall, then link |
| AD5M | GuppyScreen on Forge-X (fully stock: Forge-X's uninstall image on a stick); ZMOD: stock screen | `install.sh --uninstall`, then link Forge-X's uninstall guide; ZMOD macro |
| AD5X | Stock screen with ZMOD kept, or fully stock | ZMOD macro, or `REMOVE_ZMOD` |

## Testing

- **Recorded probes.** A hidden record mode saves everything discovery and `probe` saw as
  JSON. Recordings from our K1, K2, AD5X, pi5 and pi3 (and testers' QIDIs) become fixtures for
  `identify()`.
- **Recipes against fakes in CI.** Moonraker HTTP, SSH and the drive list sit behind
  interfaces with scriptable fakes (a small HTTP server, an in-process russh server, a fake
  drive list). Every manifest recipe runs end to end on each build: the success path and each
  failure branch (timeout, wrong login, hash mismatch, non-FAT32 stick, wrong model after the
  point of no return).
- **Manifest lint gate.** Every artifact has a sha256; every printer has a refuse fallback
  with a doc link and a `restore` recipe; every referenced screenshot exists; every recipe ends
  in done or refuse; no `when` ranges overlap.
- **Shown verification.** `verified: {by, date, firmware}` is displayed: "Tested by
  HelixScreen on K1 firmware 1.3.3.46" versus "Community-verified". A recipe nobody has run on
  hardware does not ship.
- **This repo.** `install.sh --progress` gets bats tests for its markers.

## Release

- GitHub Actions on Windows, macOS and Ubuntu with `tauri-action`: `.msi`, notarized `.dmg`,
  AppImage and `.deb`.
- Signing: Apple Developer ID ($99/yr) and Azure Trusted Signing (about $10/mo).
- Two keys: Tauri's updater key and the manifest's ed25519 key. New printer support ships as
  a manifest update, with no app release.
- Download page at `helixscreen.org/installer` that picks the file for the visitor's OS.

## Not in v1

- Elegoo Centauri Carbon (COSMOS): multi-step on-screen flow with a real brick path.
- Creator 5 / 5 Pro, Snapmaker U1, Sovol, Artillery M1: candidates for the first manifest
  update after v1.
- Sonic Pad (PC-tethered flash), Anycubic Kobra (no installer platform), 2025 K1C (no root),
  QIDI Plus 4 / 3-series (TJC panel): refused with a doc link.

## Before building

- **Ask the mod maintainers** (pellcorp, ghzserg) whether they are comfortable with the app
  driving their installers, and agree on a support path for failures inside their step.
