#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# SubagentStart hook (wired in .claude/settings.json). Injects the shared-machine
# rules into every subagent's context, so a brief cannot forget them. A subagent
# reads none of its parent's memory and briefs already run long, so the block
# carries only the rules whose breach costs the other sessions on this box.

# The rules are about thelio and its test host; a cloud session or a Mac has neither.
case " ${HELIX_ADVISOR_HOSTS:-thelio} " in
    *" $(hostname -s 2>/dev/null) "*) ;;
    *) exit 0 ;;
esac
command -v jq >/dev/null 2>&1 || exit 0

block=$(cat <<'EOF'
Shared machine: thelio (32 threads) is used by several sessions at once; the test host (HELIX_TEST_HOST) is usually idle.
- Claims: you run inside your lead's session, so its claims are yours too. Before building, `scripts/helix-claim check build:<tree>`: FREE means take it (`take build:<tree> "<why>"`) and release it when done; LIVE means build under it only if your brief says your lead holds it, otherwise stop and report. Never release a claim you did not take.
- Plain `make`, never a fixed -j: `make` here is the jobpool shim (one machine pool; a -j is stripped). `jobpool status` shows how full it is. Without jobpool: `-j$(scripts/helix-claim jobs)`.
- Mutation, ASAN and symbolizing go to the test host: `scripts/test-host-run.sh` (mutate needs your branch pushed, never main). Never addr2line or gdb against helix-tests on thelio.
- Run builds and tests in the foreground. You are not woken when a background job ends, so never go idle waiting on one. If a command times out into the background, kill its PID before moving on.
- Kill only PIDs you started; never pkill or pgrep -f by name.
- Report: a TODO list of what was asked, done / not done, first; then what you did NOT verify.
EOF
)

jq -cn --arg c "$block" '{hookSpecificOutput: {hookEventName: "SubagentStart", additionalContext: $c}}'
