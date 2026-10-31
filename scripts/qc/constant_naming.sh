# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Constant naming: kCamelCase constants, UPPER_SNAKE only for macros
# ====================================================================
# Reads added lines only: the pre-commit run checks the index, the full run the
# branch's committed work since its fork point. A macro ignores scope, so a
# constant spelled like one is silently rewritten by any same-named #define.
qc_constant_naming() {
  local EXIT_CODE=0 out="$QC_TMP/constant_naming.out" mode="--base auto"
  SECTION_START=$(date +%s)
  echo -n "🔤 Checking constant naming (no new UPPER_SNAKE constants)..."
  [ "$STAGED_ONLY" = true ] && mode="--staged"
  # shellcheck disable=SC2086
  if python3 scripts/check_constant_naming.py $mode >"$out" 2>&1; then
    section_time $SECTION_START
    echo ""
    tail -1 "$out"
  else
    section_time $SECTION_START
    echo ""
    cat "$out"
    echo "   Rename to kCamelCase (scripts/rename_constants.py --apply --files <path>)."
    EXIT_CODE=1
  fi
  echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_constant_naming="$QC_TRIGGER_NATIVE_SRC|check_constant_naming\.py$|rename_constants\.py$"
