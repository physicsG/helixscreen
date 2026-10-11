# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# AmsState publishes its XML subject names twice: init_subjects() on first init
# and register_xml_subject_names() when a later init re-enters. A name missing
# from the second stays unpublished after a register_xml=false first init, and
# nothing else notices (prestonbrown/helixscreen#1439).
qc_ams_xml_mirror() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🪞 Checking the AmsState XML-name mirror..."

if python3 scripts/check_ams_xml_mirror.py >/tmp/ams_xml_mirror.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/ams_xml_mirror.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/ams_xml_mirror.out
  echo "   Run: python3 scripts/check_ams_xml_mirror.py"
  EXIT_CODE=1
fi

echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_ams_xml_mirror='^src/printer/ams_state\.cpp$|^include/state/subject_macros\.h$|check_ams_xml_mirror\.py$'
