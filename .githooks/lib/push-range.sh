# shellcheck shell=bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by pre-push: what the push publishes, and the patch-drift question
# asked where lib/ is owned.

# push_base REMOTE_SHA TARGET: the commit the pushed range starts from, run in
# the pushing tree. What the remote already has; for a new branch, or a remote
# sha this clone has never fetched, TARGET's nearest fork among the branches it
# could have been cut from (scripts/diff_base.py), so a branch cut from a
# release line is not measured against trunk. A tree whose diff_base.py has no
# --head falls back to the merge-base with origin/main. Fails when none exists.
push_base() {
    local base="$1" target="$2"
    case "$base" in *[!0]*) ;; *) base="" ;; esac
    if [ -n "$base" ] && git cat-file -e "$base^{commit}" 2>/dev/null; then
        printf '%s\n' "$base"
        return 0
    fi
    if [ -f scripts/diff_base.py ] && grep -q -- '--head' scripts/diff_base.py; then
        python3 scripts/diff_base.py --head "$target" 2>/dev/null
        return
    fi
    git merge-base "$target" origin/main 2>/dev/null
}

# prepush_patch_drift TREE BASE TARGET: patch drift in TREE, the checkout that
# owns lib/. Drift whose only cause is a patches/ file BASE..TARGET changed is
# that tree not having built the pushed commits yet, so the gate reports it as
# a note; any other drift fails. An empty BASE, or a TREE whose gate has no
# --pending-since, asks the plain question.
prepush_patch_drift() {
    local tree="$1" base="$2" target="$3"
    local gate="$tree/scripts/check_patch_drift.py"
    [ -f "$gate" ] || return 0
    local args=()
    if [ -n "$base" ] && grep -q -- '--pending-since' "$gate"; then
        args=(--pending-since "$base" --pending-head "$target")
    fi
    echo ""
    echo "🩹 Checking patch drift in your working tree (it owns lib/)..."
    if ! (cd "$tree" && python3 scripts/check_patch_drift.py "${args[@]}"); then
        echo ""
        echo "❌ Patch drift is in YOUR WORKING TREE's lib/, not in ${target:0:9}."
        echo "   Repair it where it lives:  make reapply-patches"
        return 1
    fi
}
