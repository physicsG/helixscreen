# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Translation format-specifier parity (crash #1073)
# ====================================================================
qc_translation_fmt() {
  local EXIT_CODE=0
# Background: format strings passed to snprintf/fmt::format via lv_tr() are
# runtime-translated. If a translation adds an extra %s/%d (or {} field), the
# format call reads an argument that was never passed → SIGSEGV (snprintf) or
# fmt::format_error (fmt). #1073 was the French '%d additional fan%s' translated
# with two %s, crashing the Controls panel for French users.
SECTION_START=$(date +%s)
echo -n "🌐 Checking translation format specifiers..."

TRANS_FMT_PY="${VENV_PYTHON:-python3}"
[ -x "$TRANS_FMT_PY" ] || TRANS_FMT_PY=python3
if "$TRANS_FMT_PY" scripts/check_translation_format_specifiers.py >/tmp/trans_fmt.out 2>&1; then
  section_time $SECTION_START
  echo ""
  echo "✅ All translated format strings preserve their source placeholders"
else
  section_time $SECTION_START
  echo ""
  cat /tmp/trans_fmt.out
  echo "   Run: $TRANS_FMT_PY scripts/check_translation_format_specifiers.py"
  echo "   Fix the offending translation in translations/<locale>.yml, then run: make translations"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_translation_fmt='^translations/|^ui_xml/|\.py$'
