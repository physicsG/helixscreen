<p align="center">
  <img src="assets/images/helix-icon-256.png" alt="HelixScreen" width="128"/>
  <br>
  <h1 align="center">HelixScreen for the Snapmaker U1</h1>
  <p align="center"><strong>A fork of HelixScreen that supports the U1's toolchanger and multiple ACE units</strong></p>
</p>

<p align="center">
  <a href="https://github.com/physicsG/helixscreen/actions/workflows/snapmaker-u1.yml"><img src="https://github.com/physicsG/helixscreen/actions/workflows/snapmaker-u1.yml/badge.svg" alt="Snapmaker U1 build"></a>
  <a href="https://github.com/physicsG/helixscreen/releases"><img src="https://img.shields.io/github/v/release/physicsG/helixscreen?label=U1%20build" alt="Latest U1 build"></a>
  <a href="https://www.gnu.org/licenses/gpl-3.0"><img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPL v3"></a>
</p>

> **This is a fork of [prestonbrown/helixscreen](https://github.com/prestonbrown/helixscreen).**
> Everything upstream HelixScreen does, this does — see [upstream's README](https://github.com/prestonbrown/helixscreen#readme)
> for the full feature tour. What follows is only what this fork adds on top, for
> the Snapmaker U1.
>
> Upstream is merged in regularly, and U1 work goes back upstream where it fits.

---

## What this fork adds

### multiACE and LAN pairing are upstream now

Multiple ACE units alongside the U1's own toolheads ([multiACE](https://github.com/decay71/multiACE))
and LAN pairing for Snapmaker Orca and the Snapmaker App both started in this fork
and now ship in upstream HelixScreen, which this fork tracks. The ACE units appear
as their own units beside the U1's heads, with the filament path tracing which bay
feeds which head and per-unit drying controls:

<img src="docs/images/u1/ams-overview.png" alt="Multi-filament overview: SnapSwap and ACE 2 Pro" width="800"/>

<img src="docs/images/u1/filament-environment.png" alt="ACE 2 Pro drying controls" width="800"/>

The pairing prompt answers the firmware's approval step, so Orca and the phone app
connect instead of hanging at *"requesting connection"*
([`docs/devel/LAN_CLIENT_AUTHORIZATION.md`](docs/devel/LAN_CLIENT_AUTHORIZATION.md)):

<img src="docs/images/u1/pairing-prompt.png" alt="Connection Request prompt" width="800"/>

### Runout recovery on a paused U1

A runout-paused U1 can be recovered from the runout dialog: Resume refeeds the
head without the `PRINTING=1` flag that makes the firmware skip the load, and the
manual Load/Unload/Purge row stays available while paused.

### Installable U1 builds

Upstream's release pipeline needs secrets a fork does not have. This fork ships its
own U1 workflow that cross-compiles, packages and publishes a working build from a
`u1-v*` tag — plus a fork-aware installer, so the printer is offered *this* repo's
releases as updates rather than upstream's.

---

## Install

On the printer, over SSH:

```sh
curl -fsSL https://github.com/physicsG/helixscreen/releases/latest/download/install-fork.sh | sh
```

That is a thin front-end for upstream's installer — platform detection, service
setup, backup/rollback and SHA256 verification are all the same code path. It only
pins the repo to this fork and skips upstream's CDN, which would otherwise serve
upstream's binary.

```sh
sh install-fork.sh --update                  # update in place
sh install-fork.sh --version u1-v0.99.116    # pin a specific build
sh install-fork.sh --local helixscreen-snapmaker-u1.zip   # from a downloaded archive
```

SSH on the U1: `Settings > Maintenance > Root Access`, or the `firmware-config` web
page. Credentials are `root` / `snapmaker`.

---

## Branches

| Branch | What it is |
|---|---|
| `develop/snapmaker-multiace` | **Default.** The U1 + multiACE work. Releases are cut from here |
| `main` | A clean mirror of upstream `main` — never committed to directly, so upstream merges stay conflict-free |

Release tags are `u1-v<version>` (e.g. `u1-v0.99.116`), deliberately not `v*`:
upstream's tag would start a release pipeline this fork cannot complete.

---

## Everything else

Features, supported printers, configuration, the user guide and troubleshooting are
all upstream's and unchanged — start at
**[upstream's README](https://github.com/prestonbrown/helixscreen#readme)** and the
[User Guide](docs/user/USER_GUIDE.md).

Bugs in U1 or multiACE behaviour belong [here](https://github.com/physicsG/helixscreen/issues).
Anything that reproduces without a U1 is better reported
[upstream](https://github.com/prestonbrown/helixscreen/issues).
