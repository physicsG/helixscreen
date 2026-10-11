# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# Memory Safety Audit (Critical Patterns Only)
# ====================================================================
qc_mem_safety() {
  local EXIT_CODE=0
if [ "$STAGED_ONLY" = true ]; then
  # Get all staged .cpp and .xml files for audit. ACMRT admits a rename: a
  # `git mv` plus an edit reports as R (destination path only), which plain
  # ACM silently drops - the audit would then never see the edited content.
  #
  # Built NUL-delimited (-z) into an array, one path per element, rather than
  # a plain `git diff | grep` string later expanded unquoted: a space in a
  # staged path is not C-quoted by `--name-only`, so an unquoted expansion
  # word-splits it into bogus tokens that resolve to nothing and the audit
  # silently never sees it.
  AUDIT_FILES=()
  while IFS= read -r -d '' af_f; do
    case "$af_f" in
      *.cpp|*.xml) AUDIT_FILES+=("$af_f") ;;
    esac
  done < <(git diff --cached --name-only -z --diff-filter=ACMRT)

  if [ ${#AUDIT_FILES[@]} -gt 0 ]; then
    echo "🛡️  Running memory safety audit on staged files..."

    # Run audit in file mode - only check critical patterns (errors fail, warnings pass)
    if ./scripts/audit_codebase.sh --files "${AUDIT_FILES[@]}" 2>/dev/null; then
      echo "✅ Memory safety audit passed"
    else
      echo "❌ Memory safety audit found critical issues!"
      echo "   Run './scripts/audit_codebase.sh --files <files>' to see details"
      EXIT_CODE=1
    fi
    echo ""
  fi
fi

  return $EXIT_CODE
}

QC_TRIGGER_qc_mem_safety="$QC_TRIGGER_NATIVE_SRC"
