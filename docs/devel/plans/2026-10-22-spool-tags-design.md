# Spool Tags: Read, Remember, Write - Design

Status: design approved by Preston 2026-10-22, tracked in #1861. Phase 1 plan waits on DIEHARDave's answer about the shared core.

## Why

Tapping a spool on a reader should be enough for HelixScreen to know it: what it is,
how much is left, which lane it is in. Today HelixScreen only knows what a filament
box's own firmware decodes (CFS, ACE, U1, QIDI), nothing links a tag to a Spoolman
spool (`include/spoolman_slot_saver.h#LinkIntent` assumes nothing can detect a spool
swap), and users without Spoolman have no spool list at all.

The open tag ecosystem (TigerTag, OpenSpool, OpenTag3D, OpenPrintTag) has readers and
phone apps but no Klipper integration: TigerTag's own docs list Klipper as "to be
scoped", and its ESP32 scanner needs a cloud account to know which printers exist.
Preston does not want users to need a cloud account. Everything here runs locally.

## What we measured (2026-10-22, TigerPOD = 2x ACR122U on thelio)

| Spool | Tag |
|---|---|
| Sunlu (2 recent spools), eSun, Elegoo, Polymaker | none |
| Snapmaker | MIFARE Classic 1K, 4-byte UID, contents keyed |
| Bambu | 2x MIFARE Classic 1K (one per hub side), both read in one placement |
| Blank sticker (Preston's stock) | NTAG215, 496 B, NDEF-formatted empty |

- A throwaway spike (scratchpad, not in the repo) wrote and read back a TigerTag twin
  pair on two NTAG215 stickers from our own code and the CC0 ID tables, with no network.
- Third-party spools in the US mostly ship untagged, despite TigerTag listing Sunlu and
  eSun as partners. **The main case is a bare spool plus a sticker the user applies,
  written from HelixScreen.** Decoding factory tags is the secondary case.
- Writes near the edge of the field fail transiently (APDU status `6300`); a per-page
  retry plus read-back fixed it. Two processes on one reader corrupt each other: a
  reader needs exactly one owner.

## Prior art we build on: zmod_nfc and LaneTag

A HelixScreen community user, DIEHARDave, maintains two GPL-3.0 projects that already
cover most of phase 1. **Preston's call: treat him as a collaborator**, not build a
parallel copy.

- [zmod_nfc](https://github.com/DIEHARDave/zmod_nfc) (Python, Moonraker plugin, created
  2026-10-18): decoders for OpenPrintTag, OpenSpool, OpenTag3D, TigerTag, Anycubic,
  Elegoo, Spoolman links/nfc2klipper text, and keyed MIFARE Classic tags (Bambu,
  Creality incl. encrypted, Snapmaker, Qidi) from published key derivations. Reader
  drivers with no pcscd or pip dependencies (CCID over usbfs for ACR122U/ACR1552U/other
  PC/SC readers, PN532 over USB-serial, an ESP32 USB bridge for RC522/PN5180). Spoolman
  lookup by UID or tag-carried spool id, camera QR labels, test vectors. Its printer side
  is Zmod-specific (`CHANGE_ZCOLOR`, zmod_afclite, AD5X/Creator 5 slot popup). It does
  not write tags and has no non-Spoolman registry.
- [LaneTag](https://github.com/DIEHARDave/LaneTag) (ESP32 firmware): a Wi-Fi reader
  serving several printers, already able to feed a U1 through paxx's
  `filament_detect/set`.

**Direction:** propose to Dave a printer-neutral core (decoders, encoders, reader
drivers) that zmod_nfc and our plugin both depend on, with printer integration (Zmod vs
HelixScreen/generic Klipper) living in each consumer. Packaging (shared repo, vendored
subtree, or our plugin depending on his) is settled with him; Preston leans toward
the core living in its own repo, separate from both helixscreen and zmod_nfc. Our new work concentrates
on what nobody has: tag writing, the registry, usage tracking, and the HelixScreen UI.
LaneTag becomes a network reader source alongside Spoolman's `/tag/scan`.

## Goals

1. Recognize a spool from any tag we can see, by UID first and decoded content second.
2. Remember spools without Spoolman: a lightweight spool registry with remaining weight
   that goes down as jobs print.
3. Write tags from what the user enters in HelixScreen (material, color, brand,
   starting weight, temps), so the data travels with the spool and nothing is typed twice.
4. With Spoolman present, keep it in sync without manual linking.
5. Accept all three reader kinds: a filament box's own reader, a network reader that
   posts to Spoolman, and a USB reader on the host.

## Non-goals

- Writing Bambu or Snapmaker tags. Their contents are signed; they stay UID-only.
- OpenPrintTag (Prusament). It needs an NFC-V reader (ACR1552U, PN5180); the ACR122U and
  PN532 cannot see the chip. Leave room in the codec and source interfaces, build later.
- Any cloud service, account, or online lookup on the read or write path. The TigerTag
  product catalogue (16.7 MB, online) is not used.
- Replacing Spoolman. The registry is deliberately small.

## Anti-goals (would be failure even if it works)

- A feature that needs an account or internet to identify a spool.
- A heavy Moonraker database: an unbounded spool table, per-print history rows, or
  growth in `lane_data`.
- Two copies of a tag codec (Python and C++) drifting apart.
- Vendor or format names leaking into generic code (`.claude/rules/vendor-abstraction.md`):
  each format lives in one codec module; generic code asks "decode this" and gets a
  vendor-neutral record.
- "TigerTag" in a feature or product name, or its logo artwork in the repo (trademark
  terms; text mentions of format support are fine).

## Decided (by Preston)

- Our own code, no cloud dependency.
- The tag machinery lives in the HelixScreen **Moonraker plugin** (`moonraker-plugin/`).
- A non-Spoolman fallback exists and is a "super lightweight Spoolman" that stays light
  in Moonraker's database.
- Rich writing matters: what the user enters in HelixScreen goes onto the tag.
- All three reader kinds are in scope.
- Phase order below.
- **Tag refresh:** any tap on a reader we own quietly rewrites the tag's state record
  when it differs. When tag and registry disagree, the newer timestamp wins.
- **Reader setup:** the plugin's `install.sh` offers an opt-in step ("Set up a USB NFC
  reader?") that installs `pcscd` + `pyscard` and writes the `pn533` modprobe blacklist.
  Off by default; bats coverage for it.

## Architecture

| Part | Home | Responsibility |
|---|---|---|
| Codecs | shared core with zmod_nfc, one module per format | Pure `decode(uid, chip_kind, bytes) -> SpoolTag or None`, `encode(SpoolTag) -> bytes`. TigerTag, OpenSpool, OpenTag3D, Elegoo, Anycubic ACE, Creality CFS, our own state record |
| Reader sources | shared core, run by the plugin | zmod_nfc's drivers (CCID over usbfs, PN532 over USB-serial, ESP32 bridge); LaneTag and Spoolman `/tag/scan` as network sources. Each source owns its reader exclusively and emits scan events |
| Spool registry | plugin, own Moonraker DB namespace | UID(s) -> spool record. Active when Spoolman is absent |
| Usage tracking | plugin | Deducts each finished job's filament use from the spool loaded in that lane/tool (no-Spoolman mode only) |
| Spoolman bridge | plugin | Spoolman tag API (Spoolman PR #1096, merged 2026-08-23): `GET /spool?tag=<uid>`, `POST/DELETE /spool/{id}/tag`; subscribes to the `/tag/scan` websocket so any network reader works |
| Box readers | HelixScreen backends (existing C++) | Keep producing `TagReading` from firmware objects; additionally report the UID (and raw bytes where firmware exposes them) to the plugin |
| UI | HelixScreen | "Spool detected" flow, fill-in-the-gaps editor, lane assignment, write-tag action, small spool list for no-Spoolman users |
| API | plugin endpoints + notifications | scan events, resolve UID, write tag, list/edit/archive spools |

### The pipeline

scan (any source) -> UID -> Spoolman lookup if present, else registry -> if unknown,
decode content -> offer to create the spool, user fills missing fields -> link UID ->
optionally write tag -> assign to lane/tool.

A spool may carry several UIDs (twin stickers, Bambu's two hub tags). A UID belongs to
exactly one spool. This matches Spoolman's tag model.

### Data ownership

- The tag stores what the spool **is**: material, modifiers, brand, color(s), diameter,
  temps, density, starting weight, empty-spool weight.
- What **changes** (remaining weight, last seen, location) lives in Spoolman when present,
  else in the registry, and is mirrored to the tag's state record when we write, so a
  no-Spoolman spool carries its remaining weight to another printer.

### Write format

One NTAG215 holds several NDEF records; OpenTag3D readers take the first matching record
and ignore the rest. Default write:

1. OpenTag3D record (`application/opentag3d`, spec v2.003) - identity, free-text brand,
   up to 4 colors, weights, temps. Widest adoption among open formats (17 listed adopters,
   including OpenSpool's project).
2. OpenSpool record (`application/json`, `protocol: openspool`) - so a stock Snapmaker U1
   reads it.
3. Our state record (own MIME type) - remaining weight, last-written time.

About 400 of NTAG215's ~500 bytes. TigerTag (raw pages, not NDEF) cannot share a tag with
these; offer it as an alternate single-format write, not the default.

**Must verify before phase 3 commits to this:** that a stock U1 and at least one OpenTag3D
reader accept a multi-record tag. If either rejects it, fall back to a per-tag format
choice with OpenTag3D as the default.

## Phases (each shippable alone)

1. **Read anything.** Shared core with DIEHARDave (his decoders and reader drivers made
   printer-neutral), scan notification, HelixScreen shows the decoded spool. The
   opt-in installer step shrinks or disappears if the core's usbfs drivers need no
   system packages; confirm whether they get the ACR122U away from the `pn533_usb`
   kernel driver.
2. **Remember spools.** Registry, usage tracking, lane assignment, spool list UI.
3. **Write tags.** Encoders, write-with-retry-and-verify, HelixScreen editor -> tag.
4. **Spoolman mode.** Tag API bridge, `/tag/scan` subscription, create Spoolman spools
   from decoded tags, registry steps aside.
5. **Box readers join.** CFS, ACE, U1, Elegoo UIDs and raw data feed the pipeline.

## Constraints

- Follow `docs/devel/HELIXPRINT_PLUGIN.md`: spool tags register as a plugin feature
  (`register_feature`, the status `features` map, one decision function on the app
  side), and the plugin's Python 3.8 / Moonraker API 1.4.0 floor applies to the shared
  core too, which bears on packaging it with Dave.

- Plugin runs wherever Moonraker runs, including MIPS/ARM embedded boxes where `pcscd`
  and `pyscard` are unavailable. The PC/SC source must be optional: absent dependencies
  disable it with a clear capability flag, never break plugin load
  (`moonraker-plugin/helix_capabilities.py` pattern).
- The kernel `pn533_usb` driver claims ACR122U readers before `pcscd`; install docs need
  the blacklist step.
- CC0 TigerTag ID tables (~60 KB: material, brand, aspect, type, diameter, unit, version)
  are vendored into the plugin; updating them is a manual refresh, not a runtime fetch.
- TigerTag's brand field is a registry ID (146 brands): unknown brands encode as
  `Generic` (65535). Decoding must handle that and the documented unit-label ambiguity
  defensively.
- Firmware for every write: per-page retry, then full read-back compare.
- Codec tests run with captured byte fixtures; no hardware in CI.

## Calls made during review (not Preston's, check these)

- **Where decoding lives.** Plugin codecs decode raw chip bytes only. Box backends that
  read firmware-decoded objects (CFS `material_type`, ACE, U1 `filament_detect`, QIDI
  IDs) keep doing so in C++ and keep working without the plugin; that is parsing a
  firmware's report, not a second chip codec. Where a format's code tables are needed on
  both sides (CFS material codes), share the table as data (the catalog `codes` map),
  not as two hand-written mappings. Spool tags beyond what box firmware reports are a
  plugin-required feature, behind a capability flag.
- **Where "spool X is in lane/tool N" lives without Spoolman.** In the registry record
  (`location`: lane id, or tool index for a printer with no filament system). Assigning a
  spool also writes the lane's material/color through the existing
  `FilamentSlotOverrideStore` path exactly as a manual edit does today; nothing new goes
  into `lane_data`. The registry is the source of truth for which spool usage is
  deducted from.
- **Spoolman without the tag API.** Spoolman mode requires v0.27.0 or newer (first
  release after #1096). On an older or unreachable Spoolman the registry stays active,
  holding UID -> Spoolman spool id, so taps still resolve; HelixScreen says once that
  upgrading Spoolman moves the links there.

## Left to the builder

- Plugin module layout, endpoint names, notification shapes, registry record schema
  (keep it compact), pruning policy for archived/empty spools.
- Whether HelixScreen's spool UI extends the existing Spoolman views or adds a sibling.
- How usage is attributed per lane on multi-material jobs (reuse whatever Moonraker's
  Spoolman integration and our lane state already know).
- Keyed MIFARE formats (Creality, Bambu, Snapmaker, Qidi): take zmod_nfc's
  implementation and its cited public write-ups as the reference; do not re-derive.
- TigerTag signature verification: optional, informational only (the sample code
  disagrees with itself on the hash and has no full test vector).

## Open questions for later phases

- PN532-on-UART source: worth it, or USB-only?
- NFC-V reader support for OpenPrintTag.
- Importing a no-Spoolman registry into Spoolman when a user adds Spoolman later.
