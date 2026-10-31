#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Pins the third-party header ABI stamp (mk/patches.mk).
#
# Our objects reach the patched LVGL and libhv headers through -isystem, and
# DEPFLAGS is -MMD, which leaves system headers out of the .d files. Two libhv
# patches add members to hv::TcpClientEventLoopTmpl and hv::WebSocketClient, so
# a change to those headers moves every member after the insertion point while
# every .o in the tree still looks up to date. lib/ is shared between worktrees
# and build/ is not, so the change can also land while a build is running.
#
# Nothing downstream reports the resulting disagreement: the link succeeds, and
# a std::mutex read at the wrong offset locks bytes that were never a mutex -
# which glibc accepts and macOS libc++ rejects with EINVAL. The stamp and the
# link guard are the only things standing between that and a green suite, and
# either is one word in a prerequisite list away from being dropped.

load helpers

setup() {
    cd "$(scratch_tree)" || return 1
}

# Every makefile that compiles a translation unit against these headers. The
# five beyond the app and test trees build their own binaries and archives -
# helix-splash, helix-watchdog, libhelix-display.a, the fbdev variant objects
# and the tools - out of the same sources, so a member offset they disagree
# about is the same defect in a binary that ships.
ABI_MAKEFILES="mk/rules.mk mk/tests.mk mk/bluetooth.mk mk/tools.mk \
mk/display-lib.mk mk/splash.mk mk/watchdog.mk mk/pi-dual-link.mk"

# Self-contained third-party translation units: they include neither our
# headers nor the patched LVGL and libhv types, so no member offset they see
# can disagree with ours.
ABI_EXEMPT='dns_resolv|quirc|minilzo|catch_amalgamated|demos'

# Target lines of every rule in those files whose recipe compiles a TU.
compile_rule_targets() {
    awk '
        /^[^\t #]/ && /:/ { target = $0 }
        /-c \$</ && target != "" { print target; target = "" }
    ' $ABI_MAKEFILES
}

@test "every rule compiling a translation unit depends on ABI_STAMP" {
    local missing=""
    while IFS= read -r rule; do
        [[ "$rule" =~ $ABI_EXEMPT ]] && continue
        [[ "$rule" == *'$(ABI_STAMP)'* ]] && continue
        missing="${missing}${rule}"$'\n'
    done < <(compile_rule_targets)
    [ -z "$missing" ] || {
        echo "Compile rules missing \$(ABI_STAMP):"
        echo "$missing"
        false
    }
}

# A stamp prerequisite decides that make reruns the compile; it does not decide
# what the compiler produces. ccache stands between the two, and its depend mode
# keys an entry on the source plus the paths -MMD lists - the set that omits
# these -isystem headers. Without the hash on the command line the rerun resolves
# to the entry built against the previous layout, and the stamp buys nothing.
# Every rule pinned above draws its flags from one of these four variables.
@test "the ABI hash is on the compiler command line, not only in the dependency graph" {
    local db missing=""
    db="$(make_db)"
    for var in CFLAGS CXXFLAGS SUBMODULE_CFLAGS SUBMODULE_CXXFLAGS; do
        printf '%s\n' "$db" | grep -m1 "^${var} :\{0,1\}= " | grep -q -- '-DHELIX_TP_ABI=' \
            || missing="${missing}${var} "
    done
    [ -z "$missing" ] || { echo "flag sets carrying no -DHELIX_TP_ABI: $missing"; false; }
}

# cksum prints a checksum AND a byte count, so the value has to be folded into a
# single token or the second field arrives as a stray source-file argument.
@test "the define is one token and tracks the tracked headers' content" {
    local work="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-define"
    rm -rf "$work" && mkdir -p "$work"

    define_for() {
        printf '%s' "$1" > "$work/tracked.h"
        make_db BUILD_DIR="$work" ABI_HEADERS="$work/tracked.h" \
            | grep -m1 '^ABI_DEFINE :\{0,1\}= ' | cut -d= -f2-
    }

    local before after problems=""
    before="$(define_for 'struct S { int a; };')"
    after="$(define_for 'struct S { int b; int a; };')"

    [ -n "$before" ] || problems="ABI_DEFINE is empty; "
    [ "$before" != "$after" ] || problems="${problems}changing a tracked header left the define at '$before'; "
    [ "$(printf '%s' "$before" | wc -w)" -eq 1 ] \
        || problems="${problems}define is not a single token: '$before'"

    [ -z "$problems" ] || { echo "$problems"; false; }
}

@test "both link recipes call check_abi_unchanged" {
    grep -q 'check_abi_unchanged' mk/rules.mk
    grep -q 'check_abi_unchanged' mk/tests.mk
}

@test "ABI_HEADERS covers the layout-bearing libhv headers at their source paths" {
    local line missing=""
    line="$(make_db | grep -m1 '^ABI_HEADERS :=')"
    for header in lib/libhv/evpp/TcpClient.h \
                  lib/libhv/http/client/WebSocketClient.h; do
        case "$line" in
            *"$header"*) ;;
            *) missing="${missing}${header} " ;;
        esac
    done
    [ -z "$missing" ] || { echo "ABI_HEADERS does not cover: $missing"; false; }
}

# A fixture holding the real ABI_HEADERS definition and the lists it is built
# from, lifted out of mk/patches.mk, over a fake libhv/lvgl/lua tree. Each
# definition runs from its `NAME :=` line to the first line without a trailing
# backslash.
write_hash_fixture() {
    local work="$1"
    rm -rf "$work" && mkdir -p "$work"

    awk '
        /^(LVGL_PATCHED_FILES|LIBHV_PATCHED_FILES|LUA_PATCHED_FILES|ABI_HEADERS) :=/ { r = 1 }
        /^ABI_HASH_CMD = / { print; next }
        r { print; if ($0 !~ /\\$/) r = 0 }
    ' mk/patches.mk > "$work/abi.mk"
    grep -q '^ABI_HEADERS :=' "$work/abi.mk" || fail "ABI_HEADERS not found in mk/patches.mk"
    grep -q '^ABI_HASH_CMD = ' "$work/abi.mk" || fail "ABI_HASH_CMD not found in mk/patches.mk"

    cat > "$work/Makefile" <<'MK'
ifneq ($(MAKELEVEL),0)
$(error the ABI hash fixture must not run inside another make)
endif
LVGL_DIR := lvgl
LIBHV_DIR := libhv
LUA_DIR := lua
PATCH_FILES :=
include abi.mk
print-hash:
	@$(ABI_HASH_CMD)
MK

    # Every tracked source header gets distinct content, so the hash notices
    # any one of them going missing. include/hv/ is left to the case: it is
    # what libhv's build installs.
    local f
    for f in $(make -s -C "$work" -f - print-headers <<'MK'
include Makefile
print-headers:
	@echo $(ABI_HEADERS)
MK
    ); do
        [[ "$f" == */include/hv/* ]] && continue
        mkdir -p "$work/$(dirname "$f")"
        printf 'header %s\n' "$f" > "$work/$f"
    done
}

# libhv's own build installs include/hv/ copies of its headers, and only that
# build creates them, so a cold tree parses without them and a built tree with
# them. A hash that read them would differ between the first make and the
# second, and the -D it feeds would recompile every object a second time.
@test "the ABI hash is the same whether or not libhv's installed include/hv exists" {
    local work="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-cold"
    [[ "$work" == "$BATS_TEST_TMPDIR"/* ]] || fail "fixture escaped BATS_TEST_TMPDIR: $work"
    export JOBPOOL=0 MAKEFLAGS= MAKELEVEL=
    write_hash_fixture "$work"

    local cold built
    cold="$(make -s -C "$work" print-hash)"

    # What libhv's install leaves behind: a copy of every libhv header.
    mkdir -p "$work/libhv/include/hv"
    find "$work/libhv" -path "$work/libhv/include" -prune -o -name '*.h' -print \
        | while read -r h; do cp "$h" "$work/libhv/include/hv/"; done
    built="$(make -s -C "$work" print-hash)"

    [ -n "$cold" ] || fail "empty hash"
    [ "$cold" = "$built" ] || fail "hash moved from '$cold' to '$built' once include/hv existed"
}

# A stamp left over from an earlier header revision is what lets stale objects
# look current, so the parse-time hook has to overwrite one that disagrees.
#
# Driven against a private BUILD_DIR. Corrupting the real build/.thirdparty-abi
# would leave it repaired but with a fresh mtime, and every object in the tree
# depends on it - the gate would hand whoever ran it a full rebuild.
@test "a stamp that disagrees with the headers is rewritten at parse time" {
    local sandbox="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-stamp"
    rm -rf "$sandbox" && mkdir -p "$sandbox"
    printf 'not-a-hash' > "$sandbox/.thirdparty-abi"

    local hash problems=""
    hash="$(make_db BUILD_DIR="$sandbox" | grep -m1 '^ABI_HASH :=' | cut -d' ' -f3-)"
    [ -n "$hash" ] || problems="ABI_HASH is empty; "
    [ "$(cat "$sandbox/.thirdparty-abi")" = "$hash" ] \
        || problems="${problems}stamp still reads '$(cat "$sandbox/.thirdparty-abi")', want '$hash'"
    [ -z "$problems" ] || { echo "$problems"; false; }
}

# The other half of that: a build re-enters this makefile repeatedly, through
# the -j fixup, libhv, SDL2 and the translations. A descendant that re-stamped
# would record a header a peer rewrote mid-build as the value the objects were
# compiled against, and the link guard would then pass. The cross-compile
# re-invocation is the one that must still stamp, and it is told apart by the
# BUILD_DIR it is building, not by how deep it is.
@test "a make inside another build re-stamps only for a different BUILD_DIR" {
    local sandbox="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-nested"
    rm -rf "$sandbox" && mkdir -p "$sandbox"

    local problems=""

    printf 'not-a-hash' > "$sandbox/.thirdparty-abi"
    ABI_STAMPED_FOR="$sandbox" make_db BUILD_DIR="$sandbox" >/dev/null
    [ "$(cat "$sandbox/.thirdparty-abi")" = "not-a-hash" ] \
        || problems="a make inside this BUILD_DIR's own build re-stamped it; "

    printf 'not-a-hash' > "$sandbox/.thirdparty-abi"
    ABI_STAMPED_FOR="$sandbox/other" make_db BUILD_DIR="$sandbox" >/dev/null
    [ "$(cat "$sandbox/.thirdparty-abi")" != "not-a-hash" ] \
        || problems="${problems}a make starting a different BUILD_DIR left the stale stamp"

    [ -z "$problems" ] || { echo "$problems"; false; }
}

# A fixture tree carrying the real definitions of the guard and the recorder,
# lifted out of mk/patches.mk. Copies would let the two drift: gutting either
# recipe there has to fail here.
#
#   steady    the headers have not moved since the stamp was written
#   flipped   a header changes under the running build and nothing re-records
#   recorded  a header changes and the build records the new value, the shape a
#             clean checkout takes when it applies its own patches
write_guard_fixture() {
    local work="$1"
    rm -rf "$work" && mkdir -p "$work"

    sed -n '/^define check_abi_unchanged$/,/^endef$/p' mk/patches.mk > "$work/guard.mk"
    sed -n '/^define record_abi_stamp$/,/^endef$/p' mk/patches.mk >> "$work/guard.mk"
    [ -s "$work/guard.mk" ] || fail "check_abi_unchanged / record_abi_stamp not found in mk/patches.mk"

    printf 'struct S { int a; };' > "$work/tracked.h"
    cat > "$work/Makefile" <<'MK'
Q :=
RED :=
BOLD :=
RESET :=
YELLOW :=
BUILD_DIR := .
ABI_HEADERS := tracked.h
ABI_STAMP := thirdparty-abi
ABI_HASH_CMD = cat /dev/null $(ABI_HEADERS) 2>/dev/null | cksum
$(shell $(ABI_HASH_CMD) > $(ABI_STAMP))
include guard.mk
steady:
	$(call check_abi_unchanged)
	@echo LINKED
flipped:
	@printf 'struct S { int b; int a; };' > tracked.h
	$(call check_abi_unchanged)
	@echo LINKED
recorded:
	@printf 'struct S { int b; int a; };' > tracked.h
	$(call record_abi_stamp)
	$(call check_abi_unchanged)
	@echo LINKED
MK
}

@test "check_abi_unchanged fails when a tracked header changes mid-build" {
    local work="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-guard"
    write_guard_fixture "$work"

    local problems=""

    run make -C "$work" steady
    [ "$status" -eq 0 ] || problems="untouched headers were rejected; "
    [[ "$output" == *LINKED* ]] || problems="${problems}steady link did not run; "

    run make -C "$work" flipped
    [ "$status" -ne 0 ] || problems="${problems}changed headers were accepted; "
    [[ "$output" != *LINKED* ]] || problems="${problems}link ran anyway; "
    [[ "$output" == *"changed while this build was running"* ]] \
        || problems="${problems}no explanation for the failure"

    [ -z "$problems" ] || { echo "$problems"; false; }
}

# A checkout that has not been patched yet holds upstream's headers while make
# is parsing, and ours by the time the first object is compiled. Comparing the
# link against the parse-time value rejects every clean checkout there is, so
# the reference has to be the stamp the build re-records.
@test "check_abi_unchanged follows the recorded stamp, not the parse-time value" {
    local work="${BATS_TEST_TMPDIR:-$(mktemp -d)}/abi-recorded"
    write_guard_fixture "$work"

    local problems=""
    run make -C "$work" recorded
    [ "$status" -eq 0 ] || problems="a re-recorded header set was rejected; "
    [[ "$output" == *LINKED* ]] || problems="${problems}the link did not run"

    [ -z "$problems" ] || { echo "$problems"; echo "$output"; false; }
}

# Which is only true while the recipe that moves these headers inside the build
# still records it: the patch step rewrites them. Drop the call and every clean
# checkout fails at the link again.
@test "the recipe that changes the tracked headers re-records the stamp" {
    local problems=""

    # The range runs from the target line to the recipe's own last line, so a
    # call that moved out of the recipe and into a comment or a neighbouring
    # rule reads as missing, which is what it would be.
    awk '/^\$\(PATCHES_STAMP\):/{r=1} r{print} r && index($0,"\t@touch $@")==1{exit}' mk/patches.mk \
        | grep -q 'call record_abi_stamp' \
        || problems="the patch-stamp recipe does not re-record the ABI stamp"

    [ -z "$problems" ] || { echo "$problems"; false; }
}
