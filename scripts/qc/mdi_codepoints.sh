# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# MDI Codepoint Label Verification
# ====================================================================
qc_mdi_codepoints() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🔤 Verifying MDI codepoint labels..."

python3 scripts/verify_mdi_codepoints.py 2>/dev/null
RESULT=$?
section_time $SECTION_START
echo ""
if [ $RESULT -eq 0 ]; then
  echo "✅ All MDI codepoint labels verified"
elif [ $RESULT -eq 1 ]; then
  echo "❌ MDI codepoint verification failed!"
  echo "   Some icon codepoints don't match their labels."
  echo "   Run: python3 scripts/verify_mdi_codepoints.py"
  EXIT_CODE=1
elif [ $RESULT -eq 2 ]; then
  echo "⚠️  MDI metadata cache missing"
  echo "   Run: make update-mdi-cache"
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_mdi_codepoints="$QC_TRIGGER_ICONS"
