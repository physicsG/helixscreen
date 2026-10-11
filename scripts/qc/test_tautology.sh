# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Assertions must be able to fail
# ====================================================================
qc_test_tautology() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🎯 Checking for assertions that cannot fail..."

# Ratchet, read from mk/tests.mk for the reason above. All findings are a
# set_X(literal) round-trip through an accessor pair that only stores and
# loads. May fall, never rise.
TAUTOLOGY_MAX=$(sed -n 's/^TAUTOLOGY_MAX ?= *\([0-9][0-9]*\).*/\1/p' mk/tests.mk | head -1)
if python3 scripts/check_test_tautology.py --summary --max-allowed "${TAUTOLOGY_MAX:-0}" >/tmp/test_tautology.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/test_tautology.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/test_tautology.out
  echo "   Run: python3 scripts/check_test_tautology.py --list"
  EXIT_CODE=1
fi

echo ""

return $EXIT_CODE
}

QC_TRIGGER_qc_test_tautology='^tests/|^include/|^src/|^scripts/check_test_tautology\.py$'
