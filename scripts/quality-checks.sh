#!/bin/bash
# SPDX-FileCopyrightText: 2024 Patrick Brown <opensource@pbdigital.org>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Quality checks script - single source of truth for pre-commit and CI
# Usage:
#   ./scripts/quality-checks.sh                      # Check all files (for CI)
#   ./scripts/quality-checks.sh --staged-only        # Check only staged files (for pre-commit)
#   ./scripts/quality-checks.sh --auto-fix           # Auto-fix formatting issues
#   ./scripts/quality-checks.sh --staged-only --auto-fix  # Fix staged files

set -e

# Parse arguments
STAGED_ONLY=false
AUTO_FIX=false
for arg in "$@"; do
  case "$arg" in
    --staged-only) STAGED_ONLY=true ;;
    --auto-fix) AUTO_FIX=true ;;
  esac
done

# Change to repo root
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

EXIT_CODE=0
SCRIPT_START=$(date +%s)

# Timing helper - prints elapsed time for a section (seconds)
section_time() {
  local start=$1
  local end=$(date +%s)
  local elapsed=$((end - start))
  if [ $elapsed -gt 0 ]; then
    printf " (%ds)" "$elapsed"
  fi
}

echo "🔍 Running quality checks..."
if [ "$STAGED_ONLY" = true ]; then
  echo "   Mode: Staged files only (pre-commit)"
else
  echo "   Mode: All files (CI)"
fi
echo ""

# ====================================================================
# Copyright (C) 2025-2026 356C LLC
# ====================================================================
SECTION_START=$(date +%s)
echo -n "📝 Checking copyright headers..."

if [ "$STAGED_ONLY" = true ]; then
  # Pre-commit mode: check only staged files (git-ignored files can't be staged)
  # The firmware/ exclusions mirror the ones above it, which exist because a
  # generated or vendored file is not ours to license. Under firmware/ the same
  # three categories just sit at a different prefix: LVGL font-converter output
  # (as in assets/fonts/), vendored lv_conf.h, and files vendored from
  # espressif/esp-bsp that carry their own Apache-2.0 SPDX line. Stamping
  # GPL-3.0 on any of those would be a false licence claim on third-party code.
  # firmware/native-audit is the Phase 0 feasibility audit, self-described
  # throwaway scaffolding committed only for reproducibility.
  FILES=$(git diff --cached --name-only --diff-filter=ACM | \
    grep -E '\.(cpp|c|h|mm)$' | \
    grep -v '^lib/' | \
    grep -v '^assets/fonts/' | \
    grep -v '/fonts/' | \
    grep -v '^lv_conf\.h$' | \
    grep -v '/lv_conf\.h$' | \
    grep -v '/simd/esp_lvgl_port_' | \
    grep -v '^firmware/native-audit/' | \
    grep -v '^node_modules/' | \
    grep -v '^build/' | \
    grep -v '/\.' || true)
else
  # CI mode: check all files in src/ and include/ (lib/ and assets/fonts/ excluded as auto-generated)
  FILES=$(find src include -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.mm" 2>/dev/null | \
    grep -v '/\.' | \
    grep -v '^lv_conf\.h$' || true)
fi

if [ -n "$FILES" ]; then
  MISSING_HEADERS=""
  for file in $FILES; do
    if [ -f "$file" ]; then
      if ! head -3 "$file" | grep -q "SPDX-License-Identifier: GPL-3.0-or-later"; then
        echo "❌ Missing GPL v3 header: $file"
        MISSING_HEADERS="$MISSING_HEADERS $file"
        EXIT_CODE=1
      fi
    fi
  done

  if [ -n "$MISSING_HEADERS" ]; then
    section_time $SECTION_START
    echo ""
    echo "See docs/COPYRIGHT_HEADERS.md for the required header format"
  else
    section_time $SECTION_START
    echo ""
    echo "✅ All source files have proper copyright headers"
  fi
else
  if [ "$STAGED_ONLY" = true ]; then
    section_time $SECTION_START
    echo ""
    echo "ℹ️  No source files staged for commit"
  else
    section_time $SECTION_START
    echo ""
    echo "ℹ️  No source files found"
  fi
fi

echo ""

# ====================================================================
# Phase 1: Critical Checks
# ====================================================================

# Merge Conflict Markers Check
echo "⚠️  Checking for merge conflict markers..."
if [ -n "$FILES" ]; then
  CONFLICT_FILES=$(echo "$FILES" | xargs grep -l "^<<<<<<< \|^=======$\|^>>>>>>> " 2>/dev/null || true)
  if [ -n "$CONFLICT_FILES" ]; then
    echo "❌ Merge conflict markers found in:"
    echo "$CONFLICT_FILES" | sed 's/^/   /'
    EXIT_CODE=1
  else
    echo "✅ No merge conflict markers"
  fi
else
  echo "ℹ️  No files to check"
fi

echo ""

# Trailing Whitespace Check
echo "🧹 Checking for trailing whitespace..."
if [ -n "$FILES" ]; then
  TRAILING_WS=$(echo "$FILES" | xargs grep -n "[[:space:]]$" 2>/dev/null || true)
  if [ -n "$TRAILING_WS" ]; then
    echo "⚠️  Found trailing whitespace:"
    echo "$TRAILING_WS" | head -10 | sed 's/^/   /'
    if [ $(echo "$TRAILING_WS" | wc -l) -gt 10 ]; then
      echo "   ... and $(($(echo "$TRAILING_WS" | wc -l) - 10)) more"
    fi
    echo "ℹ️  Fix with: sed -i 's/[[:space:]]*$//' <file>"
  else
    echo "✅ No trailing whitespace"
  fi
else
  echo "ℹ️  No files to check"
fi

echo ""

# XML Validation
echo "📄 Validating XML files..."
if [ "$STAGED_ONLY" = true ]; then
  XML_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep "\.xml$" || true)
else
  XML_FILES=$(find ui_xml -name "*.xml" 2>/dev/null || true)
fi

if [ -n "$XML_FILES" ]; then
  if command -v xmllint >/dev/null 2>&1; then
    XML_ERRORS=0
    for xml in $XML_FILES; do
      if [ -f "$xml" ]; then
        # Use --recover to continue on namespace errors (LVGL uses colon syntax like
        # style_arc_color:indicator which xmllint interprets as namespace prefixes).
        # Filter: only keep error lines (file:line: format) and exclude namespace errors.
        XMLLINT_OUTPUT=$(xmllint --noout --recover "$xml" 2>&1 | grep -E "^[^:]+:[0-9]+:" | grep -v "namespace error" || true)
        if [ -n "$XMLLINT_OUTPUT" ]; then
          echo "❌ Invalid XML: $xml"
          echo "$XMLLINT_OUTPUT"
          XML_ERRORS=$((XML_ERRORS + 1))
          EXIT_CODE=1
        fi
      fi
    done
    if [ $XML_ERRORS -eq 0 ]; then
      echo "✅ All XML files are valid"
    fi
  else
    echo "⚠️  xmllint not found - skipping XML validation"
    echo "   Install with: brew install libxml2 (macOS) or apt install libxml2-utils (Linux)"
  fi
else
  echo "ℹ️  No XML files to validate"
fi

echo ""

# ====================================================================
# XML Constant Set Validation
# ====================================================================
echo "🔤 Validating XML constant sets..."

if [ -x "build/bin/validate-xml-constants" ]; then
  if ./build/bin/validate-xml-constants; then
    : # Success message already printed by tool
  else
    echo ""
    echo "   Incomplete constant sets can cause runtime warnings."
    echo "   - Responsive px: Need ALL of _small, _medium, _large (or none)"
    echo "   - Theme colors: Need BOTH _light and _dark (or neither)"
    EXIT_CODE=1
  fi
else
  echo "⚠️  validate-xml-constants not built - skipping"
  echo "   Run 'make' to build validation tools"
fi

echo ""

# ====================================================================
# XML Attribute Validation
# ====================================================================
echo "📄 Validating XML attributes..."

if [ -x "build/bin/validate-xml-attributes" ]; then
  if [ "$STAGED_ONLY" = true ]; then
    # Check only staged XML files in pre-commit mode
    STAGED_XML_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.xml$' || true)
    if [ -n "$STAGED_XML_FILES" ]; then
      # shellcheck disable=SC2086
      if ./build/bin/validate-xml-attributes --warn-only $STAGED_XML_FILES 2>/dev/null; then
        echo "✅ XML attribute validation passed"
      else
        echo "⚠️  Unknown XML attributes found (warnings only for now)"
        echo "   Run './build/bin/validate-xml-attributes' for details"
        # NOTE: Using --warn-only so this doesn't block commits during adoption
        # Remove --warn-only once all false positives are resolved
      fi
    else
      echo "ℹ️  No XML files staged for commit"
    fi
  else
    # CI mode: check all XML files with --warn-only
    if ./build/bin/validate-xml-attributes --warn-only 2>/dev/null; then
      echo "✅ XML attribute validation passed"
    else
      echo "⚠️  Unknown XML attributes found (warnings only for now)"
      echo "   Run './build/bin/validate-xml-attributes' for details"
    fi
  fi
else
  echo "⚠️  validate-xml-attributes not built - skipping"
  echo "   Run 'make validate-xml-attrs' to build validation tool"
fi

echo ""

# ====================================================================
# Duplicate XML widget names
# ====================================================================
# Background: lv_obj_find_by_name() returns the FIRST depth-first match and
# warns about nothing, so a name declared twice in one file makes the second
# element unreachable — built, then silently never configured (#1136,
# ams_panel.xml name="endless_arrows" twice).
#
# Per-name ratcheting baseline lives in the script (DUPLICATE_NAME_BASELINE).
# The pre-existing entries are settings/about rows whose label/value names are
# only ever looked up with the ROW as search parent.
echo "🏷️  Checking for duplicate XML widget names..."

if [ -f "scripts/check_duplicate_xml_names.py" ]; then
  if [ "$STAGED_ONLY" = true ]; then
    DUP_NAME_ARGS="--staged-only"
  else
    DUP_NAME_ARGS=""
  fi
  # shellcheck disable=SC2086
  if python3 scripts/check_duplicate_xml_names.py $DUP_NAME_ARGS --summary >/tmp/duplicate_xml_names.out 2>&1; then
    cat /tmp/duplicate_xml_names.out
  else
    cat /tmp/duplicate_xml_names.out
    echo "   Run: python3 scripts/check_duplicate_xml_names.py --list"
    EXIT_CODE=1
  fi
else
  echo "⚠️  check_duplicate_xml_names.py not found — skipping"
fi

echo ""

# ====================================================================
# helix-xml-linter (mirrors the "XML Lint" CI gate)
# ====================================================================
# The linter resolves every #const reference against a committed snapshot,
# tools/xml-linter/schema/schema.json. Adding a <px>/<color>/<string> to an XML
# file without regenerating that snapshot leaves the new name unknown, so every
# reference to it is an unknown-const-ref on CI while the commit sails through
# locally — three times now (#1204 most recently).
#
# Kept cheap (~0.7s) three ways, because this runs on every commit:
#   - Triggers on staged paths for the XML half (ui_xml/, tools/xml-linter/)
#     and on staged diff CONTENT for the rest of the schema's inputs. The
#     extractor (mk/tools.mk regen-xml-schema) also reads src/ui/ widget
#     registrations, src/ lv_xml_register_const() calls, lib/helix-xml/src/xml
#     and the theme defaults, and none of those reach the linter except through
#     the schema. A path trigger on src/ would fire on nearly every commit and
#     pay make's startup for nothing, so the schema-input signals below match
#     the added/removed lines instead — the same technique XML_CONST_DELETED
#     uses. CI's XML Lint job still covers the wider path set.
#   - Lints against the snapshot on disk first. That is what CI does with the
#     committed copy, so on a clean tree a pass here means CI passes and no
#     regen is owed. It also stops schema.json churning on commits where
#     staleness changes no outcome.
#   - Regenerates only on the paths where the snapshot can actually be at
#     fault: a failing lint, a staged edit that deletes a const definition
#     (which would otherwise leave dangling refs resolving against a stale
#     snapshot), or a staged change to a schema input the linter cannot see.
#     Only those rare paths pay the 0.5s `make` startup.
echo "🧩 Running helix-xml-linter..."

XML_LINTER_SCHEMA_PATH="tools/xml-linter/schema/schema.json"

# Mirrors `make lint-xml` (mk/tools.mk) but skips make's ~0.5s startup. Always
# lints all of ui_xml/, never just the staged files: the linter builds its
# cross-file component registry from the paths it is handed, so a staged-only
# run reports every component defined in an unstaged file as unknown.
run_xml_linter() {
  PYTHONPATH=tools/xml-linter/src python3 -m helix_xml_linter.cli \
    --schema "$XML_LINTER_SCHEMA_PATH" --severity error ui_xml/
}

# True when the staged diff adds or removes a C++ schema input. Both patterns
# mirror extract_schema.py exactly:
#   - _REGISTER_WIDGET_RE / _auto_discover_cpp_widgets() rglob *.cpp under
#     --cpp-src src/ui for lv_xml_register_widget("name", …)
#   - _CPP_REGISTER_CONST_RE / extract_cpp_registered_constants() rglob *.cpp
#     under --cpp-const-dirs src for lv_xml_register_const(scope, "name")
# .cpp only, because that is all the extractor globs — a registration moved to a
# header is invisible to the schema either way. `-U0` leaves only changed lines,
# and no ---/+++ file header can contain either call, so `^[-+]` is enough.
schema_cpp_input_changed() {
  if git diff --cached -U0 -- 'src/ui/*.cpp' | \
     grep -qE '^[-+].*lv_xml_register_widget[[:space:]]*\('; then
    return 0
  fi
  if git diff --cached -U0 -- 'src/*.cpp' | \
     grep -qE '^[-+].*lv_xml_register_const[[:space:]]*\('; then
    return 0
  fi
  return 1
}

# Staged schema inputs the linter cannot see through the current snapshot, so
# they force a regen below rather than trusting the first lint. lib/helix-xml is
# a gitlink: its files never appear in this index, only the pointer bump does.
# mk/tools.mk is in the set because it owns the extractor's argument list.
SCHEMA_INPUT_STAGED=false

if [ "$STAGED_ONLY" = true ]; then
  XML_LINT_TRIGGERS=$(git diff --cached --name-only --diff-filter=ACM | \
    grep -E '^(ui_xml/.*\.xml$|tools/xml-linter/)' || true)
  if git diff --cached --name-only --diff-filter=ACMD | \
     grep -qE '^(lib/helix-xml$|mk/tools\.mk$|assets/config/themes/defaults/)' || \
     schema_cpp_input_changed; then
    SCHEMA_INPUT_STAGED=true
    XML_LINT_TRIGGERS="$XML_LINT_TRIGGERS schema-inputs"
  fi
else
  XML_LINT_TRIGGERS="all"
fi

if [ -z "$XML_LINT_TRIGGERS" ]; then
  echo "ℹ️  No XML linter inputs staged"
elif ! command -v python3 >/dev/null 2>&1; then
  echo "⚠️  python3 not found — skipping XML lint"
else
  # A deleted const can leave a dangling ref that still resolves against the
  # stale snapshot — the one staleness a passing lint cannot rule out.
  XML_CONST_DELETED=false
  if [ "$STAGED_ONLY" = true ]; then
    if git diff --cached -U0 -- 'ui_xml/*.xml' | \
       grep -qE '^-[[:space:]]*<(px|color|string|int|percentage|font|tiny_ttf|bin|const)[[:space:]][^>]*name='; then
      XML_CONST_DELETED=true
    fi
  fi

  if [ "$XML_CONST_DELETED" = false ] && [ "$SCHEMA_INPUT_STAGED" = false ] && \
     run_xml_linter >/tmp/lint_xml.out 2>&1; then
    echo "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
  else
    # The lint failed, a const was deleted, or a schema input the lint cannot
    # see is staged. Refresh the snapshot and retry — `make` here (not the raw
    # extractor) keeps mk/tools.mk the single source of truth for the
    # extractor's argument list.
    #
    # SCHEMA_DIRTY_BEFORE is worktree-vs-index: true means the snapshot carries
    # unstaged edits, which may belong to a change that is not this commit's.
    SCHEMA_DIRTY_BEFORE=false
    git diff --quiet -- "$XML_LINTER_SCHEMA_PATH" || SCHEMA_DIRTY_BEFORE=true

    # Stale means REGENERATION CHANGED THE BYTES, which is a different question
    # from "differs from the index". On a tree where the snapshot has already
    # been regenerated and not staged, the bytes on disk are correct yet still
    # differ from the index — comparing against the index there calls a correct
    # file stale and blocks the commit on a diagnosis nobody can act on. So keep
    # the pre-regen bytes and compare the regen output against those.
    SCHEMA_BEFORE_REGEN=$(mktemp "${TMPDIR:-/tmp}/helix_schema_before.XXXXXX")
    cp "$XML_LINTER_SCHEMA_PATH" "$SCHEMA_BEFORE_REGEN"

    if make regen-xml-schema >/tmp/regen_xml_schema.out 2>&1; then
      SCHEMA_WAS_STALE=false
      cmp -s "$SCHEMA_BEFORE_REGEN" "$XML_LINTER_SCHEMA_PATH" || SCHEMA_WAS_STALE=true
      rm -f "$SCHEMA_BEFORE_REGEN"

      if run_xml_linter >/tmp/lint_xml.out 2>&1; then
        if [ "$SCHEMA_WAS_STALE" = false ] && [ "$SCHEMA_DIRTY_BEFORE" = false ]; then
          echo "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
        elif [ "$SCHEMA_WAS_STALE" = false ]; then
          # Regeneration changed nothing: the snapshot is already correct and
          # merely unstaged. Nothing to fix, and nothing this hook may stage —
          # those bytes belong to whichever change regenerated them, which is
          # the same reason the stale branch below refuses to stage a dirty
          # file. Not a commit blocker: the committer cannot resolve it from
          # inside this commit without absorbing someone else's content. Say it
          # plainly instead, because CI lints the COMMITTED copy.
          echo "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
          echo "ℹ️  $XML_LINTER_SCHEMA_PATH is already up to date but unstaged"
          echo "   CI's XML Lint job lints the committed copy — commit it:"
          echo "   git add $XML_LINTER_SCHEMA_PATH"
        # The snapshot was the problem. Stage it with the XML that made it
        # stale — unless it was already dirty, in which case it belongs to
        # unrelated WIP and is not ours to stage.
        elif [ "$AUTO_FIX" = true ] && [ "$STAGED_ONLY" = true ] && [ "$SCHEMA_DIRTY_BEFORE" = false ]; then
          git add "$XML_LINTER_SCHEMA_PATH"
          echo "   ✓ Regenerated and staged $XML_LINTER_SCHEMA_PATH"
          echo "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
        else
          echo "⚠️  $XML_LINTER_SCHEMA_PATH was stale — regenerated in place"
          echo "   Commit it or CI's XML Lint job will fail:"
          echo "   git add $XML_LINTER_SCHEMA_PATH"
          EXIT_CODE=1
        fi
      else
        cat /tmp/lint_xml.out
        echo "   Run 'make lint-xml-all' to see warnings too"
        EXIT_CODE=1
      fi
    else
      # Regeneration is best-effort — fall back to the snapshot on disk so a
      # broken extractor can't block every commit, and fail only on real lint errors.
      rm -f "$SCHEMA_BEFORE_REGEN"
      cat /tmp/regen_xml_schema.out
      echo "⚠️  Schema regeneration failed — linting against the snapshot on disk"
      if run_xml_linter >/tmp/lint_xml.out 2>&1; then
        echo "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
      else
        cat /tmp/lint_xml.out
        echo "   Run 'make lint-xml-all' to see warnings too"
        EXIT_CODE=1
      fi
    fi
  fi
fi

echo ""

# ====================================================================
# helix-xml submodule test suite (CMake + Unity + ctest)
# ====================================================================
# lib/helix-xml is our own submodule and carries its own standalone suite,
# which `make test` does NOT build: helix-tests only reaches the engine through
# the app. A pointer bump that regresses the parser is therefore invisible to
# every other gate here.
#
# Kept off the hot path three ways, because a first configure clones LVGL
# (FetchContent, minutes, needs network) and that must never land on someone's
# unrelated commit:
#   - Triggers on staged lib/helix-xml only. The submodule's own files are not
#     tracked by this repo, so the one thing that CAN be staged is the pointer
#     itself — which is exactly the change that alters what the suite tests.
#   - Never configures. If the build tree is absent this SKIPS with an
#     instruction to run `make test-xml` once by hand, rather than blocking a
#     commit on a network fetch. That also means CI mode (no staged files, no
#     build tree) skips, leaving the fetch to a job that opts into it.
#   - No cmake, no submodule checkout: skip with a note, same as bats/xmllint.
# When it does run, a failing suite is a hard failure like any other test gate.
SECTION_START=$(date +%s)
echo -n "🧩 Checking helix-xml submodule tests..."

HELIX_XML_TEST_BUILD_DIR="build/helix-xml-tests"

if [ "$STAGED_ONLY" = true ]; then
  HELIX_XML_TRIGGERS=$(git diff --cached --name-only --diff-filter=ACM | grep -E '^lib/helix-xml' || true)
else
  # CI mode has no staged set; the build-tree check below is what keeps this
  # from triggering a fetch.
  HELIX_XML_TRIGGERS="all"
fi

section_time $SECTION_START
echo ""

if [ -z "$HELIX_XML_TRIGGERS" ]; then
  echo "ℹ️  No lib/helix-xml changes staged — skipping submodule tests"
elif [ ! -f "lib/helix-xml/tests/CMakeLists.txt" ]; then
  echo "⚠️  lib/helix-xml/tests not found — skipping submodule tests"
  echo "   Run: git submodule update --init --recursive"
elif ! command -v cmake >/dev/null 2>&1; then
  echo "⚠️  cmake not found — skipping helix-xml submodule tests"
  echo "   Install with: brew install cmake (macOS) or apt install cmake (Linux)"
elif [ ! -f "$HELIX_XML_TEST_BUILD_DIR/CMakeCache.txt" ]; then
  echo "⚠️  helix-xml test build tree not configured — skipping"
  echo "   The first configure clones LVGL (needs network, several minutes),"
  echo "   which is too slow to run from a commit hook."
  echo "   Run 'make test-xml' once by hand to enable this gate."
else
  SECTION_START=$(date +%s)
  if make test-xml >/tmp/test_xml.out 2>&1; then
    printf "✅ helix-xml submodule tests passed"
    section_time $SECTION_START
    echo ""
  else
    tail -30 /tmp/test_xml.out
    echo "❌ helix-xml submodule tests failed"
    echo "   Run: make test-xml"
    EXIT_CODE=1
  fi
fi

echo ""

# ====================================================================
# Hidden test set (make test-hidden)
# ====================================================================
# The 89 [.]-tagged tests are excluded from `make test-run`: they need ui_xml/
# on a relative path, own destructive global state, or are timing-sensitive
# stress harnesses. Nothing else runs them, which is how six of them rotted red
# without anyone noticing. See docs/devel/HIDDEN_TESTS_TRACKER.md.
#
# Kept off the hot path the same two ways the helix-xml block above is:
#   - Triggers only on staged code. A docs/installer/asset commit skips.
#   - NEVER builds. A cold test build is ten minutes, which must not land on a
#     commit hook, so this runs only when the test binary is already current
#     (`make -q`) — i.e. the author has built tests anyway and the marginal
#     cost is the ~65s run itself. Stale or absent binary skips with an
#     instruction. That also means CI mode skips: the Code Quality runner
#     never builds the binary. The hidden set belongs in nightly there.
# When it does run, a failure blocks like any other test gate.
SECTION_START=$(date +%s)
echo -n "🙈 Checking hidden test set..."

if [ "$STAGED_ONLY" = true ]; then
  HIDDEN_TRIGGERS=$(git diff --cached --name-only --diff-filter=ACM | \
    grep -E '^(src/|include/|ui_xml/|tests/)' || true)
else
  # CI mode has no staged set; the up-to-date check below is what keeps this
  # from triggering a ten-minute build on a runner that has no test binary.
  HIDDEN_TRIGGERS="all"
fi

section_time $SECTION_START
echo ""

if [ -z "$HIDDEN_TRIGGERS" ]; then
  echo "ℹ️  No src/include/ui_xml/tests changes staged — skipping hidden tests"
elif [ ! -x "build/bin/helix-tests" ]; then
  echo "⚠️  build/bin/helix-tests not built — skipping hidden tests"
  echo "   Run 'make test-hidden' by hand to enable this gate."
elif ! make -q _PARALLEL_GUARD=1 build/bin/helix-tests >/dev/null 2>&1; then
  echo "⚠️  Test binary is stale — skipping hidden tests"
  echo "   A test build is too slow for a commit hook. Run: make test-hidden"
else
  SECTION_START=$(date +%s)
  if make test-hidden >/tmp/test_hidden.out 2>&1; then
    printf "✅ Hidden tests passed (%s)" "$(grep -E '^test cases:' /tmp/test_hidden.out | tail -1)"
    section_time $SECTION_START
    echo ""
  else
    grep -E '^(tests/|  |test cases:|assertions:)' /tmp/test_hidden.out | tail -40
    echo "❌ Hidden tests failed"
    echo "   Run: make test-hidden"
    EXIT_CODE=1
  fi
fi

echo ""

# ====================================================================
# Overlay width is decided at push time, never in XML (#1178)
# ====================================================================
# The two width constants encode destination-vs-transient-layer, and which one
# an overlay gets depends on how the user reached it — the same
# fan_control_overlay is a transient layer from Controls and a drill-down from
# Settings > Fans. Hand-picking a constant in XML is what left 20 panels gapped
# and 36 full with no rule, and made console_settings_overlay render wider than
# the console_panel it was pushed from.
echo "📐 Checking overlay width declarations..."

if [ -f "scripts/check_overlay_width.py" ]; then
  if [ "$STAGED_ONLY" = true ]; then
    OVERLAY_WIDTH_ARGS="--staged-only"
  else
    OVERLAY_WIDTH_ARGS=""
  fi
  # shellcheck disable=SC2086
  if python3 scripts/check_overlay_width.py $OVERLAY_WIDTH_ARGS >/tmp/overlay_width.out 2>&1; then
    echo "✅ No hand-picked overlay widths"
  else
    cat /tmp/overlay_width.out
    EXIT_CODE=1
  fi
else
  echo "⚠️  check_overlay_width.py not found — skipping"
fi

echo ""

# ====================================================================
# Spacing and sizing go through design tokens, not raw pixel literals
# ====================================================================
# HelixScreen ships on 480x272 through 1440p. A literal style_pad_all="12" is
# 12px on all of them, so a layout tuned in a 1024x600 dev window is cramped on
# a Snapmaker U1 and lost in whitespace on a 1280x720 panel. The ladders in
# ui_xml/globals.xml resolve per breakpoint; a literal freezes one column of the
# ladder forever.
#
# Ratcheting baseline. The remaining sites are real debt — mostly 2px hairline
# gaps and two negative overlap margins with no token to name them. The number
# may go DOWN (convert a site, then lower this baseline) but must never go up.
# A reasoned exception is annotated SIZE_OK, the way ui_xml/color_picker.xml
# walks its swatch-grid content floor.
echo "📏 Checking design-token usage (hardcoded pixels)..."

if [ -f "scripts/check_hardcoded_pixels.py" ]; then
  # Pre-commit: scan the post-commit tree (index + HEAD), not the dirty working
  # tree — so another session's unstaged WIP cannot trip the ratchet on a clean
  # commit. CI and manual runs use the whole-working-tree scan (no flag).
  if [ "$STAGED_ONLY" = true ]; then
    PIXELS_ARGS="--staged-only"
  else
    PIXELS_ARGS=""
  fi
  # shellcheck disable=SC2086
  if python3 scripts/check_hardcoded_pixels.py --max-allowed 162 --summary $PIXELS_ARGS \
      >/tmp/hardcoded_pixels.out 2>&1; then
    tail -1 /tmp/hardcoded_pixels.out
  else
    cat /tmp/hardcoded_pixels.out
    echo "   Run: python3 scripts/check_hardcoded_pixels.py --list"
    echo "   Use a token; see CLAUDE.md § Design Tokens (MANDATORY)."
    EXIT_CODE=1
  fi
else
  echo "⚠️  check_hardcoded_pixels.py not found — skipping"
fi

echo ""

echo "🪟 Checking layout-variant parity..."

# A ui_xml/<variant>/ file replaces its base wholesale, so nothing else notices
# when the base grows a binding the variant never gets. Those failures are
# silent at runtime (prestonbrown/helixscreen#1203). Always whole-tree: parity
# is a property of a file PAIR, so staging only one half still has to be checked.
if [ -f "scripts/check_variant_parity.py" ]; then
  if python3 scripts/check_variant_parity.py >/tmp/variant_parity.out 2>&1; then
    echo "✅ Layout variants match their base wiring"
  else
    cat /tmp/variant_parity.out
    EXIT_CODE=1
  fi
else
  echo "⚠️  check_variant_parity.py not found — skipping"
fi

echo ""

echo "🎭 Checking layout-variant content drift (warning only)..."

# Wiring parity (above) doesn't catch a font bump, an icon src= swap, or an
# edited translation_tag= in a base file whose ui_xml/<variant>/ sibling
# didn't get the same edit -- check_variant_parity.py deliberately does not
# compare attributes. WARNING ONLY, never fails: additive divergence (e.g.
# portrait's temperature section, absent from the landscape base) is
# legitimate, and this gate cannot tell that apart from rot -- only a human
# glancing at the named file/attribute can. Staged-diff scoped by design: a
# base+variant pair staged TOGETHER is the human already keeping them in sync.
if [ -f "scripts/check_variant_content_drift.py" ]; then
  python3 scripts/check_variant_content_drift.py
  # NOTE: intentionally not gating -- see docstring in the script.
  # EXIT_CODE=1
else
  echo "⚠️  check_variant_content_drift.py not found — skipping"
fi

echo ""

echo "📏 Checking responsive token placement..."

# theme_manager_find_xml_files() skips subdirectories, so a responsive token
# declared below the top level of ui_xml/ is never registered and every #token
# reading it resolves to nothing, silently (prestonbrown/helixscreen#1211).
# Always whole-tree: the scan is a regex over ~330 small files, and the rule is
# about where a file SITS, so a staged-only view buys nothing.
if [ -f "scripts/check_responsive_token_scope.py" ]; then
  if python3 scripts/check_responsive_token_scope.py >/tmp/responsive_token_scope.out 2>&1; then
    echo "✅ Responsive tokens are all top-level"
  else
    cat /tmp/responsive_token_scope.out
    EXIT_CODE=1
  fi
else
  echo "⚠️  check_responsive_token_scope.py not found — skipping"
fi

# ESP32 firmware app_srcs manifest drift. The manifest is a hand-maintained
# subset of src/ (v1 Core+AMS cut); a new src/ file that misses it breaks the
# firmware link ~25 min into esp32-build CI. This makes the drift loud here.
# Skips cleanly when the firmware tree is absent (e.g. a shallow checkout).
if [ -f "firmware/helixscreen-esp32/components/helixapp/app_srcs.txt" ] && \
   [ -f "scripts/check_esp32_app_srcs.py" ]; then
  if python3 scripts/check_esp32_app_srcs.py >/tmp/esp32_app_srcs.out 2>&1; then
    echo "✅ ESP32 app_srcs manifest covers src/ (no drift)"
  else
    cat /tmp/esp32_app_srcs.out
    EXIT_CODE=1
  fi
else
  echo "⚠️  esp32 app_srcs manifest or gate not found — skipping"
fi

# A printer_database.json entry naming an image that does not exist is silent at
# runtime: the lookup falls through to generic-corexy and logs nothing above debug,
# so a bed-slinger just quietly shows a CoreXY frame. Twenty entries had drifted
# that way before anyone noticed.
if [ -f "assets/config/printer_database.json" ] && [ -f "scripts/check_printer_images.py" ]; then
  if python3 scripts/check_printer_images.py >/tmp/printer_images.out 2>&1; then
    cat /tmp/printer_images.out
  else
    cat /tmp/printer_images.out
    EXIT_CODE=1
  fi
else
  echo "⚠️  printer database or image gate not found — skipping"
fi

echo ""

# ====================================================================
# Phase 2: Code Quality Checks
# ====================================================================

# Code Formatting Check (clang-format) - WARNING ONLY
# NOTE: clang-format versions differ between local (macOS Homebrew) and CI (Ubuntu)
# which can cause false positives. Use pre-commit hook for local enforcement.
echo "🎨 Checking code formatting (clang-format)..."
# Resolve clang-format to the EXACT pinned wheel (clang-format==18.1.8 in
# requirements.txt, installed into .venv by `make deps`). Preference order:
# $CLANG_FORMAT override, then the project .venv (the single source of truth —
# byte-identical on every OS + CI), then a system clang-format-18, then bare
# clang-format. The .venv wins over the system binary so a machine's Homebrew
# (newer) or distro (older 18.1.x patch) clang-format never affects formatting.
# Auto-fix only runs when the resolved binary is v18, so a non-18 fallback can
# never reflow whole files.
CF_BIN=""
CF_VER=""
for cf_cand in "${CLANG_FORMAT:-}" "$REPO_ROOT/.venv/bin/clang-format" clang-format-18 clang-format; do
  [ -n "$cf_cand" ] || continue
  command -v "$cf_cand" >/dev/null 2>&1 || [ -x "$cf_cand" ] || continue
  cf_v="$("$cf_cand" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
  [ -n "$cf_v" ] || continue
  CF_BIN="$cf_cand"
  CF_VER="$cf_v"
  case "$cf_v" in 18.*) break ;; esac
done
if [ -n "$FILES" ]; then
  if [ -n "$CF_BIN" ]; then
    if [ -f ".clang-format" ]; then
      FORMAT_ISSUES=""
      for file in $FILES; do
        if [ -f "$file" ]; then
          # Skip auto-generated sources: their on-disk format is owned by the
          # generator (e.g. src/generated/lv_i18n_translations.c from
          # generate_translations.py). Running clang-format on them fights the
          # generator on every build, producing perpetual post-build churn.
          if head -5 "$file" | grep -qiE 'auto-generated|DO NOT EDIT'; then
            continue
          fi
          # Check if file needs formatting
          if ! "$CF_BIN" --dry-run --Werror "$file" >/dev/null 2>&1; then
            FORMAT_ISSUES="$FORMAT_ISSUES $file"
            if [ "$AUTO_FIX" = true ]; then
              case "$CF_VER" in
                18.*)
                  "$CF_BIN" -i "$file"
                  echo "   ✓ Auto-formatted: $file"
                  ;;
                *)
                  echo "   ⚠️  Skipping auto-format of $file: resolved clang-format $CF_VER != 18"
                  echo "       (auto-formatting with a non-CI version would reflow the whole file)"
                  echo "       Install v18: pip install 'clang-format==18.1.8' into .venv, or set CLANG_FORMAT=clang-format-18"
                  ;;
              esac
            fi
          fi
        fi
      done

      if [ -n "$FORMAT_ISSUES" ]; then
        if [ "$AUTO_FIX" = true ]; then
          # Auto-stage formatted files when in pre-commit mode (--staged-only)
          if [ "$STAGED_ONLY" = true ]; then
            git add $FORMAT_ISSUES
            echo "✅ Auto-formatted and re-staged files:"
            echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
          else
            echo "✅ Auto-formatted files - re-stage them before committing:"
            echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
            echo ""
            echo "ℹ️  Stage formatted files with:"
            echo "   git add$FORMAT_ISSUES"
          fi
        else
          echo "⚠️  Files may need formatting (version differences may cause false positives):"
          echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
          echo ""
          echo "ℹ️  Fix with: clang-format -i <file>"
          echo "ℹ️  Or run: ./scripts/quality-checks.sh --auto-fix"
          # NOTE: Don't fail CI for formatting - version differences cause issues
          # EXIT_CODE=1
        fi
      else
        echo "✅ All files properly formatted"
      fi
    else
      echo "ℹ️  No .clang-format file found - skipping format check"
    fi
  else
    echo "⚠️  clang-format not found - skipping format check"
    echo "   Install with: brew install clang-format (macOS) or apt install clang-format (Linux)"
  fi
else
  echo "ℹ️  No files to check"
fi

echo ""

# XML Formatting Check
# ui_xml/translations/ is generator output (rewritten by every build), so it is
# excluded from FORMATTING but not from the validation pass above - the generator
# still has to emit well-formed XML. Same exclusion as mk/format.mk; format-xml.py
# self-guards via GENERATED_DIRS, but the xmllint fallback below does not.
echo "📐 Checking XML formatting..."
if [ "$STAGED_ONLY" = true ]; then
  XML_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep "\.xml$" | grep -v "^ui_xml/translations/" || true)
else
  XML_FILES=$(find ui_xml -name "*.xml" -not -path "ui_xml/translations/*" 2>/dev/null || true)
fi

VENV_PYTHON=".venv/bin/python"

if [ -n "$XML_FILES" ]; then
  # Prefer Python formatter with attribute wrapping, fallback to xmllint
  if [ -x "$VENV_PYTHON" ] && $VENV_PYTHON -c "import lxml" 2>/dev/null; then
    # Use our custom formatter with --check mode
    if $VENV_PYTHON scripts/format-xml.py --check $XML_FILES 2>/dev/null; then
      echo "✅ All XML files properly formatted"
    else
      echo "⚠️  XML files need formatting"
      echo "ℹ️  Fix with: .venv/bin/python scripts/format-xml.py <files>"
      echo "ℹ️  Or run: make format"
      # Don't fail CI for XML formatting - it's a style preference
      # EXIT_CODE=1
    fi
  elif command -v xmllint >/dev/null 2>&1; then
    echo "ℹ️  Python formatter not available, using xmllint (basic check only)"
    FORMAT_ISSUES=""
    for file in $XML_FILES; do
      if [ -f "$file" ]; then
        # Check if file needs formatting (xmllint --format for consistent indentation)
        FORMATTED=$(xmllint --format "$file" 2>/dev/null || echo "PARSE_ERROR")
        if [ "$FORMATTED" = "PARSE_ERROR" ]; then
          echo "⚠️  Cannot format $file (may have XML errors)"
        else
          ORIGINAL=$(cat "$file")
          if [ "$FORMATTED" != "$ORIGINAL" ]; then
            FORMAT_ISSUES="$FORMAT_ISSUES $file"
          fi
        fi
      fi
    done

    if [ -n "$FORMAT_ISSUES" ]; then
      echo "⚠️  XML files may need formatting (basic check):"
      echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
      echo "ℹ️  For proper formatting: make venv-setup && make format"
    else
      echo "✅ All XML files pass basic formatting check"
    fi
  else
    echo "ℹ️  No XML formatter available - skipping XML format check"
    echo "   Run 'make venv-setup' to enable full XML formatting"
  fi
else
  echo "ℹ️  No XML files to check"
fi

echo ""

# Build Verification
if [ "$STAGED_ONLY" = true ]; then
  SECTION_START=$(date +%s)
  echo -n "🔨 Verifying incremental build..."

  # Fast timestamp check (avoids 2-3s make startup overhead)
  # Check if binary exists and no source files are newer
  TARGET="build/bin/helix-screen"
  BUILD_NEEDED=false

  if [ ! -f "$TARGET" ]; then
    BUILD_NEEDED=true
  elif find src include -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.mm' \) -newer "$TARGET" 2>/dev/null | grep -q .; then
    BUILD_NEEDED=true
  fi

  if [ "$BUILD_NEEDED" = false ]; then
    section_time $SECTION_START
    echo ""
    echo "✅ Build up to date"
  else
    # Something needs building - run actual build
    # Use SKIP_COMPILE_COMMANDS=1 to avoid slow LSP re-indexing
    if make SKIP_COMPILE_COMMANDS=1 -j >/dev/null 2>&1; then
      section_time $SECTION_START
      echo ""
      echo "✅ Build successful"
    else
      section_time $SECTION_START
      echo ""
      echo "❌ Build failed - fix compilation errors before committing"
      echo "   Run 'make' to see full error output"
      EXIT_CODE=1
    fi
  fi
  echo ""
fi

# ====================================================================
# Icon Font Validation
# ====================================================================
SECTION_START=$(date +%s)
echo -n "🔤 Validating icon font codepoints..."

# Check if all icons in ui_icon_codepoints.h are present in compiled fonts
# This prevents the bug where icons are added to code but fonts aren't regenerated
if [ -f "scripts/validate_icon_fonts.sh" ]; then
  if ./scripts/validate_icon_fonts.sh 2>/dev/null; then
    section_time $SECTION_START
    echo ""
    echo "✅ All icon codepoints present in fonts"
  else
    section_time $SECTION_START
    echo ""
    echo "❌ Missing icon codepoints in fonts!"
    echo ""
    echo "   Some icons in include/ui_icon_codepoints.h are not in the compiled fonts."
    echo "   Run './scripts/regen_mdi_fonts.sh' to regenerate fonts, then rebuild."
    echo ""
    echo "   Or run './scripts/validate_icon_fonts.sh --fix' to auto-regenerate."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  validate_icon_fonts.sh not found - skipping icon validation"
fi

echo ""

# ====================================================================
# MDI Codepoint Label Verification
# ====================================================================
SECTION_START=$(date +%s)
echo -n "🔤 Verifying MDI codepoint labels..."

if [ -f "scripts/verify_mdi_codepoints.py" ]; then
  python3 scripts/verify_mdi_codepoints.py 2>/dev/null
  RESULT=$?
  section_time $SECTION_START
  echo ""
  if [ $RESULT -eq 0 ]; then
    echo "✅ All MDI codepoint labels verified"
  elif [ $RESULT -eq 1 ]; then
    echo "❌ MDI codepoint verification failed!"
    echo "   Some icon codepoints don't match their labels."
    echo "   Run: python3 scripts/verify_mdi_codepoints.py"
    EXIT_CODE=1
  elif [ $RESULT -eq 2 ]; then
    echo "⚠️  MDI metadata cache missing"
    echo "   Run: make update-mdi-cache"
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  verify_mdi_codepoints.py not found - skipping"
fi

echo ""

# ====================================================================
# Code Style Check
# ====================================================================
echo "🔍 Checking for TODO/FIXME markers..."

# Check for TODO/FIXME/XXX comments (informational only)
if [ -n "$FILES" ]; then
  if echo "$FILES" | xargs grep -n "TODO\|FIXME\|XXX" 2>/dev/null | head -20; then
    echo "ℹ️  Found TODO/FIXME markers (informational only)"
  else
    echo "✅ No TODO/FIXME markers found"
  fi
else
  echo "ℹ️  No source files to check"
fi

echo ""

# ====================================================================
# Memory Safety Audit (Critical Patterns Only)
# ====================================================================
if [ "$STAGED_ONLY" = true ]; then
  # Get all staged .cpp and .xml files for audit
  AUDIT_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|xml)$' || true)

  if [ -n "$AUDIT_FILES" ]; then
    echo "🛡️  Running memory safety audit on staged files..."

    if [ -f "scripts/audit_codebase.sh" ]; then
      # Run audit in file mode - only check critical patterns (errors fail, warnings pass)
      # shellcheck disable=SC2086
      if ./scripts/audit_codebase.sh --files $AUDIT_FILES 2>/dev/null; then
        echo "✅ Memory safety audit passed"
      else
        echo "❌ Memory safety audit found critical issues!"
        echo "   Run './scripts/audit_codebase.sh --files <files>' to see details"
        EXIT_CODE=1
      fi
    else
      echo "⚠️  audit_codebase.sh not found - skipping memory safety audit"
    fi
    echo ""
  fi
fi

# ====================================================================
# Subscription Null-Safety Check
# ====================================================================
# Background: Moonraker delivers JSON null for subscribed fields the underlying
# Klipper object lacks. .value() and .get<T>() throw type_error.302 on null;
# an uncaught throw inside a subscription handler exits 134 → watchdog crash
# loop (#filament_motion_sensor, fixed in f75b961d8).
#
# Baseline ratchets down as violations are fixed. New code adds to the count
# only via opt-out comment (`// JSON_NULL_SAFE: <reason>`).
SECTION_START=$(date +%s)
echo -n "🔒 Checking subscription null-safety..."

if [ -f "scripts/check_subscription_null_safety.py" ]; then
  # Baseline: 0 — every subscription-handler `.get<T>()` must have an
  # `.is_<type>()` guard within 15 lines, every `.value("k", default)` must
  # have an explicit `// JSON_NULL_SAFE` opt-out. Don't regress.
  if python3 scripts/check_subscription_null_safety.py --max-allowed 0 --summary >/tmp/null_safety.out 2>&1; then
    section_time $SECTION_START
    echo ""
    cat /tmp/null_safety.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/null_safety.out
    echo "   Run: python3 scripts/check_subscription_null_safety.py"
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_subscription_null_safety.py not found — skipping"
fi

echo ""

# ====================================================================
# L081 Mechanism C anti-pattern (cluster:pstat-async-delete)
# ====================================================================
# Background: bg-thread `tok.expired()` followed by `this->`/`api_->` member
# access races on owner destruction → UAF crashes that look unrelated in
# backtraces. The runtime detector emits `cluster:pstat-async-delete Mechanism C`
# warnings; this gate catches new instances at commit time.
#
# Scope: known bg-thread directories only (src/printer, src/calibration,
# src/led, src/print, src/system, src/sensors, src/api, src/network,
# src/bluetooth). src/ui/ is excluded — observer cbs there fire on main thread
# and would false-positive without AST-level lambda-context analysis.
SECTION_START=$(date +%s)
echo -n "🧵 Checking L081 bg-thread anti-pattern..."

if [ -f "scripts/check_l081_anti_pattern.py" ]; then
  if [ "$STAGED_ONLY" = true ]; then
    L081_ARGS="--staged-only"
  else
    L081_ARGS=""
  fi
  if python3 scripts/check_l081_anti_pattern.py $L081_ARGS >/tmp/l081_check.out 2>&1; then
    section_time $SECTION_START
    echo ""
    echo "✅ No L081 anti-pattern sites found"
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/l081_check.out
    echo "   Run: python3 scripts/check_l081_anti_pattern.py"
    echo "   See include/async_lifetime_guard.h for the canonical fix."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_l081_anti_pattern.py not found — skipping"
fi

echo ""

# ====================================================================
# Network PII: no SSID/BSSID/MAC logged above trace level
# ====================================================================
# Background: the in-memory log ring is captured at debug regardless of the
# user's configured verbosity, and leaves the machine three ways — the debug
# bundle, the crash reporter's automatic upload, and the `ctl log` RPC. A set
# of nearby SSIDs with signal strengths is a geolocation fingerprint, and a
# scan enumerates the neighbours' networks too. No downstream regex can catch
# an SSID, so the control has to be at the log call site (#1191).
SECTION_START=$(date +%s)
echo -n "🔒 Checking network PII in log calls..."

if [ -f "scripts/check_wifi_pii_logging.py" ]; then
  if [ "$STAGED_ONLY" = true ]; then
    PII_ARGS="--staged-only"
  else
    PII_ARGS=""
  fi
  if python3 scripts/check_wifi_pii_logging.py $PII_ARGS >/tmp/wifi_pii_check.out 2>&1; then
    section_time $SECTION_START
    echo ""
    echo "✅ No network identifiers logged above trace"
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/wifi_pii_check.out
    echo "   Run: python3 scripts/check_wifi_pii_logging.py"
    echo "   See include/log_redact.h for the redaction helpers."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_wifi_pii_logging.py not found — skipping"
fi

echo ""

# ====================================================================
# Declarative UI: no XML-owned widget driven imperatively from C++
# ====================================================================
SECTION_START=$(date +%s)
echo -n "🎨 Checking declarative UI (imperative XML-widget mutation)..."

if [ -f "scripts/check_imperative_ui.py" ]; then
  # Ratcheting baseline. These are XML widgets fetched with lv_obj_find_by_name()
  # and then mutated from C++ instead of bound to a subject. Some predate the gate
  # as deliberate pragmatism (the XML engine couldn't express it at the time), some
  # are plain mistakes — both are debt. The number may go DOWN (port a site, then
  # lower this baseline) but must never go up.
  if python3 scripts/check_imperative_ui.py --max-allowed 384 --summary >/tmp/imperative_ui.out 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 /tmp/imperative_ui.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/imperative_ui.out
    echo "   Run: python3 scripts/check_imperative_ui.py --list"
    echo "   Bind subjects in XML; see CLAUDE.md § CRITICAL RULES - Declarative UI."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_imperative_ui.py not found — skipping"
fi

echo ""

if [ -f "scripts/check_raw_this_queue_update.py" ]; then
  # The ratchet has reached zero (#1165) — every queue_update() in src/ now routes
  # through an AsyncLifetimeGuard, so this is a hard gate, not a baseline.
  # queue_update([this, ...]) runs at the next drain whether or not the owner is
  # still alive; if the body touches a member lv_subject_t, lv_subject_notify walks
  # a freed observer list (#1146, #1165). Keep it at 0: guard new sites with
  # lifetime_.bg_cb() / tok.defer(), or annotate a genuine exception with
  # // QUEUE_RAW_THIS_OK: <reason>.
  if python3 scripts/check_raw_this_queue_update.py --max-allowed 0 --summary >/tmp/raw_this_qu.out 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 /tmp/raw_this_qu.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/raw_this_qu.out
    echo "   Run: python3 scripts/check_raw_this_queue_update.py --list"
    echo "   Guard with lifetime_.bg_cb() / tok.defer(); see docs/devel/THREADING.md §2."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_raw_this_queue_update.py not found — skipping"
fi

echo ""

SECTION_START=$(date +%s)
echo -n "⏱️  Checking timer destructor cancels..."

if [ -f "scripts/check_timer_destructor_cancel.py" ]; then
  # Ratcheting baseline. A raw lv_timer_t* cancelled only in cleanup()/stop_*()
  # stays armed on any teardown that destroys the owner without that call, and
  # StaticPanelRegistry::destroy_all() runs BEFORE lv_deinit() — so the callback
  # fires into a freed `this` (#1173, twice: the wizard auto-probe timer and the
  # PID ETA tick). The check is transitive, so a destructor that reaches the
  # cancel through cleanup()/detach()/deinit_subjects() passes. Timers whose
  # callback is LifetimeToken-guarded or routed through a singleton accessor are
  # safe by another mechanism — annotate those `// TIMER_DTOR_OK: <reason>`.
  if python3 scripts/check_timer_destructor_cancel.py --max-allowed 3 >/tmp/timer_dtor.out 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 /tmp/timer_dtor.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/timer_dtor.out
    echo "   Run: python3 scripts/check_timer_destructor_cancel.py --list"
    echo "   Cancel from the destructor via lv_timer_cancel_safe(); see CLAUDE.md § Threading."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_timer_destructor_cancel.py not found — skipping"
fi

echo ""

SECTION_START=$(date +%s)
echo -n "🖼️  Checking guarded ThumbnailCache access..."

if [ -f "scripts/check_thumbnail_cache_guard.py" ]; then
  # Hard gate, never a baseline: src/ has no legacy call sites left. The two
  # unguarded overloads — fetch(api, path, ...) and get_if_cached(path, mtime) —
  # stay public only because tests exercise them deliberately, so the compiler
  # cannot enforce this. They take no ThumbnailLoadContext, which is what lets
  # fetch() drop a superseded on_success; without it an in-flight download that
  # has already been outdated still lands and overwrites a NEWER thumbnail.
  # Build a ThumbnailRequest + ThumbnailLoadContext, or annotate a genuine
  # exception with // THUMB_LEGACY_OK: <reason>.
  if python3 scripts/check_thumbnail_cache_guard.py >/tmp/thumb_guard.out 2>&1; then
    section_time $SECTION_START
    echo ""
    echo "✅ ThumbnailCache: every src/ consumer passes a ThumbnailLoadContext"
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/thumb_guard.out
    echo "   Run: python3 scripts/check_thumbnail_cache_guard.py"
    echo "   Use fetch(req, ctx, ...) / get_if_cached(req); see include/thumbnail_cache.h."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_thumbnail_cache_guard.py not found — skipping"
fi

echo ""

SECTION_START=$(date +%s)
echo -n "⏱️  Checking grid cell-metrics single source..."

if [ -f "scripts/check_grid_metrics_single_source.py" ]; then
  # Every drag/resize/preview/lattice path needs the same cols/rows/cell size,
  # and each independent computation is free to drift from the others on
  # gutter handling or int-vs-float rounding. GridEditMode::current_metrics()
  # is the one place allowed to ask GridLayout for the grid's dimensions; this
  # caps GridLayout::get_cols/get_rows/get_dimensions call sites at 2 (the pair
  # inside current_metrics() itself) so a new call site cannot grow a second copy.
  if python3 scripts/check_grid_metrics_single_source.py >/tmp/grid_metrics.out 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 /tmp/grid_metrics.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/grid_metrics.out
    echo "   Take a helix::CellMetrics from GridEditMode::current_metrics() instead."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_grid_metrics_single_source.py not found — skipping"
fi

echo ""

SECTION_START=$(date +%s)
echo -n "⏱️  Checking series_meta slot-vs-handle indexing..."

if [ -f "scripts/check_series_meta_indexing.py" ]; then
  # series_meta is SLOT-indexed; TempGraphHit::series_id is a monotonic handle
  # that is never reused. remove_series frees a slot without lowering
  # next_series_id, so after one remove-then-add the same number means two
  # different things: indexing with the handle renders the wrong series, and
  # past 16 cycles reads off the end of the array. Resolve with
  # find_meta_by_id() instead. This shipped once in temp_graph_tooltip_draw_cb
  # and was caught in review rather than by a test, because the only symptom is
  # drawn pixels and there is no draw-pass readback here.
  if python3 scripts/check_series_meta_indexing.py >/tmp/series_meta_indexing.out 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 /tmp/series_meta_indexing.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/series_meta_indexing.out
    echo "   Use helix::temp_graph_internal::find_meta_by_id(graph, id) instead."
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_series_meta_indexing.py not found — skipping"
fi

echo ""

# ====================================================================
# spdlog only: no printf/cout/cerr/LV_LOG_ outside CLI subcommands
# ====================================================================
SECTION_START=$(date +%s)
echo -n "📢 Checking spdlog-only logging..."

# stdout IS the product in these files (CLI subcommands, splash, demo, ctl client),
# so printing there is correct. Everywhere else, logging goes through spdlog.
LOG_ALLOW='src/system/cli_args.cpp|src/application/detect_printer_cmd.cpp|src/helix_splash.cpp|src/lvgl-demo/|src/remote/remote_client.cpp'
LOG_HITS=$(grep -rnE '\bprintf\(|std::cout|std::cerr|\bLV_LOG_[A-Z]+\(' src include 2>/dev/null \
             | grep -vE "$LOG_ALLOW" || true)
if [ -z "$LOG_HITS" ]; then
  section_time $SECTION_START
  echo ""
  echo "✅ spdlog-only: no stray printf/cout/LV_LOG_"
else
  section_time $SECTION_START
  echo ""
  echo "$LOG_HITS"
  echo "❌ Use spdlog::info/debug/warn/error instead (docs/devel/LOGGING.md)."
  echo "   stdout printing belongs only in CLI subcommands."
  EXIT_CODE=1
fi

echo ""

# ====================================================================
# Design tokens + no private LVGL APIs
# ====================================================================
SECTION_START=$(date +%s)
echo -n "🎨 Checking design tokens and LVGL API surface..."

TOKEN_EXIT=0

# Private LVGL internals. remote_control_server.cpp walks LVGL's XML subject
# linked list for `ctl list_subjects` — there is no public API for that.
PRIV=$(grep -rnoE '\b_lv_[a-z_]+\(' src include 2>/dev/null \
         | grep -v 'src/remote/remote_control_server.cpp' || true)
if [ -n "$PRIV" ]; then
  echo ""
  echo "$PRIV"
  echo "❌ Private LVGL API (_lv_*) — use the public API."
  TOKEN_EXIT=1
fi

# Hardcoded colors. Exempt: theme_manager (it parses hex into tokens, definitional),
# procedural canvas renderers, and helix-splash (a separate binary that does not
# link ThemeManager). Ratcheting baseline — port these to theme_manager_get_color().
HEX_ALLOW='theme_manager|src/rendering/|canvas|confetti|glyph|src/helix_splash.cpp'
HEX_BASELINE=34
HEX_COUNT=$(grep -rn 'lv_color_hex(0x' src include 2>/dev/null | grep -vcE "$HEX_ALLOW" || true)
if [ "$HEX_COUNT" -gt "$HEX_BASELINE" ]; then
  echo ""
  grep -rn 'lv_color_hex(0x' src include 2>/dev/null | grep -vE "$HEX_ALLOW" || true
  echo "❌ Hardcoded colors: $HEX_COUNT exceeds baseline ($HEX_BASELINE)."
  echo "   Use theme_manager_get_color(\"token\") or an XML design token."
  TOKEN_EXIT=1
fi

section_time $SECTION_START
if [ "$TOKEN_EXIT" -eq 0 ]; then
  echo ""
  if [ "$HEX_COUNT" -lt "$HEX_BASELINE" ]; then
    echo "✅ Design tokens: $HEX_COUNT hardcoded colors (baseline $HEX_BASELINE — ratchet down)"
  else
    echo "✅ Design tokens: $HEX_COUNT == baseline ($HEX_BASELINE), no private LVGL APIs"
  fi
else
  echo ""
  EXIT_CODE=1
fi

echo ""

# ====================================================================
# Agent-facing docs: references resolve, doc index is complete
# ====================================================================
SECTION_START=$(date +%s)
echo -n "📚 Checking doc references and index..."

if [ -f "scripts/check_doc_refs.py" ]; then
  if python3 scripts/check_doc_refs.py >/tmp/doc_refs.out 2>&1; then
    section_time $SECTION_START
    echo ""
    cat /tmp/doc_refs.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/doc_refs.out
    echo "   Run: python3 scripts/check_doc_refs.py"
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_doc_refs.py not found — skipping"
fi

echo ""

# ====================================================================
# Translation format-specifier parity (crash #1073)
# ====================================================================
# Background: format strings passed to snprintf/fmt::format via lv_tr() are
# runtime-translated. If a translation adds an extra %s/%d (or {} field), the
# format call reads an argument that was never passed → SIGSEGV (snprintf) or
# fmt::format_error (fmt). #1073 was the French '%d additional fan%s' translated
# with two %s, crashing the Controls panel for French users.
SECTION_START=$(date +%s)
echo -n "🌐 Checking translation format specifiers..."

TRANS_FMT_PY="${VENV_PYTHON:-python3}"
[ -x "$TRANS_FMT_PY" ] || TRANS_FMT_PY=python3
if [ -f "scripts/check_translation_format_specifiers.py" ]; then
  if "$TRANS_FMT_PY" scripts/check_translation_format_specifiers.py >/tmp/trans_fmt.out 2>&1; then
    section_time $SECTION_START
    echo ""
    echo "✅ All translated format strings preserve their source placeholders"
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/trans_fmt.out
    echo "   Run: $TRANS_FMT_PY scripts/check_translation_format_specifiers.py"
    echo "   Fix the offending translation in translations/<locale>.yml, then run: make translations"
    EXIT_CODE=1
  fi
else
  section_time $SECTION_START
  echo ""
  echo "⚠️  check_translation_format_specifiers.py not found — skipping"
fi

echo ""

# ====================================================================
# Shell Script Linting (shellcheck)
# ====================================================================
SECTION_START=$(date +%s)
echo -n "🐚 Checking shell scripts (shellcheck)..."

# Two trees, held to two different bars.
#
#   config/  - platform hooks and the init script. Clean at shellcheck's
#              default severity; kept there.
#   scripts/ - installer modules, launcher, release tooling. These ship to
#              devices but were outside this gate entirely until now, so 19
#              files carry pre-existing findings. Those are listed in
#              SHELLCHECK_BASELINE: still reported, but not fatal. Every other
#              file must be clean. The list may shrink, never grow.
#
# install.sh / uninstall.sh are skipped: they are bundled artifacts of
# install-dev.sh + lib/installer/, which are themselves checked here.
#
# Two codes are excluded for scripts/:
#   SC3043 - "local is undefined in POSIX sh". Deliberate - the installer and
#            launcher target BusyBox ash, which does implement local.
#   SC1091 - "not following sourced file". The installer sources its modules
#            by a path that only exists once unpacked on the device.
SHELLCHECK_SCRIPTS_EXCLUDE="SC3043,SC1091"
SHELLCHECK_BASELINE="scripts/audit_codebase.sh
scripts/benchmark_hosts.sh
scripts/benchmark_neon.sh
scripts/check_cjk_font_staleness.sh
scripts/git-stats.sh
scripts/install-dev.sh
scripts/lib/installer/common.sh
scripts/lib/installer/forgex.sh
scripts/lib/installer/main.sh
scripts/lib/installer/platform.sh
scripts/lib/installer/release.sh
scripts/lib/installer/requirements.sh
scripts/lib/installer/service.sh
scripts/lib/lvgl_image_lib.sh
scripts/quality-checks.sh
scripts/regen_images.sh
scripts/regen_printer_images.sh
scripts/resolve-backtrace.sh
scripts/screenshot.sh"

SHELL_FILES=""
if [ "$STAGED_ONLY" = true ]; then
  SHELL_FILES=$(git diff --cached --name-only --diff-filter=ACM | \
    grep -E '(config/platform/.*\.sh|config/helixscreen\.init|^scripts/.*\.sh)$' || true)
else
  SHELL_FILES=$(find config/platform -name "*.sh" 2>/dev/null || true)
  if [ -f "config/helixscreen.init" ]; then
    SHELL_FILES="$SHELL_FILES config/helixscreen.init"
  fi
  SHELL_FILES="$SHELL_FILES $(git ls-files 'scripts/*.sh' 'scripts/**/*.sh' 2>/dev/null || true)"
fi
# Drop the generated bundles regardless of how the list was built.
SHELL_FILES=$(printf '%s\n' $SHELL_FILES | \
  grep -vE '^scripts/(install|uninstall)\.sh$' || true)

if [ -n "$SHELL_FILES" ]; then
  if command -v shellcheck >/dev/null 2>&1; then
    SHELL_ERRORS=0
    SHELL_BASELINED=0
    SHELL_FAILED_FILES=""
    for script in $SHELL_FILES; do
      if [ -f "$script" ]; then
        # scripts/ is linted at warning severity minus the two excluded
        # codes; config/ keeps the stricter default.
        case "$script" in
          scripts/*) SC_FLAGS="-S warning -e $SHELLCHECK_SCRIPTS_EXCLUDE" ;;
          *)         SC_FLAGS="" ;;
        esac
        if ! shellcheck $SC_FLAGS "$script" 2>/dev/null; then
          if printf '%s\n' "$SHELLCHECK_BASELINE" | grep -Fxq "$script"; then
            SHELL_BASELINED=$((SHELL_BASELINED + 1))
          else
            SHELL_ERRORS=$((SHELL_ERRORS + 1))
            SHELL_FAILED_FILES="$SHELL_FAILED_FILES $script"
          fi
        fi
      fi
    done
    section_time $SECTION_START
    echo ""
    if [ $SHELL_ERRORS -eq 0 ]; then
      if [ $SHELL_BASELINED -gt 0 ]; then
        echo "✅ shellcheck clean ($SHELL_BASELINED baselined file(s) still dirty)"
      else
        echo "✅ All shell scripts pass shellcheck"
      fi
    else
      echo "❌ shellcheck found issues in $SHELL_ERRORS file(s)"
      for script in $SHELL_FAILED_FILES; do
        echo "   Run: shellcheck $script"
      done
      EXIT_CODE=1
    fi
  else
    section_time $SECTION_START
    echo ""
    echo "⚠️  shellcheck not found - skipping shell script linting"
    echo "   Install with: brew install shellcheck (macOS) or apt install shellcheck (Linux)"
  fi
else
  section_time $SECTION_START
  echo ""
  if [ "$STAGED_ONLY" = true ]; then
    echo "ℹ️  No shell scripts staged for commit"
  else
    echo "ℹ️  No shell scripts found"
  fi
fi

echo ""

# ====================================================================
# Final Result
# ====================================================================
SCRIPT_END=$(date +%s)
TOTAL_SEC=$((SCRIPT_END - SCRIPT_START))

if [ $EXIT_CODE -eq 0 ]; then
  echo "✅ Quality checks passed! (${TOTAL_SEC}s total)"
  exit 0
else
  echo "❌ Quality checks failed! (${TOTAL_SEC}s total)"
  exit 1
fi
