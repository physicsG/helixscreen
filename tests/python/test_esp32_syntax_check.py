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
