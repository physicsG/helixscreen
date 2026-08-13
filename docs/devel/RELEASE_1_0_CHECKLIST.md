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

- [ ] Close or punt the open 1.0-milestone issues:
  - #1201 — Toolchanger: `can_unload_from_toolhead` assumes one mounted tool; IDEX has two carriages
  - #1065 — AD5X native ZMOD IFS: purge-timeout + Chan-authority/power-cycle gaps
  - #986 — Sovol SV06 Ace bugs
- [ ] `VERSION.txt`: `0.99.111` → `1.0.0`. Every existing install is on `0.99.x`,
      so this is an ordinary forward step for the updater — no special handling.
- [ ] Confirm the `ALLOW_CHANNEL_DOWNGRADE` repository variable is **unset**. It
      is the escape hatch for the downgrade guard and must be off by default.
- [ ] Decide on **tar.gz Phase 2**. Dropping tar.gz production is a separate,
      telemetry-gated rollout that is *not* automatically part of 1.0 — Phase 1
      (zip-primary manifest) shipped in `a8d2320ee`. Re-pull telemetry before
      deciding; the last count was 3 of 510 active devices on pre-v0.99.31, none
      self-updating. If Phase 2 is in scope for 1.0, `generate-manifest.sh` and
      `dev-release.sh` must be converted off their `*.tar.gz` globs **in the same
      change** as the `mk/cross.mk` / `release.yml` producer edits.

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

**Known behaviour change to watch:** `stable` no longer publishes to the `dev`
channel. Anyone currently pinned to Dev who is tracking the stable line stops
receiving updates until a beta/devel release publishes. Check the Dev population
first — telemetry reports `auto_update_channel`.

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
- [ ] A full devel → stable → devel config round trip with real settings. The unit
      tests pin the version stamp and unknown-key survival
      (`tests/unit/test_config_migration_future.cpp`); they do not prove every
      individual migration is idempotent if a stamp is ever rolled back by some
      other route.
