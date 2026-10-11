# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Helpers every gate in scripts/qc/ may call. Sourced by scripts/quality-checks.sh
# before any gate, and by a bats test before it sources the one gate it drives.
# shellcheck disable=SC2034,SC2154

# Timing helper - prints elapsed time for a section (seconds)
section_time() {
  local start=$1
  local end
  end=$(date +%s)
  local elapsed=$((end - start))
  if [ $elapsed -gt 0 ]; then
    printf " (%ds)" "$elapsed"
  fi
}

# The -j for a make a gate runs. A commit hook runs on every session's commit,
# so an unbounded -j would be N unbounded builds at once: the number comes
# from `helix-claim jobs` (the jobpool when one is live, else the cores capped
# by memory). With the jobpool shim in front of make the -j is stripped and
# the pool decides. 6 is the answer for a tree without the script, and
# HELIX_QC_JOBS overrides both.
qc_build_jobs() {
  printf '%s\n' "${HELIX_QC_JOBS:-$(scripts/helix-claim jobs 2>/dev/null || echo 6)}"
}

# The Makefile requires GNU make >= 4.2 and errors below it. macOS ships 3.81
# as /usr/bin/make and Homebrew installs 4.x only as `gmake`. On Linux plain
# `make` stays: there it may be the jobpool shim, which `gmake` would bypass.
qc_make() {
  if [ "$(uname -s)" = Darwin ] && command -v gmake >/dev/null 2>&1; then
    echo gmake
  else
    echo make
  fi
}

# A verdict that carries a count is recorded in $QC_COUNTS as well as printed,
# so a cached pass can replay what the full run examined.
qc_note() { printf '%s\n' "$1" >> "$QC_COUNTS" 2>/dev/null || true; }
qc_count() { echo "$1"; qc_note "$1"; }

# Path triggers more than one gate shares (see QC_TRIGGER_<gate> in each file).
QC_TRIGGER_XML='\.xml$|^src/ui/|^tools/xml-linter/'
QC_TRIGGER_NATIVE_SRC='\.(cpp|c|h|mm)$'
QC_TRIGGER_ICONS='\.xml$|icon|font'
