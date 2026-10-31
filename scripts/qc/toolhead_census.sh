# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Toolhead-safety census (the [job_holds_machine] classification test)
# ====================================================================
# A ui_xml file that declares a control must be classified in
# tests/unit/test_job_holds_machine.cpp. The test only runs in the unit sweep;
# this answers the same question from the same tables in ~0.2s.
qc_toolhead_census() {
  local EXIT_CODE=0
  local mode=""
  [ "$STAGED_ONLY" = true ] && mode="--staged-only"
  # shellcheck disable=SC2086
  if python3 scripts/check_toolhead_census.py $mode >"$QC_TMP/toolhead_census.out" 2>&1; then
    qc_count "$(cat "$QC_TMP/toolhead_census.out")"
  else
    cat "$QC_TMP/toolhead_census.out"
    EXIT_CODE=1
  fi
  echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_toolhead_census='^ui_xml/.*\.xml$|^tests/unit/test_job_holds_machine\.cpp$|^scripts/check_toolhead_census\.py$'
