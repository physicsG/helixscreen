#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# parallel-jobs-file.sh: GNU parallel, with its --jobs N replaced by the file
# HELIX_JOBS_FILE names when that file exists.
#
# bash, not sh: every test below it inherits the suite's exported functions
# (the sandbox's shims among them), and dash drops those from the environment.
#
# bats checks its --jobs value arithmetically, so it cannot be given the file
# GNU parallel re-reads while it runs (`--jobs FILE`). `make test-shell` gives
# bats a number and this script as --parallel-binary-name instead, so the
# suite's concurrency follows the count `jobpool hold --grow` keeps in the
# file. Without the file every argument passes through unchanged.
f=${HELIX_JOBS_FILE-}
if [ -n "$f" ] && [ -f "$f" ]; then
    n=$#
    swap='' opts=1
    for a do
        if [ -n "$swap" ]; then
            a=$f
            swap=
        elif [ -n "$opts" ]; then
            case $a in
                --jobs | -j) swap=1 ;;
                --) opts= ;;
            esac
        fi
        set -- "$@" "$a"
    done
    shift "$n"
fi
exec parallel "$@"
