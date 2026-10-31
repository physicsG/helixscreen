# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for scripts/rename_constants.py and its added-lines gate,
scripts/check_constant_naming.py."""

import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

import check_constant_naming as gate  # noqa: E402
import rename_constants as rc  # noqa: E402


def make_repo(tmp_path, files):
    for rel, text in files.items():
        p = tmp_path / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    subprocess.run(["git", "add", "-A"], cwd=tmp_path, check=True)
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "init"],
                   cwd=tmp_path, check=True)
    return tmp_path


def plan(tmp_path):
    out = tmp_path / "plan.tsv"
    rc.main(["--plan", str(out), "--root", str(tmp_path)])
    rows = {}
    for line in out.read_text().splitlines():
        if not line.startswith("#"):
            status, old, new, _sites = line.split("\t")
            rows[old] = (status, new)
    return rows, out.read_text()


# --- conversion -------------------------------------------------------------

def test_conversion_lowercases_every_word_after_its_first_character():
    assert rc.to_k_camel("MAX_RETRY_COUNT") == "kMaxRetryCount"
    assert rc.to_k_camel("HTTP_TIMEOUT_MS") == "kHttpTimeoutMs"
    assert rc.to_k_camel("MAX_3D_SIZE") == "kMax3dSize"
    assert rc.to_k_camel("RGB565_BPP") == "kRgb565Bpp"
    assert rc.to_k_camel("PI") == "kPi"


def test_upper_snake_needs_two_capitals_and_clean_underscores():
    for name in ("MAX_X", "PI", "RGB565", "A_B"):
        assert rc.is_upper_snake(name), name
    for name in ("N", "T2", "_FOO", "FOO_", "FOO__BAR", "kFoo", "Foo_Bar", "X_1"):
        assert not rc.is_upper_snake(name), name


# --- declarations -----------------------------------------------------------

def test_declarations_found_at_every_scope_and_not_elsewhere():
    src = (
        "namespace a { constexpr int NS_CONST = 1; }\n"
        "struct S {\n"
        "    static constexpr std::array<int, 2> MEMBER_ARR{1, 2};\n"
        "    static const char* const NAMES[];\n"
        "};\n"
        "const char* const S::NAMES[] = {\"x\"};\n"
        "void f(const int PARAM_NAME = 3) {\n"
        "    const auto LOCAL_CONST = 4;\n"
        "    int MUTABLE_NAME = 5;\n"
        "    return_type x = MUTABLE_NAME;\n"
        "}\n"
    )
    names = [n for n, _ in rc.find_const_decls(rc.code_only(src))]
    assert names == ["NS_CONST", "MEMBER_ARR", "NAMES", "NAMES", "LOCAL_CONST"]


def test_commented_and_quoted_declarations_are_not_declarations():
    src = '// constexpr int IN_COMMENT = 1;\nconst char* s = "constexpr int IN_STRING = 2;";\n'
    assert rc.find_const_decls(rc.code_only(src)) == []


# --- plan: exclusions -------------------------------------------------------

BASE = {
    "src/a.cpp": (
        "constexpr int RENAME_ME = 1;\n"
        "constexpr int MACRO_CLASH = 2;\n"
        "constexpr int DATA_USE = 3;\n"
        "constexpr int ENUM_CLASH = 4;\n"
        "constexpr int MAX_3D = 5;\n"
        "constexpr int MAX3D = 6;\n"
        "constexpr int TAKEN_NAME = 7;\n"
        "constexpr int kTakenName = 8;\n"
        "enum class E { ENUM_CLASH, OTHER_VALUE };\n"
        "const char* k = \"DATA_USE\";\n"
        "int f() { return RENAME_ME + MACRO_CLASH; } // RENAME_ME here\n"
        "const char* s = \"RENAME_ME stays\";\n"
    ),
    "lib/vendor/v.h": "#define MACRO_CLASH 9\n",
}


def test_plan_renames_one_and_reports_each_skip_by_reason(tmp_path):
    rows, text = plan(make_repo(tmp_path, BASE))
    assert rows["RENAME_ME"] == ("rename", "kRenameMe")
    assert rows["MACRO_CLASH"][0] == "macro"
    assert rows["DATA_USE"][0] == "data"
    assert rows["ENUM_CLASH"][0] == "enumerator"
    assert rows["MAX_3D"][0] == "collision"
    assert rows["MAX3D"][0] == "collision"
    assert rows["TAKEN_NAME"][0] == "collision"
    assert "OTHER_VALUE" not in rows
    assert "# UPPER_SNAKE enumerators left alone: 2" in text


def test_plan_is_deterministic(tmp_path):
    root = make_repo(tmp_path, BASE)
    assert plan(root)[1] == plan(root)[1]


# --- plan: collisions across scopes -----------------------------------------

def test_a_new_name_in_an_unrelated_file_is_no_collision(tmp_path):
    rows, _ = plan(make_repo(tmp_path, {
        "src/a.cpp": "constexpr int GAP = 1;\nint f() { return GAP; }\n",
        "src/b.cpp": "constexpr int kGap = 2;\n",
    }))
    assert rows["GAP"] == ("rename", "kGap")


def test_a_new_name_one_include_away_is_a_collision(tmp_path, capsys):
    rows, _ = plan(make_repo(tmp_path, {
        "include/b.h": "namespace ui { constexpr int kGap = 2; }\n",
        "include/c.h": "#include \"b.h\"\n",
        "src/a.cpp": "#include \"c.h\"\nconstexpr int GAP = 1;\n",
    }))
    assert rows["GAP"][0] == "collision"
    assert "kGap is visible beside it: include/b.h" in capsys.readouterr().out


def test_an_old_name_in_a_header_collides_with_a_new_name_in_its_includer(tmp_path):
    rows, _ = plan(make_repo(tmp_path, {
        "include/h.h": "constexpr int GAP = 1;\n",
        "tests/t.cpp": "#include <h.h>\nconstexpr int kGap = 2;\n",
    }))
    assert rows["GAP"][0] == "collision"


def test_a_forced_include_is_seen_by_every_unit(tmp_path):
    rows, _ = plan(make_repo(tmp_path, {
        "Makefile": "FORCED_INCLUDE := -include $(INC_DIR)/pch.h\n",
        "include/pch.h": "#include \"style.h\"\n",
        "include/style.h": "constexpr int kGap = 2;\n",
        "src/a.cpp": "constexpr int GAP = 1;\n",
    }))
    assert rows["GAP"][0] == "collision"


# --- skip reporting ---------------------------------------------------------

def test_plan_and_apply_list_every_skip_with_reason_and_sites(tmp_path, capsys):
    root = make_repo(tmp_path, BASE)
    rc.main(["--plan", str(tmp_path / "p.tsv"), "--root", str(root)])
    out = capsys.readouterr().out
    assert "skipped 6 name(s), left UPPER_SNAKE:" in out
    assert "  macro       MACRO_CLASH -> kMacroClash  src/a.cpp:2" in out
    assert "  data        DATA_USE -> kDataUse  src/a.cpp:3" in out
    assert "MAX3D and MAX_3D both convert to kMax3d" in out
    assert "RENAME_ME" not in out

    rc.main(["--apply", "--root", str(root)])
    out = capsys.readouterr().out
    assert "  enumerator  ENUM_CLASH -> kEnumClash  src/a.cpp:4" in out


def branch_adding(root, rel, text):
    (root / rel).write_text(text)
    subprocess.run(["git", "add", rel], cwd=root, check=True)
    subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-qm", "b"],
                   cwd=root, check=True)


def test_git_range_fails_on_a_skipped_declaration_the_gate_will_flag(tmp_path, capsys):
    root = make_repo(tmp_path, BASE)
    branch_adding(root, "src/c.cpp", "constexpr int NEW_ONE = 1;\nconstexpr int MACRO_CLASH2 = 2;\n"
                                     "#define MACRO_CLASH2 3\n")
    assert rc.main(["--apply", "--git-range", "HEAD~1..HEAD", "--root", str(root)]) == 1
    out = capsys.readouterr().out
    assert "src/c.cpp:2: MACRO_CLASH2 -> kMacroClash2  (macro)" in out
    assert "src/c.cpp:1" not in out
    assert "constexpr int kNewOne = 1;" in (root / "src/c.cpp").read_text()

    assert rc.main(["--apply", "--git-range", "HEAD~1..HEAD", "--allow-skips",
                    "--root", str(root)]) == 0


def test_git_range_passes_when_the_range_declares_no_skipped_name(tmp_path):
    root = make_repo(tmp_path, BASE)
    # The range uses a skipped name but declares none: the gate has nothing to flag.
    branch_adding(root, "src/c.cpp", "constexpr int NEW_ONE = MACRO_CLASH;\n")
    assert rc.main(["--apply", "--git-range", "HEAD~1..HEAD", "--root", str(root)]) == 0


# --- apply ------------------------------------------------------------------

def test_apply_renames_code_and_comments_but_never_strings(tmp_path):
    root = make_repo(tmp_path, BASE)
    rc.main(["--apply", "--root", str(root)])
    text = (root / "src/a.cpp").read_text()
    assert "constexpr int kRenameMe = 1;" in text
    assert "return kRenameMe + MACRO_CLASH; } // kRenameMe here" in text
    assert 'const char* s = "RENAME_ME stays";' in text
    assert "constexpr int MACRO_CLASH = 2;" in text
    assert "enum class E { ENUM_CLASH, OTHER_VALUE };" in text


def test_second_apply_is_a_no_op(tmp_path):
    root = make_repo(tmp_path, BASE)
    rc.main(["--apply", "--root", str(root)])
    first = (root / "src/a.cpp").read_text()
    rows, _ = plan(root)
    assert all(status != "rename" for status, _ in rows.values())
    rc.main(["--apply", "--root", str(root)])
    assert (root / "src/a.cpp").read_text() == first


def test_mapping_carries_an_earlier_plan_to_files_that_no_longer_declare_it(tmp_path):
    root = make_repo(tmp_path, {"src/b.cpp": "int g() { return OLD_NAME; }\n"})
    mapping = tmp_path / "earlier.tsv"
    mapping.write_text("# plan\nrename\tOLD_NAME\tkOldName\tsrc/x.h:1\n")
    rc.main(["--apply", "--root", str(root), "--mapping", str(mapping),
             "--files", str(root / "src/b.cpp")])
    assert (root / "src/b.cpp").read_text() == "int g() { return kOldName; }\n"


def test_docs_rename_snake_names_and_bare_words_only_in_code_spans():
    mapping = {"MAX_SLOTS": "kMaxSlots", "TOTAL": "kTotal"}
    md = "Up to MAX_SLOTS lanes. TOTAL is prose; `TOTAL` is code. See MAX_SLOTS.txt.\n"
    assert rc.rewrite_text(md, mapping, markdown=True) == (
        "Up to kMaxSlots lanes. TOTAL is prose; `kTotal` is code. See MAX_SLOTS.txt.\n"
    )


def test_scripts_keep_a_name_they_use_as_a_variable():
    mapping = {"MAX_SLOTS": "kMaxSlots", "TIMEOUT_MS": "kTimeoutMs"}
    sh = 'TIMEOUT_MS=5\necho "$TIMEOUT_MS"\ngrep MAX_SLOTS src/x.h\n'
    assert rc.rewrite_text(sh, mapping) == 'TIMEOUT_MS=5\necho "$TIMEOUT_MS"\ngrep kMaxSlots src/x.h\n'


# --- the added-lines gate ---------------------------------------------------

def diff(path, added, removed=()):
    lines = [f"--- a/{path}", f"+++ b/{path}", f"@@ -1,{len(removed)} +1,{len(added)} @@"]
    lines += [f"-{r}" for r in removed] + [f"+{a}" for a in added]
    return "\n".join(lines) + "\n"


def run_gate(path, added, removed=(), content=None):
    text = content if content is not None else "\n".join(added) + "\n"
    return gate.findings(diff(path, added, removed), lambda p: text)


def test_gate_fires_on_a_new_upper_snake_constant():
    hits = run_gate("src/x.cpp", ["static constexpr int NEW_LIMIT = 3;"])
    assert [(h[0], h[1], h[2]) for h in hits] == [("src/x.cpp", 1, "NEW_LIMIT")]


def test_gate_ignores_macros_enumerators_and_kcamel_constants():
    added = [
        "#define NEW_MACRO 1",
        "enum class Mode { NEW_ENUMERATOR, OTHER_ENUMERATOR };",
        "enum { PLAIN_ENUMERATOR = 2 };",
        "constexpr int kNewLimit = 3;",
        'const char* s = "constexpr int IN_STRING = 1;";',
    ]
    assert run_gate("src/x.cpp", added) == []


def test_gate_ignores_a_changed_line_that_keeps_its_name():
    hits = run_gate("src/x.cpp", ["constexpr int MACRO_CLASH = kOther;"],
                    removed=["constexpr int MACRO_CLASH = OTHER;"])
    assert hits == []


def test_gate_honours_the_escape_hatch_on_the_line_or_above():
    same = ["constexpr int WIRE_NAME = 1;  // CONST_NAME_OK: mirrors the protocol field"]
    above = ["// CONST_NAME_OK: mirrors the protocol field", "constexpr int WIRE_NAME = 1;"]
    assert run_gate("src/x.cpp", same) == []
    assert run_gate("src/x.cpp", above) == []


def test_gate_skips_lib_c_files_and_non_code():
    line = ["constexpr int NEW_LIMIT = 3;"]
    for path in ("lib/lvgl/x.h", "src/x.c", "docs/x.md", "scripts/x.py"):
        assert run_gate(path, line) == [], path


def test_gate_reads_the_index_in_staged_mode(tmp_path):
    root = make_repo(tmp_path, {"src/x.cpp": "int a = 1;\n"})
    (root / "src/x.cpp").write_text("int a = 1;\nconstexpr int NEW_LIMIT = 3;\n")
    assert gate.main(["--staged", "--root", str(root)]) == 0
    subprocess.run(["git", "add", "src/x.cpp"], cwd=root, check=True)
    assert gate.main(["--staged", "--root", str(root)]) == 1


def test_gate_is_wired_into_the_quality_checks():
    qc = (REPO_ROOT / "scripts/quality-checks.sh").read_text()
    assert any(line.startswith("QC_ALL=") and " qc_constant_naming " in line
               for line in qc.splitlines())
    assert (REPO_ROOT / "scripts/qc/constant_naming.sh").is_file()
