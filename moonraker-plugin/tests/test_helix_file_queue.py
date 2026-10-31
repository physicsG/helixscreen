# SPDX-License-Identifier: GPL-3.0-or-later
"""
The file bookkeeping helix_analysis and helix_3mf share (helix_file_queue.py):
filelist_changed events, the one-at-a-time queue, the record namespace and the
gcodes scan.

Run with: pytest tests/test_helix_file_queue.py -v
"""

import asyncio
import os
from pathlib import Path

import pytest

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))

import helix_file_queue as fq
from helix_file_queue import FileEvent, FileTracker, RecordStore, WorkQueue, file_event


def _payload(action, path, old=None, root="gcodes"):
    payload = {"action": action, "item": {"root": root, "path": path}}
    if old is not None:
        payload["source_item"] = {"root": root, "path": old}
    return payload


# ============================================================================
# Events
# ============================================================================

class TestFileEvent:

    @pytest.mark.parametrize("payload,expected", [
        (_payload("create_file", "/a.gcode"), FileEvent("changed", "a.gcode", "")),
        (_payload("modify_file", "d/a.gcode"), FileEvent("changed", "d/a.gcode", "")),
        (_payload("delete_file", "a.gcode"), FileEvent("deleted", "a.gcode", "")),
        (_payload("move_file", "b.gcode", "a.gcode"), FileEvent("moved", "b.gcode", "a.gcode")),
        (_payload("delete_dir", "d/"), FileEvent("dir_deleted", "d", "")),
        (_payload("move_dir", "e", "d"), FileEvent("dir_moved", "e", "d")),
        # A hidden path is staging nobody tracks: out of one is a new file,
        # into one is a file gone.
        (_payload("move_file", "a.gcode", ".helix_temp/a.gcode"), FileEvent("changed", "a.gcode", "")),
        (_payload("move_file", ".trash/a.gcode", "a.gcode"), FileEvent("deleted", "a.gcode", "")),
        (_payload("move_dir", ".trash/d", "d"), FileEvent("dir_deleted", "d", "")),
        (_payload("create_file", ".helix_temp/a.gcode"), None),
        (_payload("delete_file", "d/.a.gcode"), None),
        (_payload("move_file", ".x/b", ".y/a"), None),
        (_payload("move_dir", "d", ".x/d"), None),
        (_payload("create_file", "a.gcode", root="config"), None),
        (_payload("root_update", "a.gcode"), None),
        ({"action": "create_file"}, None),
    ])
    def test_payloads(self, payload, expected):
        assert file_event(payload) == expected

    def test_a_missing_root_reads_as_gcodes(self):
        assert file_event({"action": "delete_file", "item": {"path": "a.gcode"}}) == \
            FileEvent("deleted", "a.gcode", "")


class _Tracker(FileTracker):
    def __init__(self, records):
        self.records = records
        self.calls = []
        self.on = True

    def tracking(self):
        return self.on

    async def file_changed(self, path):
        self.calls.append(("changed", path))

    async def file_deleted(self, path):
        self.calls.append(("deleted", path))
        self.records.pop(path, None)

    async def file_moved(self, old, new):
        self.calls.append(("moved", old, new))
        self.records[new] = self.records.pop(old)


class TestFileTracker:

    @pytest.mark.asyncio
    async def test_a_directory_event_reaches_each_record_under_it(self):
        tracker = _Tracker({"d/a": 1, "d/s/b": 2, "dd/c": 3, "e": 4})
        await tracker.on_filelist_changed(_payload("move_dir", "x/y", "d"))
        assert sorted(tracker.calls) == [("moved", "d/a", "x/y/a"), ("moved", "d/s/b", "x/y/s/b")]
        tracker.calls.clear()
        await tracker.on_filelist_changed(_payload("delete_dir", "x"))
        assert sorted(tracker.calls) == [("deleted", "x/y/a"), ("deleted", "x/y/s/b")]
        assert tracker.records == {"dd/c": 3, "e": 4}

    @pytest.mark.asyncio
    async def test_file_events_dispatch(self):
        tracker = _Tracker({"a": 1})
        await tracker.on_filelist_changed(_payload("create_file", "n"))
        await tracker.on_filelist_changed(_payload("move_file", "b", "a"))
        await tracker.on_filelist_changed(_payload("delete_file", "b"))
        assert tracker.calls == [("changed", "n"), ("moved", "a", "b"), ("deleted", "b")]

    @pytest.mark.asyncio
    async def test_nothing_reaches_a_tracker_that_is_not_tracking(self):
        tracker = _Tracker({"a": 1})
        tracker.on = False
        await tracker.on_filelist_changed(_payload("delete_file", "a"))
        assert tracker.calls == []


# ============================================================================
# Queue
# ============================================================================

class TestWorkQueue:

    @pytest.mark.asyncio
    async def test_items_run_once_each_in_order_and_front_jumps_ahead(self):
        seen = []

        async def process(key, item):
            seen.append(key)

        queue = WorkQueue(process)
        queue.put("a")
        queue.put("b")
        queue.put("a")
        queue.put("c", front=True)
        await queue.wait_idle()
        assert seen == ["c", "a", "b"]
        assert len(queue) == 0

    @pytest.mark.asyncio
    async def test_put_keeps_the_existing_item(self):
        made = []

        async def process(key, item):
            pass

        queue = WorkQueue(process)
        first = queue.put("a", make=lambda: made.append(1) or object())
        assert queue.put("a", make=lambda: made.append(2) or object()) is first
        assert made == [1]
        await queue.wait_idle()

    @pytest.mark.asyncio
    async def test_an_item_that_raises_does_not_stop_the_queue(self):
        seen = []

        async def process(key, item):
            seen.append(key)
            if key == "a":
                raise RuntimeError("boom")

        queue = WorkQueue(process)
        queue.put("a")
        queue.put("b")
        await queue.wait_idle()
        assert seen == ["a", "b"]

    @pytest.mark.asyncio
    async def test_a_gate_that_says_stop_drops_the_queue(self):
        seen = []

        async def process(key, item):
            seen.append(key)

        async def ready():
            return False

        queue = WorkQueue(process, ready)
        queue.put("a")
        await queue.wait_idle()
        assert seen == [] and "a" not in queue

    @pytest.mark.asyncio
    async def test_a_gate_that_raises_does_not_kill_the_queue_for_good(self):
        seen = []
        calls = []

        async def process(key, item):
            seen.append(key)

        async def ready():
            calls.append(1)
            if len(calls) == 1:
                raise RuntimeError("db gone")
            return True

        queue = WorkQueue(process, ready)
        queue.put("a")
        await queue.wait_idle()
        queue.put("b")
        await queue.wait_idle()
        assert "b" in seen

    @pytest.mark.asyncio
    async def test_cancel_stops_the_worker(self):
        started = asyncio.Event()

        async def process(key, item):
            started.set()
            await asyncio.sleep(60)

        queue = WorkQueue(process)
        queue.put("a")
        await started.wait()
        queue.cancel()
        await asyncio.wait_for(queue.wait_idle(), 5)


# ============================================================================
# Records
# ============================================================================

class _Db:
    def __init__(self, whole_namespace_reads=True):
        self.ns = {}
        self.whole = whole_namespace_reads

    async def insert_item(self, namespace, key, value):
        self.ns.setdefault(namespace, {})[key[0]] = value

    async def get_item(self, namespace, key=None, default=None):
        if key is None:
            if not self.whole:
                raise KeyError(namespace)
            return dict(self.ns[namespace]) if namespace in self.ns else default
        return self.ns.get(namespace, {}).get(key[0], default)

    async def delete_item(self, namespace, key):
        self.ns.get(namespace, {}).pop(key[0], None)

    async def ns_items(self, namespace):
        return list(self.ns.get(namespace, {}).items())


class TestRecordStore:

    @pytest.mark.asyncio
    @pytest.mark.parametrize("whole", [True, False])
    async def test_load_reads_the_namespace_either_way(self, whole):
        db = _Db(whole)
        store = RecordStore(lambda: db, "ns", "test records")
        await store.save("a.gcode", {"x": 1})
        await store.save("b.gcode", 2)
        assert await store.load() == {"a.gcode": {"x": 1}, "b.gcode": 2}
        await store.delete("a.gcode")
        assert await store.load() == {"b.gcode": 2}

    @pytest.mark.asyncio
    async def test_no_database_is_an_empty_store(self):
        store = RecordStore(lambda: None, "ns", "test records")
        assert await store.load() == {}
        await store.save("a", 1)
        await store.delete("a")

    @pytest.mark.asyncio
    async def test_a_failing_database_is_logged_not_raised(self):
        class Broken:
            async def get_item(self, *a):
                raise RuntimeError("x")

            async def insert_item(self, *a):
                raise RuntimeError("x")

            async def delete_item(self, *a):
                raise RuntimeError("x")

        store = RecordStore(lambda: Broken(), "ns", "test records")
        assert await store.load() == {}
        await store.save("a", 1)
        await store.delete("a")


# ============================================================================
# Scan
# ============================================================================

def test_scan_skips_hidden_paths_and_keeps_what_the_filter_keeps(tmp_path):
    for name in ("a.gcode", "d/b.gcode", "d/c.txt", ".helix_temp/x.gcode", "d/.y.gcode"):
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"12345")
    found = fq.scan_files(tmp_path, lambda rel: rel.endswith(".gcode"))
    assert sorted(found) == ["a.gcode", "d/b.gcode"]
    st = os.stat(str(tmp_path / "a.gcode"))
    assert found["a.gcode"] == (5, st.st_mtime)


def test_is_hidden():
    assert fq.is_hidden(".a/b") and fq.is_hidden("a/.b") and not fq.is_hidden("a/b.c")
