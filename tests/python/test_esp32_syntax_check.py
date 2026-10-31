# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/esp32_syntax_check.py: which firmware units a change reaches, and their commands."""
import importlib.util
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "esp32_syntax_check.py"
spec = importlib.util.spec_from_file_location("esp32_syntax", SCRIPT)
esc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esc)

UNITS = {
    "src/ui/a.cpp": {},
    "src/ui/b.cpp": {},
    "firmware/helixscreen-esp32/components/helixapp/stubs.cpp": {},
}
SOURCES = {
    "src/ui/a.cpp": '#include "ui_a.h"\n',
    "src/ui/b.cpp": '#include <spdlog/spdlog.h>\n#include "printer/b.h"\n',
    "firmware/helixscreen-esp32/components/helixapp/stubs.cpp": '#  include "saver.h"\n',
}


def select(changed):
    return esc.select_units(UNITS, changed, SOURCES.get)


def test_a_changed_source_is_checked_only_if_the_firmware_compiles_it():
    assert select(["src/ui/a.cpp", "src/desktop_only.cpp"]) == ["src/ui/a.cpp"]


def test_a_changed_header_reaches_the_units_that_include_it():
    assert select(["include/saver.h"]) == [
        "firmware/helixscreen-esp32/components/helixapp/stubs.cpp"]
    assert select(["include/printer/b.h"]) == ["src/ui/b.cpp"]


def test_a_header_matches_by_path_suffix_not_by_substring():
    assert select(["include/my_saver.h", "include/b.h"]) == []


def test_the_command_compiles_syntax_only_with_no_object_or_depfile():
    entry = {"command": "g++ -DX=\\\"y\\\" -fdiagnostics-color=always -MD -MT o -MF o.d "
                        "-o out.obj -c /r/src/a.cpp"}
    assert esc.compile_args(entry) == ["g++", '-DX="y"', "-fdiagnostics-color=never", "-c",
                                       "/r/src/a.cpp", "-fsyntax-only"]


def test_paths_move_to_the_checked_tree_and_missing_ones_fall_back(tmp_path):
    tree, primary = tmp_path / "tree", tmp_path / "primary"
    (tree / "include").mkdir(parents=True)
    (primary / "build/generated").mkdir(parents=True)
    rebase = lambda t: esc.rebase_path(t, "/cfg", str(tree), str(primary))
    assert rebase("-I/cfg/include") == f"-I{tree}/include"
    assert rebase("/cfg//include") == f"{tree}/include"
    assert rebase("-I/cfg/build/generated") == f"-I{primary}/build/generated"
    assert rebase("-I/opt/esp/idf/components/log/include") == "-I/opt/esp/idf/components/log/include"


def test_rebase_path_rewrites_tree_paths_embedded_in_define_values(tmp_path):
    tree, primary = tmp_path / "tree", tmp_path / "primary"
    (tree / "include").mkdir(parents=True)
    (primary / "build").mkdir(parents=True)
    rebase = lambda t: esc.rebase_path(t, "/cfg", str(tree), str(primary))
    assert rebase('-DLV_CONF_PATH="/cfg/include/lv_conf.h"') == \
        f'-DLV_CONF_PATH="{tree}/include/lv_conf.h"'
    assert rebase("-DLV_CONF_PATH=/cfg/include/lv_conf.h") == \
        f"-DLV_CONF_PATH={tree}/include/lv_conf.h"
    assert rebase('-DX="/cfg/build"') == f'-DX="{primary}/build"'
    assert rebase("-fmacro-prefix-map=/cfg/include=.") == f"-fmacro-prefix-map={tree}/include=."


def test_rebase_path_rewrites_the_bare_root(tmp_path):
    tree, primary = tmp_path / "tree", tmp_path / "primary"
    tree.mkdir()
    rebase = lambda t: esc.rebase_path(t, "/cfg", str(tree), str(primary))
    assert rebase("-I/cfg") == f"-I{tree}"
    assert rebase("-fmacro-prefix-map=/cfg=.") == f"-fmacro-prefix-map={tree}=."
    assert rebase("-I/cfgx/include") == "-I/cfgx/include"


def test_one_per_header_checks_a_header_through_a_single_includer():
    units = {"src/x.cpp": {}, "src/y.cpp": {}, "src/z.cpp": {}}
    sources = {r: '#include "common.h"\n' for r in units}
    assert esc.select_units(units, ["include/common.h"], sources.get,
                            one_per_header=True) == ["src/x.cpp"]
    assert esc.select_units(units, ["include/common.h"], sources.get) == sorted(units)


def test_one_per_header_reuses_a_staged_includer_instead_of_adding_one():
    units = {"src/x.cpp": {}, "src/y.cpp": {}}
    sources = {r: '#include "common.h"\n' for r in units}
    assert esc.select_units(units, ["include/common.h", "src/y.cpp"], sources.get,
                            one_per_header=True) == ["src/y.cpp"]


def _entry(component, rel, output=True):
    obj = f"esp-idf/{component}/CMakeFiles/__idf_{component}.dir/r/{rel}.obj"
    entry = {"directory": "/b", "file": f"/r/{rel}", "command": f"g++ -o {obj} -c /r/{rel}"}
    if output:
        entry["output"] = obj
    return entry


def test_a_source_two_components_compile_keeps_both_entries():
    shared = "src/printer/discovery.cpp"
    entries = [_entry("helixnet", shared), _entry("helixapp", shared),
               _entry("helixapp", "src/ui/a.cpp"), _entry("helixapp", "lib/x/y.h"),
               {"directory": "/b", "file": "/opt/esp/idf/x.c", "command": "gcc -c x.c"}]
    units = esc.firmware_units(entries, "/r")
    assert sorted(units) == [shared, "src/ui/a.cpp"]
    assert [esc.component_of(e) for e in units[shared]] == ["helixnet", "helixapp"]


def test_the_component_comes_from_the_command_when_the_entry_has_no_output():
    assert esc.component_of(_entry("main", "m.cpp", output=False)) == "main"
    assert esc.component_of({"command": "g++ -c x.cpp", "file": "x.cpp"}) == "?"


def test_the_idf_container_runs_under_a_memory_limit_with_no_swap():
    cmd = esc.docker_base(["/src"])
    assert cmd[cmd.index("--memory") + 1] == esc.DOCKER_MEMORY
    assert cmd[cmd.index("--memory-swap") + 1] == esc.DOCKER_MEMORY
    assert cmd.index("--memory") < cmd.index(esc.IDF_IMAGE)


def test_require_makes_a_check_that_could_not_run_exit_3(monkeypatch, capsys):
    monkeypatch.setenv("HELIX_ESP32_SYNTAX", "0")
    monkeypatch.setattr(esc.sys, "argv", ["esp32_syntax_check.py", "--base", "HEAD"])
    assert esc.main() == 0
    monkeypatch.setattr(esc.sys, "argv", ["esp32_syntax_check.py", "--base", "HEAD", "--require"])
    assert esc.main() == esc.kCannotAnswer == 3
    assert "skipped" in capsys.readouterr().out


def test_require_turns_a_missing_docker_into_exit_3(monkeypatch, tmp_path):
    monkeypatch.delenv("HELIX_ESP32_SYNTAX", raising=False)
    monkeypatch.setattr(esc.shutil, "which", lambda name: None)
    monkeypatch.setattr(esc, "git", lambda tree, *args: {
        "rev-parse": str(tmp_path) + "\n",
        "diff": "src/a.cpp\n",
    }[args[0]])
    argv = ["esp32_syntax_check.py", "--tree", str(tmp_path), "--base", "A", "--head", "B"]
    monkeypatch.setattr(esc.sys, "argv", argv)
    assert esc.main() == 0
    monkeypatch.setattr(esc.sys, "argv", argv + ["--require"])
    assert esc.main() == 3
