---
description: Create a release — bump version, changelog, tag, push. Automates the full release workflow.
argument-hint: "[patch | minor | major | X.Y.Z | X.Y.Z-beta | X.Y.Z-rc.N]"
allowed-tools: ["Bash", "Read", "Glob", "Grep", "Edit", "Write", "AskUserQuestion", "WebFetch"]
---

# RELEASE WORKFLOW

You are automating the HelixScreen release process. Follow each step in order. Hard stop on failures — do not proceed with warnings or partial state.

## CONTEXT (gather first)

Collect all of these before starting:

```bash
git rev-parse --abbrev-ref HEAD          # "main", or the release/X.Y line this version belongs to
git status --porcelain | grep -v -E '\.claude-recall/(LESSONS\.md|LESSONS\.md\.lock|CLAUDE\.md|injection-stats\.json|stats\.json)'  # Must be empty (ignoring recall files)
git fetch origin && git status -sb        # Must be up-to-date with this branch's upstream
cat VERSION.txt                           # Current version
git describe --tags --abbrev=0 2>/dev/null  # Last tag (e.g., v0.9.3)
git log $(git describe --tags --abbrev=0 2>/dev/null)..HEAD --oneline  # Commits since last tag
```

Today's date: use `date +%Y-%m-%d` output.

---

## STEP 0: PRE-FLIGHT CHECKS

All of these must pass. If ANY fails, STOP and tell the user why.

| Check | How | Fail action |
|-------|-----|-------------|
| Branch is releasable | `git rev-parse --abbrev-ref HEAD` is `main` or `release/X.Y`. Store it as `BRANCH`. | STOP: "{branch} is neither main nor a release/X.Y line" |
| Clean working tree | `git status --porcelain` is empty after ignoring `LESSONS.md`, `LESSONS.md.lock`, `CLAUDE.md`, `injection-stats.json`, and `stats.json` in `.claude-recall/`. All five are recall's own state, rewritten by any session; since worktrees route their writes here (`PROJECT_DIR`, see `setup-worktree.sh`), they are dirty most of the time and are not a reason to hold a release. Commit them with it. | STOP: "Uncommitted changes - commit them first" |
| Up to date with origin | `git fetch origin` then check `git status -sb` for "behind" | STOP: "Branch is behind origin/$BRANCH — pull first" |
| Tags fetched | `git fetch --tags origin` | Just do it silently |

**Which branch may release which version.** A patch on a shipped line lives on that
line's branch, never on `main`, because `main` carries the next minor's development.
Tagging `1.0.1` from a `main` reading `1.1.0-beta.1` would publish 1.1 work as a 1.0
patch, and tags only move forward, so there is no taking it back. `main` may release any
version; a `release/X.Y` branch may release only `X.Y.*`. STEP 1 enforces the pairing
once the version is resolved.

Every `main` hardcoded below is really `$BRANCH`: the upstream comparison, the CI query,
and the push. A release-line branch also lags `main`'s own tooling, so check before
reaching for it - `scripts/version-compare.sh` and `make full-test-run` may not exist
there. The equivalents are a careful manual semver comparison and `make test-all`.

---

## STEP 1: DETERMINE VERSION

**Argument:** `$ARGUMENTS` (the user's argument to `/release`)

### If argument is a bump type (`patch`, `minor`, `major`):
- Parse last tag (strip `v` prefix) into MAJOR.MINOR.PATCH **plus any prerelease suffix**
- **A prerelease on the last tag means that triple has not shipped.** `patch` finishes
  the prerelease line and resolves to the triple itself, not to the next patch. Treating
  the suffix as noise ships the patch of a release that never existed and strands the
  triple unpublished forever, because tags only move forward.
- Otherwise bump the named component and reset the lower components to 0
- Examples:
  - last tag `v0.9.3` + `patch` = `0.9.4`
  - last tag `v1.1.0-beta.1` + `patch` = `1.1.0`  (NOT `1.1.1`)
  - last tag `v1.1.0-beta.1` + `minor` = `1.2.0`
  - last tag `v1.1.0-beta.1` + `major` = `2.0.0`

### If argument is an explicit version (e.g., `1.0.0`, `1.0.0-beta`, `1.0.0-rc.1`):
- Strip leading `v` if present
- Validate: must match pattern `MAJOR.MINOR.PATCH[-PRERELEASE]`
- PRERELEASE can be: `alpha`, `beta`, `rc.N`

### If no argument:
- Compare `VERSION.txt` content with last tag (strip `v` prefix)
- If VERSION.txt is ALREADY different (higher) than last tag → use VERSION.txt value
- If VERSION.txt matches last tag → use AskUserQuestion:
  - Question: "No version specified and VERSION.txt matches the last tag (vX.Y.Z). What version should this release be?"
  - Header: "Version"
  - Options: "Patch (X.Y.Z+1)", "Minor (X.Y+1.0)", "Major (X+1.0.0)"
  - (user can also type a custom version via Other)

### Validation (all cases):
- Must be valid semver: `MAJOR.MINOR.PATCH` with optional `-PRERELEASE`
- Must rank strictly above the last tag. Do not eyeball this, and do not reach for
  `sort -V`: GNU version sort ranks `1.1.0` BELOW `1.1.0-beta.1`, the opposite of
  semver. Ask the comparator:
  ```bash
  scripts/version-compare.sh "$NEW_VERSION" "$(git describe --tags --abbrev=0 | sed 's/^v//')"
  ```
  `1` is the only answer that may proceed. `0` or `-1` is a STOP. A non-zero exit
  means it refused to rank the pair rather than guessing — also a STOP, never read
  that as agreement.
- The version must belong to `$BRANCH`. On a `release/X.Y` branch, `NEW_VERSION` must
  start with `X.Y.`; on `main` anything valid passes:
  ```bash
  case "$BRANCH" in
    release/*) case "$NEW_VERSION" in
                 "${BRANCH#release/}".*) ;;
                 *) echo "STOP: $NEW_VERSION does not belong to $BRANCH"; exit 1 ;;
               esac ;;
  esac
  ```
- If validation fails → STOP with clear error

Store the resolved version as `NEW_VERSION` (without `v` prefix) for all subsequent steps.

---

## STEP 2: TEST GATE

Run tests. This is mandatory and cannot be skipped.

### Quality checks

Same script CI's "Code Quality" workflow runs — single source of truth. Run it first so
lint/validation failures surface before the long test build.

```bash
./scripts/quality-checks.sh
```

- If it exits 0 → continue
- If it exits non-zero → STOP: "Quality checks failed — fix before releasing." Show the
  failing check. Look for the `❌` lines; `⚠️` lines (clang-format / XML formatting) are
  advisory and do NOT fail the script.

Do not release on a red branch. Before starting, also confirm CI is green for the commit
being released:

```bash
# EVERY workflow, not just Code Quality. CI's toolchains and -Werror set differ from
# the release machine's, so a diagnostic can be red on CI and green locally -
# checking one workflow reads as all-clear.
gh run list --branch "$BRANCH" --limit 20 --json conclusion,status,workflowName,headSha \
  --jq '.[] | select(.conclusion == "failure") | "\(.workflowName)\t\(.headSha[0:9])"'
```

If ANY workflow's latest run is a `failure`, treat it as a STOP — fix it first.
`Build Status` prints "tolerated because this is an integration branch" on a red main,
so nothing else stops a release on a red build.

Without `--branch` this reads whichever branch ran most recently, so on a release line it
happily reports `main`'s runs as if they were yours. A workflow that does not apply to the
line being released is not a reason to stop, but say which one you are setting aside and
why, rather than passing over it silently.

### C++ tests
```bash
make full-test-run
```

- If tests pass → continue
- If tests fail → STOP: "Tests failed — fix before releasing." Show the failure output.

### Shell tests
`make full-test-run` already runs the bats suite. Only on a release line that fell back
to `make test-all` (see above), also run `make test-shell`. A missing bats or a failure
is a STOP with the make output shown.

### UI tests
`make full-test-run` does not run the out-of-process UI suite, and CI does. A stale
`tests/ui/` case passes every local gate and then reds CI's Test job on the release head.
Run exactly what CI runs: `make test-ui-pytest` also runs the golden-image tests in
`tests/ui/test_screens.py`, which CI excludes because they are machine-sensitive.

```bash
make && .venv/bin/python -m pytest tests/ui --ignore=tests/ui/test_screens.py -q
```

- If it passes → continue
- If it fails → STOP: "UI tests failed — fix before releasing." Show the failing test.
- A missing `.venv` is `make venv-setup`, not a reason to skip the suite.

### Release-only builds

Per-push CI links splash and watchdog for x86 only; the Release workflow cross-compiles
every platform with `HELIX_PACKAGING=1`. A break that only those builds see (a MIPS32
link needing libatomic, a packaged binary still carrying mocks or the ctl server) shows
up first on the tag otherwise.

```bash
make release-gate TIERS=heavy                              # unified MIPS, the default
make release-gate TIERS=heavy RELEASE_GATE_TARGETS="mips pi32 k2"   # more targets
```

It runs the packaged docker cross build, `make release-<target>`, and the splash,
watchdog and mock-symbol checks, under a `heavy:release-gate` claim, and prints a
PASS/FAIL table. Any FAIL row → STOP: "Release gate failed for {target}." Show the
table and the tail of the failing step's log (`build/release-gate/<step>.log`).

### Regenerate the XML linter schema

`tools/xml-linter/schema/schema.json` is a committed snapshot of every constant and
widget attribute the runtime can resolve. The pre-commit hook only regenerates it when a
lint actually fails, so a snapshot can drift ahead of or behind the tree without any gate
noticing — it ships stale. A release is the one point where it is worth forcing.

```bash
make regen-xml-schema
make lint-xml
```

- If `make lint-xml` fails → STOP: "XML lint failed against a freshly regenerated schema."
  These are real errors (a `#const` reference with no definition anywhere), not staleness.
  Show the failing lines and fix before releasing.
- If it passes and `git diff --name-only` shows `tools/xml-linter/schema/schema.json`
  changed, stage it with the release commit (Step 6). A regenerated-but-unchanged snapshot
  is the normal, expected outcome.

---

## STEP 3: CHANGELOG (CHECKPOINT 1)

### Gather commits
```bash
git log {last_tag}..HEAD --format="%h %s" --no-merges
```

### Draft changelog entry

Follow `docs/devel/CHANGELOG_STYLE.md` for separator, issue links, voice, and entry
shapes (daily vs milestone). The skeleton is Keep a Changelog:

```markdown
## [X.Y.Z] - YYYY-MM-DD

### Added
- Feature descriptions in user-facing language

### Fixed
- Bug fix descriptions

### Changed
- Changes to existing functionality
```

Rules for writing the changelog:
- **Rewrite** commit messages into user-facing language (not developer shorthand)
- **Merge** related commits into single entries
- **Omit** noise: refactors with no user impact, CI-only changes, typo fixes, test-only changes
- **Categorize** using conventional commit types: `feat:` → Added, `fix:` → Fixed, `refactor:/perf:/build:` → Changed, `docs:` → Changed (only if user-visible)
- Only include categories that have entries (don't add empty sections)
- If this is a significant release (minor or major bump), use the milestone shape from `docs/devel/CHANGELOG_STYLE.md`: intro paragraph naming the release's role, bold sub-group headers inside categories, and a `<!-- whatsnew -->` block

### Show draft to user

Use AskUserQuestion to present the drafted changelog entry:
- Question: "Here's the draft changelog for v{NEW_VERSION}:\n\n{formatted entry}\n\nHow does this look?"
- Header: "Changelog"
- Options: "Looks good", "I'll edit it after commit", "Abort release"

If "Abort release" → STOP.

---

## STEP 4: README & DOCS SCAN

Quick scan for things that might need updating:

1. **Check README.md** for version strings, beta notices, or outdated references
2. **If major version bump** (MAJOR changed): flag that an upgrade guide may be needed
3. **If going from prerelease to stable** (e.g., `1.0.0-beta` → `1.0.0`): flag beta language to remove
4. **If going to `1.0.0` or higher**: check for "beta" or "pre-release" language in README

If there are findings, use AskUserQuestion:
- Question: "Found some docs that might need updating:\n\n{findings}\n\nWant me to update these?"
- Header: "Docs"
- Options: "Update them", "Skip for now", "Abort release"

If nothing to flag, skip this step silently.

---

## STEP 5: APPLY CHANGES

### Update VERSION.txt
Write `NEW_VERSION` (just the version string, with trailing newline) to `VERSION.txt`.

### Update CHANGELOG.md
- Insert the new entry after the header block (after the "adheres to Semantic Versioning" line and blank line)
- Add a comparison link at the bottom of the file following the existing pattern:
  ```
  [X.Y.Z]: https://github.com/prestonbrown/helixscreen/compare/v{PREVIOUS}...v{NEW_VERSION}
  ```
- Update the previous version's link if needed (the `[PREVIOUS]` entry should compare against its predecessor, not HEAD)

### Apply any approved doc changes from Step 4

### Show summary
Run `git diff` and show a brief summary of what changed.

### Release gate, fast tier

```bash
make release-gate TIERS=fast
```

Checks that `VERSION.txt` has a `## [X.Y.Z] - date` heading and a `[X.Y.Z]:` compare
link in `CHANGELOG.md`, that the tag does not exist yet, and that the installer bundles
build. Any FAIL row → STOP and fix it before committing.

---

## STEP 6: COMMIT + TAG

### Commit
Commit the pathspecs directly; never `git add` in the shared tree (CLAUDE.md § Sharing
This Tree), where a peer's commit can sweep up whatever sits in the index:

```bash
git commit -m "chore(release): v{NEW_VERSION}" -- VERSION.txt CHANGELOG.md
# Append tools/xml-linter/schema/schema.json if Step 2's regeneration changed it,
# and any other file modified in Step 5.
```

### Create annotated tag
The tag annotation becomes the GitHub release body (CI extracts it). Use the changelog entry as the tag message.

**CRITICAL — preserve the `###` markdown headers.** `git tag`'s default cleanup mode is `strip`, which deletes every line beginning with `#` — that silently removes the `### Added` / `### Fixed` / `### Changed` section headers from the release body (passing `-m "..."` does NOT avoid this; `strip` applies to `-m` and `-F` alike). You MUST write the message to a file and pass `--cleanup=verbatim`:

```bash
# Write the changelog entry (with its ### section headers) to a temp file:
cat > /tmp/helix-tag-msg-v{NEW_VERSION}.txt <<'TAGMSG'
v{NEW_VERSION}

### Added
- ...

### Fixed
- ...

### Changed
- ...
TAGMSG

git tag -a "v{NEW_VERSION}" -F /tmp/helix-tag-msg-v{NEW_VERSION}.txt --cleanup=verbatim
```

(If the shell mangles the heredoc, use the Write tool to create the temp file instead — same `--cleanup=verbatim` flag.)

The tag message should be the changelog entry content (the Added/Fixed/Changed sections, optionally with the prose summary). Format it cleanly — this is what users see on the GitHub release page.

**Verify before continuing** that the headers survived:

```bash
git cat-file tag "v{NEW_VERSION}" | grep -E '^### (Added|Fixed|Changed)'
```

If that prints nothing, the headers were stripped — delete the tag (`git tag -d v{NEW_VERSION}`, safe while unpushed) and recreate it with `--cleanup=verbatim`.

---

## STEP 7: PUSH (CHECKPOINT 2)

### Show release summary

Display a summary box:
```
## Release Summary: v{NEW_VERSION}

Previous: v{LAST_TAG}
Commits:  {count} commits
Files:    VERSION.txt, CHANGELOG.md{, other files}

Changelog preview:
{first 5 lines of changelog entry}

Pre-release: {yes/no} (tags with `-` are marked prerelease on GitHub)
```

### Ask for confirmation

Use AskUserQuestion:
- Question: "Ready to push v{NEW_VERSION} to origin? This will trigger the GitHub Actions release pipeline."
- Header: "Push"
- Options:
  - "Push it" — push commit and tag
  - "Keep local" — don't push, user will push manually later
  - "Undo" — reset the commit and delete the tag

### If "Push it":
```bash
git push origin "$BRANCH"
git push origin "v{NEW_VERSION}"
```
Then show: "Pushed! Watch the build: https://github.com/prestonbrown/helixscreen/actions"

For stable releases (tag without `-`), the `notify-website` job will fire a
`repository_dispatch` to helixscreen-website once the release job finishes,
auto-rebuilding helixscreen.org from the new tag's `docs/user/`. No manual
website deploy needed. Prereleases skip this.

### If "Keep local":
Show: "Release commit and tag created locally. Push when ready:\n`git push origin $BRANCH && git push origin v{NEW_VERSION}`"

### If "Undo":
**Ask for explicit confirmation** before running destructive commands:
```bash
git tag -d "v{NEW_VERSION}"
git reset --soft HEAD~1
```
Show: "Undone. Commit removed (changes preserved as staged), tag deleted."

---

## ERROR HANDLING

| Error | Action |
|-------|--------|
| Branch neither `main` nor `release/X.Y` | STOP with message, do not offer to switch |
| Version does not match the release line | STOP: "v{NEW_VERSION} does not belong to {BRANCH}" |
| Dirty working tree | STOP with message, do not offer to stash |
| Behind origin | STOP with message, do not offer to pull |
| Quality checks fail | STOP with the `❌` output — do not release on a red branch |
| Tests fail | STOP with failure output |
| Release gate fails | STOP with the PASS/FAIL table and the failing step's log tail |
| Version not greater | STOP: "v{NEW_VERSION} is not greater than v{LAST_TAG}" |
| User aborts at any checkpoint | STOP cleanly, no partial state |
| Push fails | Show error, commit+tag remain local for manual retry |
| Tag already exists | STOP: "Tag v{NEW_VERSION} already exists" |
