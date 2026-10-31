# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# ESP32 compile check of the staged firmware units
# ====================================================================
# Host-green, ESP32-red code (int32_t is long on Xtensa, -fno-exceptions)
# otherwise surfaces at pre-push or in esp32-build CI. Scoped to the staged
# sources the firmware compiles and one includer per staged header; about 6s
# for a couple of units, nothing at all when no staged file is firmware code.
# Advisory skip without docker, the IDF image or cached firmware flags.
qc_esp32_compile() {
  local EXIT_CODE=0
  [ "$STAGED_ONLY" = true ] || return 0
  if ! python3 scripts/esp32_syntax_check.py --staged >"$QC_TMP/esp32_compile.out" 2>&1; then
    EXIT_CODE=1
  fi
  if [ -s "$QC_TMP/esp32_compile.out" ]; then
    cat "$QC_TMP/esp32_compile.out"
    echo ""
  fi
  return $EXIT_CODE
}

QC_TRIGGER_qc_esp32_compile='^(src|include|firmware/helixscreen-esp32)/.*\.(c|cpp|h|hpp)$'
