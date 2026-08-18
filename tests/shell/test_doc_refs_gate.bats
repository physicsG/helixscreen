#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for check_doc_refs.py --devel: the docs/devel dead-path and
# dead-link gate added for the architecture overhaul.

setup() {
    CHECK="$BATS_TEST_DIRNAME/../../scripts/check_doc_refs.py"
    FIX="$BATS_TEST_TMPDIR/devel"
    mkdir -p "$FIX"
}

@test "devel: clean doc with file ref, :line ref, and live link passes" {
    mkdir -p "$BATS_TEST_TMPDIR/src"
    echo x > "$BATS_TEST_TMPDIR/src/zz_fixture_real.cpp"
    cat > "$FIX/good.md" <<'EOF'
See `src/zz_fixture_real.cpp` and `src/zz_fixture_real.cpp:42` and
[the other doc](other.md). Glob `src/*.cpp` and `<placeholder>` are skipped.
EOF
    echo x > "$FIX/other.md"
    run python3 "$CHECK" --devel "$FIX/good.md"
    [ "$status" -eq 0 ]
}

@test "devel: dead file path fails with file:line report" {
    cat > "$FIX/bad.md" <<'EOF'
Cites `src/zz_fixture_missing_thing.cpp` which does not exist.
EOF
    run python3 "$CHECK" --devel "$FIX/bad.md"
    [ "$status" -eq 1 ]
    [[ "$output" == *"zz_fixture_missing_thing.cpp"* ]]
}

@test "devel: dead relative markdown link fails" {
    echo x > "$FIX/target.md"
    cat > "$FIX/badlink.md" <<'EOF'
See [gone](nope.md) and [anchor ok](target.md#section).
EOF
    run python3 "$CHECK" --devel "$FIX/badlink.md"
    [ "$status" -eq 1 ]
    [[ "$output" == *"nope.md"* ]]
}

@test "devel: plans and printer-research subdirs are exempt" {
    mkdir -p "$FIX/plans"
    cat > "$FIX/plans/old.md" <<'EOF'
`src/zz_fixture_missing_thing.cpp`
EOF
    run python3 "$CHECK" --devel "$FIX"
    [ "$status" -eq 0 ]
}

@test "devel: C++ lambda with unnamed pointer param is not a link" {
    cat > "$FIX/lambda.md" <<'EOF'
```cpp
.on_destroy = [](lv_obj_t*) {
    g_api->log_debug("cleanup");
}
```
EOF
    run python3 "$CHECK" --devel "$FIX/lambda.md"
    [ "$status" -eq 0 ]
    [[ "$output" != *"lv_obj_t*"* ]]
}
