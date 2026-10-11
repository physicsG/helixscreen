#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test-host-run.sh mirrors the working tree to the test host (scripts/test-host-run.sh).
#
# A plain run copies the tree as it is on disk - uncommitted edits and
# untracked files included, ignored files and build output excluded - into a
# per-tree mirror on the test host, and runs the job there. Each mirror has its own
# lock, so two trees run at once while two runs of one tree queue.
#
# ssh is stubbed so its command runs here: rsync's remote half, the lock
# holder and the job heredoc all execute against a sandbox directory that
# stands in for the test host's mirror root. docker is stubbed to record, so no build
# runs. git is real: each test builds a small repository to sync.

TEST_HOST_RUN="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)/scripts/test-host-run.sh"

setup() {
    load helpers
    export HELIX_TEST_HOST=testhost.invalid
    export HELIX_TEST_LOCK_DIR="$BATS_TEST_TMPDIR/locks"
    export HELIX_TEST_TREES_HOST="$BATS_TEST_TMPDIR/host-trees"
    export TMPDIR="$BATS_TEST_TMPDIR"
    export MOCK_DOCKER_LOG="$BATS_TEST_TMPDIR/docker.log"
    export HELIX_TEST_JOBPOOL="$BATS_TEST_TMPDIR/no-jobpool"
    export HELIX_TEST_ARC_PARAM="$BATS_TEST_TMPDIR/no-zfs/zfs_arc_max"
    export HELIX_TEST_ARC_SYS_FREE="$BATS_TEST_TMPDIR/no-zfs/zfs_arc_sys_free"
    export HELIX_TEST_ARC_MARK="$BATS_TEST_TMPDIR/arc-mark"
    export HELIX_TEST_TREES=/work/trees
    export HELIX_TEST_ORPHAN_POLL_SECS=0
    mkdir -p "$HELIX_TEST_LOCK_DIR"
    REAL_RSYNC=$(command -v rsync)
    export REAL_RSYNC

    # ssh [-o opt]... <host> <command...>: run the command here through a
    # shell, the way sshd hands it to the remote login shell.
    mock_command_script ssh '
while [ $# -gt 0 ]; do
    case "$1" in -o) shift 2 ;; -*) shift ;; *) break ;; esac
done
shift
exec bash -c "$*"'
    mock_command_script sudo 'shift; exec "$@"'
    mock_command_script docker '
case "$1" in
    ps) echo helix-test ;;
    start) exit 0 ;;
    inspect) exit 0 ;;
    exec)
        case "$*" in
            *pgrep*)
                # MOCK_PGREP_HITS: the first poll sees a live make, and
                # records what the mirror held at that moment.
                if [ -n "${MOCK_PGREP_HITS:-}" ]; then
                    n=$(cat "$MOCK_PGREP_HITS" 2>/dev/null || echo 0)
                    n=$((n + 1)); echo "$n" > "$MOCK_PGREP_HITS"
                    if [ "$n" -eq 1 ]; then
                        cat "$MOCK_MIRROR_PROBE" > "$MOCK_PGREP_SAW" 2>/dev/null || echo absent > "$MOCK_PGREP_SAW"
                        if [ -n "${MOCK_MIRROR_STALE:-}" ] && [ -e "$MOCK_MIRROR_STALE" ]; then
                            echo stale-present >> "$MOCK_PGREP_SAW"
                        fi
                        exit 0
                    fi
                fi
                # MOCK_RUN_PROBE: run the probe script itself, here.
                if [ -n "${MOCK_RUN_PROBE:-}" ]; then
                    shift; while [ "$1" != bash ]; do shift; done
                    exec bash -c "$3"
                fi
                exit 1 ;;
            *) echo "[docker-exec] $*" >> "$MOCK_DOCKER_LOG" ;;
        esac ;;
    *) echo "unhandled docker call: $*" >&2; exit 1 ;;
esac
exit 0'
}

# A repository named <name> under the sandbox, with one committed file, an
# ignore rule for build/ and *.ignored, and a commit identity.
make_tree() { # <name>
    local t="$BATS_TEST_TMPDIR/$1"
    mkdir -p "$t"
    git -C "$t" init -q -b main
    git -C "$t" config user.email t@example.com
    git -C "$t" config user.name tester
    printf 'build/\n*.ignored\n' > "$t/.gitignore"
    echo one > "$t/tracked.txt"
    git -C "$t" add .gitignore tracked.txt
    git -C "$t" commit -qm base --no-verify
    echo "$t"
}

mirror_of() { echo "$HELIX_TEST_TREES_HOST/$1"; }

@test "an uncommitted edit and an untracked file reach the mirror; ignored files and build/ do not" {
    local t m
    t=$(make_tree tree-a)
    m=$(mirror_of tree-a)
    echo edited > "$t/tracked.txt"
    echo new > "$t/untracked.txt"
    echo secret > "$t/local.ignored"
    mkdir -p "$t/build"; echo obj > "$t/build/thing.o"

    cd "$t"
    run "$TEST_HOST_RUN" test '[x]'
    [ "$status" -eq 0 ]
    [ "$(cat "$m/tracked.txt")" = edited ]
    [ "$(cat "$m/untracked.txt")" = new ]
    [ ! -e "$m/local.ignored" ]
    [ ! -e "$m/build" ]
    # The job runs in the mirror, not in the shared commit checkout.
    grep -qF -- "-w /work/trees/tree-a " "$MOCK_DOCKER_LOG"
    refute_grep "git reset" "$MOCK_DOCKER_LOG"
}

@test "a file deleted locally is gone from the mirror after the next sync" {
    local t m
    t=$(make_tree tree-a)
    m=$(mirror_of tree-a)
    echo a > "$t/committed-delete.txt"
    echo b > "$t/worktree-delete.txt"
    echo c > "$t/untracked-delete.txt"
    git -C "$t" add committed-delete.txt worktree-delete.txt
    git -C "$t" commit -qm more --no-verify
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    [ -e "$m/committed-delete.txt" ] && [ -e "$m/worktree-delete.txt" ] && [ -e "$m/untracked-delete.txt" ]
    # Build output the job wrote is the mirror's own, never in the file list.
    mkdir -p "$m/build"; echo obj > "$m/build/keep.o"

    git -C "$t" rm -q committed-delete.txt
    git -C "$t" commit -qm drop --no-verify
    rm "$t/worktree-delete.txt" "$t/untracked-delete.txt"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    [ ! -e "$m/committed-delete.txt" ]
    [ ! -e "$m/worktree-delete.txt" ]
    [ ! -e "$m/untracked-delete.txt" ]
    [ -e "$m/tracked.txt" ]
    [ -e "$m/build/keep.o" ]
}

# A submodule at lib/<name> whose checkout lives elsewhere, reached through a
# symlink the way setup-worktree.sh shares lib/ with the main tree.
add_symlinked_submodule() { # <tree> <name> [private]
    local t="$1" name="$2" src="$BATS_TEST_TMPDIR/src-$2"
    git init -q -b main "$src"
    echo "$name source" > "$src/source.c"
    git -C "$src" add source.c
    git -C "$src" -c user.email=t@example.com -c user.name=t commit -qm sub --no-verify
    git -C "$t" -c protocol.file.allow=always submodule add -q "$src" "lib/$name"
    git -C "$t" commit -qm "add $name" --no-verify
    # A private checkout, as setup-worktree.sh gives lvgl, libhv, lua and helix-xml.
    [ "${3:-}" != private ] || return 0
    rm -rf "${t:?}/lib/$name"
    git clone -q "$src" "$BATS_TEST_TMPDIR/shared-$name"
    ln -s "$BATS_TEST_TMPDIR/shared-$name" "$t/lib/$name"
}

@test "a symlinked lib/ submodule syncs as the target's files, not a link" {
    local t m
    t=$(make_tree tree-a)
    m=$(mirror_of tree-a)
    add_symlinked_submodule "$t" lvgl
    add_symlinked_submodule "$t" wpa_supplicant
    # A patch-created file in a patched submodule is part of the source; build
    # output in a shared one belongs to the local toolchain.
    echo created > "$BATS_TEST_TMPDIR/shared-lvgl/patch-created.h"
    echo arm > "$BATS_TEST_TMPDIR/shared-wpa_supplicant/libwpa_client.a"
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    [ -d "$m/lib/lvgl" ] && [ ! -L "$m/lib/lvgl" ]
    [ "$(cat "$m/lib/lvgl/source.c")" = "lvgl source" ]
    [ "$(cat "$m/lib/lvgl/patch-created.h")" = created ]
    [ -f "$m/lib/wpa_supplicant/source.c" ]
    [ ! -e "$m/lib/wpa_supplicant/libwpa_client.a" ]
}

wait_for_file() { # <path>
    local _
    for _ in $(seq 1 200); do [ -e "$1" ] && return 0; sleep 0.05; done
    return 1
}

wait_for_line() { # <substring> <file>
    local _
    for _ in $(seq 1 200); do grep -qF "$1" "$2" 2>/dev/null && return 0; sleep 0.05; done
    return 1
}

hold_lock() { # <lock file> <ready marker>: prints the holder pid
    (
        exec 9>>"$1"
        flock 9
        printf 'held by pid %s: sweep 0123456 since 2026-10-08 12:00:00\n' "$$" >&9
        touch "$2"
        sleep 30 9>&-
    ) >/dev/null 2>&1 &
    echo $!
}

@test "two trees hold different locks and both proceed; two runs of one tree queue" {
    local a b holder out runner
    a=$(make_tree tree-a)
    b=$(make_tree tree-b)
    holder=$(hold_lock "$HELIX_TEST_LOCK_DIR/helix-test-host-run.tree-tree-a.lock" "$BATS_TEST_TMPDIR/ready")
    wait_for_file "$BATS_TEST_TMPDIR/ready"

    # tree-a's lock is held: tree-b runs straight through.
    cd "$b"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    lacks "busy:" "$output"
    grep -q "held by pid [0-9]*: test " "$HELIX_TEST_LOCK_DIR/helix-test-host-run.tree-tree-b.lock"

    # A run of tree-a waits for it, names the holder, and has synced nothing.
    out="$BATS_TEST_TMPDIR/waiting.log"
    cd "$a"
    "$TEST_HOST_RUN" test >"$out" 2>&1 &
    runner=$!
    wait_for_line "busy:" "$out"
    grep -q "busy: held by pid.*: sweep 0123456 since " "$out"
    [ ! -e "$(mirror_of tree-a)/tracked.txt" ]

    kill "$holder"
    wait "$holder" 2>/dev/null || true
    wait "$runner"
    grep -q "free; continuing" "$out"
    [ -e "$(mirror_of tree-a)/tracked.txt" ]
}

@test "with no jobpool on the test host, runs of different trees take turns" {
    # -j is sized from MemAvailable at start, which only holds for one run.
    local b holder out runner
    b=$(make_tree tree-b)
    holder=$(hold_lock "$HELIX_TEST_LOCK_DIR/helix-test-host-run.global.lock" "$BATS_TEST_TMPDIR/ready")
    wait_for_file "$BATS_TEST_TMPDIR/ready"
    out="$BATS_TEST_TMPDIR/waiting.log"
    cd "$b"
    "$TEST_HOST_RUN" test >"$out" 2>&1 &
    runner=$!
    wait_for_line "no jobpool on" "$out"
    refute_grep "MemAvailable" "$out"
    kill "$holder"
    wait "$holder" 2>/dev/null || true
    wait "$runner"
    grep -q "MemAvailable" "$out"
}

@test "the provenance line and log name carry -dirty and the diff hash only when dirty" {
    local t sha short h1 h2
    t=$(make_tree tree-a)
    cd "$t"
    sha=$(git rev-parse HEAD)
    short=$(git rev-parse --short HEAD)

    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "HEAD $sha + clean" "$output"
    [ -f "$TMPDIR/test-host-test-tree-a-$short.log" ]
    lacks "dirty" "$output"

    echo edited > tracked.txt
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    h1=$(printf '%s\n' "$output" | sed -n 's/.*HEAD [0-9a-f]* + dirty \([0-9a-f]*\), 0 untracked.*/\1/p' | head -1)
    [ -n "$h1" ]
    [ -f "$TMPDIR/test-host-test-tree-a-$short-dirty-$h1.log" ]

    # Another edit is another hash; an untracked file is counted and hashed.
    echo new > untracked.txt
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    h2=$(printf '%s\n' "$output" | sed -n 's/.*HEAD [0-9a-f]* + dirty \([0-9a-f]*\), 1 untracked.*/\1/p' | head -1)
    [ -n "$h2" ]
    [ "$h1" != "$h2" ]
}

@test "--commit on an unpushed commit still refuses" {
    local t
    t=$(make_tree tree-a)
    cd "$t"
    run "$TEST_HOST_RUN" --commit test
    [ "$status" -eq 1 ]
    contains "is not on any remote branch" "$output"
    [ ! -e "$MOCK_DOCKER_LOG" ]
    [ ! -e "$(mirror_of tree-a)" ]
}

make_mirror() { # <name> <age in days>
    mkdir -p "$HELIX_TEST_TREES_HOST/$1"
    touch -d "$2 days ago" "$HELIX_TEST_TREES_HOST/$1/.helix-mirror-files"
}

@test "--prune removes an old mirror, keeps a fresh one and one in use" {
    make_mirror old 20
    make_mirror fresh 1
    make_mirror busy 20
    local holder
    holder=$(hold_lock "$HELIX_TEST_LOCK_DIR/helix-test-host-run.tree-busy.lock" "$BATS_TEST_TMPDIR/ready")
    wait_for_file "$BATS_TEST_TMPDIR/ready"

    run "$TEST_HOST_RUN" --prune
    kill "$holder"; wait "$holder" 2>/dev/null || true
    [ "$status" -eq 0 ]
    [ ! -e "$HELIX_TEST_TREES_HOST/old" ]
    [ -d "$HELIX_TEST_TREES_HOST/fresh" ]
    [ -d "$HELIX_TEST_TREES_HOST/busy" ]
    contains "removed old" "$output"

    # DAYS is honoured: a 1-day-old mirror goes at --prune 0.
    run "$TEST_HOST_RUN" --prune 0
    [ "$status" -eq 0 ]
    [ ! -e "$HELIX_TEST_TREES_HOST/fresh" ]
}

@test "--drop removes one tree's mirror" {
    make_mirror gone 0
    make_mirror stays 0
    run "$TEST_HOST_RUN" --drop gone
    [ "$status" -eq 0 ]
    [ ! -e "$HELIX_TEST_TREES_HOST/gone" ]
    [ -d "$HELIX_TEST_TREES_HOST/stays" ]
}

# --- --probe: the automatic full-test-run default ---------------------------

@test "--probe: an unreachable test host means local" {
    mock_command_fail ssh
    run "$TEST_HOST_RUN" --probe
    [ "$status" -eq 1 ]
    contains "unreachable" "$output"
}

@test "--probe: a LAN round trip means the test host without measuring the sync" {
    mock_command ping "64 bytes from testhost: icmp_seq=1 ttl=64 time=0.137 ms"
    local t
    t=$(make_tree tree-a)
    cd "$t"
    run "$TEST_HOST_RUN" --probe
    [ "$status" -eq 0 ]
    contains "rtt 0.137 ms" "$output"
    contains "sweep runs on testhost.invalid" "$output"
    lacks "estimated sync" "$output"
}

@test "--probe: a slow link with a cold mirror means local, a warm one means the test host" {
    mock_command ping "64 bytes from testhost: icmp_seq=1 ttl=64 time=40.5 ms"
    export HELIX_TEST_PROBE_BYTES_PER_SEC=1000   # pins the measured throughput
    local t
    t=$(make_tree tree-a)
    head -c 50000 /dev/urandom > "$t/big.bin"
    cd "$t"
    run "$TEST_HOST_RUN" --probe
    [ "$status" -eq 1 ]
    contains "estimated sync" "$output"

    # Warm: the mirror already holds every byte, so almost nothing would move.
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    run "$TEST_HOST_RUN" --probe
    [ "$status" -eq 0 ]
    contains "estimated sync" "$output"
}

# --- the orphan wait, and what may happen before it --------------------------

@test "mirror mode waits out an orphaned build before syncing or deleting anything" {
    # A run interrupted by Ctrl-C releases its lock while its make keeps
    # building in the mirror; the next run must not change files under it.
    local t m
    t=$(make_tree tree-a)
    m=$(mirror_of tree-a)
    echo doomed > "$t/doomed.txt"
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    echo second-sync > tracked.txt
    rm doomed.txt
    export MOCK_PGREP_HITS="$BATS_TEST_TMPDIR/hits" MOCK_PGREP_SAW="$BATS_TEST_TMPDIR/saw"
    export MOCK_MIRROR_PROBE="$m/tracked.txt" MOCK_MIRROR_STALE="$m/doomed.txt"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "orphaned build still running" "$output"
    # At the first poll the mirror still held the previous sync: neither the
    # new content nor the stale-file delete had happened yet.
    [ "$(cat "$MOCK_PGREP_SAW")" = "$(printf 'one\nstale-present')" ]
    [ "$(cat "$m/tracked.txt")" = second-sync ]
    [ ! -e "$m/doomed.txt" ]
}

@test "the orphan wait counts only makes whose cwd is inside this tree's mirror" {
    local t rundir other pid
    export HELIX_TEST_TREES="$BATS_TEST_TMPDIR/container-trees"
    export MOCK_RUN_PROBE=1
    rundir="$HELIX_TEST_TREES/tree-a"
    other="$HELIX_TEST_TREES/tree-ab"
    mkdir -p "$rundir/lib" "$other"
    # pgrep reports one make; its cwd decides whether it is ours.
    mock_command_script pgrep 'cat "$BATS_TEST_TMPDIR/make.pid"'
    t=$(make_tree tree-a)
    cd "$t"

    # Another tree's build, in a sibling whose name extends ours: not ours.
    (cd "$other" && exec sleep 30) & pid=$!
    echo "$pid" > "$BATS_TEST_TMPDIR/make.pid"
    run timeout 20 "$TEST_HOST_RUN" test
    kill "$pid"; wait "$pid" 2>/dev/null || true
    [ "$status" -eq 0 ]
    lacks "orphaned build" "$output"

    # A sub-make inside our mirror: waited out until it is gone.
    (cd "$rundir/lib" && exec sleep 30) & pid=$!
    echo "$pid" > "$BATS_TEST_TMPDIR/make.pid"
    "$TEST_HOST_RUN" test > "$BATS_TEST_TMPDIR/out" 2>&1 &
    local runner=$!
    for _ in $(seq 1 200); do grep -q "orphaned build" "$BATS_TEST_TMPDIR/out" && break; sleep 0.05; done
    grep -q "orphaned build" "$BATS_TEST_TMPDIR/out"
    kill "$pid"; wait "$pid" 2>/dev/null || true
    wait "$runner"
}

@test "a path that changes between file and directory syncs without wedging the mirror" {
    local t m
    t=$(make_tree tree-a)
    m=$(mirror_of tree-a)
    echo x > "$t/thing"; mkdir -p "$t/dir"; echo y > "$t/dir/a"
    git -C "$t" add thing dir/a; git -C "$t" commit -qm kinds --no-verify
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    # Output the job wrote there: the directory is not empty when it goes.
    echo obj > "$m/dir/out.o"

    git rm -q thing dir/a
    mkdir thing; echo y > thing/a
    echo z > dir
    git add thing/a dir; git commit -qm swapped --no-verify
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    [ "$(cat "$m/thing/a")" = y ]
    [ "$(cat "$m/dir")" = z ]
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]

    # A stale path that is a directory in the mirror, as an interrupted sync
    # leaves one, does not stop the run either.
    rm -rf "${m:?}/dir"; mkdir -p "$m/dir/sub"
    git rm -q dir; git commit -qm gone --no-verify
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
}

@test "a tree named global runs with no jobpool on the test host" {
    # The per-tree lock and the no-pool global lock are different files.
    local t
    t=$(make_tree global)
    cd "$t"
    run timeout 20 "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
}

@test "an untracked file vanishing while the tree is hashed does not kill the run" {
    local t real
    t=$(make_tree tree-a)
    echo gone > "$t/vanishing.txt"
    real=$(command -v sha1sum)
    # sha1sum of a file that is gone by then: an error, as from a real race.
    mock_command_script sha1sum '
for a in "$@"; do case "$a" in *vanishing.txt) echo "sha1sum: $a: No such file" >&2; exit 1 ;; esac; done
exec '"$real"' "$@"'
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "+ dirty " "$output"
}

@test "rsync's vanished-files exit is a sync, any other failure is not" {
    local t
    t=$(make_tree tree-a)
    # The local rsync only: the server half runs with --server.
    mock_command_script rsync '
"$REAL_RSYNC" "$@" || exit $?
case " $* " in *" --server "*|*" --dry-run "*) exit 0 ;; esac
case " $* " in *" --files-from="*) exit "${MOCK_RSYNC_RC:-0}" ;; esac'
    cd "$t"
    MOCK_RSYNC_RC=24 run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    MOCK_RSYNC_RC=23 run "$TEST_HOST_RUN" test
    [ "$status" -ne 0 ]
}

@test "an untracked file in helix-xml makes the tree dirty" {
    local t
    t=$(make_tree tree-a)
    add_symlinked_submodule "$t" helix-xml private
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "+ clean" "$output"
    echo new > lib/helix-xml/new_widget.c
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "+ dirty " "$output"
    contains "1 untracked" "$output"
}

@test "a tree or --drop name that is not a plain name is refused" {
    mkdir -p "$HELIX_TEST_TREES_HOST/keep"
    local bad
    for bad in '*' '..' '.' 'a b' 'x$(touch pwned)' 'a/b'; do
        run "$TEST_HOST_RUN" --drop "$bad"
        [ "$status" -eq 2 ]
    done
    [ -d "$HELIX_TEST_TREES_HOST/keep" ]
    [ ! -e pwned ] && [ ! -e "$HELIX_TEST_TREES_HOST/pwned" ]
    local t
    t=$(make_tree 'tree$x')
    cd "$t"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 2 ]
    [ ! -e "$MOCK_DOCKER_LOG" ]
}

@test "--commit is recognised after the job name too" {
    local t
    t=$(make_tree tree-a)
    cd "$t"
    run "$TEST_HOST_RUN" test '[x]' --commit
    [ "$status" -eq 1 ]
    contains "is not on any remote branch" "$output"
}

@test "with no test host configured every command refuses in one line and reaches nothing" {
    unset HELIX_TEST_HOST
    local t cmd
    t=$(make_tree tree-a)
    cd "$t"

    # The build-hosts file alone names one.
    echo "HELIX_TEST_HOST=testhost.invalid" > "$HELIX_BUILD_HOSTS_FILE"
    run "$TEST_HOST_RUN" test
    [ "$status" -eq 0 ]
    contains "testhost.invalid:helix-test" "$output"

    rm "$HELIX_BUILD_HOSTS_FILE"
    mock_command_script ssh 'touch "$BATS_TEST_TMPDIR/ssh-called"; exit 255'
    for cmd in "test" "--commit sweep" "--prune" "--drop tree-a" "--probe"; do
        # shellcheck disable=SC2086  # one argument per word
        run "$TEST_HOST_RUN" $cmd
        [ "$status" -eq 2 ]
        [ "${#lines[@]}" -eq 1 ]
        contains "HELIX_TEST_HOST is not set" "$output"
        contains "$HELIX_BUILD_HOSTS_FILE" "$output"
    done
    [ ! -e "$BATS_TEST_TMPDIR/ssh-called" ]
}
