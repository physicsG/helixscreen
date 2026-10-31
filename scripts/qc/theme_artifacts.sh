# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Generated theme artefacts: token table and [theme_pins] golden
# ====================================================================
# src/generated/theme_token_table.cpp and tests/fixtures/theme_const_golden.txt
# derive from the top-level ui_xml/*.xml tokens; a stale one otherwise fails
# only in the unit sweep. ~0.1s. schema.json, the third artefact, is
# qc_xml_linter's.
qc_theme_artifacts() {
  local EXIT_CODE=0
  local mode=""
  [ "$STAGED_ONLY" = true ] && mode="--staged-only"
  # shellcheck disable=SC2086
  if python3 scripts/check_theme_artifacts.py $mode >"$QC_TMP/theme_artifacts.out" 2>&1; then
    qc_count "$(cat "$QC_TMP/theme_artifacts.out")"
  else
    cat "$QC_TMP/theme_artifacts.out"
    EXIT_CODE=1
  fi
  echo ""
  return $EXIT_CODE
}

QC_TRIGGER_qc_theme_artifacts='^ui_xml/[^/]+\.xml$|^src/generated/theme_token_table\.cpp$|^tests/fixtures/theme_const_golden\.txt$|^scripts/(gen_theme_tokens|check_theme_artifacts)\.py$'
