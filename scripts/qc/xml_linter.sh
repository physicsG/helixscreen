# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# ====================================================================
# helix-xml-linter (mirrors the "XML Lint" CI gate)
# ====================================================================
qc_xml_linter() {
  local EXIT_CODE=0
# The linter resolves every #const reference against a committed snapshot,
# tools/xml-linter/schema/schema.json. Adding a <px>/<color>/<string> to an XML
# file without regenerating that snapshot leaves the new name unknown, so every
# reference to it is an unknown-const-ref on CI while the commit sails through
# locally — three times now (#1204 most recently).
#
# Kept cheap (~0.7s) three ways, because this runs on every commit:
#   - Triggers on staged paths for the XML half (ui_xml/, tools/xml-linter/)
#     and on staged diff CONTENT for the rest of the schema's inputs. The
#     extractor (mk/tools.mk regen-xml-schema) also reads src/ui/ widget
#     registrations, src/ lv_xml_register_const() calls, lib/helix-xml/src/xml
#     and the theme defaults, and none of those reach the linter except through
#     the schema. A path trigger on src/ would fire on nearly every commit and
#     pay make's startup for nothing, so the schema-input signals below match
#     the added/removed lines instead — the same technique XML_CONST_DELETED
#     uses. CI's XML Lint job still covers the wider path set.
#   - Lints against the snapshot on disk first. That is what CI does with the
#     committed copy, so on a clean tree a pass here means CI passes and no
#     regen is owed. It also stops schema.json churning on commits where
#     staleness changes no outcome.
#   - Regenerates only on the paths where the snapshot can actually be at
#     fault: a failing lint, a staged edit that deletes a const definition
#     (which would otherwise leave dangling refs resolving against a stale
#     snapshot), or a staged change to a schema input the linter cannot see.
#     Only those rare paths pay the 0.5s `make` startup.
echo "🧩 Running helix-xml-linter..."

XML_LINTER_SCHEMA_PATH="tools/xml-linter/schema/schema.json"

# Mirrors `make lint-xml` (mk/tools.mk) but skips make's ~0.5s startup. Always
# lints all of ui_xml/, never just the staged files: the linter builds its
# cross-file component registry from the paths it is handed, so a staged-only
# run reports every component defined in an unstaged file as unknown.
run_xml_linter() {
  PYTHONPATH=tools/xml-linter/src python3 -m helix_xml_linter.cli \
    --schema "$XML_LINTER_SCHEMA_PATH" --severity error ui_xml/
}

# True when the staged diff adds or removes a C++ schema input. Both patterns
# mirror extract_schema.py exactly:
#   - _REGISTER_WIDGET_RE / _auto_discover_cpp_widgets() rglob *.cpp under
#     --cpp-src src/ui for lv_xml_register_widget("name", …)
#   - _CPP_REGISTER_CONST_RE / extract_cpp_registered_constants() rglob *.cpp
#     under --cpp-const-dirs src for lv_xml_register_const(scope, "name")
# .cpp only, because that is all the extractor globs — a registration moved to a
# header is invisible to the schema either way. `-U0` leaves only changed lines,
# and no ---/+++ file header can contain either call, so `^[-+]` is enough.
schema_cpp_input_changed() {
  if git diff --cached -U0 -- 'src/ui/*.cpp' | \
     grep -qE '^[-+].*lv_xml_register_widget[[:space:]]*\('; then
    return 0
  fi
  if git diff --cached -U0 -- 'src/*.cpp' | \
     grep -qE '^[-+].*lv_xml_register_const[[:space:]]*\('; then
    return 0
  fi
  return 1
}

# Staged schema inputs the linter cannot see through the current snapshot, so
# they force a regen below rather than trusting the first lint. lib/helix-xml is
# a gitlink: its files never appear in this index, only the pointer bump does.
# mk/tools.mk is in the set because it owns the extractor's argument list.
SCHEMA_INPUT_STAGED=false

if [ "$STAGED_ONLY" = true ]; then
  XML_LINT_TRIGGERS=$(git diff --cached --name-only --diff-filter=ACM | \
    grep -E '^(ui_xml/.*\.xml$|tools/xml-linter/)' || true)
  if git diff --cached --name-only --diff-filter=ACMD | \
     grep -qE '^(lib/helix-xml$|mk/tools\.mk$|assets/config/themes/defaults/)' || \
     schema_cpp_input_changed; then
    SCHEMA_INPUT_STAGED=true
    XML_LINT_TRIGGERS="$XML_LINT_TRIGGERS schema-inputs"
  fi
else
  XML_LINT_TRIGGERS="all"
fi

if [ -z "$XML_LINT_TRIGGERS" ]; then
  echo "ℹ️  No XML linter inputs staged"
elif ! command -v python3 >/dev/null 2>&1; then
  echo "⚠️  python3 not found — skipping XML lint"
else
  # A deleted const can leave a dangling ref that still resolves against the
  # stale snapshot — the one staleness a passing lint cannot rule out.
  XML_CONST_DELETED=false
  if [ "$STAGED_ONLY" = true ]; then
    if git diff --cached -U0 -- 'ui_xml/*.xml' | \
       grep -qE '^-[[:space:]]*<(px|color|string|int|percentage|font|tiny_ttf|bin|const)[[:space:]][^>]*name='; then
      XML_CONST_DELETED=true
    fi
  fi

  if [ "$XML_CONST_DELETED" = false ] && [ "$SCHEMA_INPUT_STAGED" = false ] && \
     run_xml_linter >/tmp/lint_xml.out 2>&1; then
    qc_count "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
  else
    # The lint failed, a const was deleted, or a schema input the lint cannot
    # see is staged. Refresh the snapshot and retry — `make` here (not the raw
    # extractor) keeps mk/tools.mk the single source of truth for the
    # extractor's argument list.
    #
    # SCHEMA_DIRTY_BEFORE is worktree-vs-index: true means the snapshot carries
    # unstaged edits, which may belong to a change that is not this commit's.
    SCHEMA_DIRTY_BEFORE=false
    git diff --quiet -- "$XML_LINTER_SCHEMA_PATH" || SCHEMA_DIRTY_BEFORE=true

    # Stale means REGENERATION CHANGED THE BYTES, which is a different question
    # from "differs from the index". On a tree where the snapshot has already
    # been regenerated and not staged, the bytes on disk are correct yet still
    # differ from the index — comparing against the index there calls a correct
    # file stale and blocks the commit on a diagnosis nobody can act on. So keep
    # the pre-regen bytes and compare the regen output against those.
    SCHEMA_BEFORE_REGEN=$(mktemp "${TMPDIR:-/tmp}/helix_schema_before.XXXXXX")
    cp "$XML_LINTER_SCHEMA_PATH" "$SCHEMA_BEFORE_REGEN"

    if make regen-xml-schema >/tmp/regen_xml_schema.out 2>&1; then
      SCHEMA_WAS_STALE=false
      cmp -s "$SCHEMA_BEFORE_REGEN" "$XML_LINTER_SCHEMA_PATH" || SCHEMA_WAS_STALE=true
      rm -f "$SCHEMA_BEFORE_REGEN"

      if run_xml_linter >/tmp/lint_xml.out 2>&1; then
        if [ "$SCHEMA_WAS_STALE" = false ] && [ "$SCHEMA_DIRTY_BEFORE" = false ]; then
          qc_count "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
        elif [ "$SCHEMA_WAS_STALE" = false ]; then
          # Regeneration changed nothing: the snapshot is already correct and
          # merely unstaged. Nothing to fix, and nothing this hook may stage —
          # those bytes belong to whichever change regenerated them, which is
          # the same reason the stale branch below refuses to stage a dirty
          # file. Not a commit blocker: the committer cannot resolve it from
          # inside this commit without absorbing someone else's content. Say it
          # plainly instead, because CI lints the COMMITTED copy.
          qc_count "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
          echo "ℹ️  $XML_LINTER_SCHEMA_PATH is already up to date but unstaged"
          echo "   CI's XML Lint job lints the committed copy — commit it:"
          echo "   git add $XML_LINTER_SCHEMA_PATH"
        # The snapshot was the problem. Stage it with the XML that made it
        # stale — unless it was already dirty, in which case it belongs to
        # unrelated WIP and is not ours to stage.
        elif [ "$AUTO_FIX" = true ] && [ "$STAGED_ONLY" = true ] && [ "$SCHEMA_DIRTY_BEFORE" = false ]; then
          git add "$XML_LINTER_SCHEMA_PATH"
          echo "   ✓ Regenerated and staged $XML_LINTER_SCHEMA_PATH"
          qc_count "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
        else
          echo "⚠️  $XML_LINTER_SCHEMA_PATH was stale — regenerated in place"
          echo "   Commit it or CI's XML Lint job will fail:"
          echo "   git add $XML_LINTER_SCHEMA_PATH"
          EXIT_CODE=1
        fi
      else
        cat /tmp/lint_xml.out
        echo "   Run 'make lint-xml-all' to see warnings too"
        EXIT_CODE=1
      fi
    else
      # Regeneration is best-effort — fall back to the snapshot on disk so a
      # broken extractor can't block every commit, and fail only on real lint errors.
      rm -f "$SCHEMA_BEFORE_REGEN"
      cat /tmp/regen_xml_schema.out
      echo "⚠️  Schema regeneration failed — linting against the snapshot on disk"
      if run_xml_linter >/tmp/lint_xml.out 2>&1; then
        qc_count "✅ helix-xml-linter passed ($(tail -1 /tmp/lint_xml.out))"
      else
        cat /tmp/lint_xml.out
        echo "   Run 'make lint-xml-all' to see warnings too"
        EXIT_CODE=1
      fi
    fi
  fi
fi

echo ""

  return $EXIT_CODE
}

QC_TRIGGER_qc_xml_linter="$QC_TRIGGER_XML"
