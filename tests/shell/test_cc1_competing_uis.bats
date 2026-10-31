#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for stop_cc1_competing_uis() in competing_uis.sh
# Specifically the sibling-wrapper substitution that works around
# OpenCentauri config-manager's hardcoded screen_ui allowlist
# (see OpenCentauri/cosmos#145). The installer wraps ALL THREE siblings
# (grumpyscreen, guppyscreen, atomscreen) so whichever one COSMOS selects
# as screen_ui after an upgrade delegates to HelixScreen.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

# Write a stock (non-wrapper) sibling init script that echoes its name + arg,
# so we can detect (a) whether it was replaced by a wrapper and (b) whether the
# .helix-bak backup still holds the stock original.
_make_stock_sibling() {
    local name="$1"
    cat > "$MOCK_ROOT/etc/init.d/$name" <<EOF
#!/bin/sh
# Stock COSMOS $name init (mocked)
echo "stock $name \$1"
EOF
    chmod +x "$MOCK_ROOT/etc/init.d/$name"
}

# (Re)write the config-manager mock. $1 = value returned by the 2-arg "get"
# form. The 3-arg "set" form always exits 1, modeling upstream rejecting any
# attempt to persist a value (and in particular 'helixscreen').
_make_config_manager() {
    local current="$1"
    cat > "$MOCK_ROOT/usr/bin/config-manager" <<EOF
#!/bin/sh
# Fake matching upstream behavior: exits 1 on the 3-arg "set" form
# (upstream only supports 2-arg get) and reports a fixed value on get,
# modeling the validator silently rejecting any set.
[ \$# -ne 2 ] && exit 1
echo $current
EOF
    chmod +x "$MOCK_ROOT/usr/bin/config-manager"
}

setup() {
    load helpers
    # competing_uis.sh falls back to `sed -i` on cosmos.conf when config-manager
    # refuses the value; that GNU form needs translating on a BSD host.
    install_gnu_sed_shim

    log_info() { echo "INFO: $*"; }
    log_warn() { echo "WARN: $*"; }
    export -f log_info log_warn

    # Mock root mirroring CC1 layout. SUDO stays empty so file ops happen
    # directly under MOCK_ROOT instead of the real /etc.
    export MOCK_ROOT="$BATS_TEST_TMPDIR/cc1"
    mkdir -p "$MOCK_ROOT/etc/init.d" "$MOCK_ROOT/etc/klipper/config" "$MOCK_ROOT/usr/bin"

    # All three stock COSMOS sibling init scripts present.
    _make_stock_sibling grumpyscreen
    _make_stock_sibling guppyscreen
    _make_stock_sibling atomscreen

    # HelixScreen init script (placeholder — installer normally drops this)
    cat > "$MOCK_ROOT/etc/init.d/helixscreen" <<'EOF'
#!/bin/sh
echo "helixscreen $1"
EOF
    chmod +x "$MOCK_ROOT/etc/init.d/helixscreen"

    # Minimal cosmos.conf
    cat > "$MOCK_ROOT/etc/klipper/config/cosmos.conf" <<'EOF'
[ui]
screen_ui = grumpyscreen
web_ui = mainsail
EOF

    # config-manager mimicking the upstream allowlist behavior. Default get
    # returns "grumpyscreen" (already a wrapped sibling).
    _make_config_manager grumpyscreen
    export PATH="$MOCK_ROOT/usr/bin:$PATH"

    # Read the production module body, substitute the absolute COSMOS path
    # PREFIXES to MOCK_ROOT, then source the patched copy. A directory-prefix
    # rewrite (not per-name) is required because the new code builds sibling
    # paths dynamically as /etc/init.d/${s}. The wrapper heredoc body's
    # `exec /etc/init.d/helixscreen "$@"` is also redirected by this rewrite.
    local patched="$BATS_TEST_TMPDIR/competing_uis.sh"
    sed -e "s|/etc/init.d|$MOCK_ROOT/etc/init.d|g" \
        -e "s|/etc/klipper|$MOCK_ROOT/etc/klipper|g" \
        -e "s|/usr/bin/update-cosmos|$MOCK_ROOT/usr/bin/update-cosmos|g" \
        "$WORKTREE_ROOT/scripts/lib/installer/competing_uis.sh" > "$patched"

    # Safety net: the rewritten copy must NOT reference the real /etc tree.
    # (config-manager itself is found via PATH, so it is allowed to be bare.)
    ! grep -qE '(^|[^A-Za-z0-9_.-])/etc/init\.d' "$patched"
    ! grep -qE '(^|[^A-Za-z0-9_.-])/etc/klipper' "$patched"

    unset _HELIX_COMPETING_UIS_SOURCED
    # shellcheck disable=SC1090
    . "$WORKTREE_ROOT/scripts/lib/installer/common.sh"
    . "$patched"
}

@test "cc1: substitutes grumpyscreen with helixscreen wrapper" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Wrapper must contain the marker that exec's into helixscreen
    grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/grumpyscreen"
    grep -q "exec .*/etc/init.d/helixscreen" "$MOCK_ROOT/etc/init.d/grumpyscreen"
    [ -x "$MOCK_ROOT/etc/init.d/grumpyscreen" ]
}

@test "cc1: all three siblings are wrapped, executable, and carry the marker" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    for s in grumpyscreen guppyscreen atomscreen; do
        grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/$s"
        grep -q "exec .*/etc/init.d/helixscreen" "$MOCK_ROOT/etc/init.d/$s"
        [ -x "$MOCK_ROOT/etc/init.d/$s" ]
    done
}

@test "cc1: backs up original grumpyscreen to .helix-bak" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    [ -f "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" ]
    grep -q "Stock COSMOS grumpyscreen" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
}

@test "cc1: each sibling .helix-bak holds the STOCK original, not the wrapper" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    for s in grumpyscreen guppyscreen atomscreen; do
        [ -f "$MOCK_ROOT/etc/init.d/$s.helix-bak" ]
        grep -q "Stock COSMOS $s" "$MOCK_ROOT/etc/init.d/$s.helix-bak"
        # The backup must be the real UI, never our own wrapper.
        refute grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/$s.helix-bak"
    done
}

@test "cc1: idempotent — does not re-wrap an already-wrapped grumpyscreen" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Capture wrapper content + backup mtime after first run
    cp -p "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" "$MOCK_ROOT/bak1"
    sleep 1
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    # Backup must NOT have been overwritten with the wrapper itself
    diff "$MOCK_ROOT/bak1" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
    # Backup still contains the original content, not the wrapper marker
    ! grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak"
}

@test "cc1: idempotent re-run does not overwrite any sibling .helix-bak" {
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    for s in grumpyscreen guppyscreen atomscreen; do
        cp -p "$MOCK_ROOT/etc/init.d/$s.helix-bak" "$MOCK_ROOT/$s.bak1"
    done
    sleep 1
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    for s in grumpyscreen guppyscreen atomscreen; do
        diff "$MOCK_ROOT/$s.bak1" "$MOCK_ROOT/etc/init.d/$s.helix-bak"
        refute grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/$s.helix-bak"
    done
}

@test "cc1: gui-switcher invoking grumpyscreen now reaches helixscreen" {
    found_any=false
    stop_cc1_competing_uis >/dev/null
    # Simulate gui-switcher's call path: exec /etc/init.d/<config-manager value> start
    run "$MOCK_ROOT/etc/init.d/grumpyscreen" start
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen start"* ]]
}

@test "cc1: cosmos.conf is NOT set to helixscreen; sibling screen_ui stays put" {
    # config-manager get returns grumpyscreen (already a wrapped sibling), so the
    # function must leave screen_ui untouched — and must NEVER write 'helixscreen'.
    found_any=false
    stop_cc1_competing_uis >/dev/null
    refute grep -q "screen_ui = helixscreen" "$MOCK_ROOT/etc/klipper/config/cosmos.conf"
    grep -q "^screen_ui = grumpyscreen" "$MOCK_ROOT/etc/klipper/config/cosmos.conf"
}

@test "cc1: screen_ui set to grumpyscreen when current value is unknown/none" {
    # Current screen_ui is "none" (not one of the wrapped siblings). The function
    # tries `config-manager ui screen_ui grumpyscreen` (set form exits 1 in our
    # mock), then falls back to sed-editing cosmos.conf to grumpyscreen.
    _make_config_manager none
    cat > "$MOCK_ROOT/etc/klipper/config/cosmos.conf" <<'EOF'
[ui]
screen_ui = none
web_ui = mainsail
EOF
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    grep -q "^screen_ui = grumpyscreen" "$MOCK_ROOT/etc/klipper/config/cosmos.conf"
    ! grep -q "screen_ui = helixscreen" "$MOCK_ROOT/etc/klipper/config/cosmos.conf"
}

@test "cc1: a sibling with no stock init script is skipped (no wrapper, no backup)" {
    # atomscreen is absent on this device — must not be created or backed up.
    rm -f "$MOCK_ROOT/etc/init.d/atomscreen"
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    [ ! -e "$MOCK_ROOT/etc/init.d/atomscreen" ]
    [ ! -e "$MOCK_ROOT/etc/init.d/atomscreen.helix-bak" ]
    # The present siblings are still wrapped.
    grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/grumpyscreen"
    grep -q "HELIXSCREEN_WRAPPER" "$MOCK_ROOT/etc/init.d/guppyscreen"
}

@test "cc1: missing grumpyscreen init script is a no-op (no wrapper, no error)" {
    rm -f "$MOCK_ROOT/etc/init.d/grumpyscreen"
    found_any=false
    run stop_cc1_competing_uis
    [ "$status" -eq 0 ]
    [ ! -f "$MOCK_ROOT/etc/init.d/grumpyscreen" ]
    [ ! -f "$MOCK_ROOT/etc/init.d/grumpyscreen.helix-bak" ]
}
