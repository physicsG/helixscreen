# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for the QIDI print_state adapter (helix_plr_qidi.py).

The golden rows are checked against QIDI's own parser, reproduced below line
for line from the Q2's 01.01.02.01 sources:
  moonraker/components/database.py#MoonrakerDatabase.generate_power_off_print_gcode
  klippy/extras/virtual_sdcard.py#VirtualSD.cmd_SDCARD_POWER_OFF_CONTINUES_PINT_FILE
  klippy/extras/virtual_sdcard.py#VirtualSD._set_printer_state_internal

Run with: pytest tests/test_helix_plr_qidi.py -v
"""

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

import helix_plr_qidi as q
from helix_gcode_reader import ModalState


# ---------------------------------------------------------------------------
# QIDI's parser, quoted
# ---------------------------------------------------------------------------


def qidi_parse(rows):
    """What QIDI's resume does with the rows: the Moonraker packing, then the
    Klipper unpacking, then the values each command is built from."""
    item = dict(rows)
    # database.py#generate_power_off_print_gcode
    all_objs = json.loads(item.get("all_objects", "[]"))
    exclude_objs = json.loads(item.get("exclude_objects", "[]"))
    if len(all_objs) > 64:
        all_objs = []
        exclude_objs = []
    item.update({"all_objects": all_objs, "exclude_objects": exclude_objs})
    for key in ["gcode_position", "homing_position", "base_position"]:
        raw = json.loads(item.get(key, '[0,0,0,0]'))
        item[key] = [(float(raw[i]) if i < len(raw) else 0.0) for i in range(4)]
    p = json.loads(json.dumps({"params": item}))["params"]

    # virtual_sdcard.py#_set_printer_state_internal
    def _get_float(key, default=0.0):
        try:
            return float(p.get(key, default))
        except (ValueError, TypeError):
            return default

    def _get_bool(key, default=True):
        val = p.get(key, default)
        return str(val).lower() in ("true", "1", "yes") if not isinstance(val, bool) else val

    last_pos = (list(p.get("gcode_position")) + [0.0] * 4)[:4]
    home = (list(p.get("homing_position")) + [0.0] * 4)[:4]
    is_abs_coord = _get_bool('absolute_coord', True)
    is_abs_extrude = _get_bool('absolute_extrude', True)
    targets = {
        'ext': _get_float('extruder_target', 0.0),
        'bed': _get_float('heater_bed_target', 0.0),
        'chm': _get_float('chamber_target', 0.0),
        'f1': _get_float('cooling_fan_speed', 0.0),
        'f2': _get_float('auxiliary_cooling_fan_speed', 0.0),
        'f3': _get_float('chamber_circulation_fan_speed', 0.0),
        'sf': _get_float('speed_factor', 0.016667) * 60,
        'ef': _get_float('extrude_factor', 1.0)
    }
    lines = [
        f"M140 S{targets['bed']:.2f}",
        f"M141 S{targets['chm']:.2f}",
        f"M104 S{targets['ext']:.2f}",
        f"M106 S{int(targets['f1'] * 255)}",
        f"M106 P2 S{int(targets['f2'] * 255)}",
        f"M106 P3 S{int(targets['f3'] * 255)}",
        f"M220 S{int(targets['sf'] * 100)}",
        f"M221 S{int(targets['ef'] * 100)}",
        f"SET_KINEMATIC_POSITION X={last_pos[0]} Y={last_pos[1]} Z={last_pos[2]}",
        f"SET_GCODE_OFFSET X={home[0]} Y={home[1]} Z={home[2]}",
        f"G1 Z{last_pos[2]} F300",
        "M82" if is_abs_extrude else "M83",
        f"G92 E{last_pos[3] if is_abs_extrude else 0}",
        "G90" if is_abs_coord else "G91",
    ]
    # cmd_SDCARD_POWER_OFF_CONTINUES_PINT_FILE
    file_pos = int(float(p.get('file_position', 0)))
    return {
        "lines": lines,
        "file_position": max(file_pos - 4096, 0),
        "speed": _get_float('speed', 50.0),
        "objects": all_objs,
        "excluded": exclude_objs,
        "plate_index": p.get('plate_index', '1'),
    }


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


def make_snapshot(**over):
    snap = {
        "schema": 1,
        "filename": "cube.gcode",
        "file_position": 123456,
        "toolhead_pos": [121.0, 81.0, 10.4, 1240.0],
        "live_pos": [120.5, 80.25, 10.31, 1234.5],
        "gcode_pos": [120.5, 80.25, 10.2, 1234.5678],
        "layer": 51,
        "gcode_offset": [0.0, 0.0, 0.15, 0.0],
        "abs_coord": True,
        "abs_extrude": False,
        "speed": 3000.0,
        "speed_factor": 1.0,
        "extrude_factor": 1.0,
        "heaters": {
            "extruder": {"target": 220.0, "temp": 219.8},
            "heater_bed": {"target": 60.0, "temp": 60.1},
            "heater_generic chamber": {"target": 45.0, "temp": 44.0},
        },
        "fans": {
            "fan_generic cooling_fan": 1.0,
            "fan_generic auxiliary_cooling_fan": 0.5,
            "fan_generic chamber_circulation_fan": 0.0,
        },
        "objects": [{"name": "cube_1", "center": [100.0, 100.0],
                     "polygon": [[90.0, 90.0], [110.0, 90.0], [110.0, 110.0], [90.0, 110.0]]}],
        "excluded": [],
    }
    snap.update(over)
    return snap


GOLDEN_ROWS = {
    "file_position": "123456",
    "plate_index": "1",
    "gcode_position": "[120.5,80.25,10.31,1234.5678]",
    "homing_position": "[0.0,0.0,0.15,0.0]",
    "base_position": "[0.0,0.0,0.15,0.0]",
    "speed": "50.000000",
    "speed_factor": "0.016667",
    "extrude_factor": "1.000000",
    "absolute_coord": "1",
    "absolute_extrude": "0",
    "extruder_target": "220",
    "heater_bed_target": "60",
    "chamber_target": "45",
    "cooling_fan_speed": "1.000000",
    "auxiliary_cooling_fan_speed": "0.500000",
    "chamber_circulation_fan_speed": "0.000000",
    "all_objects": json.dumps(make_snapshot()["objects"]),
    "exclude_objects": "[]",
    "current_layer": "51",
}


class Cursor:
    def __init__(self, row):
        self._row = row

    async def fetchone(self):
        return self._row


class FakeDatabase:
    def __init__(self, log, batch=True, fail=0):
        self.log = log
        self.rows = {}
        self.fail = fail
        if batch:
            self.update_value_from_config_batch = self._batch

    async def _batch(self, records):
        if self.fail:
            self.fail -= 1
            raise RuntimeError("disk full")
        self.log.append(("batch", [k for _s, k, _v in records]))
        for section, key, value in records:
            assert section == "print_state"
            assert isinstance(value, str)
            self.rows[key] = value

    async def update_value_from_config(self, section, key, value):
        assert section == "print_state"
        assert isinstance(value, str)
        self.log.append(("update", key))
        self.rows[key] = value

    async def delete_value_from_config(self, section, key):
        self.log.append(("delete", section, key))
        if key is None:
            self.rows = {}

    async def get_value_from_config(self, section, key):
        value = self.rows.get(key)
        return Cursor({"value": value} if value is not None else None)


class FakeKlippyApis:
    def __init__(self, log, db, fail=False):
        self.log = log
        self.db = db
        self.fail = fail
        self.seen_rows = None

    async def power_off_print(self, web_request):
        self.log.append(("power_off_print",))
        self.seen_rows = dict(self.db.rows)
        if self.fail:
            raise RuntimeError("SD busy")
        return "ok"


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


@pytest.fixture
def cache_dir(tmp_path):
    d = tmp_path / ".cache"
    d.mkdir()
    (d / "cube.gcode").write_text("G1 X1\n")
    return str(d)


def make_adapter(cache_dir, batch=True, db_fail=0, kapis_fail=False):
    log = []
    db = FakeDatabase(log, batch=batch, fail=db_fail)
    kapis = FakeKlippyApis(log, db, fail=kapis_fail)
    clock = Clock()
    adapter = q.QidiPrintStateAdapter(
        db, kapis,
        mark=lambda name, kind: log.append(("mark", name, kind)),
        unmark=lambda name: log.append(("unmark", name)),
        clock=clock, cache_dir=cache_dir)
    return adapter, db, kapis, clock, log


# ---------------------------------------------------------------------------
# Detection
# ---------------------------------------------------------------------------


class TestDetection:
    def test_resume_command_in_gcode_help(self):
        help_ = {"SDCARD_PRINT_FILE": "Loads a SD file", q.QIDI_RESUME_COMMAND: "断电续打接口"}
        assert q.qidi_print_state_present(help_, {"speed_factor": 1.0}) is True

    def test_speed_factor_2_in_gcode_move(self):
        assert q.qidi_print_state_present(None, {"speed_factor": 1.0, "speed_factor_2": 1 / 60}) is True

    def test_mainline_klipper_is_not_qidi(self):
        help_ = {"SDCARD_PRINT_FILE": "Loads a SD file", "SDCARD_RESET_FILE": "Clears a loaded SD File"}
        gm = {"speed_factor": 1.0, "speed": 1500.0, "gcode_position": [0, 0, 0, 0]}
        assert q.qidi_print_state_present(help_, gm) is False

    def test_nothing_known_is_not_qidi(self):
        assert q.qidi_print_state_present(None, None) is False
        assert q.qidi_print_state_present({}, {}) is False

    def test_moonraker_endpoint_probe(self, cache_dir):
        adapter, db, kapis, _c, _l = make_adapter(cache_dir)
        assert q.moonraker_resumes_print_state(kapis, db) is True
        assert q.moonraker_resumes_print_state(object(), db) is False


# ---------------------------------------------------------------------------
# Row formats
# ---------------------------------------------------------------------------


class TestRowFormats:
    def test_golden_rows(self):
        assert q.print_state_rows(make_snapshot()) == GOLDEN_ROWS

    def test_every_resume_key_is_written(self):
        assert set(q.print_state_rows(make_snapshot())) == set(q.PRINT_STATE_KEYS)

    def test_qidi_parser_reads_golden_rows(self):
        parsed = qidi_parse(GOLDEN_ROWS)
        assert parsed["lines"] == [
            "M140 S60.00",
            "M141 S45.00",
            "M104 S220.00",
            "M106 S255",
            "M106 P2 S127",
            "M106 P3 S0",
            "M220 S100",
            "M221 S100",
            "SET_KINEMATIC_POSITION X=120.5 Y=80.25 Z=10.31",
            "SET_GCODE_OFFSET X=0.0 Y=0.0 Z=0.15",
            "G1 Z10.31 F300",
            "M83",
            "G92 E0",
            "G90",
        ]
        assert parsed["file_position"] == 123456 - 4096
        assert parsed["speed"] == pytest.approx(50.0)
        assert parsed["objects"][0]["name"] == "cube_1"
        assert parsed["plate_index"] == "1"

    def test_speed_factor_is_internal_never_public(self):
        # 'sf': _get_float('speed_factor', 0.016667) * 60 -> M220 S{int(sf * 100)}:
        # the public 1.0 would send M220 S6000.
        rows = q.print_state_rows(make_snapshot(speed_factor=1.0))
        assert rows["speed_factor"] == "0.016667"
        assert "M220 S100" in qidi_parse(rows)["lines"]

    def test_speed_factor_2_wins_over_public(self):
        status = {"gcode_move": {"speed_factor_2": 1.5 / 60, "speed_2": 75.0}}
        rows = q.print_state_rows(make_snapshot(speed_factor=1.0), status)
        assert rows["speed_factor"] == "0.025000"
        assert rows["speed"] == "75.000000"
        assert "M220 S150" in qidi_parse(rows)["lines"]

    @pytest.mark.parametrize("pct", [90, 95, 110, 133])
    def test_m220_survives_qidis_truncation(self, pct):
        # A plain '%f' of 0.9/60 is 0.015000, and int(0.015 * 60 * 100) is 89.
        rows = q.print_state_rows(make_snapshot(speed_factor=pct / 100.0))
        assert "M220 S%d" % pct in qidi_parse(rows)["lines"]

    def test_m221_and_fans_survive_qidis_truncation(self):
        snap = make_snapshot(extrude_factor=0.95,
                             fans={"fan_generic cooling_fan": 85 / 255.0,
                                   "fan_generic auxiliary_cooling_fan": 2 / 255.0,
                                   "fan_generic chamber_circulation_fan": 0.0})
        lines = qidi_parse(q.print_state_rows(snap))["lines"]
        assert "M221 S95" in lines
        assert "M106 S85" in lines
        assert "M106 P2 S2" in lines

    def test_fractional_target_keeps_its_fraction(self):
        snap = make_snapshot(heaters={"extruder": {"target": 212.5, "temp": 0.0}})
        rows = q.print_state_rows(snap)
        assert rows["extruder_target"] == "212.500000"
        assert rows["heater_bed_target"] == "0"
        assert "M104 S212.50" in qidi_parse(rows)["lines"]

    def test_plate_index_and_base_position_from_qidi_status(self):
        status = {"virtual_sdcard": {"plate_index": "3"},
                  "gcode_move": {"base_position": [0.0, 0.0, 0.15, -12.0]}}
        rows = q.print_state_rows(make_snapshot(), status)
        assert rows["plate_index"] == "3"
        assert rows["base_position"] == "[0.0,0.0,0.15,-12.0]"

    def test_objects_and_exclusions_as_json_strings(self):
        objs = [{"name": "a", "center": [1.0, 2.0], "polygon": [[0, 0], [1, 1]]},
                {"name": "b", "center": [3.0, 4.0], "polygon": [[2, 2], [3, 3]]}]
        rows = q.print_state_rows(make_snapshot(objects=objs, excluded=["b"]))
        parsed = qidi_parse(rows)
        assert [o["name"] for o in parsed["objects"]] == ["a", "b"]
        assert parsed["excluded"] == ["b"]

    def test_unknown_layer_writes_zero(self):
        assert q.print_state_rows(make_snapshot(layer=None))["current_layer"] == "0"


class TestFrameZ:
    def test_live_position_is_the_frame(self):
        assert q.frame_z(make_snapshot()) == 10.31

    def test_toolhead_when_no_live_position(self):
        assert q.frame_z(make_snapshot(live_pos=None)) == 10.4

    def test_gcode_plus_offset_as_last_resort(self):
        snap = make_snapshot(live_pos=None, toolhead_pos=None)
        assert q.frame_z(snap) == pytest.approx(10.35)


class TestResumeFilePosition:
    def test_lands_on_the_measured_offset(self):
        row = q.resume_file_position_row(100000, 5000000)
        assert row == 104096
        assert qidi_parse({"file_position": str(row)})["file_position"] == 100000

    def test_offset_under_4096_is_not_flattened_to_zero(self):
        row = q.resume_file_position_row(1000, 5000000)
        assert row == 5096
        assert qidi_parse({"file_position": str(row)})["file_position"] == 1000

    def test_offset_in_last_4k_still_lands_exactly(self):
        row = q.resume_file_position_row(4998000, 5000000)
        assert row == 5002096
        assert qidi_parse({"file_position": str(row)})["file_position"] == 4998000

    def test_offset_past_the_file_is_clamped_to_it(self):
        row = q.resume_file_position_row(6000000, 5000000)
        assert qidi_parse({"file_position": str(row)})["file_position"] == 5000000

    def test_negative_offset_clamps_to_start(self):
        assert q.resume_file_position_row(-5, 100) == 4096

    def test_resume_rows_use_the_resume_state(self):
        state = ModalState()
        state.last_position = [50.0, 60.0, 10.2, 400.0]
        state.base_position = [0.0, 0.0, 0.0, 100.0]
        state.absolute_extrude = True
        state.absolute_coord = True
        state.speed = 40.0  # F2400
        rows = q.resume_rows(make_snapshot(), 200000, 5000000, state)
        parsed = qidi_parse(rows)
        assert parsed["file_position"] == 200000
        assert "SET_KINEMATIC_POSITION X=50.0 Y=60.0 Z=10.31" in parsed["lines"]
        assert "G92 E300.0" in parsed["lines"]
        assert "M82" in parsed["lines"]
        # gcode_move.speed = F x internal factor: 2400 / 60 at 100 %
        assert parsed["speed"] == pytest.approx(40.0)


# ---------------------------------------------------------------------------
# Writer cadence
# ---------------------------------------------------------------------------


class TestWriter:
    @pytest.mark.asyncio
    async def test_first_write_waits_for_qidis_start_delete(self, cache_dir):
        adapter, db, _k, clock, log = make_adapter(cache_dir)
        adapter.on_job_start()
        clock.t += 1.0
        assert await adapter.on_snapshot(make_snapshot()) == {}
        assert log == []
        clock.t += q.FIRST_WRITE_DELAY_S
        written = await adapter.on_snapshot(make_snapshot())
        assert set(written) == set(q.PRINT_STATE_KEYS)
        assert log == [("batch", list(written))]
        assert db.rows == GOLDEN_ROWS

    @pytest.mark.asyncio
    async def test_then_only_changed_rows_at_most_1hz(self, cache_dir):
        adapter, db, _k, clock, log = make_adapter(cache_dir)
        await adapter.on_snapshot(make_snapshot())
        log.clear()
        clock.t += 0.5
        assert await adapter.on_snapshot(make_snapshot(file_position=130000)) == {}
        clock.t += 0.5
        written = await adapter.on_snapshot(make_snapshot(file_position=130000))
        assert written == {"file_position": "130000"}
        clock.t += 1.0
        assert await adapter.on_snapshot(make_snapshot(file_position=130000)) == {}
        assert log == [("batch", ["file_position"])]

    @pytest.mark.asyncio
    async def test_event_save_bypasses_the_throttle(self, cache_dir):
        adapter, db, _k, clock, log = make_adapter(cache_dir)
        await adapter.on_snapshot(make_snapshot())
        clock.t += 0.1
        written = await adapter.on_snapshot(make_snapshot(layer=52), force=True)
        assert written == {"current_layer": "52"}

    @pytest.mark.asyncio
    async def test_per_key_upsert_without_batch(self, cache_dir):
        adapter, db, _k, _c, log = make_adapter(cache_dir, batch=False)
        await adapter.on_snapshot(make_snapshot())
        assert sorted(k for _op, k in log) == sorted(q.PRINT_STATE_KEYS)
        assert all(op == "update" for op, _k in log)

    @pytest.mark.asyncio
    async def test_failed_write_is_retried(self, cache_dir):
        adapter, db, _k, clock, log = make_adapter(cache_dir, db_fail=1)
        assert await adapter.on_snapshot(make_snapshot()) == {}
        clock.t += 0.1
        assert set(await adapter.on_snapshot(make_snapshot())) == set(q.PRINT_STATE_KEYS)

    @pytest.mark.asyncio
    async def test_new_job_writes_everything_again(self, cache_dir):
        adapter, db, _k, clock, log = make_adapter(cache_dir)
        await adapter.on_snapshot(make_snapshot())
        adapter.on_job_end()
        adapter.on_job_start()
        clock.t += q.FIRST_WRITE_DELAY_S
        assert set(await adapter.on_snapshot(make_snapshot())) == set(q.PRINT_STATE_KEYS)


# ---------------------------------------------------------------------------
# Resume and discard
# ---------------------------------------------------------------------------


async def no_wait():
    pass


class TestResume:
    @pytest.mark.asyncio
    async def test_prepare_only_waits(self, cache_dir):
        adapter, db, kapis, _c, log = make_adapter(cache_dir)

        async def heat_wait():
            log.append(("heat_wait",))

        await adapter.prepare(heat_wait)
        assert log == [("heat_wait",)]

    @pytest.mark.asyncio
    async def test_abort_keeps_the_rows(self, cache_dir):
        adapter, db, _k, _c, log = make_adapter(cache_dir)
        await adapter.on_snapshot(make_snapshot())
        adapter.abort()
        assert not any(entry[0] == "delete" for entry in log)
        assert await adapter.has_resume_rows() is True

    @pytest.mark.asyncio
    async def test_bed_wait_rows_marker_then_endpoint(self, cache_dir):
        adapter, db, kapis, _c, log = make_adapter(cache_dir)

        async def heat_wait():
            log.append(("heat_wait",))

        result = await adapter.commit(make_snapshot(), 200000, 5000000, heat_wait)
        assert result == "ok"
        ops = [entry[0] for entry in log]
        assert ops == ["heat_wait", "batch", "mark", "power_off_print"]
        assert log[2] == ("mark", str(Path(cache_dir) / "cube.gcode"), "plr_resume")
        assert kapis.seen_rows["file_position"] == "204096"

    @pytest.mark.asyncio
    async def test_failed_endpoint_withdraws_the_marker(self, cache_dir):
        adapter, db, kapis, _c, log = make_adapter(cache_dir, kapis_fail=True)
        with pytest.raises(RuntimeError):
            await adapter.commit(make_snapshot(), 200000, 5000000, no_wait)
        assert log[-1] == ("unmark", str(Path(cache_dir) / "cube.gcode"))

    @pytest.mark.asyncio
    async def test_no_endpoint_call_when_rows_fail(self, cache_dir):
        adapter, db, kapis, _c, log = make_adapter(cache_dir, db_fail=1)
        with pytest.raises(q.QidiPrintStateError):
            await adapter.commit(make_snapshot(), 200000, 5000000, no_wait)
        assert ("power_off_print",) not in log
        assert not any(entry[0] == "mark" for entry in log)

    @pytest.mark.asyncio
    async def test_empty_cache_refuses(self, tmp_path):
        adapter, db, kapis, _c, log = make_adapter(str(tmp_path / "missing"))
        with pytest.raises(q.QidiPrintStateError):
            await adapter.commit(make_snapshot(), 200000, 5000000, no_wait)
        assert ("power_off_print",) not in log

    @pytest.mark.asyncio
    async def test_discard_deletes_the_section(self, cache_dir):
        adapter, db, _k, _c, log = make_adapter(cache_dir)
        await adapter.on_snapshot(make_snapshot())
        assert await adapter.has_resume_rows() is True
        await adapter.discard()
        assert log[-1] == ("delete", "print_state", None)
        assert await adapter.has_resume_rows() is False


def test_rows_from_a_snapshot_whose_fields_have_not_arrived():
    snap = make_snapshot()
    for key in ("file_position", "extrude_factor", "speed", "speed_factor", "abs_coord",
                "abs_extrude"):
        snap[key] = None
    snap["heaters"] = {"extruder": {"target": None}, "heater_bed": {"target": None}}
    snap["fans"] = {"fan": None}
    rows = q.print_state_rows(snap, {})
    assert rows["file_position"] == "0" and rows["extruder_target"] == "0"
    assert rows["absolute_coord"] == "1"


@pytest.mark.asyncio
async def test_on_snapshot_survives_an_unwritable_snapshot(cache_dir):
    adapter, db, _k, clock, _log = make_adapter(cache_dir)
    adapter.on_job_start()
    clock.t += 10.0
    assert await adapter.on_snapshot({"speed_factor": "fast"}, {}) == {}
    assert db.rows == {}
