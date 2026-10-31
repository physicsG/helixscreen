# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# A hold handler that acts ends its press, so the release is not a tap
# ====================================================================
qc_hold_ends_press() {
  local EXIT_CODE=0
  local SECTION_START
  SECTION_START=$(date +%s)
  echo -n "✋ Checking hold handlers end their press..."

  if python3 scripts/check_hold_ends_press.py >"$QC_TMP/hold_ends_press.out" 2>&1; then
    section_time "$SECTION_START"
    echo ""
    echo "✅ Every hold handler ends its press or declines"
  else
    section_time "$SECTION_START"
    echo ""
    cat "$QC_TMP/hold_ends_press.out"
    EXIT_CODE=1
  fi

  echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_hold_ends_press='^src/.*\.cpp$|^ui_xml/.*\.xml$|^scripts/check_hold_ends_press\.py$'
