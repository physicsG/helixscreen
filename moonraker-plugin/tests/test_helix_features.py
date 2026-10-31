# SPDX-License-Identifier: GPL-3.0-or-later
"""
The plugin's feature framework: the status `features` map with its opt-in and
own-switch rules, and the helix-initiated job marker.

Run with: pytest tests/test_helix_features.py -v
"""

import json
import logging
import subprocess
import time
from pathlib import Path

import pytest

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent))

import helix_print
from helix_print import (
    HELIX_BINDING_MAX_AGE_S,
    HELIX_JOB_KINDS,
    HELIX_JOBS_NAMESPACE,
    HELIX_MARKER_TTL_S,
    HELIX_SCREEN_NAMESPACE,
    PLUGIN_FEATURES_KEY,
    SIBLING_LOAD_ERRORS,
    HelixJobMarkers,
    JobBinding,
    load_component,
)
from test_helix_print import (  # noqa: F401  (fixtures are used by name)
    MockConfigHelper,
    MockFileManager,
    MockKlippy,
    MockKlippyApis,
    MockHistory,
    MockWebRequest,
    Moonraker093Database,
    helix_print_component,
    mock_server,
    temp_gcodes_dir,
)


async def _status(component, mock_server):
    return await mock_server.endpoints["/server/helix/status"](MockWebRequest({}))


async def _opt_in(component, record):
    await component.database.insert_item(
        HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY, record
    )


# A history job's start_time
T0 = 1700000000.25


def _kind(binding):
    return None if binding is None else binding.kind


class FakeClock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


# ============================================================================
# Features map
# ============================================================================

class TestFeaturesMap:

    @pytest.mark.asyncio
    async def test_status_carries_the_api_level_and_map(self, helix_print_component,
                                                        mock_server):
        await helix_print_component.component_init()
        helix_print_component.register_feature("file_analysis", 3, acts=False)

        result = await _status(helix_print_component, mock_server)

        assert result["api"] == 2
        assert result["features"]["file_analysis"] == {
            "version": 3, "available": True, "enabled": True, "reason": None,
        }

    @pytest.mark.asyncio
    async def test_defaults_observers_on_actors_off(self, helix_print_component,
                                                    mock_server):
        await helix_print_component.component_init()
        helix_print_component.register_feature("journal", 1, acts=False)
        helix_print_component.register_feature("print_hooks", 1, acts=True)

        features = (await _status(helix_print_component, mock_server))["features"]

        assert features["journal"]["enabled"] is True
        assert features["print_hooks"]["enabled"] is False
        assert await helix_print_component.feature_enabled("journal") is True
        assert await helix_print_component.feature_enabled("print_hooks") is False

    @pytest.mark.asyncio
    async def test_opt_in_record_overrides_the_default(self, helix_print_component,
                                                       mock_server):
        await helix_print_component.component_init()
        helix_print_component.register_feature("journal", 1, acts=False)
        helix_print_component.register_feature("print_hooks", 1, acts=True)
        await _opt_in(helix_print_component, {"journal": False, "print_hooks": True})

        features = (await _status(helix_print_component, mock_server))["features"]

        assert features["journal"]["enabled"] is False
        assert features["print_hooks"]["enabled"] is True
        assert await helix_print_component.feature_enabled("print_hooks") is True

    @pytest.mark.asyncio
    async def test_a_non_bool_opt_in_reads_as_the_default(self, helix_print_component):
        await helix_print_component.component_init()
        helix_print_component.register_feature("print_hooks", 1, acts=True)
        await _opt_in(helix_print_component, {"print_hooks": "yes"})

        assert await helix_print_component.feature_enabled("print_hooks") is False

    @pytest.mark.asyncio
    async def test_an_owned_switch_ignores_plugin_features(self, helix_print_component,
                                                           mock_server):
        await helix_print_component.component_init()
        switch_value = {"on": False}

        async def switch():
            return switch_value["on"]

        helix_print_component.register_feature("plr", 1, acts=True, switch=switch)
        await _opt_in(helix_print_component, {"plr": True})

        features = (await _status(helix_print_component, mock_server))["features"]
        assert features["plr"]["enabled"] is False
        assert await helix_print_component.feature_enabled("plr") is False

        switch_value["on"] = True
        await _opt_in(helix_print_component, {"plr": False})

        features = (await _status(helix_print_component, mock_server))["features"]
        assert features["plr"]["enabled"] is True
        assert await helix_print_component.feature_enabled("plr") is True

    @pytest.mark.asyncio
    async def test_a_switch_that_raises_reads_off(self, helix_print_component):
        await helix_print_component.component_init()
        async def switch():
            raise RuntimeError("store unreadable")

        helix_print_component.register_feature("plr", 1, acts=True, switch=switch)

        assert await helix_print_component.feature_enabled("plr") is False

    @pytest.mark.asyncio
    async def test_an_unknown_feature_is_off_and_not_reported(
        self, helix_print_component, mock_server
    ):
        await helix_print_component.component_init()
        await _opt_in(helix_print_component, {"no_such_feature": True})

        features = (await _status(helix_print_component, mock_server))["features"]

        assert "no_such_feature" not in features
        assert await helix_print_component.feature_enabled("no_such_feature") is False

    @pytest.mark.asyncio
    async def test_an_unavailable_feature_is_never_enabled(self, helix_print_component,
                                                           mock_server):
        await helix_print_component.component_init()
        helix_print_component.register_feature(
            "event_journal", 1, acts=False, available=False, reason="moonraker<0.9"
        )
        await _opt_in(helix_print_component, {"event_journal": True})

        entry = (await _status(helix_print_component, mock_server))["features"][
            "event_journal"
        ]

        assert entry == {
            "version": 1, "available": False, "enabled": False, "reason": "moonraker<0.9",
        }
        assert await helix_print_component.feature_enabled("event_journal") is False

    @pytest.mark.asyncio
    async def test_an_unreadable_settings_store_reads_as_defaults(
        self, helix_print_component, mock_server
    ):
        await helix_print_component.component_init()
        async def broken_get_item(*args, **kwargs):
            raise RuntimeError("database locked")

        helix_print_component.register_feature("journal", 1, acts=False)
        helix_print_component.register_feature("print_hooks", 1, acts=True)
        helix_print_component.database.get_item = broken_get_item

        features = (await _status(helix_print_component, mock_server))["features"]

        assert features["journal"]["enabled"] is True
        assert features["print_hooks"]["enabled"] is False


class TestModuleFeatures:
    """A sibling module that fails disables only the feature built on it."""

    def test_a_sibling_that_raises_on_import_records_why(self, tmp_path, monkeypatch):
        (tmp_path / "helix_broken.py").write_text("raise ImportError('needs 3.12')\n")
        (tmp_path / "helix_fine.py").write_text("VALUE = 7\n")
        monkeypatch.setattr(helix_print, "__file__", str(tmp_path / "helix_print.py"))

        broken = helix_print._load_sibling("helix_broken")
        fine = helix_print._load_sibling("helix_fine")
        missing = helix_print._load_sibling("helix_absent")

        assert broken is None
        assert "needs 3.12" in SIBLING_LOAD_ERRORS["helix_broken"]
        assert fine is not None and fine.VALUE == 7
        assert "helix_fine" not in SIBLING_LOAD_ERRORS
        assert missing is None
        assert SIBLING_LOAD_ERRORS["helix_absent"] == "module_missing"

    @pytest.mark.asyncio
    async def test_a_failed_converter_module_reports_unavailable(
        self, helix_print_component, mock_server, monkeypatch
    ):
        monkeypatch.setattr(helix_print, "helix_3mf", None)
        monkeypatch.setitem(SIBLING_LOAD_ERRORS, "helix_3mf", "load_failed: SyntaxError")
        helix_print_component.register_feature("journal", 1, acts=False)
        await helix_print_component.component_init()

        features = (await _status(helix_print_component, mock_server))["features"]

        assert features["convert_3mf"]["available"] is False
        assert features["convert_3mf"]["enabled"] is False
        assert features["convert_3mf"]["reason"] == "load_failed: SyntaxError"
        assert features["journal"]["enabled"] is True

    @pytest.mark.asyncio
    async def test_conversion_switched_off_in_config_is_available_but_off(
        self, mock_server, temp_gcodes_dir
    ):
        mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
        mock_server.components["database"] = Moonraker093Database()
        mock_server.components["klippy_apis"] = MockKlippyApis()
        mock_server.components["history"] = MockHistory()
        component = load_component(MockConfigHelper(mock_server, {
            "cleanup_delay": 3600, "enabled": True, "convert_3mf": False,
        }))
        await component.component_init()

        entry = (await _status(component, mock_server))["features"]["convert_3mf"]

        assert entry["available"] is True
        assert entry["enabled"] is False
        assert entry["reason"] is None


# ============================================================================
# Helix-initiated job marker
# ============================================================================

class TestJobMarkers:

    @pytest.mark.parametrize("kind", HELIX_JOB_KINDS)
    def test_a_marker_binds_to_the_job_for_its_file(self, kind):
        markers = HelixJobMarkers(FakeClock())
        markers.mark("benchy.gcode", kind)

        assert _kind(markers.bind("benchy.gcode", "00001A", T0)) == kind
        assert markers.kind_of("00001A", "benchy.gcode", T0) == kind
        assert markers.pending_count() == 0

    def test_the_three_kinds_are_the_spec_set(self):
        assert set(HELIX_JOB_KINDS) == {"plr_resume", "print_modified", "print_edited"}

    def test_an_unknown_kind_is_refused(self):
        markers = HelixJobMarkers(FakeClock())
        with pytest.raises(ValueError):
            markers.mark("benchy.gcode", "print_started")

    def test_a_foreign_job_gets_no_kind(self):
        markers = HelixJobMarkers(FakeClock())
        markers.mark("benchy.gcode", "plr_resume")

        assert _kind(markers.bind("somebody_else.gcode", "00001B", T0)) is None
        assert markers.kind_of("00001B", "somebody_else.gcode", T0) is None
        # The marker still waits for its own file
        assert _kind(markers.bind("benchy.gcode", "00001C", T0)) == "plr_resume"

    def test_an_unmatched_marker_expires(self):
        clock = FakeClock()
        markers = HelixJobMarkers(clock)
        markers.mark("benchy.gcode", "print_modified")

        clock.now += HELIX_MARKER_TTL_S + 0.1

        assert _kind(markers.bind("benchy.gcode", "00001A", T0)) is None
        assert markers.kind_of("00001A", "benchy.gcode", T0) is None

    def test_a_marker_is_live_up_to_its_ttl(self):
        clock = FakeClock()
        markers = HelixJobMarkers(clock)
        markers.mark("benchy.gcode", "print_modified")

        clock.now += HELIX_MARKER_TTL_S

        assert _kind(markers.bind("benchy.gcode", "00001A", T0)) == "print_modified"

    def test_a_marker_binds_once(self):
        markers = HelixJobMarkers(FakeClock())
        markers.mark("benchy.gcode", "print_modified")
        _kind(markers.bind("benchy.gcode", "00001A", T0))

        assert _kind(markers.bind("benchy.gcode", "00001B", T0)) is None

    def test_the_oldest_marker_for_a_file_binds_first(self):
        clock = FakeClock()
        markers = HelixJobMarkers(clock)
        markers.mark("benchy.gcode", "print_modified")
        clock.now += 1
        markers.mark("benchy.gcode", "plr_resume")

        assert _kind(markers.bind("benchy.gcode", "00001A", T0)) == "print_modified"
        assert _kind(markers.bind("benchy.gcode", "00001B", T0)) == "plr_resume"

    def test_an_int_job_id_names_the_same_job(self):
        markers = HelixJobMarkers(FakeClock())
        markers.mark("benchy.gcode", "plr_resume")
        _kind(markers.bind("benchy.gcode", 0x1A, T0))

        assert markers.kind_of("00001A", "benchy.gcode", T0) == "plr_resume"


def _stage_modified(gcodes: Path) -> MockWebRequest:
    (gcodes / "benchy.gcode").write_text("G28\n")
    (gcodes / ".helix_temp").mkdir(exist_ok=True)
    (gcodes / ".helix_temp" / "modified_1_benchy.gcode").write_text("G28\nG1\n")
    return MockWebRequest({
        "original_filename": "benchy.gcode",
        "temp_file_path": ".helix_temp/modified_1_benchy.gcode",
        "modifications": [],
    })


def _added(filename, job_id, start_time=None):
    return {
        "action": "added",
        "job": {
            "filename": filename,
            "job_id": job_id,
            "start_time": T0 if start_time is None else start_time,
        },
    }


class TestJobMarkerWiring:

    @pytest.mark.asyncio
    async def test_print_modified_marks_its_job(self, helix_print_component,
                                                mock_server, temp_gcodes_dir):
        await helix_print_component.component_init()
        result = await mock_server.endpoints["/server/helix/print_modified"](
            _stage_modified(Path(temp_gcodes_dir))
        )

        await helix_print_component._on_history_changed(
            _added(result["print_filename"], "00002A")
        )

        assert helix_print_component.helix_initiated(
            "00002A", result["print_filename"], T0
        ) == "print_modified"
        stored = helix_print_component.database.namespaces[HELIX_JOBS_NAMESPACE]
        assert stored["00002A"]["kind"] == "print_modified"

    @pytest.mark.asyncio
    async def test_a_job_another_client_started_is_not_helix_initiated(
        self, helix_print_component
    ):
        await helix_print_component.component_init()
        helix_print_component.mark_helix_initiated("benchy.gcode", "plr_resume")

        await helix_print_component._on_history_changed(_added("other.gcode", "00002B"))

        assert helix_print_component.helix_initiated("00002B", "other.gcode", T0) is None
        assert HELIX_JOBS_NAMESPACE not in helix_print_component.database.namespaces

    @pytest.mark.asyncio
    async def test_a_binding_survives_a_restart_and_old_ones_are_dropped(
        self, mock_server, temp_gcodes_dir
    ):
        db = Moonraker093Database()
        mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
        mock_server.components["database"] = db
        mock_server.components["klippy_apis"] = MockKlippyApis()
        mock_server.components["history"] = MockHistory()
        config = MockConfigHelper(mock_server, {"cleanup_delay": 3600, "enabled": True})

        first = load_component(config)
        await first.component_init()
        first.mark_helix_initiated("benchy.gcode", "plr_resume")
        await first._on_history_changed(_added("benchy.gcode", "00003A"))
        await db.insert_item(
            HELIX_JOBS_NAMESPACE, ["000001"],
            {"kind": "print_modified", "filenames": ["old.gcode"], "start_time": T0,
             "bound_at": time.time() - HELIX_BINDING_MAX_AGE_S - 10},
        )
        now = time.time()
        malformed = {
            "000002": {"kind": "plr_resume", "filenames": ["a.gcode"], "start_time": T0},
            "000003": {"kind": "plr_resume", "start_time": T0, "bound_at": now},
            "000004": {"kind": "plr_resume", "filenames": ["a.gcode"], "bound_at": now},
            "000005": {"kind": "plr_resume", "filenames": [], "start_time": T0,
                       "bound_at": now},
            "000006": {"kind": "plr_resume", "filenames": ["a.gcode"], "start_time": "x",
                       "bound_at": now},
            "000007": {"kind": "plr_resume", "filename": "a.gcode", "bound_at": now},
        }
        for key, record in malformed.items():
            await db.insert_item(HELIX_JOBS_NAMESPACE, [key], record)

        second = load_component(config)
        await second.component_init()

        assert second.helix_initiated("00003A", "benchy.gcode", T0) == "plr_resume"
        assert second.helix_initiated("000001", "old.gcode", T0) is None
        assert set(db.ns[HELIX_JOBS_NAMESPACE]) == {"00003A"}


class TestJobIdReuse:
    """History reuses job ids, so a binding must not outlive its job."""

    @pytest.fixture
    def db(self):
        return Moonraker093Database()

    @pytest.fixture
    def config(self, mock_server, temp_gcodes_dir, db):
        mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
        mock_server.components["database"] = db
        mock_server.components["klippy_apis"] = MockKlippyApis()
        mock_server.components["history"] = MockHistory()
        return MockConfigHelper(mock_server, {"cleanup_delay": 3600, "enabled": True})

    @pytest.mark.asyncio
    async def test_a_reused_id_after_delete_all_is_not_helix_initiated(self, config, db):
        component = load_component(config)
        await component.component_init()
        component.mark_helix_initiated("benchy.gcode", "plr_resume")
        await component._on_history_changed(_added("benchy.gcode", "00002A"))
        assert component.helix_initiated("00002A", "benchy.gcode", T0) == "plr_resume"

        # History cleared; the next print, from another client, gets the same id
        await component._on_history_changed(_added("benchy.gcode", "00002A"))

        assert component.helix_initiated("00002A", "benchy.gcode", T0) is None
        assert "00002A" not in db.ns.get(HELIX_JOBS_NAMESPACE, {})

    @pytest.mark.asyncio
    async def test_a_binding_answers_only_for_its_own_file(self, config):
        component = load_component(config)
        await component.component_init()
        component.mark_helix_initiated("benchy.gcode", "print_modified")
        await component._on_history_changed(_added("benchy.gcode", "00002A"))

        assert component.helix_initiated("00002A", "cube.gcode", T0) is None

    @pytest.mark.asyncio
    async def test_an_id_reused_while_the_plugin_was_down_names_another_file(
        self, config, db
    ):
        await db.insert_item(
            HELIX_JOBS_NAMESPACE, ["00002A"],
            {"kind": "plr_resume", "filenames": ["benchy.gcode"], "start_time": T0,
             "bound_at": time.time()},
        )
        component = load_component(config)
        await component.component_init()

        assert component.helix_initiated("00002A", "cube.gcode", T0) is None
        assert component.helix_initiated("00002A", "benchy.gcode", T0) == "plr_resume"

    @pytest.mark.asyncio
    async def test_same_id_and_file_with_another_start_time_is_another_job(
        self, config, db
    ):
        # The plugin missed the reused id's added event (down, or the drop
        # failed): the stored binding names the same file, but not this job.
        await db.insert_item(
            HELIX_JOBS_NAMESPACE, ["00002A"],
            {"kind": "plr_resume", "filenames": ["benchy.gcode"], "start_time": T0,
             "bound_at": time.time()},
        )
        component = load_component(config)
        await component.component_init()

        assert component.helix_initiated("00002A", "benchy.gcode", T0 + 3600) is None

    @pytest.mark.asyncio
    async def test_a_failed_drop_of_a_reused_ids_binding_warns(self, config, db, caplog):
        component = load_component(config)
        await component.component_init()
        component.mark_helix_initiated("benchy.gcode", "plr_resume")
        await component._on_history_changed(_added("benchy.gcode", "00002A"))

        async def broken_delete(*args, **kwargs):
            raise RuntimeError("database locked")

        db.delete_item = broken_delete
        with caplog.at_level(logging.DEBUG):
            await component._on_history_changed(_added("benchy.gcode", "00002A", T0 + 1))

        warnings = [r for r in caplog.records if r.levelno == logging.WARNING]
        assert any("00002A" in r.getMessage() for r in warnings)

    @pytest.mark.asyncio
    async def test_old_bindings_are_pruned_on_each_bind(self, config, db):
        component = load_component(config)
        await component.component_init()
        component.job_markers.restore(
            "000001",
            JobBinding("plr_resume", ["old.gcode"], T0,
                       time.time() - HELIX_BINDING_MAX_AGE_S - 1),
        )
        await db.insert_item(HELIX_JOBS_NAMESPACE, ["000001"], {"kind": "plr_resume"})

        await component._on_history_changed(_added("other.gcode", "00002B"))

        assert component.helix_initiated("000001", "old.gcode", T0) is None
        assert "000001" not in db.ns[HELIX_JOBS_NAMESPACE]


class TestUnmark:

    def test_unmark_withdraws_the_newest_marker_for_the_file(self):
        clock = FakeClock()
        markers = HelixJobMarkers(clock)
        markers.mark("benchy.gcode", "print_modified")
        clock.now += 1
        markers.mark("benchy.gcode", "plr_resume")

        assert markers.unmark("benchy.gcode") is True
        assert _kind(markers.bind("benchy.gcode", "00001A", T0)) == "print_modified"
        assert markers.unmark("benchy.gcode") is False

    @pytest.mark.asyncio
    async def test_a_failed_print_modified_start_leaves_no_marker(
        self, helix_print_component, mock_server, temp_gcodes_dir
    ):
        await helix_print_component.component_init()
        helix_print_component.klippy_apis.run_gcode.side_effect = RuntimeError("klippy down")

        with pytest.raises(Exception):
            await mock_server.endpoints["/server/helix/print_modified"](
                _stage_modified(Path(temp_gcodes_dir))
            )

        assert helix_print_component.job_markers.pending_count() == 0


class TestHistoryOrdering:

    @pytest.mark.asyncio
    async def test_the_job_id_is_recorded_before_the_marker_write(
        self, helix_print_component, mock_server, temp_gcodes_dir
    ):
        await helix_print_component.component_init()
        result = await mock_server.endpoints["/server/helix/print_modified"](
            _stage_modified(Path(temp_gcodes_dir))
        )
        info = helix_print_component.active_prints[result["print_filename"]]
        seen = []
        real_insert = helix_print_component.database.insert_item

        async def insert_item(namespace, key, value):
            if namespace == HELIX_JOBS_NAMESPACE:
                seen.append(info.job_id)
            await real_insert(namespace, key, value)

        helix_print_component.database.insert_item = insert_item
        await helix_print_component._on_history_changed(
            _added(result["print_filename"], "00002A")
        )

        assert seen == ["00002A"]


class TestLiveConverterAvailability:

    @pytest.mark.asyncio
    async def test_the_entry_follows_the_converter(self, helix_print_component,
                                                   mock_server):
        await helix_print_component.component_init()
        status = await _status(helix_print_component, mock_server)
        assert status["features"]["convert_3mf"]["available"] is status["convert_3mf"]

        if helix_print_component.converter is None:
            pytest.skip("converter did not start in this environment")
        helix_print_component.converter.active = False
        status = await _status(helix_print_component, mock_server)

        assert status["convert_3mf"] is False
        assert status["features"]["convert_3mf"]["available"] is False
        assert status["features"]["convert_3mf"]["reason"] == "stopped"


class TestFeatureProbe:

    @pytest.mark.asyncio
    async def test_a_probe_that_reads_unavailable_disables_the_feature(
        self, helix_print_component
    ):
        live = {"ok": True}

        async def switch():
            return True

        helix_print_component.register_feature(
            "journal", 1, acts=False, switch=switch,
            probe=lambda: (live["ok"], None if live["ok"] else "gone"),
        )
        assert await helix_print_component.feature_enabled("journal") is True

        live["ok"] = False

        assert await helix_print_component.feature_enabled("journal") is False


class TestFinishRename:
    """_patch_history_entry renames a finished print_modified job to the
    original file; the job still answers to that name."""

    @pytest.mark.asyncio
    async def test_a_print_modified_job_reads_helix_initiated_after_the_rename(
        self, helix_print_component, mock_server, temp_gcodes_dir
    ):
        history = mock_server.components["history"]
        await helix_print_component.component_init()
        result = await mock_server.endpoints["/server/helix/print_modified"](
            _stage_modified(Path(temp_gcodes_dir))
        )
        link = result["print_filename"]
        history.add_job("00002A", filename=link, start_time=T0)
        await helix_print_component._on_history_changed(_added(link, "00002A"))

        await helix_print_component._on_job_state_changed(
            None, {"state": "printing", "filename": link},
            {"state": "complete", "filename": link},
        )

        job, _job_id = history.save_job_calls[-1]
        assert job.filename == "benchy.gcode"
        assert helix_print_component.helix_initiated(
            "00002A", job.filename, job.start_time
        ) == "print_modified"
        assert helix_print_component.helix_initiated("00002A", link, T0) == "print_modified"

    def test_both_names_bind_from_one_marker(self):
        markers = HelixJobMarkers(FakeClock())
        markers.mark(".helix_print/full/benchy.gcode", "plr_resume", "benchy.gcode")
        markers.bind(".helix_print/full/benchy.gcode", "00001A", T0)

        assert markers.kind_of("00001A", "benchy.gcode", T0) == "plr_resume"
        assert markers.kind_of("00001A", ".helix_print/full/benchy.gcode", T0) == "plr_resume"
        assert markers.kind_of("00001A", "cube.gcode", T0) is None


def test_a_features_extra_fields_join_its_entry_and_cannot_replace_the_base(helix_print_component):
    import asyncio

    async def extra():
        return {"saving": True, "enabled": "overridden"}

    async def boom():
        raise RuntimeError("probe failed")

    helix_print_component.register_feature("demo", 1, acts=False, extra=extra)
    helix_print_component.register_feature("broken", 1, acts=False, extra=boom)
    features = asyncio.run(helix_print_component._features_status())
    assert features["demo"]["saving"] is True and features["demo"]["enabled"] is True
    assert features["broken"] == {"version": 1, "available": True, "enabled": True,
                                  "reason": None}


# ============================================================================
# [helix_print] enabled: False, and a feature that fails to start
# ============================================================================

FEATURE_MODULE_NAMES = ("plr", "file_analysis", "event_journal", "config_edit",
                        "server_edits", "print_hooks", "convert_3mf",
                        "fit_check_blocks_autostart")


@pytest.fixture
def disabled_component(mock_server, temp_gcodes_dir):
    from test_helix_print import MockDatabase
    mock_server.components["file_manager"] = MockFileManager(temp_gcodes_dir)
    mock_server.components["database"] = MockDatabase()
    mock_server.components["klippy_apis"] = MockKlippyApis()
    mock_server.components["history"] = MockHistory()
    return load_component(MockConfigHelper(mock_server, {
        "temp_dir": ".helix_temp", "symlink_dir": ".helix_print",
        "cleanup_delay": 3600, "enabled": False,
    }))


class TestPluginDisabled:

    @pytest.mark.asyncio
    async def test_every_feature_reads_plugin_disabled(self, disabled_component, mock_server):
        await disabled_component.component_init()
        features = (await _status(disabled_component, mock_server))["features"]
        assert set(FEATURE_MODULE_NAMES) <= set(features)
        for name, entry in features.items():
            assert (entry["available"], entry["enabled"], entry["reason"]) == \
                (False, False, "plugin_disabled"), name
            assert await disabled_component.feature_enabled(name) is False

    @pytest.mark.asyncio
    async def test_no_feature_module_starts(self, disabled_component, mock_server):
        """Nothing a module registers exists, so nothing can send gcode, write
        a config file, record an event or run a worker."""
        await disabled_component.component_init()
        assert disabled_component.print_hooks is None
        assert disabled_component.fit_check is None
        assert disabled_component.journal is None
        assert disabled_component.analysis is None
        assert disabled_component.plr is None
        for path in ("/server/helix/config_edit", "/server/helix/config",
                     "/server/helix/print_edited", "/server/helix/journal",
                     "/server/helix/analysis", "/server/helix/print_hooks/screen_start"):
            assert path not in mock_server.endpoints, path
        assert "/server/helix/status" in mock_server.endpoints

    @pytest.mark.asyncio
    async def test_history_events_send_nothing_even_opted_in(self, disabled_component,
                                                             mock_server):
        import helix_print_hooks
        await disabled_component.component_init()
        await _opt_in(disabled_component, {"print_hooks": True})
        await disabled_component.database.insert_item(
            HELIX_SCREEN_NAMESPACE, helix_print_hooks.STANDING_OPTIONS_KEY,
            {"version": 1, "gcode": "SET_X Y=1"})
        for handler in mock_server.event_handlers.get("history:history_changed", []):
            await handler({
                "action": "added",
                "job": {"job_id": "000001", "filename": "a.gcode", "start_time": T0},
            })
        mock_server.components["klippy_apis"].run_gcode.assert_not_awaited()

    @pytest.mark.asyncio
    async def test_a_feature_registered_later_reads_plugin_disabled(self, disabled_component,
                                                                    mock_server):
        await disabled_component.component_init()
        disabled_component.register_feature("late", 1, acts=False)
        entry = (await _status(disabled_component, mock_server))["features"]["late"]
        assert entry["reason"] == "plugin_disabled"
        assert await disabled_component.feature_enabled("late") is False


class TestFeatureStartIsolation:

    @pytest.mark.asyncio
    async def test_a_feature_that_raises_on_start_disables_only_itself(
        self, helix_print_component, mock_server, monkeypatch
    ):
        import helix_journal

        class Broken(helix_journal.HelixJournal):
            async def load(self):
                raise RuntimeError("store unreadable")

        monkeypatch.setattr(helix_journal, "HelixJournal", Broken)
        # A listed printer, so plr can run.
        await mock_server.components["database"].insert_item(
            HELIX_SCREEN_NAMESPACE, "plr_profile", {"tier": 1})
        await helix_print_component.component_init()

        features = (await _status(helix_print_component, mock_server))["features"]
        assert features["event_journal"]["available"] is False
        assert features["event_journal"]["reason"] == \
            "start_failed: RuntimeError: store unreadable"
        for name in ("config_edit", "server_edits", "file_analysis", "plr"):
            assert features[name]["available"] is True, name
        assert "/server/helix/print_modified" in mock_server.endpoints

    @pytest.mark.asyncio
    async def test_a_constructor_that_raises_is_isolated_too(
        self, helix_print_component, mock_server, monkeypatch
    ):
        import helix_config

        def broken(helix):
            raise ValueError("bad")

        monkeypatch.setattr(helix_config, "HelixConfig", broken)
        await helix_print_component.component_init()
        features = (await _status(helix_print_component, mock_server))["features"]
        assert features["config_edit"]["reason"] == "start_failed: ValueError: bad"
        assert features["print_hooks"]["available"] is True


@pytest.mark.asyncio
async def test_analysis_starts_only_after_recovery_knows_its_job(
    helix_print_component, monkeypatch
):
    """Analysis prunes layer indexes as it starts; recovery's job names the
    ones it must keep, so that job is loaded first."""
    order = []
    loop = helix_print_component.eventloop
    monkeypatch.setattr(loop, "register_callback",
                        lambda cb, *a: order.append(getattr(cb, "__qualname__", cb)),
                        raising=False)
    import helix_plr
    real_load = helix_plr.HelixPlr.load_job

    async def load_job(self):
        order.append("load_job")
        return await real_load(self)

    monkeypatch.setattr(helix_plr.HelixPlr, "load_job", load_job)
    await helix_print_component.component_init()
    assert "HelixAnalysis.start" in order
    assert order.index("load_job") < order.index("HelixAnalysis.start")


def test_moonraker_import_shape_loads_every_sibling():
    """Moonraker path-loads the component with the plugin dir NOT on sys.path,
    so a sibling's `from X import ...` only resolves when helix_print.py loads
    X first (prestonbrown/helixscreen#1885: helix_storage imports helix_photos).
    These tests put the plugin dir on sys.path, which is why the bug never
    showed here: this case runs the import in a bare subprocess instead,
    isolated (-I) so a PYTHONPATH naming the plugin dir cannot hide it."""
    code = (
        "import importlib.util, json, sys\n"
        "spec = importlib.util.spec_from_file_location('helix_print', sys.argv[1])\n"
        "module = importlib.util.module_from_spec(spec)\n"
        "sys.modules['helix_print'] = module\n"
        "spec.loader.exec_module(module)\n"
        "print(json.dumps(sorted(module.SIBLING_LOAD_ERRORS)))\n"
    )
    plugin = Path(__file__).resolve().parent.parent / "helix_print.py"
    out = subprocess.run(
        [sys.executable, "-I", "-c", code, str(plugin)],
        capture_output=True, text=True, cwd="/", check=True,
    )
    assert json.loads(out.stdout) == []
