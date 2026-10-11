#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A comment line inside a `\`-continued recipe ends the shell command there:
# make hands the comment to the shell, the shell drops the rest of that line,
# and every line after it runs in a fresh shell that has lost the variables
# set above it. The recipe still exits 0, so nothing reports it.

load helpers

# Prints file:line for each comment that follows a continued, non-comment line.
broken_continuations() {
    awk 'FNR == 1 { prev = "" }
         prev ~ /\\$/ && prev !~ /^[ \t]*#/ && $0 ~ /^[ \t]*#/ { print FILENAME ":" FNR ": " $0 }
         { prev = $0 }' "$@"
}

@test "no makefile breaks a continued recipe line with a comment" {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    run broken_continuations Makefile mk/*.mk
    [ "$status" -eq 0 ]
    [ -z "$output" ] || { echo "$output" >&2; false; }
}

@test "the check flags a comment inside a continuation and nothing else" {
    f="${BATS_TEST_TMPDIR:-$(mktemp -d)}/x.mk"
    printf 'a:\n\tX=1; \\\n\t# note\n\techo "$$X"\n' > "$f"
    printf '# a comment that wraps \\\n#   onto a second comment line\nb:\n\t# fine\n\techo ok\n' >> "$f"
    run broken_continuations "$f"
    [ "$output" = "$f:3: 	# note" ]
}
