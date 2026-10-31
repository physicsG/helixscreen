# SPDX-License-Identifier: GPL-3.0-or-later
"""
Camera photos in history (helix_photos.py): a snapshot once layer 2 is done
and once at job end, stored under .helix_photos, recorded per job id, linked
from auxiliary_data, and pruned with the history entry.

Run with: pytest tests/test_helix_photos.py -v
"""

import asyncio
import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent))

import helix_photos
from helix_print import HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY, HelixPrint
from helix_photos import (
    KIND_END,
    KIND_LAYER,
    PHOTOS_DIR,
    PHOTOS_FEATURE,
    PHOTOS_NAMESPACE,
    absolute_url,
    fetch_snapshot,
    redirect_allowed,
    newer_record,
    snapshot_candidates,
    history_matches,
    is_jpeg,
    layer_two_done,
    aux_paths,
    new_record,
    orphan_files,
    photo_path,
    pick_snapshot_url,
    prune_plan,
    record_paths,
    with_photo,
)
from test_helix_print import (  # noqa: F401  (fixtures are used by name)
    MockWebRequest,
    PrinterJob,
    helix_print_component,
    mock_server,
    temp_gcodes_dir,
)

T0 = 1700000000.25

# The camera check each job start makes before any photo
START_CHECKS = 1
JPEG = b"\xff\xd8\xff\xe0" + b"\x00" * 64 + b"\xff\xd9"


def _cam(name="cam", service="mjpegstreamer", snapshot="/webcam/?action=snapshot",
         stream="/webcam/?action=stream", enabled=True):
    return {"name": name, "service": service, "snapshot_url": snapshot,
            "stream_url": stream, "enabled": enabled}


class FakeWebcams:
    def __init__(self, cams):
        self.cams = cams

    def get_webcams(self):
        return {c["name"]: FakeCam(c) for c in self.cams}


class FakeCam:
    def __init__(self, data):
        self.data = data

    def as_dict(self):
        return dict(self.data)


# ============================================================================
# Pure rules
# ============================================================================


class TestLayerTwoDone:
    @pytest.mark.parametrize("prev,cur,expected", [
        (2, 3, True),
        (1, 3, True),
        (1, 2, False),
        (2, 2, False),
        (0, 3, False),
        (None, 3, False),
        (3, 4, False),
        (2, None, False),
        (True, 3, False),
    ])
    def test_cases(self, prev, cur, expected):
        assert layer_two_done(prev, cur) is expected


class TestPickSnapshotUrl:
    def test_no_webcam(self):
        assert pick_snapshot_url([]) is None
        assert pick_snapshot_url(None) is None

    def test_mjpeg_camera_first(self):
        cams = [_cam("webrtc", service="webrtc-camerastreamer", snapshot="/a.jpg"),
                _cam("main", snapshot="/main/?action=snapshot")]
        assert pick_snapshot_url(cams) == "/main/?action=snapshot"

    def test_any_camera_with_a_snapshot_otherwise(self):
        cams = [_cam("webrtc", service="webrtc-camerastreamer", snapshot="/a.jpg")]
        assert pick_snapshot_url(cams) == "/a.jpg"

    def test_disabled_html_and_screen_cameras_are_skipped(self):
        cams = [
            _cam("off", enabled=False, snapshot="/off.jpg"),
            _cam("page", snapshot="/viewer.html"),
            _cam("HelixScreen Screen", snapshot="/screen.jpg"),
            _cam("HelixScreen Screen (k1)", snapshot="/screen2.jpg"),
        ]
        assert pick_snapshot_url(cams) is None

    def test_database_spelling(self):
        cams = [{"name": "db", "service": "mjpegstreamer", "urlSnapshot": "/db.jpg",
                 "urlStream": "/db"}]
        assert pick_snapshot_url(cams) == "/db.jpg"

    def test_absolute_url(self):
        assert absolute_url("/webcam/?action=snapshot") == \
            "http://127.0.0.1/webcam/?action=snapshot"
        assert absolute_url("http://cam:8080/s.jpg") == "http://cam:8080/s.jpg"


class TestSnapshotCandidates:
    def test_absolute_url_is_the_only_candidate(self):
        assert snapshot_candidates(_cam(snapshot="http://cam:8080/s.jpg")) == \
            ["http://cam:8080/s.jpg"]

    def test_relative_url_tries_the_stream_origin_then_frontend_ports(self):
        cam = _cam(snapshot="/webcam/?action=snapshot", stream="http://10.0.0.5:4408/webcam/")
        assert snapshot_candidates(cam) == [
            "http://10.0.0.5:4408/webcam/?action=snapshot",
            "http://127.0.0.1/webcam/?action=snapshot",
            "http://127.0.0.1:4408/webcam/?action=snapshot",
            "http://127.0.0.1:4409/webcam/?action=snapshot",
        ]

    def test_relative_stream_adds_no_origin(self):
        assert snapshot_candidates(_cam())[0] == "http://127.0.0.1/webcam/?action=snapshot"


class TestRecords:
    def test_photo_path_carries_start_time(self):
        assert photo_path("00002A", T0, KIND_END) == f"{PHOTOS_DIR}/00002A_1700000000_end.jpg"
        assert photo_path("00002A", None, KIND_LAYER) == f"{PHOTOS_DIR}/00002A_0_layer2.jpg"

    def test_with_photo_replaces_same_kind_and_orders(self):
        record = new_record("00002A", "a.gcode", T0)
        record = with_photo(record, KIND_END, "x/end.jpg", 10, 1.0, "plugin")
        record = with_photo(record, KIND_LAYER, "x/l.jpg", 5, 2.0, "plugin")
        record = with_photo(record, KIND_END, "x/end2.jpg", 11, 3.0, "app")
        assert [p["kind"] for p in record["photos"]] == [KIND_LAYER, KIND_END]
        assert record["photos"][1]["path"] == "x/end2.jpg"

    def test_record_paths_only_inside_photos_dir(self):
        record = new_record("1", "a", T0)
        record["photos"] = [{"path": f"{PHOTOS_DIR}/ok.jpg"}, {"path": "../etc/passwd"},
                            {"path": f"{PHOTOS_DIR}/../x.gcode"}, {"path": f"{PHOTOS_DIR}/"},
                            "junk"]
        assert record_paths(record) == [f"{PHOTOS_DIR}/ok.jpg"]

    def test_is_jpeg(self):
        assert is_jpeg(JPEG)
        assert not is_jpeg(b"<html>")
        assert not is_jpeg(b"")
        assert not is_jpeg(None)

    def test_history_matches_by_start_time(self):
        record = new_record("1", "a", T0)
        assert history_matches(record, {"start_time": T0 + 0.5})
        assert not history_matches(record, {"start_time": T0 + 100})
        assert not history_matches(record, None)

    def test_history_aux_keeps_others_and_replaces_ours(self):
        record = with_photo(new_record("1", "a", T0), KIND_END, f"{PHOTOS_DIR}/e.jpg", 1, 1, "p")
        existing = [
            {"provider": "spoolman", "name": "spool_ids", "value": [1]},
            {"provider": "helix_print", "name": "modifications", "value": {}},
            {"provider": "helix_print", "name": "photos", "value": ["old"]},
        ]
        merged = HelixPrint.set_history_aux(existing, "photos", aux_paths(record), "Camera photos")
        names = [(e["provider"], e["name"]) for e in merged]
        assert names == [("spoolman", "spool_ids"), ("helix_print", "modifications"),
                         ("helix_print", "photos")]
        assert merged[-1]["value"] == [f"{PHOTOS_DIR}/e.jpg"]
        assert HelixPrint.set_history_aux({"not": "a list"}, "photos", [],
                                          "Camera photos")[0]["name"] == "photos"


class TestPrunePlanVersions:
    def test_a_newer_record_is_never_touched(self):
        newer = {"version": 2, "job_id": "A", "start_time": 1, "shots": []}
        assert newer_record(newer)
        assert not newer_record({"version": True})
        records = [("A", newer, False), ("B", {"version": 1}, True)]
        assert prune_plan(records, {}, max_jobs=0) == ["B"]


class TestPrunePlan:
    def _rec(self, job_id, start, paths=()):
        record = new_record(job_id, "a", start)
        for i, p in enumerate(paths):
            record = with_photo(record, f"k{i}", p, 1, 1, "plugin")
        return record

    def test_dead_and_malformed_go_unknown_stays(self):
        records = [("A", self._rec("A", 1), False), ("B", self._rec("B", 2), None),
                   ("C", {"junk": 1}, True), ("D", self._rec("D", 3), True)]
        assert sorted(prune_plan(records, {})) == ["A", "C"]

    def test_count_bound_drops_oldest(self):
        records = [(k, self._rec(k, s), True) for k, s in (("A", 30), ("B", 10), ("C", 20))]
        assert prune_plan(records, {}, max_jobs=2) == ["B"]

    def test_byte_bound_drops_oldest(self):
        p = PHOTOS_DIR + "/"
        records = [("A", self._rec("A", 1, [p + "a.jpg"]), True),
                   ("B", self._rec("B", 2, [p + "b.jpg"]), True)]
        sizes = {p + "a.jpg": 60, p + "b.jpg": 60}
        assert prune_plan(records, sizes, max_bytes=100) == ["A"]
        assert prune_plan(records, sizes, max_bytes=120) == []

    def test_orphans(self):
        names = [("a.jpg", 0.0), ("b.jpg", 0.0), ("c.jpg", 9000.0), (".x.tmp", 0.0)]
        assert orphan_files(names, [f"{PHOTOS_DIR}/a.jpg"], now=9100.0, grace=3600) == \
            ["b.jpg", ".x.tmp"]


# ============================================================================
# The component
# ============================================================================


def _added(job_id="00002A", filename="part.gcode", start_time=T0):
    return {"action": "added",
            "job": {"job_id": job_id, "filename": filename, "start_time": start_time}}


def _finished(job_id="00002A", status="completed", filename="part.gcode", start_time=T0):
    return {"action": "finished",
            "job": {"job_id": job_id, "filename": filename, "start_time": start_time,
                    "status": status}}


def _layer(n):
    return {"print_stats": {"info": {"current_layer": n, "total_layer": 100}}}


async def _history(mock_server, payload):
    """Every history handler, as Moonraker gathers them. History writes an
    added job's row before it announces it."""
    history = mock_server.components.get("history")
    job = payload["job"]
    if payload["action"] == "added" and history is not None:
        history.add_job(job["job_id"], filename=job["filename"], start_time=job["start_time"],
                        status="in_progress", end_time=None, total_duration=0.0)
    if payload["action"] == "finished" and history is not None:
        row = history.jobs.get(int(job["job_id"], 16))
        if row is not None and row.get("status") == "in_progress":
            row.update(status=job["status"], end_time=job["start_time"] + 3600.0,
                       total_duration=3600.0, filament_used=812.5)
    await asyncio.gather(
        *(h(payload) for h in mock_server.event_handlers["history:history_changed"]))


async def _status(mock_server, status):
    for h in mock_server.event_handlers["server:status_update"]:
        await h(status)


async def _drain(component):
    while component.photos._tasks:
        await asyncio.gather(*list(component.photos._tasks), return_exceptions=True)
        # A finished task leaves the list from its done callback, which runs
        # only once the loop gets a turn.
        await asyncio.sleep(0)


@pytest.fixture
def photos(helix_print_component, mock_server, temp_gcodes_dir):
    mock_server.components["webcam"] = FakeWebcams([_cam()])
    fetched = []

    def fetch(url):
        fetched.append(url)
        return JPEG

    async def setup(opt_in=None):
        await helix_print_component.component_init()
        if opt_in is not None:
            await helix_print_component.database.insert_item(
                HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY, {PHOTOS_FEATURE: opt_in})
        helix_print_component.photos.fetch = fetch
        helix_print_component.photos.clock = lambda: T0 + 500
        return helix_print_component

    setup.fetched = fetched
    return setup


def _record(component, job_id="00002A"):
    return component.database.namespaces.get(PHOTOS_NAMESPACE, {}).get(job_id)


def _files(gcodes):
    d = os.path.join(gcodes, PHOTOS_DIR)
    return sorted(os.listdir(d)) if os.path.isdir(d) else []


class TestFeatureRegistration:
    @pytest.mark.asyncio
    async def test_registered_as_an_observer_on_by_default(self, photos):
        component = await photos()
        status = await component._handle_status(MockWebRequest({}))
        entry = status["features"][PHOTOS_FEATURE]
        assert entry == {"version": 1, "available": True, "enabled": True, "reason": None}
        assert component.features[PHOTOS_FEATURE].acts is False

    @pytest.mark.asyncio
    async def test_opt_out_disables(self, photos):
        component = await photos(opt_in=False)
        status = await component._handle_status(MockWebRequest({}))
        assert status["features"][PHOTOS_FEATURE]["enabled"] is False


class TestCapture:
    @pytest.mark.asyncio
    async def test_layer_two_once_and_end_once(self, photos, mock_server, temp_gcodes_dir):
        component = await photos()
        await _history(mock_server, _added())
        for n in (1, 2, 3, 4, 3, 5):
            await _status(mock_server, _layer(n))
        await _drain(component)
        await _history(mock_server, _finished())
        assert len(photos.fetched) == START_CHECKS + 2
        assert photos.fetched[0] == "http://127.0.0.1/webcam/?action=snapshot"
        record = _record(component)
        assert [p["kind"] for p in record["photos"]] == [KIND_LAYER, KIND_END]
        assert record["start_time"] == T0
        assert record["photos"][0]["source"] == "plugin"
        assert _files(temp_gcodes_dir) == ["00002A_1700000000_end.jpg",
                                           "00002A_1700000000_layer2.jpg"]
        with open(os.path.join(temp_gcodes_dir, record["photos"][1]["path"]), "rb") as f:
            assert f.read() == JPEG

    @pytest.mark.parametrize("status", ["completed", "cancelled", "error"])
    @pytest.mark.asyncio
    async def test_end_photo_for_each_end(self, photos, mock_server, status):
        component = await photos()
        await _history(mock_server, _added())
        await _history(mock_server, _finished(status=status))
        assert [p["kind"] for p in _record(component)["photos"]] == [KIND_END]

    @pytest.mark.asyncio
    async def test_no_end_photo_for_a_shutdown(self, photos, mock_server):
        component = await photos()
        await _history(mock_server, _added())
        await _history(mock_server, _finished(status="klippy_shutdown"))
        await _drain(component)
        assert len(photos.fetched) == START_CHECKS
        assert _record(component) is None

    @pytest.mark.asyncio
    async def test_plr_resume_skips_layer_two(self, photos, mock_server):
        component = await photos()
        component.mark_helix_initiated("part.gcode", "plr_resume")
        await _history(mock_server, _added())
        await _status(mock_server, _layer(2))
        await _status(mock_server, _layer(3))
        await _drain(component)
        assert len(photos.fetched) == START_CHECKS
        await _history(mock_server, _finished())
        assert [p["kind"] for p in _record(component)["photos"]] == [KIND_END]

    @pytest.mark.asyncio
    async def test_job_first_seen_past_layer_two_gets_no_layer_photo(self, photos, mock_server):
        component = await photos()
        await _history(mock_server, _added())
        await _status(mock_server, _layer(40))
        await _status(mock_server, _layer(41))
        await _drain(component)
        assert len(photos.fetched) == START_CHECKS

    @pytest.mark.asyncio
    async def test_no_webcam_is_a_no_op(self, photos, mock_server, temp_gcodes_dir):
        mock_server.components["webcam"] = FakeWebcams([])
        component = await photos()
        await _history(mock_server, _added())
        await _status(mock_server, _layer(2))
        await _status(mock_server, _layer(3))
        await _drain(component)
        await _history(mock_server, _finished())
        assert photos.fetched == []
        assert _record(component) is None
        assert _files(temp_gcodes_dir) == []

    @pytest.mark.asyncio
    async def test_opted_out_takes_nothing(self, photos, mock_server):
        component = await photos(opt_in=False)
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert photos.fetched == []
        assert _record(component) is None

    @pytest.mark.asyncio
    async def test_a_body_that_is_not_a_jpeg_is_not_saved(self, photos, mock_server,
                                                         temp_gcodes_dir):
        component = await photos()
        component.photos.fetch = lambda url: b"<html>no camera</html>"
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert _record(component) is None
        assert _files(temp_gcodes_dir) == []

    @pytest.mark.asyncio
    async def test_a_failed_fetch_saves_nothing(self, photos, mock_server):
        component = await photos()

        def boom(url):
            raise OSError("connection refused")

        component.photos.fetch = boom
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert _record(component) is None

    @pytest.mark.asyncio
    async def test_low_disk_takes_nothing(self, photos, mock_server, monkeypatch):
        component = await photos()
        monkeypatch.setattr(component.photos, "_free_bytes", lambda d: 1024)
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        await _drain(component)
        assert len(photos.fetched) == START_CHECKS

    @pytest.mark.asyncio
    async def test_reused_job_id_replaces_the_old_record(self, photos, mock_server,
                                                        temp_gcodes_dir):
        component = await photos()
        await _history(mock_server, _added(start_time=T0))
        await _history(mock_server, _finished(start_time=T0))
        await _history(mock_server, _added(start_time=T0 + 5000))
        await _history(mock_server, _finished(start_time=T0 + 5000))
        record = _record(component)
        assert record["start_time"] == T0 + 5000
        assert len(record["photos"]) == 1
        assert _files(temp_gcodes_dir) == ["00002A_1700005000_end.jpg"]


class TestHistoryLink:
    @pytest.mark.asyncio
    async def test_photos_listed_in_auxiliary_data(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        history.add_job("00002A", start_time=T0, auxiliary_data=[
            {"provider": "spoolman", "name": "spool_ids", "value": [3]}])
        await _history(mock_server, _finished())
        saved, numeric = history.save_job_calls[-1]
        assert numeric == 0x2A
        aux = saved.auxiliary_data
        assert aux[0]["provider"] == "spoolman"
        assert aux[-1]["name"] == "photos"
        assert aux[-1]["value"] == [photo_path("00002A", T0, KIND_END)]

    @pytest.mark.asyncio
    async def test_a_reused_row_is_not_linked(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        history.add_job("00002A", start_time=T0 + 9999)
        await _history(mock_server, _finished())
        assert history.save_job_calls == []


class TestPrune:
    @pytest.mark.asyncio
    async def test_deleted_history_job_takes_its_photos(self, photos, mock_server,
                                                       temp_gcodes_dir):
        component = await photos()
        history = mock_server.components["history"]
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert _files(temp_gcodes_dir)
        assert await component.photos.prune() == []
        del history.jobs[0x2A]
        assert await component.photos.prune() == ["00002A"]
        assert _record(component) is None
        assert _files(temp_gcodes_dir) == []

    @pytest.mark.asyncio
    async def test_history_that_cannot_be_asked_keeps_records(self, photos, mock_server):
        component = await photos()
        component.history = None
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert await component.photos.prune() == []
        assert _record(component) is not None

    @pytest.mark.asyncio
    async def test_old_unlisted_files_are_removed(self, photos, mock_server, temp_gcodes_dir):
        component = await photos()
        d = os.path.join(temp_gcodes_dir, PHOTOS_DIR)
        os.makedirs(d)
        stray = os.path.join(d, "stray.jpg")
        fresh = os.path.join(d, "fresh.jpg")
        for p in (stray, fresh):
            with open(p, "wb") as f:
                f.write(JPEG)
        os.utime(stray, (T0 - 7200, T0 - 7200))
        os.utime(fresh, (T0 + 400, T0 + 400))
        await component.photos.prune()
        assert _files(temp_gcodes_dir) == ["fresh.jpg"]

    @pytest.mark.asyncio
    async def test_a_record_pointing_outside_is_never_followed(self, photos, mock_server,
                                                              temp_gcodes_dir):
        component = await photos()
        victim = os.path.join(temp_gcodes_dir, "keep.gcode")
        with open(victim, "w") as f:
            f.write("G28\n")
        await component.database.insert_item(PHOTOS_NAMESPACE, ["00009A"], {
            "version": 1, "job_id": "00009A", "start_time": T0,
            "photos": [{"kind": "end", "path": "keep.gcode"},
                       {"kind": "end", "path": f"{PHOTOS_DIR}/../keep.gcode"}]})
        assert await component.photos.prune() == ["00009A"]
        assert os.path.exists(victim)


class TestRestart:
    @pytest.mark.asyncio
    async def test_open_job_recovered_without_a_layer_photo(self, photos, mock_server):
        history = mock_server.components["history"]
        job = PrinterJob({"filename": "part.gcode", "start_time": T0})
        history.current_job = job
        history.current_job_id = 0x2A
        history.add_job("00002A", filename="part.gcode", start_time=T0)
        component = await photos()
        assert component.photos.job == {"job_id": "00002A", "filename": "part.gcode",
                                        "start_time": T0}
        await _status(mock_server, _layer(2))
        await _status(mock_server, _layer(3))
        await _drain(component)
        assert photos.fetched == []
        await _history(mock_server, _finished())
        assert [p["kind"] for p in _record(component)["photos"]] == [KIND_END]


class TestFinishedRowsOnly:
    @pytest.mark.asyncio
    async def test_layer_photo_never_rewrites_the_running_row(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        for n in (1, 2, 3):
            await _status(mock_server, _layer(n))
        await _drain(component)
        assert [p["kind"] for p in _record(component)["photos"]] == [KIND_LAYER]
        assert history.save_job_calls == []

    @pytest.mark.asyncio
    async def test_a_running_row_is_refused_even_when_asked(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        await component.photos.capture(dict(_added()["job"]), KIND_END)
        await component.photos._link_in_history(dict(_added()["job"]))
        assert history.save_job_calls == []

    @pytest.mark.asyncio
    async def test_the_finished_row_keeps_its_status_and_end_stats(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        for n in (1, 2, 3):
            await _status(mock_server, _layer(n))
        await _drain(component)
        await _history(mock_server, _finished(status="cancelled"))
        assert len(history.save_job_calls) == 1
        saved, _ = history.save_job_calls[0]
        assert saved.status == "cancelled"
        assert saved.end_time == T0 + 3600.0
        assert saved.total_duration == 3600.0
        assert saved.filament_used == 812.5
        assert saved.auxiliary_data[-1]["value"] == [photo_path("00002A", T0, KIND_LAYER),
                                                     photo_path("00002A", T0, KIND_END)]

    @pytest.mark.asyncio
    async def test_a_job_ending_without_an_end_photo_still_links_its_layer_photo(
            self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        for n in (1, 2, 3):
            await _status(mock_server, _layer(n))
        await _drain(component)
        await _history(mock_server, _finished(status="klippy_shutdown"))
        saved, _ = history.save_job_calls[-1]
        assert saved.status == "klippy_shutdown"
        assert saved.auxiliary_data[-1]["value"] == [photo_path("00002A", T0, KIND_LAYER)]


class TestFrontendPort:
    @pytest.mark.asyncio
    async def test_a_relative_url_found_on_the_frontend_port(self, photos, mock_server):
        component = await photos()
        tried = []

        def fetch(url):
            tried.append(url)
            if ":4408/" not in url:
                raise OSError("connection refused")
            return JPEG

        component.photos.fetch = fetch
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert _record(component) is not None
        assert tried[-1] == "http://127.0.0.1:4408/webcam/?action=snapshot"
        tried.clear()
        await component.photos.capture(dict(_added()["job"]), KIND_LAYER)
        assert tried == ["http://127.0.0.1:4408/webcam/?action=snapshot"]
        status = await component._handle_status(MockWebRequest({}))
        assert status["features"][PHOTOS_FEATURE]["available"] is True

    @pytest.mark.asyncio
    async def test_an_unreachable_relative_url_hands_photos_to_the_screen(
            self, photos, mock_server, temp_gcodes_dir):
        component = await photos()

        def refuse(url):
            raise OSError("connection refused")

        component.photos.fetch = refuse
        await component.photos.check_reachable()
        status = await component._handle_status(MockWebRequest({}))
        entry = status["features"][PHOTOS_FEATURE]
        assert entry["available"] is False
        assert entry["enabled"] is False
        assert entry["reason"] == "snapshot_unreachable: /webcam/?action=snapshot"
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        assert _record(component) is None

        component.photos.fetch = lambda url: JPEG
        await _history(mock_server, _added("00002B"))
        await _drain(component)
        status = await component._handle_status(MockWebRequest({}))
        assert status["features"][PHOTOS_FEATURE]["available"] is True

    @pytest.mark.asyncio
    async def test_an_absolute_url_that_fails_stays_available(self, photos, mock_server):
        mock_server.components["webcam"] = FakeWebcams([_cam(snapshot="http://cam/s.jpg")])
        component = await photos()

        def refuse(url):
            raise OSError("timed out")

        component.photos.fetch = refuse
        await component.photos.check_reachable()
        status = await component._handle_status(MockWebRequest({}))
        assert status["features"][PHOTOS_FEATURE]["available"] is True


class TestPruneSafety:
    @pytest.mark.asyncio
    async def test_files_of_an_unreadable_or_newer_record_stay(self, photos, mock_server,
                                                               temp_gcodes_dir):
        component = await photos()
        d = os.path.join(temp_gcodes_dir, PHOTOS_DIR)
        os.makedirs(d)
        for name in ("bad.jpg", "v2.jpg", "stray.jpg"):
            with open(os.path.join(d, name), "wb") as f:
                f.write(JPEG)
            os.utime(os.path.join(d, name), (T0 - 7200, T0 - 7200))
        db = component.database
        await db.insert_item(PHOTOS_NAMESPACE, ["00000A"], {
            "version": 1, "job_id": 10, "photos": [{"path": f"{PHOTOS_DIR}/bad.jpg"}]})
        await db.insert_item(PHOTOS_NAMESPACE, ["00000B"], {
            "version": 2, "job_id": "00000B", "shots": [f"{PHOTOS_DIR}/v2.jpg"]})
        assert await component.photos.prune() == ["00000A"]
        assert _record(component, "00000B") is not None
        assert _files(temp_gcodes_dir) == ["bad.jpg", "stray.jpg", "v2.jpg"]

    @pytest.mark.asyncio
    async def test_a_linked_photos_dir_is_not_swept(self, photos, mock_server, temp_gcodes_dir,
                                                    tmp_path):
        component = await photos()
        outside = tmp_path / "elsewhere"
        outside.mkdir()
        victim = outside / "keep.jpg"
        victim.write_bytes(JPEG)
        os.utime(str(victim), (T0 - 7200, T0 - 7200))
        os.symlink(str(outside), os.path.join(temp_gcodes_dir, PHOTOS_DIR))
        await component.photos.prune()
        assert victim.exists()

    @pytest.mark.asyncio
    async def test_a_quick_prune_does_not_ask_history(self, photos, mock_server):
        component = await photos()
        history = mock_server.components["history"]
        await _history(mock_server, _added())
        await _history(mock_server, _finished())
        del history.jobs[0x2A]
        assert await component.photos.prune(check_history=False) == []
        assert await component.photos.prune() == ["00002A"]


class TestLinkOwnPhotosOnly:
    @pytest.mark.asyncio
    async def test_photos_the_screen_took_are_not_linked(self, photos, mock_server):
        component = await photos(opt_in=False)
        history = mock_server.components["history"]
        sys.modules[type(history).__module__].PrinterJob = PrinterJob
        await _history(mock_server, _added())
        await component.database.insert_item(PHOTOS_NAMESPACE, ["00002A"], {
            "version": 1, "job_id": "00002A", "filename": "part.gcode", "start_time": T0,
            "photos": [{"kind": "layer2", "path": photo_path("00002A", T0, KIND_LAYER),
                        "size": 1, "taken_at": 1, "source": "app"}]})
        await _history(mock_server, _finished())
        assert history.save_job_calls == []


class TestFeaturesChanged:
    @pytest.mark.asyncio
    async def test_screens_hear_when_the_camera_is_lost_and_found(self, photos, mock_server):
        sent = []
        mock_server.send_event = lambda event, *args: sent.append(event)
        component = await photos()

        def refuse(url):
            raise OSError("connection refused")

        component.photos.fetch = refuse
        await component.photos.check_reachable()
        await component.photos.check_reachable()
        assert sent == ["helix_print:helix_features_changed"]
        component.photos.fetch = lambda url: JPEG
        await component.photos.check_reachable()
        assert sent == ["helix_print:helix_features_changed"] * 2


class TestRedirects:
    def test_redirect_allowed_only_within_the_origin(self):
        assert redirect_allowed("http://127.0.0.1:4408/a", "http://127.0.0.1:4408/b?x")
        assert not redirect_allowed("http://127.0.0.1:4408/a", "http://127.0.0.1/a")
        assert not redirect_allowed("http://127.0.0.1/a", "https://127.0.0.1/a")
        assert not redirect_allowed("http://127.0.0.1/a", "http://evil.example/a")

    def test_fetch_follows_a_same_origin_redirect_and_refuses_another(self):
        import http.server
        import threading

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == "/same":
                    self.send_response(302)
                    self.send_header("Location", "/snap.jpg")
                    self.end_headers()
                elif self.path == "/away":
                    self.send_response(302)
                    # The same server under another name: a different origin.
                    port = self.server.server_address[1]
                    self.send_header("Location", f"http://localhost:{port}/snap.jpg")
                    self.end_headers()
                else:
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(JPEG)))
                    self.end_headers()
                    self.wfile.write(JPEG)

            def log_message(self, *args):
                pass

        server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            base = f"http://127.0.0.1:{server.server_address[1]}"
            assert fetch_snapshot(base + "/same", timeout=5) == JPEG
            with pytest.raises(Exception):
                fetch_snapshot(base + "/away", timeout=5)
        finally:
            server.shutdown()
            server.server_close()


class TestCameraLostAfterConnect:
    @pytest.mark.asyncio
    async def test_a_dead_camera_is_found_at_job_start_and_announced(self, photos, mock_server):
        sent = []
        mock_server.send_event = lambda event, *args: sent.append(event)
        component = await photos()
        await component.photos.check_reachable()
        assert component.photos.availability() == (True, None)

        def refuse(url):
            raise OSError("connection refused")

        component.photos.fetch = refuse
        await _history(mock_server, _added())
        await _drain(component)
        assert sent == ["helix_print:helix_features_changed"]
        status = await component._handle_status(MockWebRequest({}))
        assert status["features"][PHOTOS_FEATURE]["available"] is False

    @pytest.mark.asyncio
    async def test_no_check_while_switched_off(self, photos, mock_server):
        component = await photos(opt_in=False)
        await component.photos.check_reachable()
        await _history(mock_server, _added())
        await _drain(component)
        assert photos.fetched == []
