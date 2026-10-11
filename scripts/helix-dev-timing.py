#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive a wall-clock cost ledger for builds and test runs from session transcripts.

Every tool call in a transcript records the timestamp of its invocation and of its
result; the delta is what the command actually cost. Rebuilding the ledger from
those transcripts keeps it idempotent and retroactive, so it needs no hook and
nothing has to be remembered at the time a command runs.

Classification is anchored on the first token of each shell segment, never on a
substring search: a command that merely mentions `make full-test-run` while
grepping a doc costs nothing and must not be filed as a suite run.
"""

import json
import re
import statistics
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT_JSONL = REPO / ".claude-recall" / "timing.jsonl"
OUT_MD = REPO / ".claude-recall" / "TIMING.md"

# The harness kills a foreground Bash call at 600s. Those deltas are the cap, not
# a cost, and averaging them in would invent a ceiling that does not exist.
HARNESS_CAP = 600.0

ENV_PREFIX = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_]*=\S*\s+)+")
HEREDOC = re.compile(r"<<-?\s*(['\"]?)(\w+)\1")
JOBS = re.compile(r"-j\s*(\d+)")
WRAPPERS = {"time", "nohup", "setsid", "sudo", "exec", "command", "env", "stdbuf"}

MAKE_TARGETS = {
    "t": "test-build-run",
    "test": "test-build", "tests": "test-build",
    # `full-test-run` is unit-only (~[.] ~[slow]); the shell, hidden and slow
    # sets are separate targets and are counted separately here.
    "full-test-run": "unit-sweep", "test-run": "unit-sweep",
    "test-shell": "shell-suite",
    "test-hidden": "hidden-suite",
    "test-all": "test-all", "test-serial": "test-serial",
    "test-xml": "xml-suite", "test-plugin": "plugin-suite",
    "test-ui-pytest": "pytest-suite", "test-kiauh": "shell-suite",
    "mutate": "mutate", "mutate-diff": "mutate",
    "asan": "sanitizer", "tsan": "sanitizer",
    "test-asan": "sanitizer", "test-tsan": "sanitizer",
}
MAKE_CHEAP = ("check-", "regen-", "compile_commands", "help", "clean", "translation", "print-")
MAKE_CROSS = ("pi-", "remote-", "deploy-", "docker", "ad5m", "ad5x", "k1", "k2", "cc1", "snapmaker")


def strip_heredocs(cmd):
    """Drop heredoc bodies so documentation that mentions a build is not read as one."""
    lines, out, i = cmd.split("\n"), [], 0
    while i < len(lines):
        out.append(lines[i])
        m = HEREDOC.search(lines[i])
        if m:
            term = m.group(2)
            i += 1
            while i < len(lines) and lines[i].strip() != term:
                i += 1
        i += 1
    return "\n".join(out)


def head_tokens(seg):
    seg = ENV_PREFIX.sub("", seg.strip())
    toks = seg.split()
    while toks and toks[0] in WRAPPERS:
        toks.pop(1) if False else toks.pop(0)
    if toks and toks[0] == "timeout":
        toks = toks[2:]
    return toks


def classify_make(args):
    targets = [a for a in args if not a.startswith("-")]
    if not targets:
        return "app-build"
    t = targets[0]
    if t in MAKE_TARGETS:
        return MAKE_TARGETS[t]
    if t.startswith(MAKE_CHEAP):
        return None
    if t.startswith(MAKE_CROSS):
        return "cross-build"
    return "app-build"


def classify_segment(toks):
    if not toks:
        return None
    head, args = toks[0], toks[1:]
    base = head.rsplit("/", 1)[-1]

    if base == "make":
        return classify_make(args)
    if base == "helix-tests":
        positional = [a for a in args if not a.startswith("-")]
        return "test-run" if positional else "test-run-all"
    if base == "helix-screen":
        return "ctl" if args and args[0] == "ctl" else "app-run"
    if base == "bats":
        return "shell-suite"
    # Transcripts also hold runs under the script's former name.
    if base in ("test-host-run.sh", "zeus-run.sh"):
        sub = args[0] if args else ""
        return "mutate" if sub.startswith("mutate") else "sanitizer" if sub in ("asan", "tsan") else None
    if base == "quality-checks.sh" or base == "qc_timing.py":
        return "quality-gate"
    if base == "syntax_check.py":
        return "syntax-check"
    if base in ("python3", "python") and args:
        return classify_segment([args[0]] + args[1:])
    if base == "git" and args and args[0] == "commit":
        return "quality-gate"
    return None


# Most expensive wins when one command invokes several things.
PRIORITY = ["mutate", "sanitizer", "test-all", "test-serial", "unit-sweep",
            "shell-suite", "hidden-suite", "xml-suite", "plugin-suite",
            "pytest-suite", "cross-build", "test-run-all", "quality-gate",
            "test-build-run", "test-build", "test-run", "app-build", "app-run",
            "syntax-check", "ctl"]


def classify(cmd):
    cmd = strip_heredocs(cmd)
    best, detail = None, ""
    for seg in re.split(r"&&|\|\||;|\n|\|", cmd):
        toks = head_tokens(seg)
        cat = classify_segment(toks)
        if cat is None:
            continue
        if best is None or PRIORITY.index(cat) < PRIORITY.index(best):
            best = cat
            detail = extract_filter(toks, cat)
    if best is None:
        return None
    j = JOBS.search(cmd)
    return best, detail, (int(j.group(1)) if j else None)


FILTER_OK = re.compile(r"^~?[\[*]")


def looks_like_filter(s):
    """A Catch2 filter opens with a tag, an exclusion or a wildcard. A loop
    variable or a redirect lifted out of a compound command does not."""
    return bool(FILTER_OK.match(s)) and not any(c in s for c in "$&><;")


def extract_filter(toks, cat):
    cand = ""
    if cat == "test-run":
        for a in toks[1:]:
            if not a.startswith("-"):
                cand = a.strip("'\"")
                break
    elif cat in ("test-build-run", "test-build", "unit-sweep"):
        for a in toks[1:]:
            if a.startswith("F="):
                cand = a[2:].strip("'\"")
                break
    return cand if looks_like_filter(cand) else ""


def project_dirs():
    return sorted(d for d in (Path.home() / ".claude" / "projects").glob("*helixscreen*") if d.is_dir())


def iso_delta(a, b):
    fmt = "%Y-%m-%dT%H:%M:%S.%fZ"
    try:
        ta = time.mktime(time.strptime(a, fmt)) + float(a[20:23]) / 1000
        tb = time.mktime(time.strptime(b, fmt)) + float(b[20:23]) / 1000
    except (ValueError, TypeError):
        return None
    d = tb - ta
    return d if 0 <= d < 86400 else None


def harvest():
    pending, rows = {}, []
    for pdir in project_dirs():
        for path in sorted(pdir.glob("*.jsonl")):
            try:
                fh = open(path, errors="replace")
            except OSError:
                continue
            with fh:
                for line in fh:
                    try:
                        e = json.loads(line)
                    except (ValueError, TypeError):
                        continue
                    ts, msg = e.get("timestamp"), e.get("message") or {}
                    content = msg.get("content")
                    if not ts or not isinstance(content, list):
                        continue
                    for b in content:
                        if not isinstance(b, dict):
                            continue
                        if b.get("type") == "tool_use" and b.get("name") == "Bash":
                            inp = b.get("input") or {}
                            hit = classify(inp.get("command") or "")
                            if not hit:
                                continue
                            cat, detail, jobs = hit
                            pending[b.get("id")] = {
                                "start": ts, "cat": cat, "detail": detail, "jobs": jobs,
                                "cmd": (inp.get("command") or "").strip()[:300],
                                "bg": bool(inp.get("run_in_background")),
                                "branch": e.get("gitBranch") or "",
                                "session": (e.get("sessionId") or "")[:8],
                                "sidechain": bool(e.get("isSidechain")),
                            }
                        elif b.get("type") == "tool_result":
                            rec = pending.pop(b.get("tool_use_id"), None)
                            if not rec:
                                continue
                            dur = iso_delta(rec["start"], ts)
                            if dur is None:
                                continue
                            rec["sec"] = round(dur, 2)
                            rec["error"] = bool(b.get("is_error"))
                            rec["capped"] = dur >= HARNESS_CAP - 3
                            rows.append(rec)
    rows.sort(key=lambda r: r["start"])
    return rows


def fmt(sec):
    if sec < 90:
        return f"{sec:.1f}s"
    if sec < 5400:
        return f"{sec/60:.1f}m"
    return f"{sec/3600:.1f}h"


def pct(vals, p):
    vals = sorted(vals)
    if len(vals) == 1:
        return vals[0]
    k = (len(vals) - 1) * p
    lo = int(k)
    return vals[lo] + (vals[min(lo + 1, len(vals) - 1)] - vals[lo]) * (k - lo)


def table(groups, label, extra=None):
    out = [f"| {label} | n | med | p90 | max | TOTAL |", "|---|--:|--:|--:|--:|--:|"]
    # Sorted by total, not by unit cost: a 3s command run 1800 times outweighs
    # anything that only looks expensive.
    for key, vals in sorted(groups.items(), key=lambda kv: -sum(kv[1])):
        note = f" {extra[key]}" if extra and key in extra else ""
        out.append(f"| `{key}`{note} | {len(vals)} | {fmt(statistics.median(vals))} "
                   f"| {fmt(pct(vals, 0.9))} | {fmt(max(vals))} | **{fmt(sum(vals))}** |")
    return out


def main():
    t0 = time.time()
    rows = harvest()
    OUT_JSONL.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSONL, "w") as fh:
        for r in rows:
            fh.write(json.dumps(r) + "\n")

    # Backgrounded calls return at launch, failures stop early, and capped calls
    # report the harness deadline. None of the three is what a command costs.
    good = [r for r in rows if not r["bg"] and not r["error"] and not r["capped"]]
    capped = [r for r in rows if r["capped"]]

    by_cat, by_filter = {}, {}
    for r in good:
        by_cat.setdefault(r["cat"], []).append(r["sec"])
        if r["cat"] in ("test-run", "test-build-run") and r["detail"]:
            by_filter.setdefault(r["detail"], []).append(r["sec"])
    by_filter = {k: v for k, v in by_filter.items() if len(v) >= 2}

    md = [
        "# Command cost ledger", "",
        f"Derived by `scripts/helix-dev-timing.py` from {len(project_dirs())} transcript "
        "dir(s). The transcripts are the source of truth, so this file is disposable "
        "and safe to regenerate at any time.", "",
        f"{len(good)} clean samples out of {len(rows)} classified commands, "
        f"{fmt(sum(r['sec'] for r in good))} of wall clock.", "",
        "Excluded from the statistics: backgrounded calls (they return at launch), "
        f"failed calls (they stop early), and {len(capped)} calls that hit the harness "
        "600s deadline (the cap is not a duration).", "",
        "Deltas are wall clock at the terminal, so a command that waited on a "
        "permission prompt carries the human reply time. `med` is the number to plan "
        "against; `min` is the cleanest estimate of pure machine time.", "",
        "## By category", "",
    ]
    md += table(by_cat, "category")
    if by_filter:
        md += ["", "## Test runs by tag filter (2+ samples)", "", *table(by_filter, "filter")]

    md += ["", "## 20 most expensive single commands", "",
           "| cost | category | when | branch | command |", "|--:|---|---|---|---|"]
    for r in sorted(good, key=lambda r: -r["sec"])[:20]:
        cmd = r["cmd"].replace("\n", " ")[:80].replace("|", "\\|")
        md.append(f"| {fmt(r['sec'])} | {r['cat']} | {r['start'][:10]} | {r['branch'][:20]} | `{cmd}` |")
    md.append("")

    OUT_MD.write_text("\n".join(md))
    print(f"{len(rows)} classified, {len(good)} clean, {len(capped)} hit the 600s cap")
    print(f"-> {OUT_JSONL}\n-> {OUT_MD}   [{time.time()-t0:.1f}s]")


if __name__ == "__main__":
    sys.exit(main())
