# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Hand-rolled colour swatches (ratchet)
# ====================================================================
qc_hand_rolled_swatch() {
  local EXIT_CODE=0
  local SECTION_START
  SECTION_START=$(date +%s)
  echo -n "🎨 Checking for hand-rolled colour swatches..."

  local SWATCH_ARGS=""
  [ "$STAGED_ONLY" = true ] && SWATCH_ARGS="--staged-only"
  if python3 scripts/check_hand_rolled_swatch.py $SWATCH_ARGS >"$QC_TMP/hand_rolled_swatch.out" 2>&1; then
    section_time "$SECTION_START"
    echo ""
    echo "✅ No new hand-rolled swatches"
  else
    section_time "$SECTION_START"
    echo ""
    cat "$QC_TMP/hand_rolled_swatch.out"
    echo "   Run: python3 scripts/check_hand_rolled_swatch.py --list"
    echo "   See docs/devel/COLOR_SWATCHES.md."
    EXIT_CODE=1
  fi

  echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_hand_rolled_swatch='^(src|include)/.*\.(cpp|h)$|^ui_xml/.*\.xml$|^scripts/(check_hand_rolled_swatch\.py|hand_rolled_swatch_baseline\.txt)$'
