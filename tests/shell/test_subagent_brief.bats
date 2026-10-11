#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# scripts/subagent-brief.sh is the SubagentStart hook that puts the shared-machine
# rules into every subagent's context.

load helpers

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
    HELIX_ADVISOR_HOSTS=$(hostname -s)
    export HELIX_ADVISOR_HOSTS
}

@test "on a host the rules are not about it adds nothing" {
    export HELIX_ADVISOR_HOSTS="some-other-box"
    run bash -c 'echo "{}" | scripts/subagent-brief.sh'
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "the block is a SubagentStart context entry" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh'
    [ "$status" -eq 0 ]
    [ "$(printf '%s' "$output" | jq -r '.hookSpecificOutput.hookEventName')" = "SubagentStart" ]
}

@test "the block carries the rules whose breach costs other sessions" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh | jq -r .hookSpecificOutput.additionalContext'
    contains "helix-claim check build:" "$output"
    contains "Never release a claim you did not take" "$output"
    contains 'Plain `make`, never a fixed -j' "$output"
    contains "jobpool status" "$output"
    contains '-j$(scripts/helix-claim jobs)' "$output"
    contains "test-host-run.sh" "$output"
    contains "foreground" "$output"
    contains "pkill" "$output"
}

@test "the block stays short enough not to crowd a brief" {
    run bash -c 'echo "{}" | scripts/subagent-brief.sh | jq -r .hookSpecificOutput.additionalContext | wc -c'
    [ "$output" -lt 1200 ]
}

@test "the wired hooks name scripts that exist" {
    run jq -r '.hooks.SubagentStart[].hooks[].command, (.hooks.PreToolUse[] | select(.matcher == "Bash") | .hooks[].command)' .claude/settings.json
    [ "$status" -eq 0 ]
    contains "scripts/subagent-brief.sh" "$output"
    contains "scripts/resource-advisor.sh" "$output"
    while read -r c; do
        c=${c#\"\$CLAUDE_PROJECT_DIR\"/}
        [ -x "${c%% *}" ]
    done <<< "$output"
}

@test "without jq it adds nothing and exits 0" {
    local bin
    bin=$(mktemp -d)
    local t
    for t in bash cat hostname; do ln -s "$(command -v "$t")" "$bin/$t"; done
    run env PATH="$bin" HELIX_ADVISOR_HOSTS="$HELIX_ADVISOR_HOSTS" bash scripts/subagent-brief.sh
    rm -rf "$bin"
    [ "$status" -eq 0 ]
    [ -z "$output" ]
}

@test "the block is the same on a box without jobpool" {
    # sandbox-gap-reviewed: the brief runs hostname and jq, nothing sandboxed.
    local d p=""
    local IFS=:
    for d in $PATH; do [ -e "$d/jobpool" ] || p=${p:+$p:}$d; done
    unset IFS
    run env PATH="$p" bash -c 'echo "{}" | scripts/subagent-brief.sh | jq -r .hookSpecificOutput.additionalContext'
    [ "$status" -eq 0 ]
    contains "Plain \`make\`" "$output"
}
