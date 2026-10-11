# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Patch Drift
# ====================================================================
# mk/patches.mk guards every apply with "is this file already dirty?", never
# with "is it dirty with the CURRENT revision of this patch". So the first
# revision to reach a checkout is the one that stays: editing a patch afterwards
# does nothing for anyone who already carries the old hunks. A patch whose
# hunks land only in device-only source (e.g. lv_evdev.c, compiled out of
# desktop builds) can drift from main's checked-in submodule pin without any
# device cross-build noticing until it fails - `make test` skips patch
# application entirely, so nothing here could see it. Which is exactly why
# this one runs on desktop.
qc_patch_drift() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🩹 Checking patch drift..."

if [ -n "${HELIX_QC_SKIP_PATCH_DRIFT:-}" ]; then
  # Set by a caller that runs this sweep somewhere lib/ is borrowed rather than
  # owned, where the answer would describe the lending tree's branch. That caller
  # asks the question again where it is answerable.
  section_time $SECTION_START
  echo ""
  echo "⏭️  patch drift: deferred to the tree that owns lib/"
else
  if python3 scripts/check_patch_drift.py >/tmp/patch_drift.out 2>&1; then
    section_time $SECTION_START
    echo ""
    cat /tmp/patch_drift.out
  else
    section_time $SECTION_START
    echo ""
    cat /tmp/patch_drift.out
    EXIT_CODE=1
  fi
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_patch_drift='^patches/|mk/patches\.mk|check_patch_drift\.py'
