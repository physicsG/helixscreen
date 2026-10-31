#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for symbol extraction Makefile targets and variables
# Split from test_symbol_extraction.bats for parallel execution.

# Cache make -n -p output once per file (expensive: parses entire Makefile tree)

load helpers
setup_file() {
    export MAKE_DB_CACHE="$BATS_FILE_TMPDIR/make_db.txt"
    export MAKE_DB_STRIP_CACHE="$BATS_FILE_TMPDIR/make_db_strip.txt"
    make_db > "$MAKE_DB_CACHE" || true
    make_db STRIP_BINARY=yes CROSS_COMPILE=fake- > "$MAKE_DB_STRIP_CACHE" || true
}

@test "symbols target is defined in Makefile" {
    # Use make database (cached) instead of make -n which fails without deps
    grep -q '^symbols:' "$MAKE_DB_CACHE"
}

@test "strip target is defined in Makefile" {
    grep -q '^strip:' "$MAKE_DB_CACHE"
}

@test "symbols and strip are in .PHONY" {
    local phony_lines
    phony_lines=$(grep '^\.PHONY:' "$MAKE_DB_CACHE")
    contains "symbols" "$phony_lines"
    [[ "$phony_lines" == *"strip"* ]]
}

@test "LDFLAGS does not contain -s for cross-compile builds" {
    # Verify the old LDFLAGS += -s pattern is gone
    # Check the actual Makefile source, not make output (which varies by host)
    # Match -s as a complete flag (followed by end-of-line or whitespace) so
    # legitimate flags like -static-libasan don't trip the assertion.
    ! grep -qE 'LDFLAGS \+= -s($|[[:space:]])' Makefile
}

@test "STRIP_CMD is defined when STRIP_BINARY=yes" {
    grep -q 'STRIP_CMD' "$MAKE_DB_STRIP_CACHE"
}

@test "NM_CMD is defined when STRIP_BINARY=yes" {
    grep -q 'NM_CMD' "$MAKE_DB_STRIP_CACHE"
}

# The body of the extract-symbols define, which the symbols target calls.
extract_symbols_define() {
    sed -n '/^define extract-symbols$/,/^endef$/p' Makefile
}

@test "symbols target extracts through the extract-symbols define" {
    grep -A3 '^symbols:' Makefile | grep -q 'call extract-symbols,$(TARGET)'
}

@test "symbols target uses nm -nC" {
    extract_symbols_define | grep -q '$(NM_CMD) -nC'
}

@test "strip target uses strip command" {
    grep -A2 '^strip:' Makefile | grep -q 'STRIP_CMD'
}

@test "symbol map output goes to TARGET.sym" {
    extract_symbols_define | grep -q '> $(1).sym &&'
}
