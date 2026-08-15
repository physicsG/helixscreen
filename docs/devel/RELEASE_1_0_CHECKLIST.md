# Release 1.0 Checklist

Everything that must happen before `v1.0.0` ships, and before the 1.1 devel track
opens alongside it. Delete this file once 1.0 is out and 1.1 is publishing.

Background on the two-track mechanism: `UPDATE_SYSTEM.md` § "How CI Determines
Upload Channels" and § "Switching Channels (and moving backward)".

---

## 1. The atomic branch cut

**These two edits must land in the same change.** They are the only ordering
constraint in this document that can strand a fleet.

- [ ] Cut `release/1.0` from the 1.0 commit. It keeps `RELEASE_CHANNEL=stable`.
- [ ] Flip `RELEASE_CHANNEL` on `main` to `beta` **in that same change**.

Why atomic: `main` currently says `stable` because it is still the only release
line. Flip it early and the stable fleet gets no further updates. Flip it late —
i.e. tag anything from `main` after 1.1 work starts — and that tag publishes to
`stable`, overwriting the 1.0 manifest for every user.

The pre-upload downgrade guard in `release.yml` catches the *second* half of that
mistake (it refuses to move a channel manifest backward), but not the first. It
also cannot help if 1.1.0 > 1.0.0, which is exactly the dangerous case: publishing
1.1.0 to `stable` is a *forward* move and sails straight through.

---

## 2. Before tagging `v1.0.0`

- [ ] Close the open 1.0-milestone issues:
  - #1272 — Print stats between HS and Mainsail don't match (lifetime totals truncated
    at the 500-job history cache)
  - #1262 — Accelerometers never discovered: Settings > Sensors is empty on every
    printer that has one
  - #1260 — Orphaned printer presets: a DB entry with no `preset` key applies none
    of its preset's settings
- [ ] Green CI on `main`. `Build`, `esp32-build`, and the nightly suite were all red
      on 2026-08-14; the first two have known causes and fixes, the nightly SIGSEGV in
      `test_recovery_dialog_threading.cpp` is still unreproduced.
- [ ] `VERSION.txt`: `0.99.113` → `1.0.0`. Every existing install is on `0.99.x`,
      so this is an ordinary forward step for the updater — no special handling.
- [x] Confirm the `ALLOW_CHANNEL_DOWNGRADE` repository variable is **unset**. It
      is the escape hatch for the downgrade guard and must be off by default.
      *Verified 2026-08-14 (`gh variable list`): not set.*
- [x] **tar.gz Phase 2 — DEFERRED, not in 1.0.** Decided 2026-08-14 on fresh
      telemetry (549 actives, 30d).

      The pre-v0.99.31 count this item was written around is still tiny (3–5 of
      549, ~0.5–0.9%, none meaningfully self-updating) — but it was never the
      real gate. `scripts/generate-manifest.sh:36` sets
      `ZIP_EXCLUDE_PLATFORMS="ad5m ad5x cc1 k1 k2 snapmaker-u1"`, six platforms
      deliberately served tar.gz as their **only** manifest asset because
      pre-v0.99.102 updaters verify with `unzip -tqq` and BusyBox lacks `unzip -t`
      before 1.32 (K1 ships 1.31.1, AD5M 1.29.3, K2's OpenWrt has none — #993).
      **Those six platforms are 344 of 549 actives, 62.7% of the fleet.** Dropping
      tar.gz production today strands the majority, not the stragglers.

      v0.99.102 (which fixes the verifier) shipped 2026-07-26; the 7-day view has
      those fleets at 80–100% on 102+, but the 30-day view is 40–50%, and the
      long-tail device that boots monthly is exactly the one that would brick.

      **Decision rule for later: Phase 2 unblocks when `ZIP_EXCLUDE_PLATFORMS` is
      empty**, not when the pre-v0.99.31 count reaches zero. Retire platforms from
      that list one at a time as each fleet clears v0.99.102. Realistically 1.1+.

      Caveat on all of the above: telemetry is opt-in and default OFF, so 549 is a
      self-selected floor. The bias runs the wrong way — a user who disables
      telemetry is plausibly the same user who does not update.

---

## 3. First release on each track

The two-track routing has never run end-to-end against real R2. Verify both.

- [ ] First `stable` tag from `release/1.0`: confirm `stable/manifest.json` serves
      `1.0.0`, and that `notify-website` fired (docs deploy is gated on
      `channel == 'stable'` now, not on the tag lacking a hyphen).
- [ ] First `beta` tag from `main`: confirm `beta/manifest.json` **and**
      `dev/manifest.json` both move, that the GitHub release is marked
      prerelease, and that `notify-website` did **not** fire.
- [ ] Confirm an app on the Beta channel is offered the devel build, and an app on
      Stable is not.
- [ ] Sanity-check the GitHub API fallback paths once R2 has both channels
      populated: `stable` uses `/releases/latest` (excludes prereleases —
      correct), `beta` scans for the first prerelease.

**Known behaviour change — checked, nobody is affected.** `stable` no longer
publishes to the `dev` channel, so anyone pinned to Dev while tracking the stable
line would stop receiving updates. Telemetry 2026-08-14 (`auto_update_channel`
from raw `settings_snapshot` events, 483 of 484 actives reporting):
**Stable 453 (93.8%), Beta 30 (6.2%), Dev 0.** Re-confirmed on a 3-day August
sample: Stable 34, Beta 2, Dev 0. Nobody is on Dev — consistent with the dropdown
being a 7-tap easter egg. No action needed.

---

## 4. Before unhiding the channel dropdown

Tracked as #1236 ("Beta: Update Channel dropdown — finish or drop", 1.1 milestone).
Do this *after* both tracks are confirmed publishing.

- [ ] Remove the `show_beta_features` gate on `container_update_channel` in
      `about_settings_overlay.xml` (currently a 7-tap easter egg on the version row).
      Note `android/app/src/main/assets/ui_xml/` carries its own copy.
- [ ] Rename the options for a two-track UX: **Stable / Devel**, keeping **Dev**
      behind the beta gate. Dev is still rejected outright without
      `/update/dev_url`, which is fine for a hidden developer option and wrong for
      a user-facing one.
- [ ] Translate the three downgrade strings — currently English placeholders in all
      8 non-English locales: `"Switch to v%s"`, `"Install Older Version?"`, and the
      confirmation body `"This channel offers v{}, older than the installed v{}…"`.
      Consult `translations/GLOSSARY.md` per locale.

---

## 5. Verified, and not

**Verified end-to-end** (headless mock against a local dev manifest serving 0.5.0
while running 0.99.111):

- `Channel is behind: 0.99.111 -> 0.5.0 (downgrade offered)` — the `Older` branch fires
- `Auto-check: 0.5.0 is a downgrade, not notifying` — no unprompted notification
- About row subject reads `Switch to v0.5.0`, not "available"
- Confirmation modal: *"Install Older Version?"* / *"This channel offers v0.5.0,
  older than the installed v0.99.111…"* → confirming reaches the download modal

**Not verified:**

- [ ] The downgrade path on a **real device**, not desktop mock. In particular the
      install actually completing and the older binary coming up on its own config.
- [x] **A full devel → stable → devel config round trip — verified 2026-08-14, safe
      for the reachable range.** `tests/unit/test_config_migration_future.cpp` now
      carries 10 round-trip cases (tag `[config][migration][roundtrip]`) driving a
      populated config — two printers, macros, LED auto-state maps, filament slot
      overrides, widget layout, material presets, a captured touch affine. Rollback
      to config_version 18/19/20 and back is **byte-identical on the whole
      document**, and a sweep of 43 untargeted settings survives every rollback
      depth. Mutation-verified.

      **Five migrations are NOT idempotent**, and are pinned as current behavior
      rather than fixed: `config.cpp:343` (jitter 15→5, fires below v3), `:446` and
      `:488` (brightness 50→80, below v7/v9), `:457` (toolhead_style 2→5/3→2, a
      rotation — below v8), `:812` (writes `recheck_pending` unconditionally, below
      v18; the flag can invalidate a captured touch calibration at boot via
      `should_invalidate_legacy_calibration`).

      **Why this is accepted, not a blocker:** every one of them requires rolling
      the stamp below config_version 18, i.e. below v0.99.80 (2026-06-18). The
      in-app updater only ever offers what a channel's manifest serves — after the
      cut that is 1.0.0 on stable and 1.1.x on beta — so reaching that range means
      hand-installing a 2026-06 build. Not a path the product exposes. The
      forward-compat guard (v0.99.112, `7e3d6f05d`) additionally stops a newer
      config being stamped down at all, and both 1.0 and 1.1 carry it.

      If a migration below v18 ever becomes reachable again, `:812` and `:457` are
      the two to fix first — `:457` is a rotation and cannot be made idempotent
      without a marker.

