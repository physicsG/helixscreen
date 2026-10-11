# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Every icon name in ui_xml/ resolves to a registered codepoint
# ====================================================================
qc_icon_names() {
  local EXIT_CODE=0
# `<icon src="NAME">` resolves NAME at runtime and substitutes
# image_broken_variant on a miss, with one log warning and no build error, so a
# typo ships as a broken glyph the user reads as intentional. Absent from
# qc_trigger_re on purpose: deleting a codepoint from the header breaks XML that
# is nowhere near the diff, so this cannot be gated on staged .xml files.
echo "🖼️  Checking icon names resolve to codepoints..."
if python3 scripts/check_icon_names.py --summary >/tmp/icon_names.out 2>&1; then
  tail -1 /tmp/icon_names.out
else
  cat /tmp/icon_names.out
  echo "   Run: python3 scripts/check_icon_names.py --list"
  EXIT_CODE=1
fi
  return $EXIT_CODE
}
