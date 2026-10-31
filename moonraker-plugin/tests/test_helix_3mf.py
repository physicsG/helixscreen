# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for the helix_print .3mf converter (helix_3mf.py).

The Moonraker surface the converter touches is faked here with the same
shapes as Moonraker v0.9.3's file_manager (finalize_upload, delete_file),
database (insert_item / get_item / delete_item with list keys) and event
loop (run_in_thread), as vendored by Forge-X.

Run with: pytest tests/test_helix_3mf.py -v
"""

import asyncio
import hashlib
import io
import shutil
import sys
import time
import types
import zipfile
from pathlib import Path
from typing import Any, Dict, List, Optional

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

import helix_3mf
import helix_capabilities
import helix_file_queue as file_queue
from helix_3mf import (
    Conversion,
    ConversionError,
    Helix3mfConverter,
    extract_plate,
    is_3mf,
    output_stem,
    plan_output,
    sliced_plates,
    upload_hook_shape_ok,
)


# ============================================================================
# Archive builders
# ============================================================================

def plate_gcode(plate: int, tag: str = "") -> bytes:
    """A few hundred KB, so the extraction really streams in chunks."""
    body = "".join(f"G1 X{i % 200} Y{i % 150} E{i * 0.01:.2f} ; p{plate}{tag}\n"
                   for i in range(12000))
    return f"; HEADER_BLOCK_START\n; plate {plate}{tag}\n".encode() + body.encode()


def make_3mf(path: Path, plates: Dict[int, bytes], md5s: Optional[Dict[int, str]] = None,
             thumbnails: bool = True) -> None:
    """An Orca-shaped .gcode.3mf: deflated plate G-code, an uppercase md5
    beside each, stored PNGs for plates 1..11 whether sliced or not."""
    with zipfile.ZipFile(path, "w") as zf:
        zf.writestr("[Content_Types].xml", "<Types/>", zipfile.ZIP_DEFLATED)
        if thumbnails:
            for n in range(1, 12):
                zf.writestr(f"Metadata/plate_{n}.png", b"\x89PNG fake", zipfile.ZIP_STORED)
        for plate, data in plates.items():
            zf.writestr(f"Metadata/plate_{plate}.gcode", data, zipfile.ZIP_DEFLATED)
            md5 = (md5s or {}).get(plate, hashlib.md5(data).hexdigest().upper())
            zf.writestr(f"Metadata/plate_{plate}.gcode.md5", md5, zipfile.ZIP_DEFLATED)
            zf.writestr(f"Metadata/plate_{plate}.json", "{}", zipfile.ZIP_DEFLATED)


def md5_of(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


# ============================================================================
# Moonraker fakes
# ============================================================================

class ServerError(Exception):
    def __init__(self, message: str, status_code: int = 400):
        super().__init__(message)
        self.status_code = status_code


class FakeEventLoop:
    def run_in_thread(self, callback, *args):
        return asyncio.get_event_loop().run_in_executor(None, callback, *args)


class FakeServer:
    def __init__(self):
        self.event_handlers: Dict[str, List[Any]] = {}
        self.loop = FakeEventLoop()

    def get_event_loop(self):
        return self.loop

    def register_event_handler(self, event, callback):
        self.event_handlers.setdefault(event, []).append(callback)

    def error(self, message, code=400):
        return ServerError(message, code)


# The converter reads VALID_GCODE_EXTS from the file manager's own module, as
# it does from moonraker.components.file_manager.file_manager.
FM_MODULE = types.ModuleType("fake_moonraker_file_manager")
FM_MODULE.VALID_GCODE_EXTS = [".gcode", ".g", ".gco", ".ufp", ".nc"]
sys.modules[FM_MODULE.__name__] = FM_MODULE


class FakeFileManager:
    """finalize_upload / delete_file with Moonraker's observable behaviour:
    the temp file is moved into place, a loaded file refuses with 403, the
    upload result carries the item path, and `print=true` starts the print."""

    def __init__(self, gc_path: Path):
        self.gc_path = gc_path
        self.loaded: set = set()
        self.uploads: List[Dict[str, Any]] = []
        self.deletes: List[str] = []
        self.started: List[str] = []
        self.attempts: List[str] = []

    async def finalize_upload(self, form_args):
        rel = "/".join(p for p in (form_args.get("path", ""), form_args["filename"]) if p)
        self.attempts.append(rel)
        if rel in self.loaded:
            Path(form_args["tmp_file_path"]).unlink(missing_ok=True)
            raise ServerError("File is loaded, upload not permitted", 403)
        dest = self.gc_path / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(form_args["tmp_file_path"], dest)
        self.uploads.append(dict(form_args))
        started = False
        if form_args.get("print", "false") == "true" and rel.endswith(".gcode"):
            self.started.append(rel)
            started = True
        return {"item": {"path": rel, "root": "gcodes"}, "action": "create_file",
                "print_started": started, "print_queued": False}

    async def delete_file(self, path):
        rel = path.split("/", 1)[1]
        if rel in self.loaded:
            raise ServerError("File currently in use", 403)
        (self.gc_path / rel).unlink()
        self.deletes.append(rel)
        return {"item": {"path": rel, "root": "gcodes"}, "action": "delete_file"}


FakeFileManager.__module__ = FM_MODULE.__name__


class FakeDatabase:
    """Namespace items keyed by a one-element list, as the converter stores
    them so a dotted filename is not split into nested keys."""

    def __init__(self):
        self.ns: Dict[str, Dict[str, Any]] = {}

    async def insert_item(self, namespace, key, value):
        assert isinstance(key, list) and len(key) == 1
        self.ns.setdefault(namespace, {})[key[0]] = value

    async def get_item(self, namespace, key=None, default=None):
        items = self.ns.get(namespace)
        if key is None:
            return dict(items) if items is not None else default
        return items.get(key[0], default) if items else default

    async def delete_item(self, namespace, key):
        self.ns.get(namespace, {}).pop(key[0], None)


class FakeKlippyApis:
    def __init__(self):
        self.started: List[str] = []

    async def start_print(self, filename, wait_klippy_started=False, user=None):
        self.started.append(filename)
        return "ok"


class FakeRequest:
    def __init__(self, params=None):
        self.params = params or {}

    def get_str(self, key, default=None):
        return self.params.get(key, default)


@pytest.fixture
def gc(tmp_path):
    path = tmp_path / "gcodes"
    path.mkdir()
    (path / ".helix_temp").mkdir()
    return path


@pytest.fixture
def env(gc):
    server = FakeServer()
    fm = FakeFileManager(gc)
    db = FakeDatabase()
    apis = FakeKlippyApis()

    def make(start_on_upload=False):
        return Helix3mfConverter(server, fm, db, apis, gc, ".helix_temp", start_on_upload,
                                 helix_capabilities.prints_3mf_natively)

    return types.SimpleNamespace(server=server, fm=fm, db=db, apis=apis, gc=gc, make=make)


async def started(env, start_on_upload=False) -> Helix3mfConverter:
    conv = env.make(start_on_upload)
    await conv.start()
    await conv.wait_idle()
    return conv


async def notify(conv, action, path, source=None):
    payload = {"action": action, "item": {"root": "gcodes", "path": path}}
    if source is not None:
        payload["source_item"] = {"root": "gcodes", "path": source}
    await conv.on_filelist_changed(payload)
    await conv.wait_idle()


async def listing(conv, source=None):
    params = {"source": source} if source is not None else {}
    return (await conv.handle_conversions(FakeRequest(params)))["conversions"]


# ============================================================================
# Pure helpers
# ============================================================================

class TestNames:
    @pytest.mark.parametrize("name,expected", [
        ("benchy.gcode.3mf", True), ("dir/A.GCODE.3MF", True), ("x.3mf", True),
        ("benchy.gcode", False), ("x.3mf.gcode", False),
    ])
    def test_is_3mf(self, name, expected):
        assert is_3mf(name) is expected

    @pytest.mark.parametrize("source,stem", [
        ("benchy.gcode.3mf", "benchy"),
        ("dir/Part (1).GCODE.3MF", "dir/Part (1)"),
        ("plain.3mf", "plain"),
    ])
    def test_output_stem(self, source, stem):
        assert output_stem(source) == stem

    def test_sliced_plates_reads_only_plate_gcode_entries(self):
        names = [
            "Metadata/plate_9.gcode", "Metadata/plate_9.gcode.md5", "Metadata/plate_9.json",
            "Metadata/plate_10.gcode", "Metadata/plate_9.png", "Metadata/plate_x.gcode",
            "plate_3.gcode", "Metadata/plate_.gcode", "Metadata/plate_2.gcode",
        ]
        assert sliced_plates(names) == [2, 9, 10]


class TestExtractPlate:
    def test_streams_the_plate_and_returns_its_md5(self, tmp_path):
        data = plate_gcode(9)
        make_3mf(tmp_path / "a.gcode.3mf", {9: data})
        dest = tmp_path / "out.gcode"
        with zipfile.ZipFile(tmp_path / "a.gcode.3mf") as zf:
            md5 = extract_plate(zf, 9, dest)
        assert dest.read_bytes() == data
        assert md5 == md5_of(data)

    def test_a_wrong_slicer_md5_fails_and_leaves_nothing(self, tmp_path):
        make_3mf(tmp_path / "a.gcode.3mf", {1: plate_gcode(1)}, md5s={1: "0" * 32})
        dest = tmp_path / "out.gcode"
        with zipfile.ZipFile(tmp_path / "a.gcode.3mf") as zf:
            with pytest.raises(ConversionError, match="md5"):
                extract_plate(zf, 1, dest)
        assert not dest.exists()

    def test_no_md5_entry_still_extracts(self, tmp_path):
        data = plate_gcode(1)
        with zipfile.ZipFile(tmp_path / "a.3mf", "w") as zf:
            zf.writestr("Metadata/plate_1.gcode", data, zipfile.ZIP_DEFLATED)
        with zipfile.ZipFile(tmp_path / "a.3mf") as zf:
            assert extract_plate(zf, 1, tmp_path / "o.gcode") == md5_of(data)

    def test_corrupt_deflate_data_fails_and_leaves_nothing(self, tmp_path):
        archive = tmp_path / "a.gcode.3mf"
        make_3mf(archive, {1: plate_gcode(1)}, thumbnails=False)
        raw = bytearray(archive.read_bytes())
        with zipfile.ZipFile(archive) as zf:
            info = zf.getinfo("Metadata/plate_1.gcode")
        data_start = info.header_offset + 30 + len(info.filename.encode())
        for i in range(data_start + 200, data_start + 400):
            raw[i] ^= 0x5A
        archive.write_bytes(bytes(raw))
        dest = tmp_path / "out.gcode"
        with zipfile.ZipFile(archive) as zf:
            with pytest.raises(ConversionError):
                extract_plate(zf, 1, dest)
        assert not dest.exists()


class TestPlanOutput:
    def test_free_name_is_written_and_owned(self, gc):
        assert plan_output(gc, "a.gcode.3mf", 9, False, "m", []) == ("write", "a.gcode", True)

    def test_identical_foreign_file_is_reused_not_owned(self, gc):
        data = plate_gcode(9)
        (gc / "a.gcode").write_bytes(data)
        assert plan_output(gc, "a.gcode.3mf", 9, False, md5_of(data), []) == (
            "reuse", "a.gcode", False)

    def test_different_foreign_file_falls_back_to_the_plate_name(self, gc):
        (gc / "a.gcode").write_bytes(b"user's own slice")
        assert plan_output(gc, "a.gcode.3mf", 9, False, "m", []) == (
            "write", "a_plate9.gcode", True)

    def test_our_own_output_of_this_source_is_overwritten(self, gc):
        (gc / "a.gcode").write_bytes(b"old plate")
        ours = Conversion("a.gcode.3mf", plate=9, output="a.gcode",
                          output_md5=md5_of(b"old plate"), state="done", owned=True)
        assert plan_output(gc, "a.gcode.3mf", 9, False, "new", [ours]) == (
            "write", "a.gcode", True)

    def test_our_output_edited_since_is_foreign(self, gc):
        (gc / "a.gcode").write_bytes(b"edited by the user")
        ours = Conversion("a.gcode.3mf", plate=9, output="a.gcode",
                          output_md5=md5_of(b"old plate"), state="done", owned=True)
        assert plan_output(gc, "a.gcode.3mf", 9, False, "new", [ours]) == (
            "write", "a_plate9.gcode", True)

    def test_another_sources_output_is_foreign(self, gc):
        (gc / "a.gcode").write_bytes(b"other")
        theirs = Conversion("a.3mf", plate=1, output="a.gcode",
                            output_md5=md5_of(b"other"), state="done", owned=True)
        assert plan_output(gc, "a.gcode.3mf", 9, False, "new", [theirs]) == (
            "write", "a_plate9.gcode", True)

    def test_multi_plate_skips_the_bare_name(self, gc):
        assert plan_output(gc, "a.gcode.3mf", 2, True, "m", []) == (
            "write", "a_plate2.gcode", True)

    def test_every_name_taken(self, gc):
        (gc / "a.gcode").write_bytes(b"x")
        (gc / "a_plate9.gcode").write_bytes(b"y")
        assert plan_output(gc, "a.gcode.3mf", 9, False, "m", []) == ("taken", "", False)

    def test_a_directory_in_the_way_is_skipped(self, gc):
        (gc / "a.gcode").mkdir()
        assert plan_output(gc, "a.gcode.3mf", 9, False, "m", []) == (
            "write", "a_plate9.gcode", True)


class TestUploadHookShape:
    def test_accepts_async_single_argument(self):
        async def finalize_upload(form_args):
            return {}
        assert upload_hook_shape_ok(finalize_upload)

    @pytest.mark.parametrize("method", [
        None,
        lambda form_args: {},
    ])
    def test_rejects_missing_or_sync(self, method):
        assert not upload_hook_shape_ok(method)

    def test_rejects_another_signature(self):
        async def finalize_upload(form_args, extra):
            return {}
        assert not upload_hook_shape_ok(finalize_upload)


# ============================================================================
# Converter
# ============================================================================

class TestConversion:
    @pytest.mark.asyncio
    async def test_upload_converts_and_the_endpoint_reports_the_contract(self, env):
        data = plate_gcode(9)
        make_3mf(env.gc / "a.gcode.3mf", {9: data})
        conv = await started(env)
        env.fm.uploads.clear()

        st = (env.gc / "a.gcode.3mf").stat()
        entries = await listing(conv)
        assert entries == [{
            "source": "a.gcode.3mf", "source_mtime": st.st_mtime, "source_size": st.st_size,
            "plate": 9, "plate_md5": md5_of(data), "output": "a.gcode",
            "output_md5": md5_of(data), "state": "done", "error": "",
        }]
        assert (env.gc / "a.gcode").read_bytes() == data
        assert list((env.gc / ".helix_temp").iterdir()) == []

    @pytest.mark.asyncio
    async def test_output_is_published_through_moonraker_upload(self, env):
        conv = await started(env)
        (env.gc / "sub").mkdir()
        make_3mf(env.gc / "sub" / "a.gcode.3mf", {1: plate_gcode(1)})
        await notify(conv, "create_file", "sub/a.gcode.3mf")
        [upload] = env.fm.uploads
        assert upload["root"] == "gcodes"
        assert upload["path"] == "sub" and upload["filename"] == "a.gcode"
        assert upload["print"] == "false"
        assert upload["tmp_file_path"].startswith(str(env.gc / ".helix_temp"))

    @pytest.mark.asyncio
    async def test_existing_different_sibling_is_kept(self, env):
        (env.gc / "a.gcode").write_bytes(b"; the user's own upload\n")
        make_3mf(env.gc / "a.gcode.3mf", {9: plate_gcode(9)})
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["output"] == "a_plate9.gcode"
        assert (env.gc / "a.gcode").read_bytes() == b"; the user's own upload\n"

    @pytest.mark.asyncio
    async def test_project_3mf_gets_one_failed_entry(self, env):
        make_3mf(env.gc / "project.3mf", {})
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and entry["plate"] == 0
        assert entry["output"] == "" and "no sliced plate" in entry["error"]

    @pytest.mark.asyncio
    async def test_not_a_zip_fails(self, env):
        (env.gc / "junk.gcode.3mf").write_bytes(b"not a zip at all")
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and "readable" in entry["error"]

    @pytest.mark.asyncio
    async def test_multi_plate_writes_one_file_per_plate(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {2: plate_gcode(2), 5: plate_gcode(5)})
        conv = await started(env)
        entries = await listing(conv)
        assert [(e["plate"], e["output"]) for e in entries] == [
            (2, "a_plate2.gcode"), (5, "a_plate5.gcode")]
        assert not (env.gc / "a.gcode").exists()

    @pytest.mark.asyncio
    async def test_too_little_free_space_fails_before_writing(self, env, monkeypatch):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        usage = shutil.disk_usage(str(env.gc))
        monkeypatch.setattr(
            helix_3mf.shutil, "disk_usage",
            lambda p: usage._replace(free=len(plate_gcode(1)) * 2 - 1))
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and "space" in entry["error"]
        assert env.fm.attempts == []

    @pytest.mark.asyncio
    async def test_bad_md5_fails_and_publishes_nothing(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)}, md5s={1: "F" * 32})
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and "md5" in entry["error"]
        assert env.fm.uploads == []
        assert list((env.gc / ".helix_temp").iterdir()) == []

    @pytest.mark.asyncio
    async def test_source_filter(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        make_3mf(env.gc / "b.gcode.3mf", {1: plate_gcode(1, "b")})
        conv = await started(env)
        assert [e["source"] for e in await listing(conv, "b.gcode.3mf")] == ["b.gcode.3mf"]
        assert await listing(conv, "nope.gcode.3mf") == []
        assert len(await listing(conv)) == 2

    @pytest.mark.asyncio
    async def test_records_persist_across_restart(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        first = await started(env)
        before = await listing(first)
        uploads = len(env.fm.uploads)

        second = await started(env)
        assert await listing(second) == before
        assert len(env.fm.uploads) == uploads


class TestNativeAndDisabled:
    @pytest.mark.asyncio
    async def test_native_3mf_moonraker_converts_nothing(self, env, monkeypatch):
        monkeypatch.setattr(FM_MODULE, "VALID_GCODE_EXTS", [".gcode", ".3mf"])
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        assert not conv.active
        assert await listing(conv) == []
        assert "file_manager:filelist_changed" not in env.server.event_handlers
        assert env.fm.uploads == []


def load_fake_moonraker(name: str, source: str) -> types.ModuleType:
    """Import @p source as a module named @p name, as Moonraker imports its
    file_manager: the probes read the module the FileManager class lives in."""
    module = types.ModuleType(name)
    exec(compile(source, name, "exec"), module.__dict__)
    sys.modules[name] = module
    return module


# QIDI's Moonraker fork (Q2/Max 4 file_manager.py), cut to the parts the
# probes read: the extension list beside FileManager, the .3mf upload method,
# and MetadataStorage's .3mf thumbnail parser.
QIDI_FILE_MANAGER = """
VALID_GCODE_EXTS = ['.gcode', '.g', '.gco', '.ufp', '.nc', '.3mf']

class FileManager:
    def __init__(self, gc_path):
        self.gc_path = gc_path
        self.gcode_metadata = MetadataStorage()
    async def _finish_3mf_upload(self, upload_info):
        return {}

class MetadataStorage:
    def parse_3mf_thumbnail(self, path):
        return None
"""

# A fork that kept upstream's extension list but finishes .3mf uploads itself.
METHOD_ONLY_FILE_MANAGER = """
VALID_GCODE_EXTS = ['.gcode', '.g', '.gco', '.ufp', '.nc']

class FileManager:
    def __init__(self, gc_path):
        self.gc_path = gc_path
    async def _finish_3mf_upload(self, upload_info):
        return {}
"""


class TestNativeForks:
    @pytest.mark.asyncio
    @pytest.mark.parametrize("name,source", [
        ("fake_qidi_file_manager", QIDI_FILE_MANAGER),
        ("fake_method_only_file_manager", METHOD_ONLY_FILE_MANAGER),
    ])
    async def test_a_native_3mf_fork_converts_nothing(self, env, name, source):
        module = load_fake_moonraker(name, source)
        fork_fm = module.FileManager(env.gc)
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = Helix3mfConverter(env.server, fork_fm, env.db, env.apis, env.gc,
                                 ".helix_temp", True, helix_capabilities.prints_3mf_natively)
        await conv.start()
        await conv.wait_idle()

        assert not conv.active and not conv.hook_installed
        assert not (env.gc / "a.gcode").exists()
        assert await listing(conv) == []


class TestOverwriteAndDelete:
    @pytest.mark.asyncio
    async def test_reupload_reconverts_and_drops_vanished_plates(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {2: plate_gcode(2), 5: plate_gcode(5)})
        conv = await started(env)

        new = plate_gcode(7, "v2")
        make_3mf(src, {7: new})
        await notify(conv, "create_file", "a.gcode.3mf")

        [entry] = await listing(conv)
        assert entry["output"] == "a.gcode" and entry["plate"] == 7
        assert (env.gc / "a.gcode").read_bytes() == new
        assert not (env.gc / "a_plate2.gcode").exists()
        assert not (env.gc / "a_plate5.gcode").exists()

    @pytest.mark.asyncio
    async def test_reupload_overwrites_our_own_output_in_place(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {9: plate_gcode(9)})
        conv = await started(env)
        new = plate_gcode(9, "v2")
        make_3mf(src, {9: new})
        await notify(conv, "create_file", "a.gcode.3mf")
        [entry] = await listing(conv)
        assert entry["output"] == "a.gcode"
        assert (env.gc / "a.gcode").read_bytes() == new

    @pytest.mark.asyncio
    async def test_delete_removes_our_unchanged_output(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "a.gcode.3mf").unlink()
        await notify(conv, "delete_file", "a.gcode.3mf")
        assert env.fm.deletes == ["a.gcode"]
        assert await listing(conv) == []
        assert env.db.ns["helix_3mf"] == {}

    @pytest.mark.asyncio
    async def test_delete_keeps_an_output_edited_since(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "a.gcode").write_bytes(b"; tuned by hand\n")
        (env.gc / "a.gcode.3mf").unlink()
        await notify(conv, "delete_file", "a.gcode.3mf")
        assert env.fm.deletes == []
        assert (env.gc / "a.gcode").exists()

    @pytest.mark.asyncio
    async def test_delete_keeps_a_reused_foreign_file(self, env):
        data = plate_gcode(1)
        (env.gc / "a.gcode").write_bytes(data)
        make_3mf(env.gc / "a.gcode.3mf", {1: data})
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["output"] == "a.gcode" and entry["state"] == "done"
        assert env.fm.uploads == []
        (env.gc / "a.gcode.3mf").unlink()
        await notify(conv, "delete_file", "a.gcode.3mf")
        assert env.fm.deletes == []
        assert (env.gc / "a.gcode").exists()

    @pytest.mark.asyncio
    async def test_a_deleted_output_is_not_recreated(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "a.gcode").unlink()
        await notify(conv, "delete_file", "a.gcode")
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and entry["output"] == ""

        uploads = len(env.fm.uploads)
        again = await started(env)
        assert len(env.fm.uploads) == uploads
        assert not (env.gc / "a.gcode").exists()
        assert (await listing(again))[0]["state"] == "failed"

    @pytest.mark.asyncio
    async def test_an_output_deleted_while_offline_is_not_recreated(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        await started(env)
        (env.gc / "a.gcode").unlink()
        uploads = len(env.fm.uploads)
        again = await started(env)
        [entry] = await listing(again)
        assert entry["state"] == "failed" and "deleted" in entry["error"]
        assert len(env.fm.uploads) == uploads
        assert env.db.ns["helix_3mf"]["a.gcode.3mf"][0]["state"] == "failed"

    @pytest.mark.asyncio
    async def test_moving_the_source_rekeys_its_record(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "a.gcode.3mf").rename(env.gc / "b.gcode.3mf")
        await notify(conv, "move_file", "b.gcode.3mf", source="a.gcode.3mf")
        [entry] = await listing(conv)
        assert entry["source"] == "b.gcode.3mf" and entry["output"] == "a.gcode"
        assert env.fm.deletes == []

    @pytest.mark.asyncio
    async def test_moving_an_output_follows_it(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "a.gcode").rename(env.gc / "kept.gcode")
        await notify(conv, "move_file", "kept.gcode", source="a.gcode")
        assert (await listing(conv))[0]["output"] == "kept.gcode"

    @pytest.mark.asyncio
    async def test_source_vanished_while_offline_is_cleaned_at_startup(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        await started(env)
        (env.gc / "a.gcode.3mf").unlink()
        again = await started(env)
        assert env.fm.deletes == ["a.gcode"]
        assert await listing(again) == []


class TestLoadedFile:
    @pytest.mark.asyncio
    async def test_output_being_printed_waits_for_the_print_to_end(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)})
        conv = await started(env)
        env.fm.loaded.add("a.gcode")
        new = plate_gcode(1, "v2")
        make_3mf(src, {1: new})
        await notify(conv, "create_file", "a.gcode.3mf")
        [entry] = await listing(conv)
        assert entry["state"] == "pending"
        assert (env.gc / "a.gcode").read_bytes() == plate_gcode(1)

        attempts = len(env.fm.attempts)
        await conv.on_job_state_changed(None, {"state": "printing"}, {"state": "printing"})
        await conv.wait_idle()
        assert (await listing(conv))[0]["state"] == "pending"
        assert len(env.fm.attempts) == attempts

        env.fm.loaded.clear()
        await conv.on_job_state_changed(None, {"state": "printing"}, {"state": "complete"})
        await conv.wait_idle()
        [entry] = await listing(conv)
        assert entry["state"] == "done"
        assert (env.gc / "a.gcode").read_bytes() == new


class TestStartOnUpload:
    async def upload(self, conv_env, name, plates, print_flag="true"):
        """What Moonraker's upload route does: the zip lands in a temp file
        and finalize_upload is called with the form arguments."""
        tmp = conv_env.gc / ".helix_temp" / "incoming.mru"
        make_3mf(tmp, plates)
        args = {"filename": name, "tmp_file_path": str(tmp), "current_user": "orca"}
        if print_flag is not None:
            args["print"] = print_flag
        return await conv_env.fm.finalize_upload(args)

    @pytest.mark.asyncio
    async def test_off_by_default_leaves_moonraker_untouched(self, env):
        original = env.fm.finalize_upload
        conv = await started(env)
        assert env.fm.finalize_upload == original
        assert not conv.hook_installed

    @pytest.mark.asyncio
    async def test_print_flag_starts_the_converted_plate(self, env):
        conv = await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True
        assert env.fm.started == ["a.gcode"]
        # The zip itself was stored without the print flag.
        assert "print" not in env.fm.uploads[0]
        assert env.fm.uploads[1]["print"] == "true"
        assert env.fm.uploads[1]["current_user"] == "orca"

    @pytest.mark.asyncio
    async def test_no_print_flag_converts_without_starting(self, env):
        conv = await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)}, print_flag="false")
        await notify(conv, "create_file", "a.gcode.3mf")
        assert not result.get("print_started")
        assert env.fm.started == [] and env.apis.started == []
        assert (env.gc / "a.gcode").exists()

    @pytest.mark.asyncio
    async def test_multi_plate_is_converted_but_not_started(self, env):
        conv = await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {1: plate_gcode(1), 2: plate_gcode(2)})
        assert result["print_started"] is False
        assert env.fm.started == [] and env.apis.started == []
        assert (env.gc / "a_plate1.gcode").exists()

    @pytest.mark.asyncio
    async def test_identical_existing_file_is_started_directly(self, env):
        data = plate_gcode(9)
        (env.gc / "a.gcode").write_bytes(data)
        conv = await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: data})
        assert result["print_started"] is True
        assert env.apis.started == ["a.gcode"]

    @pytest.mark.asyncio
    async def test_a_gcode_upload_passes_through(self, env):
        conv = await started(env, start_on_upload=True)
        tmp = env.gc / ".helix_temp" / "x.mru"
        tmp.write_bytes(b"G28\n")
        result = await env.fm.finalize_upload(
            {"filename": "x.gcode", "tmp_file_path": str(tmp), "print": "true"})
        assert result["print_started"] is True
        assert env.fm.uploads == [{"filename": "x.gcode", "tmp_file_path": str(tmp),
                                   "print": "true"}]

    @pytest.mark.asyncio
    async def test_unexpected_shape_disables_only_the_hook(self, env):
        async def finalize_upload(form_args, extra=None):
            return {}
        env.fm.finalize_upload = finalize_upload
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = env.make(start_on_upload=True)
        env.fm.finalize_upload = finalize_upload
        await conv.start()
        assert not conv.hook_installed
        assert env.fm.finalize_upload is finalize_upload
        assert conv.active

    @pytest.mark.asyncio
    async def test_close_restores_moonraker_method(self, env):
        original = env.fm.finalize_upload
        conv = await started(env, start_on_upload=True)
        assert env.fm.finalize_upload != original
        await conv.close()
        assert env.fm.finalize_upload == original


class TestHiddenPaths:
    @pytest.mark.asyncio
    async def test_staging_directories_are_ignored(self, env):
        conv = await started(env)
        (env.gc / ".helix_temp" / "x.gcode.3mf").write_bytes(b"PK")
        make_3mf(env.gc / ".helix_temp" / "x.gcode.3mf", {1: plate_gcode(1)})
        await notify(conv, "create_file", ".helix_temp/x.gcode.3mf")
        assert await listing(conv) == []
        assert env.fm.uploads == []


    @pytest.mark.asyncio
    async def test_a_file_moved_out_of_staging_is_converted(self, env):
        conv = await started(env)
        make_3mf(env.gc / ".helix_temp" / "x.gcode.3mf", {1: plate_gcode(1)})
        (env.gc / ".helix_temp" / "x.gcode.3mf").rename(env.gc / "x.gcode.3mf")
        await notify(conv, "move_file", "x.gcode.3mf", source=".helix_temp/x.gcode.3mf")
        [entry] = await listing(conv)
        assert entry["source"] == "x.gcode.3mf"


class TestMovedDirectory:
    @pytest.mark.asyncio
    async def test_a_moved_directory_moves_its_sources_records(self, env):
        (env.gc / "d").mkdir()
        make_3mf(env.gc / "d" / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        (env.gc / "d").rename(env.gc / "e")
        await notify(conv, "move_dir", "e", source="d")
        assert [c["source"] for c in await listing(conv)] == ["e/a.gcode.3mf"]
        assert sorted(env.db.ns["helix_3mf"]) == ["e/a.gcode.3mf"]


class TestRecordsLoad:
    @pytest.mark.asyncio
    async def test_records_load_where_only_ns_items_reads_a_namespace(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        await started(env)
        stored = env.db.ns

        class NsItemsOnly(FakeDatabase):
            async def get_item(self, namespace, key=None, default=None):
                if key is None:
                    raise KeyError(namespace)
                return await super().get_item(namespace, key, default)

            async def ns_items(self, namespace):
                return list(self.ns.get(namespace, {}).items())

        env.db = NsItemsOnly()
        env.db.ns = stored
        conv = Helix3mfConverter(env.server, env.fm, env.db, env.apis, env.gc, ".helix_temp",
                                 False, helix_capabilities.prints_3mf_natively)
        await conv._load_records()
        assert list(conv.records) == ["a.gcode.3mf"]


# ============================================================================
# Review hardening
# ============================================================================

def patch_central_entry(archive: Path, name: str, flag_bits: Optional[int] = None,
                        method: Optional[int] = None) -> None:
    """Rewrite one entry's flag bits and/or compression method in the zip's
    headers, the way an encrypted or AES-compressed slicer export carries
    them. zipfile reads both from the central directory."""
    raw = bytearray(archive.read_bytes())
    encoded = name.encode()
    pos = 0
    while True:
        pos = raw.find(b"PK\x01\x02", pos)
        assert pos >= 0, f"{name} not in central directory"
        name_len = int.from_bytes(raw[pos + 28:pos + 30], "little")
        if bytes(raw[pos + 46:pos + 46 + name_len]) == encoded:
            break
        pos += 4
    local = int.from_bytes(raw[pos + 42:pos + 46], "little")
    if flag_bits is not None:
        raw[pos + 8:pos + 10] = flag_bits.to_bytes(2, "little")
        raw[local + 6:local + 8] = flag_bits.to_bytes(2, "little")
    if method is not None:
        raw[pos + 10:pos + 12] = method.to_bytes(2, "little")
        raw[local + 8:local + 10] = method.to_bytes(2, "little")
    archive.write_bytes(bytes(raw))


class TestUnreadableEntries:
    @pytest.mark.asyncio
    async def test_an_encrypted_plate_is_a_failed_record(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)}, thumbnails=False)
        patch_central_entry(src, "Metadata/plate_1.gcode", flag_bits=0x1)
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed"
        assert "RuntimeError" in entry["error"]
        assert env.fm.uploads == [] and list((env.gc / ".helix_temp").iterdir()) == []

    @pytest.mark.asyncio
    async def test_an_encrypted_md5_entry_is_a_failed_record(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)}, thumbnails=False)
        patch_central_entry(src, "Metadata/plate_1.gcode.md5", flag_bits=0x1)
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and "RuntimeError" in entry["error"]

    @pytest.mark.asyncio
    async def test_an_unsupported_compression_method_is_a_failed_record(self, env):
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)}, thumbnails=False)
        patch_central_entry(src, "Metadata/plate_1.gcode", method=99)
        conv = await started(env)
        [entry] = await listing(conv)
        assert entry["state"] == "failed" and "NotImplementedError" in entry["error"]

    def test_a_plate_missing_from_the_archive_is_a_conversion_error(self, tmp_path):
        src = tmp_path / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)}, thumbnails=False)
        dest = tmp_path / "out.partial"
        with pytest.raises(ConversionError, match="KeyError"):
            Helix3mfConverter._extract(src, 5, dest)
        assert not dest.exists()

    def test_an_oversized_md5_entry_is_not_read(self, tmp_path):
        src = tmp_path / "a.gcode.3mf"
        data = plate_gcode(1)
        with zipfile.ZipFile(src, "w") as zf:
            zf.writestr("Metadata/plate_1.gcode", data, zipfile.ZIP_DEFLATED)
            zf.writestr("Metadata/plate_1.gcode.md5",
                        md5_of(data) + " " * 1000, zipfile.ZIP_DEFLATED)
        with zipfile.ZipFile(src) as zf:
            assert helix_3mf.read_plate_md5(zf, 1) is None
        small = tmp_path / "b.gcode.3mf"
        with zipfile.ZipFile(small, "w") as zf:
            zf.writestr("Metadata/plate_1.gcode", data, zipfile.ZIP_DEFLATED)
            zf.writestr("Metadata/plate_1.gcode.md5", md5_of(data).upper() + "\r\n",
                        zipfile.ZIP_DEFLATED)
        with zipfile.ZipFile(small) as zf:
            assert helix_3mf.read_plate_md5(zf, 1) == md5_of(data)


class TestSymlinkAtTheOutputName:
    def test_a_symlink_to_an_identical_file_is_foreign(self, gc):
        data = plate_gcode(9)
        (gc / "elsewhere.gcode").write_bytes(data)
        (gc / "a.gcode").symlink_to(gc / "elsewhere.gcode")
        assert plan_output(gc, "a.gcode.3mf", 9, False, md5_of(data), []) == (
            "write", "a_plate9.gcode", True)

    def test_a_symlink_named_in_our_own_record_is_still_foreign(self, gc):
        data = b"old plate"
        (gc / "elsewhere.gcode").write_bytes(data)
        (gc / "a.gcode").symlink_to(gc / "elsewhere.gcode")
        ours = Conversion("a.gcode.3mf", plate=9, output="a.gcode",
                          output_md5=md5_of(data), state="done", owned=True)
        assert plan_output(gc, "a.gcode.3mf", 9, False, "new", [ours]) == (
            "write", "a_plate9.gcode", True)
        assert (gc / "elsewhere.gcode").read_bytes() == data


class TestPartialSweep:
    @pytest.mark.asyncio
    async def test_startup_removes_unfinished_extractions(self, env):
        temp = env.gc / ".helix_temp"
        (temp / "3mf-0123abcd.partial").write_bytes(b"half a plate")
        (temp / "modified_1_benchy.gcode").write_text("G28\n")
        await started(env)
        assert not (temp / "3mf-0123abcd.partial").exists()
        assert (temp / "modified_1_benchy.gcode").exists()


class TestRaces:
    @pytest.mark.asyncio
    async def test_a_user_upload_arriving_mid_extraction_is_never_overwritten(
        self, env, monkeypatch
    ):
        conv = await started(env)
        data = plate_gcode(9)
        make_3mf(env.gc / "a.gcode.3mf", {9: data})
        real_extract = Helix3mfConverter._extract
        arrivals = []

        def extract_while_user_uploads(full, plate, dest):
            md5 = real_extract(full, plate, dest)
            if not arrivals:
                (env.gc / "a.gcode").write_bytes(b"; the user's own slice\n")
                arrivals.append(True)
            return md5

        monkeypatch.setattr(Helix3mfConverter, "_extract",
                            staticmethod(extract_while_user_uploads))
        await notify(conv, "create_file", "a.gcode.3mf")
        await conv.wait_idle()

        assert (env.gc / "a.gcode").read_bytes() == b"; the user's own slice\n"
        assert [u["filename"] for u in env.fm.uploads] == ["a_plate9.gcode"]
        [entry] = await listing(conv)
        assert entry["output"] == "a_plate9.gcode" and entry["state"] == "done"
        assert conv.records["a.gcode.3mf"][0].owned
        assert list((env.gc / ".helix_temp").iterdir()) == []

    @pytest.mark.asyncio
    async def test_a_source_deleted_mid_conversion_leaves_nothing(self, env, monkeypatch):
        conv = await started(env)
        src = env.gc / "a.gcode.3mf"
        make_3mf(src, {1: plate_gcode(1)})
        real_finalize = env.fm.finalize_upload

        async def finalize_then_user_deletes(form_args):
            result = await real_finalize(form_args)
            src.unlink()
            await conv.on_filelist_changed(
                {"action": "delete_file", "item": {"root": "gcodes", "path": "a.gcode.3mf"}})
            return result

        env.fm.finalize_upload = finalize_then_user_deletes
        await notify(conv, "create_file", "a.gcode.3mf")

        assert await listing(conv) == []
        assert not (env.gc / "a.gcode").exists()
        assert "a.gcode.3mf" not in env.db.ns.get("helix_3mf", {})


class TestDeferredDelete:
    @pytest.mark.asyncio
    async def test_an_output_in_use_is_deleted_when_the_print_ends(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        env.fm.loaded.add("a.gcode")
        (env.gc / "a.gcode.3mf").unlink()
        await notify(conv, "delete_file", "a.gcode.3mf")
        assert (env.gc / "a.gcode").exists()
        assert env.db.ns["helix_3mf_deferred"]["outputs"][0]["output"] == "a.gcode"

        # A restart in between does not lose it.
        again = await started(env)
        env.fm.loaded.clear()
        await again.on_job_state_changed(None, {"state": "printing"}, {"state": "complete"})

        assert not (env.gc / "a.gcode").exists()
        assert env.db.ns["helix_3mf_deferred"]["outputs"] == []

    @pytest.mark.asyncio
    async def test_a_deferred_output_edited_meanwhile_is_kept(self, env):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1)})
        conv = await started(env)
        env.fm.loaded.add("a.gcode")
        (env.gc / "a.gcode.3mf").unlink()
        await notify(conv, "delete_file", "a.gcode.3mf")
        (env.gc / "a.gcode").write_bytes(b"; edited\n")
        env.fm.loaded.clear()
        await conv.on_job_state_changed(None, {"state": "printing"}, {"state": "complete"})
        assert (env.gc / "a.gcode").read_bytes() == b"; edited\n"


async def opted_in():
    return True


async def opted_out():
    return False


class TestStartGate:
    """A start gate (the fit check's) decides every print the converter starts."""

    async def upload(self, env, name, plates):
        tmp = env.gc / ".helix_temp" / "incoming.mru"
        make_3mf(tmp, plates)
        return await env.fm.finalize_upload(
            {"filename": name, "tmp_file_path": str(tmp), "print": "true",
             "current_user": "orca"})

    @pytest.mark.asyncio
    async def test_a_refusing_gate_keeps_the_new_plate_and_starts_nothing(self, env):
        conv = await started(env, start_on_upload=True)
        asked = []

        async def gate(output, remaining):
            asked.append(output)
            return False
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert asked == ["a.gcode"]
        assert result["print_started"] is False and result["print_queued"] is False
        assert env.fm.started == [] and env.apis.started == []
        # Published without the print flag, so Moonraker did not start it either.
        assert env.fm.uploads[1].get("print") == "false"
        assert (env.gc / "a.gcode").exists()

    @pytest.mark.asyncio
    async def test_an_allowing_gate_starts_the_published_plate(self, env):
        conv = await started(env, start_on_upload=True)

        async def gate(output, remaining):
            return True
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True
        assert env.apis.started == ["a.gcode"]

    @pytest.mark.asyncio
    async def test_a_gate_that_raises_starts_unchecked(self, env):
        conv = await started(env, start_on_upload=True)

        async def gate(output, remaining):
            raise RuntimeError("check broke")
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True
        assert env.apis.started == ["a.gcode"]

    @pytest.mark.asyncio
    async def test_a_reused_output_asks_the_gate_too(self, env):
        data = plate_gcode(9)
        (env.gc / "a.gcode").write_bytes(data)
        conv = await started(env, start_on_upload=True)

        async def gate(output, remaining):
            return False
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: data})
        assert result["print_started"] is False
        assert env.apis.started == []


class TestHeldUploadDeadline:
    """With a start gate the upload is held: one deadline from the hook's entry
    covers conversion and the check, and past it nothing starts."""

    async def upload(self, env, name, plates):
        tmp = env.gc / ".helix_temp" / "incoming.mru"
        make_3mf(tmp, plates)
        return await env.fm.finalize_upload(
            {"filename": name, "tmp_file_path": str(tmp), "print": "true",
             "current_user": "orca"})

    @pytest.mark.asyncio
    async def test_a_slow_conversion_answers_not_started_and_never_starts(self, env,
                                                                          monkeypatch):
        monkeypatch.setattr(file_queue, "UPLOAD_HOLD_S", 0.2)
        conv = await started(env, start_on_upload=True)
        real_extract = conv._extract

        def slow_extract(full, plate, dest):
            time.sleep(0.4)
            return real_extract(full, plate, dest)
        monkeypatch.setattr(conv, "_extract", slow_extract)
        asked = []

        async def gate(output, remaining):
            asked.append(remaining)
            return True
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is False and result["print_queued"] is False
        await conv.wait_idle()
        assert (env.gc / "a.gcode").exists()      # converted all the same
        assert env.apis.started == []             # but never started
        assert asked == [0.0]                     # the check got no time left

    @pytest.mark.asyncio
    async def test_the_check_gets_what_conversion_left(self, env, monkeypatch):
        monkeypatch.setattr(file_queue, "UPLOAD_HOLD_S", 5.0)
        conv = await started(env, start_on_upload=True)
        asked = []

        async def gate(output, remaining):
            asked.append(remaining)
            return True
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True
        assert len(asked) == 1 and 0.0 < asked[0] < 5.0

    @pytest.mark.asyncio
    async def test_a_check_still_running_at_the_deadline_starts_nothing(self, env,
                                                                        monkeypatch):
        monkeypatch.setattr(file_queue, "UPLOAD_HOLD_S", 0.2)
        conv = await started(env, start_on_upload=True)

        async def gate(output, remaining):
            await asyncio.sleep(0.4)
            return True
        conv.start_gate = gate
        conv.hold_wanted = opted_in
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is False
        await conv.wait_idle()
        assert env.apis.started == []

    @pytest.mark.asyncio
    async def test_opted_out_a_slow_conversion_still_starts_as_before(self, env, monkeypatch):
        monkeypatch.setattr(file_queue, "UPLOAD_HOLD_S", 0.2)
        conv = await started(env, start_on_upload=True)
        real_extract = conv._extract

        def slow_extract(full, plate, dest):
            time.sleep(0.4)
            return real_extract(full, plate, dest)
        monkeypatch.setattr(conv, "_extract", slow_extract)
        asked = []

        async def gate(output, remaining):
            asked.append(output)
            return False
        conv.start_gate = gate
        conv.hold_wanted = opted_out
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True
        assert asked == []                          # no gate without the opt-in
        assert env.fm.uploads[-1]["print"] == "true"  # Moonraker's own start, as before

    @pytest.mark.asyncio
    async def test_a_resend_joining_a_hold_that_gave_up_gets_its_own_chance(self, env):
        conv = env.make(True)
        shared = helix_3mf._Request()
        conv._queue.put = lambda key, make, front=False: shared  # still queued: one request
        request, _w = conv._enqueue("a.gcode.3mf", True, "orca", True, deadline=1.0)
        request.abandoned = True
        again, _w2 = conv._enqueue("a.gcode.3mf", True, "orca", True, deadline=2.0)
        assert again is request
        assert again.abandoned is False
        assert again.deadline == 2.0

    @pytest.mark.asyncio
    async def test_without_a_gate_nothing_is_cut_short(self, env, monkeypatch):
        monkeypatch.setattr(file_queue, "UPLOAD_HOLD_S", 0.0)
        await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is True


class FakeJobQueue:
    def __init__(self):
        self.queued: List[str] = []

    async def queue_job(self, filenames, check_exists=True, user=None):
        self.queued.append(filenames)


class TestStartOnUploadQueueing:
    async def upload(self, env, name, plates):
        tmp = env.gc / ".helix_temp" / "incoming.mru"
        make_3mf(tmp, plates)
        return await env.fm.finalize_upload(
            {"filename": name, "tmp_file_path": str(tmp), "print": "true",
             "current_user": "orca"})

    @pytest.mark.asyncio
    async def test_a_busy_printer_queues_a_reused_output(self, env):
        data = plate_gcode(9)
        (env.gc / "a.gcode").write_bytes(data)
        queue = FakeJobQueue()
        env.server.components = {"job_queue": queue}
        env.server.lookup_component = lambda name, default=None: env.server.components.get(
            name, default)
        env.fm.queue_gcodes = True

        async def refuse(filename, wait_klippy_started=False, user=None):
            raise ServerError("Printer busy", 409)
        env.apis.start_print = refuse

        await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: data})

        assert result["print_started"] is False and result["print_queued"] is True
        assert queue.queued == ["a.gcode"]

    @pytest.mark.asyncio
    async def test_without_queueing_a_refused_start_is_neither(self, env):
        data = plate_gcode(9)
        (env.gc / "a.gcode").write_bytes(data)

        async def refuse(filename, wait_klippy_started=False, user=None):
            raise ServerError("Printer busy", 409)
        env.apis.start_print = refuse

        await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: data})

        assert result["print_started"] is False and result["print_queued"] is False

    @pytest.mark.asyncio
    async def test_a_published_output_reports_moonrakers_queue_answer(self, env):
        real = env.fm.finalize_upload

        async def queue_instead(form_args):
            result = await real(form_args)
            if form_args.get("print") == "true":
                result.update(print_started=False, print_queued=True)
            return result

        env.fm.finalize_upload = queue_instead
        await started(env, start_on_upload=True)
        result = await self.upload(env, "a.gcode.3mf", {9: plate_gcode(9)})
        assert result["print_started"] is False and result["print_queued"] is True

    @pytest.mark.asyncio
    async def test_the_uploads_conversion_goes_ahead_of_the_backfill(self, env, monkeypatch):
        for n in range(3):
            make_3mf(env.gc / f"old{n}.gcode.3mf", {1: plate_gcode(1, str(n))})
        conv = env.make(start_on_upload=True)
        order: List[str] = []
        real_convert = conv.convert

        async def recording(source, start_print=False, user=None):
            order.append(source)
            return await real_convert(source, start_print, user)

        conv.convert = recording
        await conv.start()
        result = await self.upload(env, "new.gcode.3mf", {9: plate_gcode(9)})
        await conv.wait_idle()

        assert result["print_started"] is True
        assert order.index("new.gcode.3mf") <= 1


class TestPendingUpFront:
    @pytest.mark.asyncio
    async def test_every_plate_is_listed_pending_before_any_is_written(self, env, monkeypatch):
        conv = await started(env)
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1), 2: plate_gcode(2), 3: plate_gcode(3)})
        real_extract = Helix3mfConverter._extract
        seen: Dict[int, Any] = {}

        def extract_and_look(full, plate, dest):
            # What the endpoint and the database hold while this plate converts.
            seen[plate] = (
                [(c.plate, c.state) for c in conv.records["a.gcode.3mf"]],
                [(r["plate"], r["state"]) for r in env.db.ns["helix_3mf"]["a.gcode.3mf"]],
            )
            return real_extract(full, plate, dest)

        monkeypatch.setattr(Helix3mfConverter, "_extract", staticmethod(extract_and_look))
        await notify(conv, "create_file", "a.gcode.3mf")

        all_pending = [(1, "pending"), (2, "pending"), (3, "pending")]
        for plate in (1, 2, 3):
            listed, stored = seen[plate]
            assert listed == all_pending, f"while converting plate {plate}: {listed}"
            assert stored == all_pending
        assert [(e["plate"], e["state"]) for e in await listing(conv)] == [
            (1, "done"), (2, "done"), (3, "done")]

    @pytest.mark.asyncio
    async def test_a_conversion_cut_short_leaves_pending_entries_that_retry(
        self, env, monkeypatch
    ):
        make_3mf(env.gc / "a.gcode.3mf", {1: plate_gcode(1), 2: plate_gcode(2)})
        conv = env.make()

        def crash(full, plate, dest):
            raise SystemError("moonraker stopped")

        monkeypatch.setattr(Helix3mfConverter, "_extract", staticmethod(crash))
        await conv.start()
        await conv.wait_idle()
        assert [e["state"] for e in await listing(conv)] == ["pending", "pending"]

        monkeypatch.undo()
        again = await started(env)
        assert [e["state"] for e in await listing(again)] == ["done", "done"]
