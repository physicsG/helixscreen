# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Subscription Null-Safety Check
# ====================================================================
qc_null_safety() {
  local EXIT_CODE=0
# Background: Moonraker delivers JSON null for subscribed fields the underlying
# Klipper object lacks. .value() and .get<T>() throw type_error.302 on null;
# an uncaught throw inside a subscription handler exits 134 → watchdog crash
# loop (e.g. a null #filament_motion_sensor field).
#
# Baseline ratchets down as violations are fixed. New code adds to the count
# only via opt-out comment (`// JSON_NULL_SAFE: <reason>`).
SECTION_START=$(date +%s)
echo -n "🔒 Checking subscription null-safety..."

# Baseline: 0 — every subscription-handler `.get<T>()` must have an
# `.is_<type>()` guard within 15 lines, every `.value("k", default)` must
# have an explicit `// JSON_NULL_SAFE` opt-out. Don't regress.
#
# Pre-commit: scan the staged blob for each changed source, not the dirty
# working tree. Rule 1's baseline is 0, so a partial (staged-file) scan is
# still a sound check of what the commit will contain; CI and manual runs
# use the whole-working-tree scan (no flag), which is what the rule-2
# per-key baseline needs to detect a now-fixed entry.
if [ "$STAGED_ONLY" = true ]; then
  NULL_SAFETY_ARGS="--staged-only"
else
  NULL_SAFETY_ARGS=""
fi
# shellcheck disable=SC2086
if python3 scripts/check_subscription_null_safety.py $NULL_SAFETY_ARGS --max-allowed 0 --summary >/tmp/null_safety.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/null_safety.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/null_safety.out
  echo "   Run: python3 scripts/check_subscription_null_safety.py"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_null_safety="$QC_TRIGGER_NATIVE_SRC"
