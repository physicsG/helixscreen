# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

qc_workflow_submodules() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🧱 Checking workflow submodule gates..."

if python3 scripts/check_workflow_submodules.py >/tmp/workflow_submodules.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/workflow_submodules.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/workflow_submodules.out
  EXIT_CODE=1
fi

echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_workflow_submodules='^\.github/workflows/|^\.github/actions/|check_workflow_submodules\.py$'
