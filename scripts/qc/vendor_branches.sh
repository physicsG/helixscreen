# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Vendor branches in generic code (ratchet)
# ====================================================================
qc_vendor_branches() {
  local EXIT_CODE=0
  local SECTION_START
  SECTION_START=$(date +%s)
  echo -n "🏭 Checking vendor branches in generic code..."

  local VENDOR_ARGS=""
  [ "$STAGED_ONLY" = true ] && VENDOR_ARGS="--staged-only"
  if python3 scripts/check_vendor_branches.py $VENDOR_ARGS >"$QC_TMP/vendor_branches.out" 2>&1; then
    section_time "$SECTION_START"
    echo ""
    echo "✅ No new vendor branches"
  else
    section_time "$SECTION_START"
    echo ""
    cat "$QC_TMP/vendor_branches.out"
    echo "   Run: python3 scripts/check_vendor_branches.py --list"
    echo "   See .claude/rules/vendor-abstraction.md."
    EXIT_CODE=1
  fi

  echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_vendor_branches='^(src|include)/.*\.(cpp|h)$|^scripts/(check_vendor_branches\.py|vendor_branch_baseline\.txt)$'
