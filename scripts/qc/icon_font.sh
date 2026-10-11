# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Icon Font Validation
# ====================================================================
qc_icon_font() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🔤 Validating icon font codepoints..."

# Check if all icons in ui_icon_codepoints.h are present in compiled fonts
# This prevents the bug where icons are added to code but fonts aren't regenerated
if ./scripts/validate_icon_fonts.sh 2>/dev/null; then
  section_time $SECTION_START
  echo ""
  echo "✅ All icon codepoints present in fonts"
else
  section_time $SECTION_START
  echo ""
  echo "❌ Missing icon codepoints in fonts!"
  echo ""
  echo "   Some icons in include/ui_icon_codepoints.h are not in the compiled fonts."
  echo "   Run './scripts/regen_mdi_fonts.sh' to regenerate fonts, then rebuild."
  echo ""
  echo "   Or run './scripts/validate_icon_fonts.sh --fix' to auto-regenerate."
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_icon_font="$QC_TRIGGER_ICONS"
