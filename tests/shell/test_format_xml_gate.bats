#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/format-xml.py — the two failure modes that let three of the
# largest ui_xml/ layouts drift unformatted for an unknown length of time.
#
# 1. process_file() returned (False, None) both when a file was already clean AND when
#    it could not be parsed at all, and main() only counted a MISSING file as an error.
#    So --check printed "All files properly formatted" and exited 0 for a file it had
#    never read. quality-checks.sh compounded it with 2>/dev/null, hiding the one clue.
#
# 2. LVGL state selectors are written `style_bg_opa:checked="0"`. Every XML parser reads
#    that colon as a namespace prefix and rejects the file, which is what made
#    filament_panel, input_shaper_panel and theme_editor_overlay unparseable. They are
#    now round-tripped through a sentinel.
#
# The quiet half matters as much as the loud half: the sentinel substitution must not
# touch colons that appear in attribute VALUES or in text, or it would corrupt every
# URL and timestamp in the tree. That is pinned below.
#
# This is advisory in quality-checks.sh by deliberate choice (EXIT_CODE=1 is commented
# out there — XML wrapping is a style preference). Genuine malformed XML is still a hard
# failure via the separate xmllint validation pass, so "advisory" here does not mean
# broken XML gets through. These tests pin the script's exit codes regardless of how the
# hook chooses to act on them.

FORMATTER="scripts/format-xml.py"
PY=".venv/bin/python"

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    FIXTURE_DIR="${BATS_TEST_TMPDIR:-$(mktemp -d)}/format_xml"
    mkdir -p "$FIXTURE_DIR"
    if [ ! -x "$PY" ] || ! "$PY" -c "import lxml" 2>/dev/null; then
        skip "lxml not available in .venv (run: make venv-setup)"
    fi
}

# Write $2 to $FIXTURE_DIR/$1 and run the formatter over it with the remaining args.
write_and_run() {
    local name="$1" body="$2"; shift 2
    printf '%s' "$body" > "$FIXTURE_DIR/$name"
    run "$PY" "$FORMATTER" "$@" "$FIXTURE_DIR/$name"
}

# --- 1. an unparseable file must not read as clean --------------------------------

@test "malformed XML exits non-zero instead of reporting clean" {
    write_and_run broken.xml '<view><lv_obj a="1"></view>
' --check
    [ "$status" -ne 0 ]
    [[ "$output" != *"All files properly formatted"* ]]
}

@test "malformed XML is reported as unprocessable, not as needing formatting" {
    write_and_run broken.xml '<view><lv_obj a="1"></view>
' --check
    [[ "$output" == *"could not be processed"* ]]
}

@test "a well-formed, already-formatted file still exits 0 and says so" {
    write_and_run clean.xml '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <lv_label text="hi"/>
  </view>
</component>
' --check
    [ "$status" -eq 0 ]
    [[ "$output" == *"All files properly formatted"* ]]
}

# --- 2. LVGL state selectors ------------------------------------------------------

@test "state-selector attributes parse instead of erroring as a namespace prefix" {
    write_and_run selector.xml '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <ui_button style_bg_opa:checked="0" style_text_color:checked="#text"/>
  </view>
</component>
' --check
    [ "$status" -eq 0 ]
    [[ "$output" != *"Namespace prefix"* ]]
    [[ "$output" != *"could not be processed"* ]]
}

@test "formatting preserves the state-selector colon verbatim" {
    printf '%s' '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <ui_button name="b" style_bg_opa:checked="0" style_radius:checked="4" style_text_color:disabled="#muted"/>
  </view>
</component>
' > "$FIXTURE_DIR/rt.xml"
    run "$PY" "$FORMATTER" "$FIXTURE_DIR/rt.xml"
    [ "$status" -eq 0 ]
    grep -q 'style_bg_opa:checked="0"' "$FIXTURE_DIR/rt.xml"
    grep -q 'style_radius:checked="4"' "$FIXTURE_DIR/rt.xml"
    grep -q 'style_text_color:disabled="#muted"' "$FIXTURE_DIR/rt.xml"
    # the sentinel must never survive into the written file
    ! grep -q 'LVGL_STATE_COLON' "$FIXTURE_DIR/rt.xml"
}

@test "formatting is idempotent on a file containing state selectors" {
    printf '%s' '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <ui_button name="b" style_bg_opa:checked="0"/>
  </view>
</component>
' > "$FIXTURE_DIR/idem.xml"
    "$PY" "$FORMATTER" "$FIXTURE_DIR/idem.xml" >/dev/null
    cp "$FIXTURE_DIR/idem.xml" "$FIXTURE_DIR/idem.once"
    "$PY" "$FORMATTER" "$FIXTURE_DIR/idem.xml" >/dev/null
    diff -u "$FIXTURE_DIR/idem.once" "$FIXTURE_DIR/idem.xml"
}

# --- 3. the substitution must stay out of values and text -------------------------

@test "colons in attribute values and text are left alone" {
    printf '%s' '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <lv_label name="l" text="ETA 9:30" src="http://example.com/x" style_text_color:checked="#text"/>
  </view>
</component>
' > "$FIXTURE_DIR/values.xml"
    run "$PY" "$FORMATTER" "$FIXTURE_DIR/values.xml"
    [ "$status" -eq 0 ]
    grep -q 'text="ETA 9:30"' "$FIXTURE_DIR/values.xml"
    grep -q 'src="http://example.com/x"' "$FIXTURE_DIR/values.xml"
    ! grep -q 'LVGL_STATE_COLON' "$FIXTURE_DIR/values.xml"
}

@test "a file already containing the sentinel is refused, not silently mangled" {
    write_and_run sentinel.xml '<?xml version="1.0"?>
<component>
  <view extends="lv_obj">
    <lv_obj __LVGL_STATE_COLON__x="1"/>
  </view>
</component>
' --check
    [ "$status" -ne 0 ]
    [[ "$output" == *"reserved sentinel"* ]]
}

# --- 4. the real tree stays clean -------------------------------------------------

@test "every ui_xml file parses and is formatted" {
    run bash -c "$PY $FORMATTER --check \$(find ui_xml -name '*.xml' | sort)"
    [ "$status" -eq 0 ]
    [[ "$output" != *"could not be processed"* ]]
}
