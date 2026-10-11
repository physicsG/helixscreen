# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Installer Step Reachability
# ====================================================================
# The installer is a set of modules wired together by exactly one orchestrator,
# main(). A step that is written, tested, and never wired in is silent: the
# shell defines the function, never calls it, and exits 0. That is #1343 --
# install_permission_rules() shipped with 34 passing tests and no call site, so
# the backlight udev rule was never written and dimming/sleep failed on every
# non-root install. The bats suites call these functions directly, which is why
# a green suite proved nothing about whether they run.
qc_installer_reachability() {
  local EXIT_CODE=0
SECTION_START=$(date +%s)
echo -n "🔌 Checking installer step reachability..."

if python3 scripts/check_installer_step_reachability.py >/tmp/installer_reachability.out 2>&1; then
  section_time $SECTION_START
  echo ""
  cat /tmp/installer_reachability.out
else
  section_time $SECTION_START
  echo ""
  cat /tmp/installer_reachability.out
  EXIT_CODE=1
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_installer_reachability='^scripts/lib/installer/|^scripts/install-dev\.sh$|^scripts/bundle-(un)?installer\.sh$|^scripts/check_installer_step_reachability\.py$'
