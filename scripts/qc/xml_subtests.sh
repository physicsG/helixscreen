# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# helix-xml submodule test suite (CMake + Unity + ctest)
# ====================================================================
qc_xml_subtests() {
  local EXIT_CODE=0
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

  return $EXIT_CODE
}

QC_TRIGGER_qc_xml_subtests="$QC_TRIGGER_XML"
