# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

qc_bats_inert() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🫥 Checking bats assertions bash 3.2 swallows..."

# Pre-commit: scan the staged blob for each .bats file, not the dirty
# working tree - a violation staged and then reverted on disk must still
# fail. CI and manual runs use the whole-working-tree scan (no flag).
if [ "$STAGED_ONLY" = true ]; then
  BATS_INERT_ARGS="--staged-only"
else
  BATS_INERT_ARGS=""
fi
# shellcheck disable=SC2086
if python3 scripts/check_bats_inert_assertions.py $BATS_INERT_ARGS >/tmp/bats_inert.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/bats_inert.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/bats_inert.out
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_bats_inert='\.bats$|^tests/shell/helpers\.bash$|check_bats_inert_assertions\.py$'
