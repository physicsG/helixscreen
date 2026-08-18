# Architecture Documentation Overhaul — Design

**Date:** 2026-08-17
**Status:** Approved in brainstorming session; pending implementation plan

## Problem

HelixScreen's architecture/system documentation is (a) woefully out of date and
(b) incomplete:

- `docs/devel/ARCHITECTURE.md` (1,615 lines) is a grab-bag: some sections freshly
  maintained, others referencing **deleted files** (`src/helix_theme.c`,
  `src/ui/ui_theme.cpp`), describing shipping platforms (fbdev, evdev) as
  "future targets," and lagging documented reality (six mock boundaries vs the
  ten sub-API interfaces that exist).
- `docs/devel/architecture/` (6 diagram pairs, ~850 lines of mermaid + ~1,030
  lines of duplicated `.d2`) has stale counts ("25+ overlays" vs 44 actual;
  "12+ modals" vs 41) and an incomplete singleton map (missing
  `PostOpCooldownManager`, `RemoteControlServer`, `AudioSettingsManager`,
  `MoonrakerManager`, and others).
- Whole subsystems are absent from any overview: `src/bluetooth/`,
  `src/network/`, `src/remote/` (ctl), camera pipeline, label printing, plugin
  system flow, the helix-xml fork story, wizard/onboarding flow.
- Entry docs (`ONBOARDING.md`, `YOUR_FIRST_CONTRIBUTION.md`,
  `DEVELOPMENT.md`) overlap with each other and with the deep dives without a
  coherent journey.
- Nothing re-verifies docs, so drift is invisible until someone is misled.

## Goals

1. A new developer can get up to speed on **one subsystem — not the whole app —
   in about an hour**, using one chapter plus its guided code tour.
2. Every factual claim in the rewritten docs is verified against the current
   codebase (full audit, not spot fixes).
3. Drift in the worst rot class (dead file paths, dead links) is caught
   mechanically in CI thereafter.
4. The docs own up to tech debt, including duplication produced by AI-assisted
   development at scale — some of it framed as contributor opportunities.

## Non-Goals

- Rewriting feature-system deep dives (`FILAMENT_MANAGEMENT.md`,
  `LABEL_PRINTER_SYSTEM.md`, etc.) beyond fact fixes surfaced by the audit.
- Touching `docs/user/` (end-user docs) beyond link repairs.
- Prose-claim verification in CI (one-time audit only; the gate checks paths
  and links).
- Fixing the code debt the debt chapter documents — this is a docs change.

## Decisions

| Decision | Choice | Rejected alternatives |
|----------|--------|----------------------|
| Structure | **A: Guide series** — `docs/devel/architecture/` becomes the multi-chapter guide; `ARCHITECTURE.md` becomes a slim router (~150 lines) preserving its role as canonical link target | One big 2,500-line doc (that size is why it rotted); pure topic reorg with no router (breaks all inbound links) |
| Scope | **Full `devel/` overhaul** — core docs rewritten, entry docs reworked, neighboring deep dives audited | Core-only; core + new-dev journey |
| Audience | **Humans + agents** — narrative docs for humans; CLAUDE.md indexes remain the agent doors pointing at the same content | Humans-only (stale agent routing); agents-only (impenetrable to people) |
| Verification | **Full audit** — every subsystem chapter fact-checked against source by a dedicated audit pass | Targeted fixes; structure-only |
| Diagrams | **Mermaid only** — embedded in chapters, renders on GitHub/VS Code, zero tooling; `.d2` files deleted | D2 + committed SVGs (render-step friction); keep both (double maintenance) |
| Guardrail | **CI gate** — `scripts/check_docs_freshness.py`: cited source paths must exist, relative doc links must resolve; wired into the existing bats lint gate | Rewrite-only; gate + process changes (rubic/process items deferred) |

## The Hour Rule (sizing principle)

The **chapter is the unit of consumption**. Every subsystem chapter is bounded
by "a competent C++ dev goes from zero to productive in ~60 minutes":

- **Subsystem chapter ≈ 200–400 lines**: mental model, one mermaid diagram,
  key-files table, the subsystem's patterns and gotchas, and a **guided code
  tour** (`file:line` starting points) — skimming real source is part of the
  hour.
- **Whole-app material ≈ 15 minutes**: the router `ARCHITECTURE.md` stays
  under ~150 lines. Nobody reads the whole app in one sitting; stop pretending.
- **Where a strong deep dive exists** (`THREADING.md`,
  `MOONRAKER_ARCHITECTURE.md`, `FILAMENT_MANAGEMENT.md`, `BUILD_SYSTEM.md`),
  the chapter is the *first hour* — mental model + file map — and defers rules
  to the deep dive (the reference / second hour). Where none exists, the
  chapter carries full content. No duplication either direction.
- Review criterion per chapter: *could a competent C++ dev read this chapter
  and skim the key files in ~60 minutes and be productive?*

## Chapter Taxonomy

```
docs/devel/architecture/
  README.md          index + suggested reading paths ("I want to work on X")
  
  Part I — The reactive core
    01-declarative-ui        XML → widgets/bindings; the helix-xml fork story
    02-subjects-dataflow     subject lifecycle, observer factories, UpdateQueue bridge
    03-threading-lifetime    on-ramp → THREADING.md (deep dive)
  
  Part II — Talking to the printer
    04-moonraker             on-ramp → MOONRAKER_ARCHITECTURE.md
    05-printer-state         the 13-domain decomposition, singleton map (absorbs old ch.6)
    06-discovery-capabilities PrinterDetector, printer DB, vendor-abstraction rule
    07-filament-ams          on-ramp → FILAMENT_MANAGEMENT.md
  
  Part III — The UI layer
    08-panels-navigation     panels/overlays/modals, NavigationManager stack
    09-home-widgets          PanelWidget system, reuse, version-observer pattern
    10-theme-tokens-layout   ThemeManager/Layout (replaces the dead-files section)
  
  Part IV — Platform & services
    11-startup-shutdown      boot phases, registry-ordered teardown
    12-system-services       update, telemetry, crash reporter, sound, LED
    13-peripherals           bluetooth, label printers, camera
    14-build-platforms       thin on-ramp → BUILD_SYSTEM.md
  
  15-known-debt              tech debt map (see below)
```

Fourteen subsystem chapters + the debt map. Each chapter is one audit+write
unit; the audit work parallelizes one subagent per chapter. The old six
diagram-only files are absorbed: their mermaid content is corrected and
embedded into the relevant chapters.

## Tech Debt Chapter (`15-known-debt.md`)

Owns the gap between the rules the docs teach and the code a new dev reads.
Three categories:

1. **The imperative-UI ledger** — the ~387 sites breaking declarative-UI rules
   (#1140): why they exist (engine limitations at the time vs plain mistakes),
   the ratchet rule (count may fall, never rise; no opportunistic refactors in
   unrelated changes), and the mechanical inventory
   (`scripts/check_imperative_ui.py --list`).
2. **Duplication / DRY debt** — an honest acknowledgment that AI-assisted
   design and build at this project's scale produced duplicated logic in
   places: parallel implementations of similar behavior, forked helpers where
   extending a near-fit would have served. Not always actively harmful, but
   confusing, inelegant, and a standing target for refactoring. Ties into the
   existing review rule ("extend the near-fit helper — never fork a twin").
   The audit names the worst offenders it trips over; the chapter frames the
   pattern rather than pretending to an exhaustive list.
3. **Deliberate tolerations** — things that look like debt but are chosen:
   widget-pool recycling, procedural rendering, chart data, the permanent
   C++-is-correct exceptions tables.

Plus **"debt as first projects"** — mapping debt items to contributor on-ramps
for people who want to tackle some of it.

## Entry-Doc Journey

One coherent path, each step pointing to the next:

```
README.md → CONTRIBUTING.md → ONBOARDING.md (env + build + 15-min mental model)
  → YOUR_FIRST_CONTRIBUTION.md (annotated real-PR walkthrough)
  → subsystem chapter for the chosen area
DEVELOPMENT.md — daily-workflow/build reference; de-overlapped with
                 BUILD_SYSTEM.md (internals) via the audit
```

Walkthrough commands get executed and file paths verified during the audit.

## Deep-Dive Audits

`THREADING.md`, `MOONRAKER_ARCHITECTURE.md`, `DEVELOPER_QUICK_REFERENCE.md`
keep their structure and single-source-of-truth status; the audit corrects
facts only. Duplicated content currently living in `ARCHITECTURE.md` (threading
summaries, theme internals, etc.) is replaced by chapter summaries + links.

## CI Gate

New `scripts/check_docs_freshness.py`, joining the existing quality-gate family
(`scripts/CLAUDE.md` gate table):

1. **Path check** — every `src/`, `include/`, `ui_xml/`, `scripts/`, etc. path
   cited in `docs/devel/**/*.md` must exist on disk (kills the
   dead-`helix_theme.c` rot class).
2. **Link check** — all relative markdown links between docs resolve.
3. Wired into `tests/shell/test_code_lint.bats` so CI fails on both.

Deliberately not checking prose claims — that is the audit's one-time job.

## Migration

- `.d2` files deleted (mermaid-only decision).
- Old `architecture/01-06` diagram files absorbed into the new chapters; no
  redirect stubs — all internal links fixed in the same change.
- `ARCHITECTURE.md` rewritten in place as the router, so inbound links from
  READMEs, CLAUDE.md files, and external sources keep working.
- The three CLAUDE.md indexes (root, `docs/`, `docs/devel/`) updated as the
  agent-facing doors into the new structure.

## Verification

- Each chapter's audit notes land alongside the rewrite (in the PR/commit
  trail): what was checked, what was wrong, what was fixed or cut.
- The hour rule is reviewed per chapter during the writing pass.
- `check_docs_freshness.py` passes on the final tree and fails on a
  deliberately-broken path/link (self-test).
- All commands in entry docs executed at least once during the audit.

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Audit is the schedule risk (14 subsystems × verification depth) | One subagent per chapter, run in parallel batches; chapters are independent units |
| Rewrite accidentally changes documented *rules* (not just facts) | Deep-dive docs (THREADING.md etc.) are fact-audit-only; rule changes stay out of scope |
| New chapters immediately drift after merge | CI gate catches the mechanical class; debt chapter and indexes reviewed at release time |
| Breaking inbound links to old `architecture/0X-*.md` | Accept breakage in a docs-only change; fix every internal link; router keeps `ARCHITECTURE.md` stable |
