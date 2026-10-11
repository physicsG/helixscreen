# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Work markers: every one is a fix, an issue, or a justified annotation
# ====================================================================
qc_todo_markers() {
  local EXIT_CODE=0
echo -n "🔍 Checking work markers in comments..."

# Ratcheting baseline. Printed as informational output (and truncated at 20
# lines) the list was never read whole, and several entries were user-facing
# controls that did nothing (prestonbrown/helixscreen#1373). What remains is
# accounted for: each cites the issue that owns it as `(#NNNN)` with the
# constraint on the same line, or sits in a file another change is rewriting.
# The number may go DOWN (fix one, then lower this baseline) but must never
# go up. Always whole-tree: a marker is a marker whichever commit adds it.
if python3 scripts/check_todo_markers.py --max-allowed 8 --summary >/tmp/todo_markers.out 2>&1; then
  echo ""
  tail -1 /tmp/todo_markers.out
else
  echo ""
  cat /tmp/todo_markers.out
  echo "   Run: python3 scripts/check_todo_markers.py --list"
  echo "   Fix it, or file the issue and cite it: // MARKER(#NNNN): <the constraint>"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_todo_markers='\.(cpp|c|h|hpp|mm|sh)$|check_todo_markers\.py$'
