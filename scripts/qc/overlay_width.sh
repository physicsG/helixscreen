# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Overlay width is decided at push time, never in XML (#1178)
# ====================================================================
qc_overlay_width() {
  local EXIT_CODE=0
# The two width constants encode destination-vs-transient-layer, and which one
# an overlay gets depends on how the user reached it — the same
# fan_control_overlay is a transient layer from Controls and a drill-down from
# Settings > Fans. Hand-picking a constant in XML is what left 20 panels gapped
# and 36 full with no rule, and made console_settings_overlay render wider than
# the console_panel it was pushed from.
echo "📐 Checking overlay width declarations..."

if [ "$STAGED_ONLY" = true ]; then
  OVERLAY_WIDTH_ARGS="--staged-only"
else
  OVERLAY_WIDTH_ARGS=""
fi
# shellcheck disable=SC2086
if python3 scripts/check_overlay_width.py $OVERLAY_WIDTH_ARGS >/tmp/overlay_width.out 2>&1; then
  echo "✅ No hand-picked overlay widths"
else
  cat /tmp/overlay_width.out
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_overlay_width='\.xml$|\.(cpp|h)$'
