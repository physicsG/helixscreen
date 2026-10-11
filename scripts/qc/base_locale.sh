# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Base-locale key identity (raw-key rendering in English UI)
# ====================================================================
qc_base_locale() {
  local EXIT_CODE=0
# English loads no translation pack (see src/system/translation_loader.cpp),
# so lv_tr() returns the key itself — a key that is not its own English text
# renders the raw key in the UI (v0.99.114: "pre_print_option.timelapse.label"
# on the timelapse toggle row, raw tour.step.* strings across the first-run
# tour). Checked against translations/en.yml, not the generated XML, so it
# fires even when `make translations` fell back to stale artifacts.
SECTION_START=$(date +%s)
echo -n "🌐 Checking base-locale key identity..."

if "$TRANS_FMT_PY" scripts/check_translation_identity.py >/tmp/trans_ident.out 2>&1; then
  section_time $SECTION_START
  echo ""
  echo "✅ All English translation keys are their own text"
else
  section_time $SECTION_START
  echo ""
  cat /tmp/trans_ident.out
  echo "   Fix: rename the key to its English text in ALL translations/*.yml"
  echo "   and at the C++/XML/JSON reference site, then: make translations"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_base_locale='^translations/'
