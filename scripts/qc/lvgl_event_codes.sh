# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Crash-worker LVGL event-code table is generated, not hand-typed
# ====================================================================
qc_lvgl_event_codes() {
  local EXIT_CODE=0
# The worker labels every auto-filed crash issue with "code=N (NAME)", and that
# name is often the entire diagnosis. The table was maintained by hand until
# LVGL 9.5 inserted four codes mid-enum; 58 of 63 entries went stale and a
# DELETE crash was filed as SCREEN_UNLOAD_START, pointing triage away from the
# teardown bug. lv_event_code_t is the source of truth now, the table is derived
# from it, and this proves the committed artifact still matches.
SECTION_START=$(date +%s)
echo -n "🩺 Checking crash-worker LVGL event codes..."

if python3 scripts/gen_lvgl_event_codes.py --diff >/tmp/lvgl_event_codes.out 2>&1; then
  :
else
  EXIT_CODE=1
  # --auto-fix repairs the working tree but still fails: the repair lands
  # in the tree, not the index, and passing here would commit the stale
  # table behind a green run.
  if [ "$AUTO_FIX" = true ]; then
    python3 scripts/gen_lvgl_event_codes.py >>/tmp/lvgl_event_codes.out 2>&1
    echo "   Regenerated in place — 'git add' the worker and commit again." >>/tmp/lvgl_event_codes.out
  fi
fi
section_time $SECTION_START
echo ""
cat /tmp/lvgl_event_codes.out

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_lvgl_event_codes='^server/crash-worker/|^scripts/gen_lvgl_event_codes\.py$|^lib/lvgl$|^lv_conf\.h$'
