# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Duplicate XML widget names
# ====================================================================
qc_dup_names() {
  local EXIT_CODE=0
# Background: lv_obj_find_by_name() returns the FIRST depth-first match and
# warns about nothing, so a name declared twice in one file makes the second
# element unreachable — built, then silently never configured (#1136,
# ams_panel.xml name="endless_arrows" twice).
#
# Per-name ratcheting baseline lives in the script (DUPLICATE_NAME_BASELINE).
# The pre-existing entries are settings/about rows whose label/value names are
# only ever looked up with the ROW as search parent.
echo "🏷️  Checking for duplicate XML widget names..."

if [ "$STAGED_ONLY" = true ]; then
  DUP_NAME_ARGS="--staged-only"
else
  DUP_NAME_ARGS=""
fi
# shellcheck disable=SC2086
if python3 scripts/check_duplicate_xml_names.py $DUP_NAME_ARGS --summary >/tmp/duplicate_xml_names.out 2>&1; then
  cat /tmp/duplicate_xml_names.out
else
  cat /tmp/duplicate_xml_names.out
  echo "   Run: python3 scripts/check_duplicate_xml_names.py --list"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_dup_names="$QC_TRIGGER_XML"
