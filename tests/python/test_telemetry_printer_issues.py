# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/telemetry-printer-issues.py."""

import copy
import importlib.util
import json
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent.parent
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))


def _load(filename, name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


issues = _load("telemetry-printer-issues.py", "telemetry_printer_issues")

FIXTURE = Path(__file__).parent / "fixtures" / "printer_analysis_sample.json"
EMPTY_DB = {"printers": []}


def analysis():
    return json.loads(FIXTURE.read_text())


def draft_json(body):
    m = re.search(r"```json\n(.*?)\n```", body, re.S)
    assert m, body
    return json.loads(m.group(1))


# Read by the Worker's vitest suite too, so both producers give the same output.
VECTORS = json.loads((ROOT / "tests" / "fixtures" / "telemetry_issue_vectors.json").read_text())


@pytest.mark.parametrize("vector", VECTORS["fingerprint"], ids=lambda v: v["expected"])
def test_fingerprint_matches_the_shared_vectors(vector):
    assert issues.fingerprint(vector["kind"], vector["parts"]) == vector["expected"]


@pytest.mark.parametrize("vector", VECTORS["code_span"], ids=lambda v: v["expected"][:20])
def test_code_span_matches_the_shared_vectors(vector):
    assert issues.code_span(vector["input"]) == vector["expected"]


@pytest.mark.parametrize("vector", VECTORS["title"], ids=lambda v: v["expected"][:20])
def test_issue_title_matches_the_shared_vectors(vector):
    assert issues.issue_title(vector["input"]) == vector["expected"]


def test_marker_carries_the_fingerprint():
    fp = issues.fingerprint("printer", ["CoreXY", " STM32H723 ", "350x350x330"])
    assert issues.marker_for(fp) == "<!-- telemetry-fp: printer:corexy|stm32h723|350x350x330 -->"


def test_device_strings_are_escaped_in_title_and_body():
    a = analysis()
    for key in ("candidate_heuristics_clusters", "undetected_clusters"):
        for c in a[key]:
            c["mcu"] = "evil|mcu @octocat\n-->"
    a["undetected_clusters"][0]["common_names"]["macros.names"] = [["BAD`MACRO|@x", 6]]
    issue = issues.candidates_to_issues(a, EMPTY_DB)[0]
    assert "@octocat" not in issue["title"] and "\n" not in issue["title"]
    body = issue["body"]
    assert issues.fingerprint_in_body(body) == issue["fingerprint"]
    assert "- MCU: `evil\\|mcu @\u200boctocat -->`" in body
    assert "`BAD'MACRO\\|@\u200bx`" in body
    assert "@octocat" not in body


def test_only_clusters_at_or_above_the_device_threshold_become_issues():
    out = issues.candidates_to_issues(analysis(), EMPTY_DB)
    assert len(out) == 1
    assert out[0]["fingerprint"] == "printer:corexy|stm32h723|350x350x350"
    assert out[0]["labels"] == ["telemetry", "printer-db"]


def test_threshold_is_a_parameter():
    assert len(issues.candidates_to_issues(analysis(), EMPTY_DB, min_devices=3)) == 2


def test_body_has_marker_count_signature_and_parsable_draft():
    issue = issues.candidates_to_issues(analysis(), EMPTY_DB)[0]
    body = issue["body"]
    assert issues.marker_for(issue["fingerprint"]) in body
    assert "6 devices" in body
    for fact in ("corexy", "STM32H723", "350x350x350", "BOX_CUT_MATERIAL", "chamber_temp"):
        assert fact in body
    assert "review" in body.lower()
    draft = draft_json(body)
    assert draft["heuristics"], draft
    assert all(h["confidence"] >= 75 for h in draft["heuristics"])
    assert not re.search(r"[0-9a-f]{64}", body)
    assert "a1b2c3d4" not in body


def test_fingerprint_is_stable_when_cluster_field_order_changes():
    base = issues.candidates_to_issues(analysis(), EMPTY_DB)[0]["fingerprint"]
    shuffled = analysis()
    for key in ("candidate_heuristics_clusters", "undetected_clusters"):
        shuffled[key] = [dict(reversed(list(c.items()))) for c in shuffled[key]]
    shuffled["candidate_heuristics_clusters"].reverse()
    shuffled["undetected_clusters"].reverse()
    again = issues.candidates_to_issues(shuffled, EMPTY_DB)
    assert [i["fingerprint"] for i in again] == [base]


def test_cluster_already_covered_by_the_database_is_skipped():
    a = analysis()
    heuristics = a["candidate_heuristics_clusters"][0]["candidate_heuristics"]
    db = {"printers": [{"id": "known", "name": "Known", "heuristics": copy.deepcopy(heuristics)}]}
    assert issues.candidates_to_issues(a, db) == []


class FakeGh:
    """Stands in for issues.gh: records writes, answers the two reads."""

    FP = "printer:corexy|stm32h723|350x350x350"

    def __init__(self, found=None, comments=()):
        self.found = [dict(f, body=issues.marker_for(self.FP)) for f in (found or [])]
        self.comments = list(comments)
        self.writes = []

    def __call__(self, args):
        if args[:2] == ["issue", "list"]:
            return json.dumps(self.found)
        if args[:2] == ["issue", "view"]:
            return json.dumps({"comments": [{"body": c} for c in self.comments]})
        self.writes.append(args)
        return ""


def run_main(monkeypatch, tmp_path, fake, *extra):
    a = tmp_path / "analysis.json"
    a.write_text(FIXTURE.read_text())
    d = tmp_path / "db.json"
    d.write_text(json.dumps(EMPTY_DB))
    monkeypatch.setattr(issues, "gh", fake)
    return issues.main(["--analysis", str(a), "--db", str(d), *extra])


def test_new_finding_creates_one_issue(monkeypatch, tmp_path):
    fake = FakeGh()
    assert run_main(monkeypatch, tmp_path, fake) == 0
    creates = [w for w in fake.writes if w[:2] == ["issue", "create"]]
    assert len(creates) == 1
    assert "telemetry,printer-db" in creates[0]
    labels = [w[2] for w in fake.writes if w[:2] == ["label", "create"]]
    assert labels == ["telemetry", "printer-db"]
    assert fake.writes.index(["label", "create", "telemetry", "--force", "--description",
                              issues.LABEL_DESCRIPTIONS["telemetry"]]) < fake.writes.index(creates[0])


def test_dry_run_prints_and_writes_nothing(monkeypatch, tmp_path, capsys):
    fake = FakeGh()
    assert run_main(monkeypatch, tmp_path, fake, "--dry-run") == 0
    assert fake.writes == []
    assert "telemetry-fp: printer:corexy" in capsys.readouterr().out


def test_closed_issue_is_not_reopened(monkeypatch, tmp_path, capsys):
    fake = FakeGh(found=[{"number": 7, "state": "CLOSED"}])
    assert run_main(monkeypatch, tmp_path, fake) == 0
    assert [w for w in fake.writes if w[0] == "issue"] == []
    assert "closed, not reopened" in capsys.readouterr().out


def test_open_issue_gets_one_comment_per_iso_week(monkeypatch, tmp_path):
    week = issues.iso_week_label()
    fake = FakeGh(found=[{"number": 7, "state": "OPEN"}])
    assert run_main(monkeypatch, tmp_path, fake) == 0
    comment = [w for w in fake.writes if w[:2] == ["issue", "comment"]]
    assert comment[0][2] == "7"
    assert f"Week {week}" in comment[0][-1]

    again = FakeGh(found=[{"number": 7, "state": "OPEN"}], comments=[f"Week {week}: 6 devices"])
    assert run_main(monkeypatch, tmp_path, again) == 0
    assert [w for w in again.writes if w[0] == "issue"] == []


def test_dry_run_makes_no_gh_calls_at_all(monkeypatch, tmp_path):
    fake = FakeGh()
    run_main(monkeypatch, tmp_path, fake, "--dry-run")
    assert fake.writes == []


def test_issue_index_matches_marker_exactly(monkeypatch):
    fp = "printer:corexy|stm32h723|350x350x350"
    listing = [
        {"number": 1, "state": "OPEN", "body": "x\n" + issues.marker_for(fp)},
        {"number": 2, "state": "CLOSED", "body": issues.marker_for("printer:other")},
        {"number": 3, "state": "OPEN", "body": issues.marker_for(fp + "x")},
        {"number": 4, "state": "OPEN", "body": "no marker"},
    ]
    monkeypatch.setattr(issues, "gh", lambda args: json.dumps(listing))
    index = issues.load_issue_index()
    assert index[fp]["number"] == 1
    assert index["printer:other"]["state"] == "CLOSED"
    assert index[fp + "x"]["number"] == 3
    assert len(index) == 3


def test_new_issue_cap_holds_back_the_sixth(monkeypatch, tmp_path, capsys):
    a = analysis()
    template = a["candidate_heuristics_clusters"][0]
    detail = a["undetected_clusters"][0]
    a["candidate_heuristics_clusters"], a["undetected_clusters"] = [], []
    for i in range(6):
        c = dict(template, mcu=f"mcu{i}")
        d = dict(detail, mcu=f"mcu{i}")
        a["candidate_heuristics_clusters"].append(c)
        a["undetected_clusters"].append(d)
    path = tmp_path / "a.json"
    path.write_text(json.dumps(a))
    db = tmp_path / "db.json"
    db.write_text(json.dumps(EMPTY_DB))
    fake = FakeGh()
    fake.found = []
    monkeypatch.setattr(issues, "gh", fake)
    assert issues.main(["--analysis", str(path), "--db", str(db)]) == 0
    assert len([w for w in fake.writes if w[:2] == ["issue", "create"]]) == issues.MAX_NEW_ISSUES
    assert capsys.readouterr().out.count("Held for next run") == 1


def test_issue_index_prefers_open_then_lowest_number(monkeypatch):
    fp = "printer:corexy|stm32h723|350x350x350"
    listing = [
        {"number": 2, "state": "CLOSED", "body": issues.marker_for(fp)},
        {"number": 9, "state": "OPEN", "body": issues.marker_for(fp)},
        {"number": 4, "state": "OPEN", "body": issues.marker_for(fp)},
        {"number": 1, "state": "CLOSED", "body": issues.marker_for("printer:closed")},
        {"number": 3, "state": "CLOSED", "body": issues.marker_for("printer:closed")},
    ]
    monkeypatch.setattr(issues, "gh", lambda args: json.dumps(listing))
    index = issues.load_issue_index()
    assert index[fp]["number"] == 4
    assert index["printer:closed"]["number"] == 1
