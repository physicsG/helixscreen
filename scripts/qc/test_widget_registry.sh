# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# No production XML widget name may be served by test code
# ====================================================================
qc_test_widget_registry() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🧩 Checking test widget registry..."

if python3 scripts/check_test_widget_registry.py >/tmp/test_widget_registry.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/test_widget_registry.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/test_widget_registry.out
  echo "   Run: python3 scripts/check_test_widget_registry.py"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_test_widget_registry='^tests/|^src/|^scripts/check_test_widget_registry\.py$'
