#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# helix-claim runs on macOS dev boxes as well as Linux, so its timestamps must
# not depend on GNU date: BSD date has no `-Is` shorthand and no `-d`.

load helpers

CLAIM="$(cd "${BATS_TEST_DIRNAME}/../.." && pwd)/scripts/helix-claim"
REAL_DATE="$(command -v date)"

setup() {
    export HELIX_CLAIM_DIR="$BATS_TEST_TMPDIR/claims"
    mkdir -p "$HELIX_CLAIM_DIR"
    # A BSD-like date: the GNU-only spellings fail the way macOS's /bin/date does.
    mock_command_script "date" "for a in \"\$@\"; do
    case \"\$a\" in
        -Is|-d|-d*) echo \"date: invalid argument 's' for -I\" >&2; exit 1 ;;
    esac
done
exec '$REAL_DATE' \"\$@\""
}

@test "take records its start time without GNU-only date options" {
    run "$CLAIM" take heavy:t probe --pid $$
    [ "$status" -eq 0 ]
    [[ "$output" != *"invalid argument"* ]] || fail "date complained: $output"
    local body
    body=$(tr -d '\n' < "$HELIX_CLAIM_DIR/heavy_t.json")
    [[ "$body" =~ \"started\":\ \"20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]T ]] || fail "no ISO start: $body"
    [[ "$body" =~ \"started_epoch\":\ [0-9]+ ]] || fail "no epoch start: $body"
}

@test "resources ages a live claim without GNU-only date options" {
    [ -r /proc/self/stat ] || skip "claim liveness needs /proc"
    "$CLAIM" take heavy:t probe --pid $$ >/dev/null
    run "$CLAIM" resources
    [ "$status" -eq 0 ]
    [[ "$output" == *"heavy:t"* ]] || fail "live claim not listed: $output"
    local line
    line=$(printf '%s\n' "$output" | grep 'heavy:t')
    [[ "$line" != *" ? "* ]] || fail "age not computed: $line"
}
