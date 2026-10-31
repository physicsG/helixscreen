# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

qc_feature_gates() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🚩 Checking HELIX_HAS_* feature gate defaults..."

# An undefined gate in `#if` is 0, so a build that forgets one compiles the feature
# out silently. include/helix_features.h holds every default; this keeps it the one place.
if python3 scripts/check_feature_gates.py >"$QC_TMP/feature_gates.out" 2>&1; then
  section_time $SECTION_START
  echo ""
  head -1 "$QC_TMP/feature_gates.out"
else
  section_time $SECTION_START
  echo ""
  cat "$QC_TMP/feature_gates.out"
  EXIT_CODE=1
fi

echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_feature_gates='^(src|include|firmware/helixscreen-esp32/components)/.*\.(c|cc|cpp|h|hpp|mm)$|^Makefile$|^mk/.*\.mk$|^android/app/jni/CMakeLists\.txt$|^firmware/helixscreen-esp32/components/helixapp/CMakeLists\.txt$|^lv_conf\.h$|^scripts/check_feature_gates\.py$'
