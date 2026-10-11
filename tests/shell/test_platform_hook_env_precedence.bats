#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The init script calls platform_pre_start() before it launches helix-screen,
# and helix-launcher.sh only exports variables from helixscreen.env that are
# not already set. A hook that assigns unconditionally therefore outranks every
# other source, including the user's own environment, and does it silently -
# the documented override appears to be accepted and is discarded.
#
# The same is true of any export the init script lets through to the launcher:
# an inherited hook default counts as "already set" and outranks
# helixscreen.env. The init script invokes platform_pre_start for side effects
# only (subshell), and the launcher runs the hook again after applying the env
# file, where a platform default belongs.
#
# Hooks supply the platform DEFAULT. Anything already set wins.
# Precedence: shell environment > helixscreen.env > platform hook > built-in.
# That ordering holds inside the launcher; the init script's own early-splash
# read of HELIX_NO_SPLASH asks the launcher (--print-env NAME) ahead of the
# hooks it sources, so both splash decisions share one parser and see one
# operator intent.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"
HOOKS_DIR="$WORKTREE_ROOT/assets/config/platform"
INIT_SCRIPT="$WORKTREE_ROOT/config/helixscreen.init"
LAUNCHER="$WORKTREE_ROOT/scripts/helix-launcher.sh"

setup() {
    load helpers

    # The launcher refuses to evaluate a group/world-writable env file; a host
    # umask of 0002 would otherwise land every fixture at 0664.
    umask 022
}

# Every export of a HELIX_* variable in a hook, anywhere in the file —
# platform_pre_start itself, the helpers it calls, everywhere. A hook export
# reaches the launcher's environment from any function the launcher's
# platform_pre_start call runs, so the defaulting rule cannot be scoped to
# one function body. The name list comes from word-splitting each export
# statement, so `export NAME` (assign-then-export) and
# `export OTHER=1 NAME=2` (multi-assignment) are seen too: any spelling
# that exports a HELIX_* name counts. Exports of other variables (camera
# plumbing configured inside a subshell around a daemon start) never escape
# that subshell and are outside this contract.
helix_exports() {
    awk '
        /^[ \t]*export([ \t]|$)/ {
            for (i = 2; i <= NF; i++) {
                name = $i
                sub(/=.*$/, "", name)
                if (name ~ /^HELIX_[A-Z0-9_]+$/) { print; break }
            }
        }
    ' "$1"
}

# The export statements in $1 that assign a HELIX_* variable unconditionally
# and so discard a value the operator already set: a statement is unguarded
# when any HELIX_* name it exports appears without the `${NAME:-` defaulting
# form on that same line.
unguarded_hook_exports() {
    awk '
        /^[ \t]*export([ \t]|$)/ {
            bad = 0
            for (i = 2; i <= NF; i++) {
                name = $i
                sub(/=.*$/, "", name)
                if (name !~ /^HELIX_[A-Z0-9_]+$/) continue
                if (index($0, "${" name ":-") == 0) bad = 1
            }
            if (bad) print
        }
    ' "$1"
}

# export statements at file scope (outside every function body). The
# function-entry rule and the reset rule each `next` on their own line, so a
# closing brace always resets the scan: an export below the last function is
# as visible as one above the first. Heredoc content is skipped line by line
# (delimiters are identifier-shaped; the terminator is the delimiter alone
# on its line, however indented): a here-document can carry a column-0
# `name() {` line with no matching `}`, which would otherwise flip the scan
# into function scope and swallow every file-scope export below it. The
# indent class holds a literal space AND tab (the launcher's env parser
# precedent): a tab-indented export inside a file-scope `if` block is
# exactly the offender the gate exists to catch.
file_scope_exports() {
    awk '
        heredoc != "" {
            if ($0 ~ "^[ \t]*" heredoc "[ \t]*$") heredoc = ""
            next
        }
        {
            detect = $0
            sub(/#.*/, "", detect)
            if ((i = index(detect, "<<")) > 0) {
                tail = substr(detect, i + 2)
                sub(/^-/, "", tail)
                if (match(tail, /[A-Za-z_][A-Za-z0-9_-]*/))
                    heredoc = substr(tail, RSTART, RLENGTH)
            }
        }
        /^[a-zA-Z_][a-zA-Z0-9_]*\(\) ?\{/ { infn = 1; next }
        infn && /^}/ { infn = 0; next }
        infn { next }
        /^[ \t]*export / { print }
    ' "$1"
}

@test "every platform hook defaults its exports instead of overriding them" {
    local offenders=""
    for f in "$HOOKS_DIR"/hooks-*.sh; do
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            offenders="$offenders
  $(basename "$f"): $(echo "$line" | sed 's/^\s*//')"
        done <<< "$(unguarded_hook_exports "$f")"
    done
    [ -z "$offenders" ] || {
        echo "These assignments discard a value the user already set:$offenders"
        echo 'Use: export VAR="${VAR:-default}"'
        false
    }
}

@test "the hooks actually export something (the check above is not vacuous)" {
    local n=0
    for f in "$HOOKS_DIR"/hooks-*.sh; do
        n=$((n + $(helix_exports "$f" | grep -c . || true)))
    done
    [ "$n" -ge 20 ]
}

@test "the defaulting scan sees unguarded exports inside helper functions" {
    # The gate above is only as good as this scan. A scan that stops at
    # platform_pre_start's closing brace would pass the gate while missing an
    # unguarded export in a start_/ensure_ helper - exactly where hooks put
    # their conditional work. Feed a fixture holding one guarded default, one
    # hardcoded helper export (tab-indented, the shape it takes inside a
    # nested block) and one subshell-confined non-HELIX export, and assert
    # only the helper export is reported.
    local fixture="$BATS_TEST_TMPDIR/defaulting-fixture.sh"
    cat > "$fixture" << 'EOF'
platform_pre_start() {
    export HELIX_LOG_DEST="${HELIX_LOG_DEST:-file}"
    helper_fn
}
helper_fn() {
    if [ -z "$HELIX_FOO" ]; then
	export HELIX_FOO=hardcoded
    fi
    ( export V4L2_TOOL=imposter; start-stop-daemon -S -x /usr/bin/lmd )
}
EOF
    run unguarded_hook_exports "$fixture"
    [ "$status" -eq 0 ]
    echo "$output" | grep -q 'export HELIX_FOO=hardcoded'
    if echo "$output" | grep -q 'HELIX_LOG_DEST'; then
        fail "scan flags a guarded default"
    fi
    if echo "$output" | grep -q 'V4L2_TOOL'; then
        fail "scan flags a subshell-confined non-HELIX export"
    fi
}

@test "no hook exports anything at file scope" {
    # A file-scope export runs when the INIT SCRIPT sources the hooks file, so
    # it reaches the launcher ahead of helixscreen.env and outranks the
    # operator's value. Hook defaults belong in platform_pre_start(), which
    # the init script runs in a subshell and the launcher runs after the env
    # file. A value the init script itself must read before any hook function
    # runs (Forge-X's splash gate) is assigned at file scope WITHOUT
    # exporting: every consumer reads it in the shell that sourced the hooks.
    local offenders=""
    for f in "$HOOKS_DIR"/hooks-*.sh; do
        while IFS= read -r line; do
            [ -n "$line" ] || continue
            offenders="$offenders
  $(basename "$f"): $(echo "$line" | sed 's/^\s*//')"
        done <<< "$(file_scope_exports "$f")"
    done
    [ -z "$offenders" ] || {
        echo "These file-scope exports outrank helixscreen.env on the init path:$offenders"
        echo "Move them into platform_pre_start(), or assign without exporting"
        echo "if the init script itself must read the value."
        false
    }
}

@test "the file-scope scan sees exports above and below functions (not vacuous)" {
    # The gate above is only as good as this scan. A scan that goes blind
    # below the first function body would pass the gate while missing real
    # offenders, so feed a fixture with exports above, inside, and below a
    # function and assert exactly the three file-scope ones come back. One
    # is tab-indented (the shape an export inside a tab-indented file-scope
    # `if` block takes) - whitespace must not decide whether the gate fires.
    local fixture="$BATS_TEST_TMPDIR/scan-fixture.sh"
    cat > "$fixture" << 'EOF'
export ABOVE=1
helper_fn() {
    export INSIDE=1
}
EOF
    printf '\texport TABBED=1\nexport BELOW=1\n' >> "$fixture"
    run file_scope_exports "$fixture"
    [ "$status" -eq 0 ]
    echo "$output" | grep -qx 'export ABOVE=1'
    # The scan reports lines as written, indent included.
    echo "$output" | grep -qx "$(printf '\t')export TABBED=1"
    echo "$output" | grep -qx 'export BELOW=1'
    if echo "$output" | grep -q 'INSIDE'; then
        fail "scan reports an export from inside a function body"
    fi
}

@test "the defaulting scan sees every export spelling a hook can use" {
    # `export NAME=value` is not the only spelling that reaches the
    # launcher's environment: a bare `export NAME` after an assignment, and
    # a multi-assignment `export OTHER=1 NAME=2`, export NAME just the same.
    # A scan anchored on `export NAME=` is blind to both. The one-line
    # guarded form remains the only spelling whose guarantee the scan can
    # verify without dataflow, so an assign-then-export pair is reported
    # even when a surrounding `if [ -z ]` guards it: write
    # `export NAME="${NAME:-default}"`.
    local fixture="$BATS_TEST_TMPDIR/spellings-fixture.sh"
    cat > "$fixture" << 'EOF'
platform_pre_start() {
    export HELIX_BARE
    export V4L2_TOOL=imposter HELIX_MULTI=2
    HELIX_LATER=/hard/coded
    export HELIX_LATER
    export HELIX_GUARDED="${HELIX_GUARDED:-ok}"
}
EOF
    run unguarded_hook_exports "$fixture"
    [ "$status" -eq 0 ]
    echo "$output" | grep -q 'export HELIX_BARE$'
    echo "$output" | grep -q 'HELIX_MULTI=2'
    echo "$output" | grep -q 'export HELIX_LATER$'
    if echo "$output" | grep -q 'HELIX_GUARDED'; then
        fail "scan flags a guarded default"
    fi
}

@test "the file-scope scan does not go blind inside a heredoc" {
    # A hook writing a config file through a heredoc can carry content lines
    # that look like function headers (`name() {`) with no matching
    # column-0 `}` to close them. The scan must skip heredoc content rather
    # than flipping into function scope there and swallowing every
    # file-scope export below the heredoc. Multi-line quoted strings keep
    # the same hazard and stay out of scope: no hook spells one at column 0.
    local fixture="$BATS_TEST_TMPDIR/heredoc-fixture.sh"
    cat > "$fixture" << 'EOF'
cat > /etc/init.d/capture <<'UNIT'
start_capture() {
    exec /usr/bin/capture
UNIT
export HELIX_AFTER_HEREDOC=1
EOF
    run file_scope_exports "$fixture"
    [ "$status" -eq 0 ]
    echo "$output" | grep -q 'HELIX_AFTER_HEREDOC'
}

@test "the defaulting form keeps a preset value and supplies one otherwise" {
    # Pin the semantics the rule depends on, so a future rewrite that looks
    # equivalent but is not gets caught here rather than on a device.
    run sh -c 'HELIX_CACHE_DIR=/user/choice; export HELIX_CACHE_DIR
               export HELIX_CACHE_DIR="${HELIX_CACHE_DIR:-/platform/default}"
               echo "$HELIX_CACHE_DIR"'
    [ "$status" -eq 0 ]
    [ "$output" = "/user/choice" ]

    run sh -c 'unset HELIX_CACHE_DIR
               export HELIX_CACHE_DIR="${HELIX_CACHE_DIR:-/platform/default}"
               echo "$HELIX_CACHE_DIR"'
    [ "$status" -eq 0 ]
    [ "$output" = "/platform/default" ]
}

# =============================================================================
# The init script's own invocation of platform_pre_start.
#
# helix-launcher.sh fills only UNSET variables from helixscreen.env, so
# anything the init script exports before exec'ing the launcher outranks the
# operator's file. The init script must therefore run platform_pre_start for
# its side effects only, confining the hook's exports to a subshell; the
# launcher re-runs the hook after the env file and supplies the defaults there.
# =============================================================================

# The line inside start() that invokes platform_pre_start, taken from the real
# init script so these tests run what ships, not a copy.
init_pre_start_call() {
    awk '
        /^start\(\) \{/ { in_start = 1 }
        in_start && /platform_pre_start/ && $0 !~ /^[ \t]*#/ { print; exit }
    ' "$INIT_SCRIPT"
}

# A hook shaped like the shipped ones: every export is a guarded default.
install_defaulting_hook() {
    mkdir -p "$MOCK_INSTALL/platform"
    cat > "$MOCK_INSTALL/platform/hooks.sh" << 'HOOKEOF'
#!/bin/sh
platform_pre_start() {
    export HELIX_LOG_DEST="${HELIX_LOG_DEST:-file}"
    export HELIX_LOG_FILE="${HELIX_LOG_FILE:-/hook/default.log}"
}
HOOKEOF
}

@test "init script's platform_pre_start leaks no export into the launcher's environment" {
    call="$(init_pre_start_call)"
    # An empty extraction would make the assertion below vacuously green.
    [ -n "$call" ]

    mkdir -p "$BATS_TEST_TMPDIR/empty-proc"
    MOCK_INSTALL="$BATS_TEST_TMPDIR/helixscreen"
    install_defaulting_hook

    HELIX_PROC_ROOT="$BATS_TEST_TMPDIR/empty-proc" sh -c '
        unset HELIX_LOG_DEST
        . "'"$MOCK_INSTALL"'/platform/hooks.sh"
        eval "'"$call"'"
        echo "dest:${HELIX_LOG_DEST:-unset}"
    ' > "$BATS_TEST_TMPDIR/leak.out" 2>&1
    grep -q '^dest:unset$' "$BATS_TEST_TMPDIR/leak.out"
}

# The init script's HELIX_NO_SPLASH query, taken from the real script as one
# block (outer `if` at column zero, closing `fi` at column zero), so these
# tests run what ships.
init_no_splash_read() {
    awk '/^if \[ -z .*HELIX_NO_SPLASH.*; then$/ { printing = 1 }
         printing && /^fi$/ { print; exit }
         printing { print }' "$INIT_SCRIPT"
}

# The shipped early-splash gate condition, as a bare command whose exit
# status IS the branch decision (0 = splash starts).
init_early_splash_gate() {
    awk '/^ *if \[ -x "\$SPLASH" \] && .*HELIX_NO_SPLASH.*; then$/ { print; exit }' "$INIT_SCRIPT" \
        | sed -e 's/^ *if //' -e 's/; then$//'
}

@test "init's early-splash read resolves HELIX_NO_SPLASH in the launcher's order" {
    # Forge-X shape: the hook assigns HELIX_NO_SPLASH at file scope when
    # nothing else did. The gate must see the operator's env file ahead of
    # that default, the shell environment ahead of the file, and the hook's
    # default when the file is silent. The read asks the launcher itself
    # (--print-env NAME), so every spelling below is resolved by the parser
    # that ships, not by a second reader approximating it.
    read_block="$(init_no_splash_read)"
    # An empty extraction would make every assertion below vacuously green.
    [ -n "$read_block" ]
    printf '%s\n' "$read_block" > "$BATS_TEST_TMPDIR/no-splash-read.sh"
    gate="$(init_early_splash_gate)"
    [ -n "$gate" ]

    MOCK_INSTALL="$BATS_TEST_TMPDIR/helixscreen"
    mkdir -p "$MOCK_INSTALL/bin" "$MOCK_INSTALL/config" "$MOCK_INSTALL/platform"
    printf '#!/bin/sh\nexit 0\n' > "$MOCK_INSTALL/bin/helix-splash"
    chmod +x "$MOCK_INSTALL/bin/helix-splash"
    # The launcher resolves BIN_DIR from a helix-screen beside itself; the
    # stub records its argv so the test can also prove the query path never
    # execs the daemon.
    printf '#!/bin/sh\necho "$@" > "%s/helix_screen_args.txt"\nexit 0\n' \
        "$MOCK_INSTALL" > "$MOCK_INSTALL/bin/helix-screen"
    chmod +x "$MOCK_INSTALL/bin/helix-screen"
    cp "$LAUNCHER" "$MOCK_INSTALL/bin/helix-launcher.sh"
    rm -f "$MOCK_INSTALL/helix_screen_args.txt"
    cat > "$MOCK_INSTALL/platform/hooks.sh" << 'EOF'
if [ -z "${HELIX_NO_SPLASH}" ]; then
    HELIX_NO_SPLASH=0
fi
EOF
    printf 'HELIX_NO_SPLASH=1\n' > "$MOCK_INSTALL/config/helixscreen.env"

    resolve_without_env() {
        DAEMON_DIR="$MOCK_INSTALL" LAUNCHER="$MOCK_INSTALL/bin/helix-launcher.sh" sh -c '
            unset HELIX_NO_SPLASH
            . "'"$BATS_TEST_TMPDIR"'/no-splash-read.sh"
            . "'"$MOCK_INSTALL"'/platform/hooks.sh"
            echo "${HELIX_NO_SPLASH:-unset}"
        '
    }
    resolve_with_env() {
        HELIX_NO_SPLASH="$1" DAEMON_DIR="$MOCK_INSTALL" \
            LAUNCHER="$MOCK_INSTALL/bin/helix-launcher.sh" sh -c '
            . "'"$BATS_TEST_TMPDIR"'/no-splash-read.sh"
            . "'"$MOCK_INSTALL"'/platform/hooks.sh"
            echo "${HELIX_NO_SPLASH:-unset}"
        '
    }
    gate_branch() {
        # Exit status of the shipped gate condition after the same resolution.
        DAEMON_DIR="$MOCK_INSTALL" SPLASH="$MOCK_INSTALL/bin/helix-splash" \
            LAUNCHER="$MOCK_INSTALL/bin/helix-launcher.sh" sh -c '
            unset HELIX_NO_SPLASH
            . "'"$BATS_TEST_TMPDIR"'/no-splash-read.sh"
            . "'"$MOCK_INSTALL"'/platform/hooks.sh"
            '"$gate"'
        '
    }

    # Operator file beats the hook default: splash disabled, in the value the
    # gate compares and in the shipped gate's own branch.
    [ "$(resolve_without_env)" = "1" ]
    if gate_branch; then
        fail "shipped gate starts the splash despite the operator's file value"
    fi
    # Every spelling the launcher's parse accepts reads as 1 here too:
    # quoted, commented, and indented lines, which a cruder reader misses.
    for shape in "HELIX_NO_SPLASH='1'" \
                 'HELIX_NO_SPLASH="1" # operator note' \
                 '  HELIX_NO_SPLASH=1'; do
        printf '%s\n' "$shape" > "$MOCK_INSTALL/config/helixscreen.env"
        [ "$(resolve_without_env)" = "1" ] || fail "read misses the shape: $shape"
    done
    # First definition in the file wins, both directions.
    printf 'HELIX_NO_SPLASH=0\nHELIX_NO_SPLASH=1\n' > "$MOCK_INSTALL/config/helixscreen.env"
    [ "$(resolve_without_env)" = "0" ]
    printf 'HELIX_NO_SPLASH=1\nHELIX_NO_SPLASH=0\n' > "$MOCK_INSTALL/config/helixscreen.env"
    [ "$(resolve_without_env)" = "1" ]
    # Shell environment beats the file.
    [ "$(resolve_with_env 0)" = "0" ]
    # No file at all: the hook default is authoritative and the splash starts.
    rm "$MOCK_INSTALL/config/helixscreen.env"
    [ "$(resolve_without_env)" = "0" ]
    gate_branch || fail "shipped gate skips the splash with no file and the hook default"
    # No launcher to ask: the read degrades to the hook default, the same
    # environment the fallback install path execs the daemon in.
    mv "$MOCK_INSTALL/bin/helix-launcher.sh" "$MOCK_INSTALL/bin/helix-launcher.sh.aside"
    [ "$(resolve_without_env)" = "0" ]
    gate_branch || fail "shipped gate skips the splash with no launcher present"
    mv "$MOCK_INSTALL/bin/helix-launcher.sh.aside" "$MOCK_INSTALL/bin/helix-launcher.sh"
    # The query path never launched the daemon.
    [ ! -e "$MOCK_INSTALL/helix_screen_args.txt" ]
}

@test "e2e: through the init ordering, helixscreen.env outranks a hook default" {
    # Production shape: the init script sources the hooks, runs
    # platform_pre_start, then execs the launcher — which reads
    # helixscreen.env and runs the hook again itself. The operator pins only
    # HELIX_LOG_DEST in the file; HELIX_LOG_FILE stays silent so the hook's
    # default must be the value forwarded for it.
    mock_command_script "killall" 'exit 0'
    mock_command_script "systemctl" 'exit 0'
    mock_command_script "setterm" 'exit 0'

    MOCK_INSTALL="$BATS_TEST_TMPDIR/helixscreen"
    mkdir -p "$MOCK_INSTALL/bin" "$MOCK_INSTALL/config" "$BATS_TEST_TMPDIR/empty-proc"
    printf '#!/bin/sh\nfor arg in "$@"; do echo "$arg"; done > "%s/helix_screen_args.txt"\nexit 0\n' \
        "$MOCK_INSTALL" > "$MOCK_INSTALL/bin/helix-screen"
    chmod +x "$MOCK_INSTALL/bin/helix-screen"
    cp "$LAUNCHER" "$MOCK_INSTALL/bin/helix-launcher.sh"
    rm -f "$MOCK_INSTALL/helix_screen_args.txt"
    install_defaulting_hook

    cat > "$MOCK_INSTALL/config/helixscreen.env" << 'EOF'
HELIX_LOG_DEST=console
EOF

    call="$(init_pre_start_call)"
    [ -n "$call" ]

    HELIX_PROC_ROOT="$BATS_TEST_TMPDIR/empty-proc" sh -c '
        unset HELIX_LOG_DEST HELIX_LOG_FILE HELIX_DEBUG HELIX_LOG_LEVEL
        . "'"$MOCK_INSTALL"'/platform/hooks.sh"
        eval "'"$call"'"
        exec sh "'"$MOCK_INSTALL"'/bin/helix-launcher.sh"
    ' >/dev/null 2>&1 || true

    [ -s "$MOCK_INSTALL/helix_screen_args.txt" ]
    grep -q '^--log-dest=console$' "$MOCK_INSTALL/helix_screen_args.txt"
    grep -q '^--log-file=/hook/default.log$' "$MOCK_INSTALL/helix_screen_args.txt"
}

# =============================================================================
# The launcher's second platform_pre_start call.
#
# The init script fires the hook in a subshell for side effects only, so the
# launcher's call is the one whose environment reaches helix-screen — and it
# runs with the subshell's side effects already on disk: pidfiles, running
# daemons. Every export the hook supplies must survive that warm second call.
# An export placed below an already-running early return is supplied only by
# the init subshell and drops silently on every later call.
# =============================================================================

# One HELIX_* variable name per line that a platform_pre_start call supplies
# in this environment. Daemon starts are staged, never executed (see the gate
# below for the staging contract); takes the hook file and the pidfile path
# the hook's daemon start would use.
capture_pre_start_env() {
    HELIX_REMOTE_SCREEN_PID="$2" sh -c '
        . "'"$1"'"
        mkdir() { :; }
        touch() { :; }
        _remote_screen_enabled() { return 0; }
        platform_pre_start >/dev/null 2>&1
        env | sed -n "s/^\(HELIX_[A-Z0-9_]*\)=.*/\1/p" | sort -u
    '
}

@test "a warm second platform_pre_start call re-supplies every export of the first" {
    # Two fresh shells share the filesystem (pidfile a first-call daemon start
    # leaves behind) but not the environment; the second call must set every
    # HELIX_* variable the first did. The Snapmaker remote-screen arm is
    # forced on (toggle override + an fbdev-only fb-http stub): it is the one
    # conditional export in the fleet.
    #
    # Hermetic by construction: every hook in the fleet runs here, so every
    # host-touching command a hook can reach on its warm path is intercepted.
    # start-stop-daemon only records the pidfile a real start would leave,
    # pointing at a live PID (this test's own); pidof reports nothing, so
    # already-running guards take their warm branch; mkdir/touch are no-ops
    # (hooks' absolute-path writes cannot land); `ip` reports an addressed
    # wlan0, so the K2 WiFi restore branch sees a configured interface and
    # never spawns its killall/wpa_supplicant/udhcpc sequence. wpa_supplicant
    # and udhcpc are recording no-ops as a second layer: if any hook reaches
    # one anyway, the tripwire below fails the test. (Forge-X drives its
    # driver via absolute /sbin/insmod from device-only paths and gates on
    # interface presence, so it cannot load anything on a dev machine.)
    # killall/pkill stay with the suite sandbox: a hook reaching one is a
    # suite failure, not silent host damage.
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    fb_stub="$BATS_TEST_TMPDIR/fb-http"
    printf '    parser.add_argument("--fb", default="/dev/fb0")\n' > "$fb_stub"
    cat > "$BATS_TEST_TMPDIR/bin/pidof" << 'EOF'
#!/bin/sh
exit 1
EOF
    cat > "$BATS_TEST_TMPDIR/bin/ip" << 'EOF'
#!/bin/sh
echo "2: wlan0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500"
echo "    inet 192.0.2.10/24 brd 192.0.2.255 scope global wlan0"
exit 0
EOF
    cat > "$BATS_TEST_TMPDIR/bin/start-stop-daemon" << 'EOF'
#!/bin/sh
# Stand-in for `start -m`: record the pidfile a real daemon start would
# leave, pointing at the live PID passed in HELIX_TEST_LIVE_PID.
while [ $# -gt 0 ]; do
    case "$1" in
        -p) echo "$HELIX_TEST_LIVE_PID" > "$2"; shift 2 ;;
        *) shift ;;
    esac
done
exit 0
EOF
    for _cmd in wpa_supplicant udhcpc; do
        cat > "$BATS_TEST_TMPDIR/bin/$_cmd" << EOF
#!/bin/sh
echo "$_cmd \$*" >> "\$TOOL_CALL_LOG"
exit 0
EOF
    done
    chmod +x "$BATS_TEST_TMPDIR"/bin/*
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"
    export HELIX_TEST_LIVE_PID="$$"
    export HELIX_FB_HTTP="$fb_stub"
    export HELIX_SAVED_WPA="$BATS_TEST_TMPDIR/no-wpa.conf"
    export TOOL_CALL_LOG="$BATS_TEST_TMPDIR/tool-calls"

    supplied=0
    offenders=""
    for f in "$HOOKS_DIR"/hooks-*.sh; do
        base="$(basename "$f")"
        pidfile="$BATS_TEST_TMPDIR/${base}.pid"
        rm -f "$pidfile"

        cold="$(capture_pre_start_env "$f" "$pidfile")"
        warm="$(capture_pre_start_env "$f" "$pidfile")"
        while IFS= read -r var; do
            [ -n "$var" ] || continue
            supplied=$((supplied + 1))
            if ! printf '%s\n' "$warm" | grep -qx "$var"; then
                offenders="$offenders
  $base: \$$var set by the first call, dropped by the warm second call"
            fi
        done <<< "$cold"
    done
    # Anti-vacuous: the harness must actually capture hook exports. Four of
    # the counted names are harness-injected (LIVE_PID, FB_HTTP, SAVED_WPA,
    # REMOTE_SCREEN_PID), so the floor sits well above them.
    [ "$supplied" -gt 10 ] || {
        echo "harness captured only $supplied HELIX_* exports - it is not exercising the hooks"
        false
    }
    [ -z "$offenders" ] || {
        echo "These exports are not re-supplied by a warm second call:$offenders"
        echo "Hoist the export above the already-running early return: it is a mode declaration, not a start side effect."
        false
    }
    # Tripwire: the mocks above must never run. A hook reaching for a real
    # WiFi tool inside this harness would be a hermeticity break on whatever
    # machine runs the suite.
    if [ -s "$TOOL_CALL_LOG" ]; then
        echo "hooks reached WiFi tools inside the harness:"
        cat "$TOOL_CALL_LOG"
        false
    fi
}

@test "a warm remote-screen call re-exports the mode fb-http was started with" {
    # fb-http can be replaced between the init subshell's cold start and the
    # launcher's warm call (an upgrade swaps the binary in place). The warm
    # call must re-declare the mode the RUNNING fb-http was started with,
    # recorded beside the pidfile - not re-probe the new binary. A build that
    # gained --backend makes the probe return DRM flags, the fb0 export is
    # skipped, and the still-running fbdev fb-http reads an fb0 nothing
    # mirrors: a silently frozen remote screen.
    mkdir -p "$BATS_TEST_TMPDIR/bin"
    cat > "$BATS_TEST_TMPDIR/bin/pidof" << 'EOF'
#!/bin/sh
exit 1
EOF
    cat > "$BATS_TEST_TMPDIR/bin/start-stop-daemon" << 'EOF'
#!/bin/sh
# Stand-in for `start -m`: record the pidfile a real daemon start would
# leave, pointing at the live PID passed in HELIX_TEST_LIVE_PID.
while [ $# -gt 0 ]; do
    case "$1" in
        -p) echo "$HELIX_TEST_LIVE_PID" > "$2"; shift 2 ;;
        *) shift ;;
    esac
done
exit 0
EOF
    chmod +x "$BATS_TEST_TMPDIR"/bin/*
    fb_old="$BATS_TEST_TMPDIR/fb-http-old"
    fb_new="$BATS_TEST_TMPDIR/fb-http-new"
    printf '    parser.add_argument("--fb", default="/dev/fb0")\n' > "$fb_old"
    printf '    parser.add_argument("--backend", choices=["drm", "fbdev"])\n' > "$fb_new"

    f="$HOOKS_DIR/hooks-snapmaker-u1.sh"
    pidfile="$BATS_TEST_TMPDIR/u1-upgrade.pid"
    rm -f "$pidfile" "$pidfile.mode"

    export HELIX_TEST_LIVE_PID="$$"
    export HELIX_SAVED_WPA="$BATS_TEST_TMPDIR/no-wpa.conf"
    export TOOL_CALL_LOG="$BATS_TEST_TMPDIR/tool-calls"
    export PATH="$BATS_TEST_TMPDIR/bin:$PATH"

    # fbdev start, then the binary gains --backend before the warm call.
    cold="$(HELIX_FB_HTTP="$fb_old" capture_pre_start_env "$f" "$pidfile")"
    printf '%s\n' "$cold" | grep -qx 'HELIX_REMOTE_SCREEN_FB0'
    [ "$(cat "$pidfile.mode" 2>/dev/null)" = "fbdev" ]
    warm="$(HELIX_FB_HTTP="$fb_new" capture_pre_start_env "$f" "$pidfile")"
    printf '%s\n' "$warm" | grep -qx 'HELIX_REMOTE_SCREEN_FB0' || \
        fail "warm call dropped the fb0 mirror after fb-http gained --backend"

    # The DRM leg stays mirror-free across its own warm call.
    rm -f "$pidfile" "$pidfile.mode"
    cold="$(HELIX_FB_HTTP="$fb_new" capture_pre_start_env "$f" "$pidfile")"
    if printf '%s\n' "$cold" | grep -qx 'HELIX_REMOTE_SCREEN_FB0'; then
        fail "a DRM start exports the fb0 mirror"
    fi
    [ "$(cat "$pidfile.mode" 2>/dev/null)" = "drm" ]
    warm="$(HELIX_FB_HTTP="$fb_new" capture_pre_start_env "$f" "$pidfile")"
    if printf '%s\n' "$warm" | grep -qx 'HELIX_REMOTE_SCREEN_FB0'; then
        fail "a warm DRM call exports the fb0 mirror"
    fi

    # stop cleans the record beside the pidfile.
    HELIX_REMOTE_SCREEN_PID="$pidfile" HELIX_FB_HTTP="$fb_new" \
        sh -c '. "'"$f"'"; stop_remote_screen' >/dev/null 2>&1
    [ ! -e "$pidfile" ]
    [ ! -e "$pidfile.mode" ]
}
