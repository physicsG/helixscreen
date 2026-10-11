---
description: Continue a multi-phase project. Reads plan doc, shows progress, and resumes work.
argument-hint: "[plan-doc-path or project-name] [--defaults]"
---

# Continue a Multi-Phase Project

The plan doc is the source of truth. Handoffs from other sessions can describe a
different project with a similar name, so trust one only when it matches the plan.

## 1. Locate the plan
Use the path if one was given. Otherwise search `docs/devel/plans/`, then
`~/.claude/plans/`. If nothing matches, ask for the path with AskUserQuestion.

## 2. Read it
Extract the project name, completed phases (checked Progress items), the next phase,
key files and the verification steps.

## 3. Match a handoff
A handoff matches when its title names the project, its refs match Key Files, and its
phase agrees with Progress.

| Situation | Action |
|-----------|--------|
| Explicit plan path given | Trust the plan; ignore handoff suggestions |
| One clear match | Resume it |
| Several, or any doubt | Ask which one with AskUserQuestion; never auto-select |
| None | Create `HANDOFF: [Project Name] Phase [N]` |

## 4. Show status before doing anything
```
## [Project Name]
Completed: [x] Phase 0 - `hash` | [x] Phase 1 - `hash`
Next: Phase 2 - [Name] ([goal])
Files: path/file.cpp | Handoff: [hf-XXX]
```

## 5. Check the worktree
If one exists, run `git status` and `git log --oneline -3` and report uncommitted work or
divergence. Uncommitted changes are someone's work: ask whether to commit or abort
before going further.

## 6. Choose execution (checkpoint)
With `--defaults`, use the defaults below without asking. Otherwise ask with
AskUserQuestion and wait for the answer:

1. **Execution** (header "Execution"): "Subagent-driven (Recommended)" - a fresh
   subagent per task, two-stage review; or "Batched with checkpoints" - human review
   between batches of 3.
2. **Overrides** (header "Overrides", multiSelect): "None - use defaults",
   "Skip TDD for this phase", "Extra review rigor".

## 7. Execute
Subagent-driven uses `superpowers:subagent-driven-development`; batched uses
`superpowers:executing-plans`. This session coordinates and delegates; it does not
implement the phase itself. If the skill is not installed, stop and tell the user.

## 8. Verify
Run the plan's verification steps, and `superpowers:requesting-code-review` for any
code change.

## 9. Record
- Commit: `feat(scope): phase N desc`
- Check off Progress in the plan: `- [x] Phase N: [Name] - \`hash\` (date)`

## 10. Next
More phases: back to step 6, keeping `--defaults` if it was given. Otherwise finish
with `superpowers:finishing-a-development-branch`.

## Defaults
Subagent-driven execution; TDD for backend work, not for UI; review for every code
change; one commit per phase.
