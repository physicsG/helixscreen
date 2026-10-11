# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by scripts/quality-checks.sh, which owns the run state this reads
# (STAGED_ONLY, AUTO_FIX, FILES, QC_TMP, ...) and reads the QC_TRIGGER_<gate>
# assignment through qc_trigger_re.
# shellcheck disable=SC2034,SC2154

# One clang-format formats this tree: the wheel pinned in requirements.txt,
# installed into .venv by `make venv-setup`. CLANG_FORMAT may name another
# binary, but only one that reports the pinned version; nothing on PATH is
# ever consulted. Sets CF_PIN and CF_BIN on success and CF_RESOLVE_ERR on
# failure.
qc_resolve_clang_format() {
  CF_PIN="$(grep -oE '^clang-format==[0-9.]+' "$REPO_ROOT/requirements.txt" 2>/dev/null | cut -d= -f3)"
  CF_BIN=""; CF_RESOLVE_ERR=""
  if [ -z "$CF_PIN" ]; then
    CF_RESOLVE_ERR="requirements.txt does not pin clang-format (clang-format==X.Y.Z)"
    return 1
  fi
  local cand="${CLANG_FORMAT:-$REPO_ROOT/.venv/bin/clang-format}" ver
  if [ ! -x "$cand" ] && ! command -v "$cand" >/dev/null 2>&1; then
    if [ -n "${CLANG_FORMAT:-}" ]; then
      CF_RESOLVE_ERR="CLANG_FORMAT=$cand is not an executable"
    else
      CF_RESOLVE_ERR="pinned clang-format $CF_PIN not found (.venv/bin/clang-format is missing)"
    fi
    return 1
  fi
  ver="$("$cand" --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
  if [ "$ver" != "$CF_PIN" ]; then
    CF_RESOLVE_ERR="$cand is clang-format ${ver:-unknown}, the pin is $CF_PIN"
    return 1
  fi
  CF_BIN="$cand"
  return 0
}
# ====================================================================
# Phase 2: Code Quality Checks
# ====================================================================
qc_phase2() {
  local EXIT_CODE=0

# Code Formatting Check (clang-format)
#
# The pinned wheel (qc_resolve_clang_format) is the only formatter this tree
# accepts, on every machine and in CI, so its verdict is byte-identical
# everywhere and a difference is a real one. A distro or Homebrew
# clang-format, even another 18.x, reflows differently, and two formatters
# taking turns on one file is how files ping-pong between commits; a tree
# that cannot resolve the pin fails here instead of formatting with whatever
# it has.
#
# Files that were unformatted when this gate started blocking are carried in
# CLANG_FORMAT_BASELINE: still-unformatted entries are reported, not failed,
# until they are next staged - the pre-commit auto-format cleans them then
# - and a full sweep refuses an entry that has come clean, so the list only
# shrinks.
echo "🎨 Checking code formatting (clang-format)..."
# Unformatted when the gate started blocking; each entry leaves when it is
# next staged and auto-formatted. Empty: every entry has come clean, so the
# gate now fails any unformatted file outright rather than reporting it.
CLANG_FORMAT_BASELINE=""
CF_OK=false
if qc_resolve_clang_format; then CF_OK=true; fi
if [ -n "$FILES" ]; then
  if [ "$CF_OK" = true ]; then
    if [ -f ".clang-format" ]; then
      # This probe was the single slowest thing in the script - 41s of a 44s
      # run - because it spawned one clang-format (plus one head+grep) per
      # source file, strictly serially. The probe is read-only, so it fans out;
      # --auto-fix then rewrites only the files that came back dirty, which is
      # normally a handful, and stays serial so its output keeps a stable order.
      FORMAT_ISSUES=""
      CF_CAND="$(mktemp)"
      CF_DIRTY="$(mktemp)"
      CF_SEEN="$(mktemp)"
      printf '%s
' $FILES > "$CF_CAND"
      CF_TOTAL=$(grep -c . "$CF_CAND")
      # Skipping auto-generated sources is part of the parallel pass: their
      # on-disk format is owned by the generator (e.g.
      # src/generated/lv_i18n_translations.c from generate_translations.py), and
      # reformatting them fights the generator on every build.
      #
      # The candidate list arrives on stdin: reading it with the -a flag is a
      # GNU xargs extension that BSD xargs rejects outright, and this pipeline treats no output as
      # no findings, so a fan-out that never ran would read as every file
      # clean. Each worker records the file it looked at in CF_SEEN, and the
      # verdict below refuses a probe that covered fewer files than it was
      # given. xargs's own stderr stays visible for the same reason.
      < "$CF_CAND" xargs -P "${QC_JOBS:-4}" -I{} sh -c '
        f="$1"
        printf "%s\n" "$f" >> "$2"
        [ -f "$f" ] || exit 0
        head -5 "$f" | grep -qiE "auto-generated|DO NOT EDIT" && exit 0
        "$0" --dry-run --Werror "$f" >/dev/null 2>&1 || printf "%s
" "$f"
      ' "$CF_BIN" {} "$CF_SEEN" | sort > "$CF_DIRTY"
      CF_EXAMINED=$(grep -c . "$CF_SEEN")
      # Leading space is load-bearing: a message below prints "git add$FORMAT_ISSUES".
      FORMAT_ISSUES="$(sed 's|^| |' "$CF_DIRTY" | tr -d '
')"
      # Which of the dirty files already carried unstaged work, captured BEFORE
      # clang-format -i runs: afterwards every reformatted file differs from the
      # index, so the question can no longer be asked. Mirrors XML_PRE_DIRTY in
      # the XML formatter below.
      CF_PRE_DIRTY=""
      if [ "$STAGED_ONLY" = true ] && [ -s "$CF_DIRTY" ]; then
        while IFS= read -r cf_f; do
          [ -n "$cf_f" ] || continue
          git diff --quiet -- "$cf_f" || CF_PRE_DIRTY="$CF_PRE_DIRTY $cf_f "
        done < "$CF_DIRTY"
      fi
      if [ -n "$FORMAT_ISSUES" ] && [ "$AUTO_FIX" = true ]; then
        while IFS= read -r file; do
          [ -n "$file" ] || continue
          "$CF_BIN" -i "$file"
          echo "   ✓ Auto-formatted: $file"
        done < "$CF_DIRTY"
      fi
      rm -f "$CF_CAND" "$CF_DIRTY" "$CF_SEEN"

      if [ "$CF_EXAMINED" -ne "$CF_TOTAL" ]; then
        echo "❌ clang-format probe covered $CF_EXAMINED of $CF_TOTAL file(s): the fan-out did not run them"
        echo "   (an xargs that rejects GNU options does this - prestonbrown/helixscreen#1488)"
        EXIT_CODE=1
      fi
      # Split the dirty list against the baseline, and find baseline entries
      # that were examined this run and came back clean.
      CF_NEW=""; CF_BASELINED=""; CF_RETIRE=""
      for cf_f in $FORMAT_ISSUES; do
        if printf '%s\n' $CLANG_FORMAT_BASELINE | grep -Fxq "$cf_f"; then
          CF_BASELINED="$CF_BASELINED $cf_f"
        else
          CF_NEW="$CF_NEW $cf_f"
        fi
      done
      for cf_f in $CLANG_FORMAT_BASELINE; do
        printf '%s\n' $FILES | grep -Fxq "$cf_f" || continue
        case "$FORMAT_ISSUES " in *" $cf_f "*) continue ;; esac
        CF_RETIRE="$CF_RETIRE $cf_f"
      done
      if [ -n "$FORMAT_ISSUES" ]; then
        if [ "$AUTO_FIX" = true ]; then
          # Auto-stage formatted files when in pre-commit mode (--staged-only)
          if [ "$STAGED_ONLY" = true ]; then
            # Re-stage only files with NOTHING unstaged. `git add` takes the whole
            # working-tree file, so on a partially staged file it would sweep in
            # hunks deliberately held back - the commit would carry work its author
            # never staged. Those get formatted on disk and named instead. Same
            # rule the XML formatter below applies.
            CF_RESTAGE=""; CF_HELD=""
            for cf_f in $FORMAT_ISSUES; do
              case "$CF_PRE_DIRTY" in
                *" $cf_f "*) CF_HELD="$CF_HELD $cf_f" ;;
                *)           CF_RESTAGE="$CF_RESTAGE $cf_f" ;;
              esac
            done
            if [ -n "$CF_RESTAGE" ]; then
              # shellcheck disable=SC2086  # word splitting is the point: a path list
              git add $CF_RESTAGE
              echo "✅ Auto-formatted and re-staged files:"
              echo "$CF_RESTAGE" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
            fi
            if [ -n "$CF_HELD" ]; then
              CF_HELD_NEW=""
              for cf_f in $CF_HELD; do
                printf '%s\n' $CLANG_FORMAT_BASELINE | grep -Fxq "$cf_f" || CF_HELD_NEW="$CF_HELD_NEW $cf_f"
              done
              if [ -n "$CF_HELD_NEW" ]; then
                echo "❌ Formatted on disk but NOT re-staged (partially staged):$CF_HELD_NEW"
                echo "   This commit would carry unformatted C++. Stage it with: git add$CF_HELD_NEW"
                EXIT_CODE=1
              else
                echo "⚠️  Formatted on disk but NOT re-staged (partially staged, baselined):$CF_HELD"
              fi
            fi
          else
            echo "✅ Auto-formatted files - re-stage them before committing:"
            echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
            echo ""
            echo "ℹ️  Stage formatted files with:"
            echo "   git add$FORMAT_ISSUES"
          fi
        else
          if [ -n "$CF_NEW" ]; then
            echo "❌ Unformatted C++ (clang-format $CF_PIN, the pinned wheel, disagrees):"
            echo "$CF_NEW" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
            echo "   Fix with: ./scripts/quality-checks.sh --auto-fix   (then git add the files it names)"
            EXIT_CODE=1
          fi
          if [ -n "$CF_BASELINED" ]; then
            echo "⚠️  Baselined and still unformatted (auto-formatted when next staged):"
            echo "$CF_BASELINED" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
          fi
          if [ -z "$CF_NEW" ] && [ "$CF_EXAMINED" -eq "$CF_TOTAL" ]; then
            qc_count "✅ clang-format: $CF_EXAMINED file(s) checked, $(echo "$CF_BASELINED" | wc -w | tr -d ' ') baselined still unformatted"
          fi
        fi
      elif [ "$CF_EXAMINED" -eq "$CF_TOTAL" ]; then
        qc_count "✅ All files properly formatted ($CF_EXAMINED file(s) checked)"
      fi
      if [ -n "$CF_RETIRE" ]; then
        if [ "$STAGED_ONLY" = true ]; then
          echo "ℹ️  Formatted now; retire from CLANG_FORMAT_BASELINE (scripts/quality-checks.sh) before pushing:$CF_RETIRE"
        else
          echo "❌ Formatted now but still listed in CLANG_FORMAT_BASELINE (scripts/quality-checks.sh); retire:$CF_RETIRE"
          EXIT_CODE=1
        fi
      fi
    else
      echo "ℹ️  No .clang-format file found - skipping format check"
    fi
  else
    echo "❌ $CF_RESOLVE_ERR"
    echo "   Run: make venv-setup   (installs the pinned wheel into .venv)"
    EXIT_CODE=1
  fi
else
  echo "ℹ️  No files to check"
fi

echo ""

# XML Formatting Check
# ui_xml/translations/ is generator output (rewritten by every build), so it is
# excluded from FORMATTING but not from the validation pass above - the generator
# still has to emit well-formed XML. Same exclusion as mk/format.mk; format-xml.py
# self-guards via GENERATED_DIRS, but the xmllint fallback below does not.
#
# android/ is excluded on the staged path for a different reason: AndroidManifest.xml
# and res/values/*.xml are Android-toolchain XML, not LVGL component XML, so this
# formatter's house style does not apply to them. Only the staged path can reach
# them - the find below walks ui_xml/ alone. Mirrors FOREIGN_DIRS in format-xml.py.
echo "📐 Checking XML formatting..."
if [ "$STAGED_ONLY" = true ]; then
  XML_FILES=$(git diff --cached --name-only --diff-filter=ACM | grep "\.xml$" | grep -v "^ui_xml/translations/" | grep -v "^android/" || true)
else
  XML_FILES=$(find ui_xml -name "*.xml" -not -path "ui_xml/translations/*" 2>/dev/null || true)
fi

VENV_PYTHON=".venv/bin/python"

if [ -n "$XML_FILES" ]; then
  # Prefer Python formatter with attribute wrapping, fallback to xmllint
  if [ -x "$VENV_PYTHON" ] && $VENV_PYTHON -c "import lxml" 2>/dev/null; then
    # Use our custom formatter with --check mode.
    # stderr is NOT swallowed: the formatter reports an unparseable file there, and
    # `2>/dev/null` meant a file it could never read produced no visible output at all.
    # That, plus process_file() returning the same value for "parse failed" and "already
    # clean", is how three LVGL state-selector layouts drifted unnoticed.
    if $VENV_PYTHON scripts/format-xml.py --check $XML_FILES; then
      echo "✅ All XML files properly formatted"
    elif [ "$AUTO_FIX" = true ]; then
      # Close the loop the way qc_phase2 does for C++. Staying purely advisory
      # here left a real hole: tests/shell/test_format_xml_gate.bats checks the
      # WHOLE ui_xml tree and fails hard, so an unformatted file that sails past
      # this warning turns the shell suite red on main until someone notices.
      # Which files already had unstaged work — recorded BEFORE formatting,
      # because the reformat itself makes every file differ from the index.
      XML_PRE_DIRTY=""
      if [ "$STAGED_ONLY" = true ]; then
        for f in $XML_FILES; do
          git diff --quiet -- "$f" || XML_PRE_DIRTY="$XML_PRE_DIRTY $f "
        done
      fi
      XML_FIXED=$($VENV_PYTHON scripts/format-xml.py $XML_FILES 2>&1 \
                  | sed -n 's/^Formatted: //p')
      if [ -n "$XML_FIXED" ]; then
        for f in $XML_FIXED; do echo "   ✓ Auto-formatted: $f"; done
        if [ "$STAGED_ONLY" = true ]; then
          # Re-stage only files with NOTHING unstaged. `git add` takes the whole
          # working-tree file, so on a partially staged file it would sweep in
          # hunks deliberately held back — the commit would carry work its author
          # never staged. Those get formatted on disk and named instead.
          XML_RESTAGE=""; XML_HELD=""
          for f in $XML_FIXED; do
            case "$XML_PRE_DIRTY" in
              *" $f "*) XML_HELD="$XML_HELD $f" ;;
              *)        XML_RESTAGE="$XML_RESTAGE $f" ;;
            esac
          done
          # shellcheck disable=SC2086  # word splitting is the point: a path list
          [ -n "$XML_RESTAGE" ] && git add $XML_RESTAGE && \
            echo "✅ Re-staged:$XML_RESTAGE"
          if [ -n "$XML_HELD" ]; then
            echo "⚠️  Formatted but NOT re-staged (partially staged):$XML_HELD"
            echo "ℹ️  This commit still carries unformatted XML. Stage it with: git add$XML_HELD"
          fi
        fi
      else
        # --check disagreed with a real run: the file is unparseable, not unformatted.
        echo "⚠️  XML could not be parsed — see above"
        echo "ℹ️  Fix with: .venv/bin/python scripts/format-xml.py <files>"
      fi
    else
      echo "⚠️  XML files need formatting (or could not be parsed — see above)"
      echo "ℹ️  Fix with: .venv/bin/python scripts/format-xml.py <files>"
      echo "ℹ️  Or run: make format"
      # Don't fail CI for XML formatting - it's a style preference, and --auto-fix
      # (the pre-commit path) now repairs it rather than nagging. Genuine malformed
      # XML is still a hard failure via the xmllint validation pass earlier in this
      # script, so staying advisory here does not let broken XML through.
      # EXIT_CODE=1
    fi
  elif command -v xmllint >/dev/null 2>&1; then
    echo "ℹ️  Python formatter not available, using xmllint (basic check only)"
    FORMAT_ISSUES=""
    for file in $XML_FILES; do
      if [ -f "$file" ]; then
        # Check if file needs formatting (xmllint --format for consistent indentation)
        FORMATTED=$(xmllint --format "$file" 2>/dev/null || echo "PARSE_ERROR")
        if [ "$FORMATTED" = "PARSE_ERROR" ]; then
          echo "⚠️  Cannot format $file (may have XML errors)"
        else
          ORIGINAL=$(cat "$file")
          if [ "$FORMATTED" != "$ORIGINAL" ]; then
            FORMAT_ISSUES="$FORMAT_ISSUES $file"
          fi
        fi
      fi
    done

    if [ -n "$FORMAT_ISSUES" ]; then
      echo "⚠️  XML files may need formatting (basic check):"
      echo "$FORMAT_ISSUES" | tr ' ' '\n' | grep -v '^$' | sed 's/^/   /'
      echo "ℹ️  For proper formatting: make venv-setup && make format"
    else
      echo "✅ All XML files pass basic formatting check"
    fi
  else
    echo "ℹ️  No XML formatter available - skipping XML format check"
    echo "   Run 'make venv-setup' to enable full XML formatting"
  fi
else
  echo "ℹ️  No XML files to check"
fi

echo ""

# Build Verification
if [ "$STAGED_ONLY" = true ]; then
  SECTION_START=$(date +%s)
  echo -n "🔨 Verifying incremental build..."

  # Fast timestamp check (avoids 2-3s make startup overhead)
  # Check if binary exists and no source files are newer
  TARGET="build/bin/helix-screen"
  BUILD_NEEDED=false

  if [ ! -f "$TARGET" ]; then
    BUILD_NEEDED=true
  elif find src include -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.h' -o -name '*.mm' \) -newer "$TARGET" 2>/dev/null | grep -q .; then
    BUILD_NEEDED=true
  fi

  # A staged .cpp can only break its own translation unit, and asking the
  # compiler that question directly costs seconds where a link costs minutes.
  # A staged HEADER is the case that breaks OTHER units, so that one still pays
  # for the build. HELIX_QC_FULL_BUILD=1 forces the build either way.
  STAGED_CXX=$(git diff --cached --name-only --diff-filter=ACM 2>/dev/null \
    | grep -E '\.(cpp|cc|cxx|mm)$' || true)
  STAGED_HDR=$(git diff --cached --name-only --diff-filter=ACM 2>/dev/null \
    | grep -E '\.(h|hh|hpp|hxx|inc)$' || true)

  BUILD_HANDLED=false
  if [ "$BUILD_NEEDED" = true ] && [ -z "$STAGED_HDR" ] && [ -n "$STAGED_CXX" ] \
     && [ -f compile_commands.json ] && [ -z "${HELIX_QC_FULL_BUILD:-}" ]; then
    BUILD_HANDLED=true
    if python3 scripts/syntax_check.py $STAGED_CXX >/tmp/qc_syntax.out 2>&1; then
      section_time $SECTION_START
      echo ""
      echo "✅ $(grep '^summary:' /tmp/qc_syntax.out || echo 'staged sources compile') - pre-push builds the tree"
    else
      section_time $SECTION_START
      echo ""
      echo "❌ A staged source does not compile"
      sed -n '1,40p' /tmp/qc_syntax.out
      EXIT_CODE=1
    fi
  fi

  if [ "$BUILD_HANDLED" = true ]; then
    :
  elif [ "$BUILD_NEEDED" = false ]; then
    section_time $SECTION_START
    echo ""
    echo "✅ Build up to date"
  else
    # Something needs building - run actual build
    # Use SKIP_COMPILE_COMMANDS=1 to avoid slow LSP re-indexing.
    #
    # Bounded -j (qc_build_jobs): this build runs from a commit hook.
    QC_JOBS=$(qc_build_jobs)
    QC_MAKE=$(qc_make)
    if "$QC_MAKE" SKIP_COMPILE_COMMANDS=1 -j"$QC_JOBS" >/dev/null 2>&1; then
      section_time $SECTION_START
      echo ""
      echo "✅ Build successful"
    else
      section_time $SECTION_START
      echo ""
      echo "❌ Build failed - fix compilation errors before committing"
      echo "   Run 'make' to see full error output"
      EXIT_CODE=1
    fi
  fi

  # Say what was actually verified. The build above compiles the WORKING TREE,
  # which in --staged-only mode is not necessarily what is being committed: a
  # rename touching five files and staged for four builds clean here and breaks
  # in CI, because the fifth file is on disk but not in the commit. pre-push
  # gates the real thing (it sweeps an isolated checkout of the pushed commit),
  # so this is a warning rather than a failure - but an unqualified
  # "Build successful" over unverified content is how the wrong thing gets
  # trusted.
  if [ "$STAGED_ONLY" = true ]; then
    UNSTAGED_SRC="$(git diff --name-only --diff-filter=ACM -- \
      '*.cpp' '*.cc' '*.c' '*.h' '*.hpp' '*.mm' 2>/dev/null || true)"
    if [ -n "$UNSTAGED_SRC" ]; then
      echo "⚠️  Build verified the WORKING TREE, not the staged commit"
      echo "   These source files are modified but NOT staged:"
      echo "$UNSTAGED_SRC" | sed 's/^/     /'
      echo "   If the commit depends on them it will fail in CI. pre-push checks"
      echo "   the pushed commit in isolation and will catch it before it leaves."
    fi
  fi
  echo ""
fi

  return $EXIT_CODE
}

QC_TRIGGER_qc_phase2='\.(cpp|c|h|mm|xml)$'
