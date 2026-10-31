# SPDX-License-Identifier: GPL-3.0-or-later
"""
Print-start hooks (helix_print_hooks.py): the screen's standing options record
sent once at the start of a job no HelixScreen path started.

Run with: pytest tests/test_helix_print_hooks.py -v
"""

import asyncio
from pathlib import Path

import pytest

import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
sys.path.insert(0, str(Path(__file__).parent))

import helix_print_hooks
from helix_print import HELIX_JOB_KINDS, HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY
from helix_print_hooks import (
    PRINT_HOOKS_FEATURE,
    SCREEN_START_KIND,
    STANDING_OPTIONS_KEY,
    hook_gcode,
    standing_gcode,
)
from test_helix_print import (  # noqa: F401  (fixtures are used by name)
    MockWebRequest,
    helix_print_component,
    mock_server,
    temp_gcodes_dir,
)

T0 = 1700000000.25

GCODE = "SET_PRINT_PREFERENCES END_UNLOAD_FILAMENT=[1,0,1,0] FORCE=1"


def _record(gcode=GCODE, version=1):
    return {"version": version, "gcode": gcode}


def _added(filename, job_id, start_time=T0):
    return {
        "action": "added",
        "job": {"job_id": job_id, "filename": filename, "start_time": start_time},
    }


def _printing(mock_server, filename, state="printing"):
    mock_server.components["klippy_apis"].query_objects.return_value = {
        "print_stats": {"state": state, "filename": filename}}


async def _history(mock_server, payload, running=True):
    """Every history handler in registration order, as Moonraker gathers them.
    With `running`, Klipper reports the added job printing."""
    job = payload.get("job") if isinstance(payload, dict) else None
    if running and payload.get("action") == "added" and isinstance(job, dict):
        _printing(mock_server, job.get("filename", ""))
    await asyncio.gather(
        *(h(payload) for h in mock_server.event_handlers["history:history_changed"])
    )


async def _setup(component, opt_in=True, record=None):
    await component.component_init()
    db = component.database
    if opt_in is not None:
        await db.insert_item(HELIX_SCREEN_NAMESPACE, PLUGIN_FEATURES_KEY,
                             {PRINT_HOOKS_FEATURE: opt_in})
    if record is not None:
        await db.insert_item(HELIX_SCREEN_NAMESPACE, STANDING_OPTIONS_KEY, record)


def _sent(component):
    return [c.args[0] for c in component.klippy_apis.run_gcode.call_args_list]


# ============================================================================
# Pure rules
# ============================================================================

class TestStandingGcode:

    def test_reads_the_gcode_of_a_current_record(self):
        assert standing_gcode(_record()) == GCODE

    def test_strips_surrounding_whitespace(self):
        assert standing_gcode(_record("\n  M117 hi \n")) == "M117 hi"

    @pytest.mark.parametrize("record", [
        None,
        "M117 hi",
        {},
        {"gcode": GCODE},
        _record(version=2),
        _record(gcode=""),
        _record(gcode="  \n"),
        _record(gcode=["M117"]),
    ])
    def test_nothing_to_send(self, record):
        assert standing_gcode(record) is None


class TestHookGcode:

    def test_a_foreign_opted_in_start_sends_the_record(self):
        assert hook_gcode(None, False, True, _record()) == GCODE

    @pytest.mark.parametrize("kind", HELIX_JOB_KINDS)
    def test_never_for_a_helix_initiated_job(self, kind):
        assert hook_gcode(kind, False, True, _record()) is None

    def test_never_for_a_start_the_screen_made(self):
        assert hook_gcode(None, True, True, _record()) is None

    def test_never_when_not_opted_in(self):
        assert hook_gcode(None, False, False, _record()) is None

    def test_absent_record_is_a_no_op(self):
        assert hook_gcode(None, False, True, None) is None


# ============================================================================
# Component
# ============================================================================

class TestFeatureEntry:

    @pytest.mark.asyncio
    async def test_registered_and_off_until_opted_in(self, helix_print_component, mock_server):
        await helix_print_component.component_init()

        status = await mock_server.endpoints["/server/helix/status"](MockWebRequest({}))

        assert status["features"][PRINT_HOOKS_FEATURE] == {
            "version": 1, "available": True, "enabled": False, "reason": None,
        }

    @pytest.mark.asyncio
    async def test_missing_module_reports_unavailable(self, helix_print_component, mock_server,
                                                      monkeypatch):
        import helix_print
        monkeypatch.setattr(helix_print, "helix_print_hooks", None)
        await helix_print_component.component_init()

        status = await mock_server.endpoints["/server/helix/status"](MockWebRequest({}))

        entry = status["features"][PRINT_HOOKS_FEATURE]
        assert entry["available"] is False
        assert entry["enabled"] is False
        assert entry["reason"]


class TestHookFires:

    @pytest.mark.asyncio
    async def test_fires_once_per_job_start(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())

        await _history(mock_server, _added("benchy.gcode", "00002A"))
        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == [GCODE]

    @pytest.mark.asyncio
    async def test_each_new_job_gets_its_own_send(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())

        await _history(mock_server, _added("benchy.gcode", "00002A"))
        await _history(mock_server, _added("cube.gcode", "00002B", T0 + 100))

        assert _sent(helix_print_component) == [GCODE, GCODE]

    @pytest.mark.asyncio
    async def test_a_queue_transition_counts_as_a_start(self, helix_print_component, mock_server):
        # Moonraker's job queue starts its next job itself: no marker, no screen.
        await _setup(helix_print_component, record=_record())

        await _history(mock_server, _added("queued/part_2.gcode", "000030"))

        assert _sent(helix_print_component) == [GCODE]

    @pytest.mark.asyncio
    async def test_reads_the_record_at_the_start(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record("M117 old"))
        await helix_print_component.database.insert_item(
            HELIX_SCREEN_NAMESPACE, STANDING_OPTIONS_KEY, _record("M117 new"))

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == ["M117 new"]

    @pytest.mark.asyncio
    async def test_other_history_actions_send_nothing(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())

        await _history(mock_server, {"action": "finished",
                                     "job": {"job_id": "00002A", "filename": "a.gcode"}})
        await _history(mock_server, {"action": "added"})

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_a_failed_send_is_contained(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())
        helix_print_component.klippy_apis.run_gcode.side_effect = RuntimeError("klippy gone")

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == [GCODE]


class TestHookStaysQuiet:

    @pytest.mark.asyncio
    @pytest.mark.parametrize("state,filename", [
        ("standby", "benchy.gcode"),
        ("cancelled", "benchy.gcode"),
        ("error", "benchy.gcode"),
        ("printing", "cube.gcode"),
    ])
    async def test_not_once_the_job_stopped_or_another_runs(
            self, helix_print_component, mock_server, state, filename):
        """The send comes after awaits; by then the job may be gone, and the
        standing gcode must never reach an idle printer or another job."""
        await _setup(helix_print_component, record=_record())
        _printing(mock_server, filename, state)

        await _history(mock_server, _added("benchy.gcode", "00002A"), running=False)

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_not_when_klipper_cannot_say(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())
        mock_server.components["klippy_apis"].query_objects.side_effect = RuntimeError("gone")

        await _history(mock_server, _added("benchy.gcode", "00002A"), running=False)

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_a_withdrawn_screen_start_no_longer_shadows_a_foreign_one(
            self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())
        endpoint = mock_server.endpoints["/server/helix/print_hooks/screen_start"]
        await endpoint(MockWebRequest({"filename": "benchy.gcode"}))
        await endpoint(MockWebRequest({"filename": "benchy.gcode", "withdraw": True}))

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == [GCODE]

    @pytest.mark.asyncio
    async def test_a_screen_start_held_up_behind_a_busy_queue_is_still_the_screens(
            self, helix_print_component, mock_server):
        """SDCARD_PRINT_FILE waits behind Klipper's gcode queue, so history can
        add the job long after the announcement."""
        await _setup(helix_print_component, record=_record())
        markers = helix_print_component.print_hooks.screen_starts
        now = [1000.0]
        markers._clock = lambda: now[0]
        await mock_server.endpoints["/server/helix/print_hooks/screen_start"](
            MockWebRequest({"filename": "benchy.gcode"}))
        now[0] += 30

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_a_screen_start_lasts_only_the_binding_window(
            self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())
        markers = helix_print_component.print_hooks.screen_starts
        now = [1000.0]
        markers._clock = lambda: now[0]
        await mock_server.endpoints["/server/helix/print_hooks/screen_start"](
            MockWebRequest({"filename": "benchy.gcode"}))
        now[0] += helix_print_hooks.SCREEN_START_TTL_S + 1

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == [GCODE]

    @pytest.mark.asyncio
    @pytest.mark.parametrize("kind", HELIX_JOB_KINDS)
    async def test_not_for_a_helix_initiated_job(self, helix_print_component, mock_server, kind):
        await _setup(helix_print_component, record=_record())
        helix_print_component.mark_helix_initiated("benchy.gcode", kind)

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_not_for_a_start_the_screen_announced(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())
        await mock_server.endpoints["/server/helix/print_hooks/screen_start"](
            MockWebRequest({"filename": "benchy.gcode"}))

        await _history(mock_server, _added("benchy.gcode", "00002A"))
        # The announcement covers that one start only.
        await _history(mock_server, _added("benchy.gcode", "00002B", T0 + 100))

        assert _sent(helix_print_component) == [GCODE]

    @pytest.mark.asyncio
    async def test_a_screen_start_is_not_helix_initiated(self, helix_print_component,
                                                         mock_server):
        await _setup(helix_print_component, record=_record())
        await mock_server.endpoints["/server/helix/print_hooks/screen_start"](
            MockWebRequest({"filename": "benchy.gcode"}))

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert helix_print_component.helix_initiated("00002A", "benchy.gcode", T0) is None

    @pytest.mark.asyncio
    async def test_screen_start_needs_a_filename(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=_record())

        with pytest.raises(Exception):
            await mock_server.endpoints["/server/helix/print_hooks/screen_start"](
                MockWebRequest({}))

    @pytest.mark.asyncio
    @pytest.mark.parametrize("opt_in", [None, False])
    async def test_not_when_opt_in_is_off(self, helix_print_component, mock_server, opt_in):
        await _setup(helix_print_component, opt_in=opt_in, record=_record())

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_absent_record_is_a_no_op(self, helix_print_component, mock_server):
        await _setup(helix_print_component, record=None)

        await _history(mock_server, _added("benchy.gcode", "00002A"))

        assert _sent(helix_print_component) == []

    @pytest.mark.asyncio
    async def test_screen_start_kind_is_private(self, helix_print_component):
        # A kind only this module marks never reaches helix_initiated(), so
        # other start-of-job features still act on the screen's own starts.
        assert SCREEN_START_KIND not in HELIX_JOB_KINDS
        with pytest.raises(ValueError):
            helix_print_component.mark_helix_initiated("benchy.gcode", SCREEN_START_KIND)
