# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Plugin i18n packs must not shadow system strings
# ====================================================================
qc_plugin_i18n_shadow() {
  local EXIT_CODE=0
# A pack is consulted before the app catalog, so a pack tag that equals a
# catalog key overrides the system translation for the whole UI while the
# plugin is loaded - not just inside the plugin. This gate keeps example and
# fixture packs to strings the app does not already say.
SECTION_START=$(date +%s)
echo -n "🌐 Checking plugin packs for system-string shadows..."

if python3 scripts/check_plugin_i18n_shadow.py >/tmp/plugin_i18n_shadow.out 2>&1; then
  section_time $SECTION_START
  echo ""
  echo "✅ No plugin translation shadows a system string"
else
  section_time $SECTION_START
  echo ""
  cat /tmp/plugin_i18n_shadow.out
  echo "   Fix: remove the shadowing entries from the pack; the catalog"
  echo "   already translates them"
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_plugin_i18n_shadow='^examples/plugins/.*/i18n/|^tests/fixtures/plugins/.*/i18n/|^translations/'
