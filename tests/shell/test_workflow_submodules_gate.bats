#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meta-tests for scripts/check_workflow_submodules.py - the gate over workflow
# jobs that depend on patched submodules.
#
# Two rules: a job running the bats suite must initialize submodules and apply
# patches (test_lvgl_event_code_gate.bats copies a file only a patched checkout
# has), and every `make apply-patches` command in any job must run with
# HELIX_PATCHES_FROM_CLEAN=1 - a CI runner is a fresh clone, where a patch that
# can neither apply nor reverse is dead, not shadowed, and must fail the job
# rather than warn into a build missing the patch. The flag counts on the make
# invocation or as workflow/job-level `env:`; a run string is judged command by
# command, so a flagged apply cannot bless an unflagged sibling.
#
# A make inside `docker run` is a third rule, because neither form reaches it:
# workflow env stops at the container boundary, so the docker command has to
# forward the flag with `-e` - and a no-value `-e` reads the runner's
# environment, which the env must therefore set.

load helpers

GATE="$BATS_TEST_DIRNAME/../../scripts/check_workflow_submodules.py"

make_fixture() {
    ROOT="${BATS_TEST_TMPDIR:-$(mktemp -d)}/wf"
    rm -rf "$ROOT"
    mkdir -p "$ROOT/.github/workflows" "$ROOT/.github/actions/init-submodules"
    printf 'name: init\nruns:\n  using: composite\n  steps: []\n' \
        > "$ROOT/.github/actions/init-submodules/action.yml"
    cp "$1" "$ROOT/.github/workflows/ci.yml"
    echo "$ROOT"
}

@test "a fully wired workflow passes" {
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_ok.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 0 ]
    grep -q 'init and patch their submodules' <<<"$output"
}

@test "an unflagged apply-patches step fails the gate" {
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_bare_apply.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q 'without HELIX_PATCHES_FROM_CLEAN=1' <<<"$output"
}

@test "workflow- or job-level env coverage passes the gate" {
    # The env form is the one that reaches the applies running inside a plain
    # `make -j` build step, which a flag pinned to an apply step never covers.
    # It must pass both at workflow level (build job) and job level (shell job,
    # including a `cd lib &&` prefix on the apply).
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_env_level_ok.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 0 ]
    grep -q 'init and patch their submodules' <<<"$output"
}

@test "a flagged apply cannot bless an unflagged sibling command" {
    # The run string is judged command by command: the first apply runs
    # unflagged even though the second carries the flag.
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_second_command_bare.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q 'without HELIX_PATCHES_FROM_CLEAN=1' <<<"$output"
    # The error names the bare command, not the flagged one.
    grep -q 'runs `make apply-patches`' <<<"$output"
    ! grep -q 'runs `make apply-patches HELIX_PATCHES_FROM_CLEAN=1`' <<<"$output"
}

@test "the from-clean rule holds for jobs that never touch the shell suite" {
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_bare_apply.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q "job 'build'" <<<"$output"
}

@test "a shell-suite job without submodule init still fails the gate" {
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_no_init.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q 'missing' <<<"$output"
}

@test "a docker-run make without the flag passthrough fails the gate" {
    # Workflow env does not cross the container boundary, so neither the env
    # form nor a flag on a sibling plain-make step can cover this build.
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_docker_bare.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q 'a make inside `docker run`' <<<"$output"
    grep -q 'container boundary' <<<"$output"
    # The command is joined across its continuations, so the error shows one
    # docker invocation, not a fragment of it.
    grep -q 'helixscreen/toolchain-k1' <<<"$output"
}

@test "a no-value -e passthrough fails without workflow or job env" {
    # `-e HELIX_PATCHES_FROM_CLEAN` takes its value from the runner's
    # environment; unset there, the container receives nothing.
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_docker_no_value.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 1 ]
    grep -q 'without a value' <<<"$output"
}

@test "a docker build with the passthrough and env passes; stats steps are not flagged" {
    # The same job's ccache -s docker step has no make in it and must not be
    # judged by the docker rule.
    ROOT=$(make_fixture "$BATS_TEST_DIRNAME/fixtures/workflow_submodules_docker_ok.yml")
    run bash -c "cd '$ROOT' && python3 '$GATE'"
    [ "$status" -eq 0 ]
    grep -q '0 job(s)' <<<"$output"
}
