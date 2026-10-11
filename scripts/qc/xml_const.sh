# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# XML Constant Set Validation
# ====================================================================
qc_xml_const() {
  local EXIT_CODE=0
echo "🔤 XML constant set gate..."

# Nothing runs here. Incomplete responsive and light/dark sets fail the unit
# test "ui_xml has no incomplete constant sets" ([ui_theme][validation]), and
# undefined #constant references fail qc_xml_linter (unknown-const-ref), so
# neither needs the app-linking validator binary in the hook
# (prestonbrown/helixscreen#1698).
echo "ℹ️  XML constant sets are enforced by the unit suite; undefined #refs by the XML linter (prestonbrown/helixscreen#1698)"

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_xml_const="$QC_TRIGGER_XML"
