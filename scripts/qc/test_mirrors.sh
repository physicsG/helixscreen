# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Tests must exercise shipped code, not a copy of it
# ====================================================================
qc_test_mirrors() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🪞 Checking for mirror tests..."

# Ratchet, not a clean-tree assertion. Signals 1 and 2 (shadow-include,
# mirror-comment) are at 0 and must stay there. Signals 3 (redefined-symbol)
# and 4 (stub-logic) carry pre-existing findings, each with its own ceiling;
# each may fall, never rise.
#
# Read from mk/tests.mk rather than repeated here. A second hand-written copy
# of the same threshold is how it goes stale: main rewrote
# test_update_checker.cpp, the real count fell 18 -> 17, and a duplicated
# constant would have kept passing at 18 with a regression's worth of slack.
MIRROR_MAX_REDEFINED=$(sed -n 's/^MIRROR_MAX_REDEFINED_SYMBOL ?= *\([0-9][0-9]*\).*/\1/p' mk/tests.mk | head -1)
MIRROR_MAX_STUB=$(sed -n 's/^MIRROR_MAX_STUB_LOGIC ?= *\([0-9][0-9]*\).*/\1/p' mk/tests.mk | head -1)
if python3 scripts/check_test_mirrors.py --summary \
     --max "redefined-symbol=${MIRROR_MAX_REDEFINED:-0}" \
     --max "stub-logic=${MIRROR_MAX_STUB:-0}" >/tmp/test_mirrors.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/test_mirrors.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/test_mirrors.out
  echo "   Run: python3 scripts/check_test_mirrors.py --list"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_test_mirrors='^tests/|^mk/tests\.mk$|^scripts/check_test_mirrors\.py$'
