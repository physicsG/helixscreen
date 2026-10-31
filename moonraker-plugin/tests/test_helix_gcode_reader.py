# SPDX-License-Identifier: GPL-3.0-or-later
"""
The shared G-code reader (helix_gcode_reader.py): the modal-state reducer
against Klipper's gcode_move rules, slicer layer markers, byte offsets, the
window, and the throttle policy.

Run with: pytest tests/test_helix_gcode_reader.py -v
"""

import os
import io
import json
import sys
import threading
import time
import tracemalloc
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

import helix_gcode_reader as reader
from helix_gcode_reader import (
    ModalState,
    RateLimiter,
    ScanCancelled,
    lower_thread_priority,
    scan_to_offset,
    whole_file_analysis_allowed,
)


def gcode(text):
    """Lines joined with LF, ending in one."""
    return ("\n".join(text.strip("\n").splitlines()) + "\n").encode("utf-8")


def scan(data, end=None, **kwargs):
    if isinstance(data, str):
        data = gcode(data)
    return scan_to_offset(io.BytesIO(data), len(data) if end is None else end, **kwargs)


def offset_of(data, needle, nth=0):
    """Byte offset of the nth line starting with `needle`."""
    if isinstance(data, str):
        data = gcode(data)
    pos = -1
    for _ in range(nth + 1):
        pos = data.index(needle.encode("utf-8"), pos + 1)
    return pos


def approx_list(values):
    return [pytest.approx(v) for v in values]


# --------------------------------------------------------------------------
# Extrusion and positioning modes
# --------------------------------------------------------------------------


@pytest.mark.parametrize(
    "program, toolhead_e, gcode_e",
    [
        # Relative E sums every word.
        ("M83\nG1 X1 E1\nG1 X2 E1\nG1 X3 E-.5", 1.5, 1.5),
        # Absolute E takes the last word.
        ("M82\nG1 X1 E1\nG1 X2 E2\nG1 X3 E3", 3.0, 3.0),
        # Klipper's default is absolute E.
        ("G1 X1 E4\nG1 X2 E5", 5.0, 5.0),
        # G91 makes E relative even under M82.
        ("M82\nG91\nG1 X1 E1\nG1 X1 E1", 2.0, 2.0),
        # G92 E0 rebases the logical E; toolhead E keeps counting.
        ("M82\nG1 X1 E10\nG92 E0\nG1 X2 E2", 12.0, 2.0),
        # G92 with no axes rebases every axis to the current position.
        ("M82\nG1 X5 E7\nG92\nG1 E1", 8.0, 1.0),
        # Relative E across a G92 reset: the logical E restarts at 0.
        ("M83\nG1 E2\nG92 E0\nG1 E3", 5.0, 3.0),
        # M221 scales toolhead E; the logical E stays as written.
        ("M82\nM221 S50\nG1 E4", 2.0, 4.0),
        # M221 mid-file keeps the logical E where it was.
        ("M82\nG1 E4\nM221 S50\nG1 E6", 5.0, 6.0),
    ],
    ids=[
        "relative", "absolute", "default-absolute", "g91-relative-e", "g92-e0",
        "g92-bare", "relative-g92", "m221", "m221-mid-file",
    ],
)
def test_extrusion(program, toolhead_e, gcode_e):
    state = scan(program).state_at_head
    assert state.last_position[3] == pytest.approx(toolhead_e)
    assert state.gcode_position()[3] == pytest.approx(gcode_e)


def test_relative_and_absolute_coordinates():
    state = scan("G1 X10 Y10 Z1\nG91\nG1 X1 Y-2 Z.2\nG90\nG1 X3").state_at_head
    assert state.last_position[:3] == approx_list([3.0, 8.0, 1.2])
    assert state.absolute_coord is True


def test_g92_axis_offsets_the_gcode_frame_only():
    state = scan("G1 X10 Y10\nG92 X0\nG1 X5").state_at_head
    assert state.last_position[0] == pytest.approx(15.0)
    assert state.gcode_position()[0] == pytest.approx(5.0)


def test_set_gcode_offset_moves_the_toolhead_frame():
    state = scan(
        "G1 Z1\nSET_GCODE_OFFSET Z=0.1\nG1 Z2\nSET_GCODE_OFFSET Z_ADJUST=0.05\nG1 Z3"
    ).state_at_head
    assert state.homing_position[2] == pytest.approx(0.15)
    assert state.last_position[2] == pytest.approx(3.15)
    assert state.gcode_position()[2] == pytest.approx(3.0)


def test_feedrate_and_speed_factor():
    state = scan("G1 X1 F600\nM220 S150\nG1 X2 F0").state_at_head
    assert state.speed == pytest.approx(10.0)
    assert state.speed_factor == pytest.approx(1.5)


# --------------------------------------------------------------------------
# Layer markers
# --------------------------------------------------------------------------

ORCA = """
M83
SET_PRINT_STATS_INFO TOTAL_LAYER=2 CURRENT_LAYER=0
G1 Z.2 F600
;LAYER_CHANGE
;Z:0.2
;HEIGHT:0.2
G1 E-.8 F2100
SET_PRINT_STATS_INFO CURRENT_LAYER=1
G1 X10 Y10 E1
;LAYER_CHANGE
;Z:0.4
G1 Z.4
SET_PRINT_STATS_INFO CURRENT_LAYER=2
G1 X20 Y10 E1
"""

PRUSA = """
M83
;LAYER_CHANGE
;Z:0.2
;HEIGHT:0.2
;BEFORE_LAYER_CHANGE
G1 Z.2
;AFTER_LAYER_CHANGE
G1 X10 E1
;LAYER_CHANGE
;Z:0.4
;BEFORE_LAYER_CHANGE
G1 Z.4
;AFTER_LAYER_CHANGE
G1 X20 E1
"""

CURA = """
;FLAVOR:Marlin
;LAYER_COUNT:2
;LAYER:0
G0 Z.2
G1 X10 E1
;LAYER:1
G0 Z.4
G1 X20 E2
"""

BAMBU = """
; CHANGE_LAYER
; Z_HEIGHT: 0.2
G1 Z.2
G1 X10 E1
; CHANGE_LAYER
; Z_HEIGHT: 0.4
G1 Z.4
G1 X20 E1
"""

# A custom layer template adds `;LAYER:<n>` next to Orca's own marker, after
# SET_PRINT_STATS_INFO TOTAL_LAYER and before its CURRENT_LAYER.
MIXED = """
;LAYER:0
SET_PRINT_STATS_INFO TOTAL_LAYER=2
;LAYER_CHANGE
;Z:0.2
G92 E0
;LAYER:1
SET_PRINT_STATS_INFO CURRENT_LAYER=1
G1 X10 E1
;LAYER_CHANGE
;Z:0.4
;LAYER:2
SET_PRINT_STATS_INFO CURRENT_LAYER=2
G1 X20 E1
"""


@pytest.mark.parametrize(
    "program, starts, source",
    [
        (ORCA, {0: ("SET_PRINT_STATS_INFO TOTAL", 0), 1: (";LAYER_CHANGE", 0),
                2: (";LAYER_CHANGE", 1)}, "print_stats"),
        (PRUSA, {1: (";LAYER_CHANGE", 0), 2: (";LAYER_CHANGE", 1)}, "count"),
        (CURA, {1: (";LAYER:0", 0), 2: (";LAYER:1", 0)}, "cura"),
        (BAMBU, {1: ("; CHANGE_LAYER", 0), 2: ("; CHANGE_LAYER", 1)}, "count"),
        (MIXED, {1: (";LAYER_CHANGE", 0), 2: (";LAYER_CHANGE", 1)}, "print_stats"),
    ],
    ids=["orca", "prusa", "cura", "bambu", "mixed-markers"],
)
def test_layer_markers(program, starts, source):
    data = gcode(program)
    seen = []
    result = scan(data, on_layer_start=lambda layer, off, st: seen.append((layer, off)))
    expected = {layer: offset_of(data, needle, nth) for layer, (needle, nth) in starts.items()}
    assert {layer: start[0] for layer, start in result.layer_starts.items()} == expected
    assert result.state_at_head.layer == max(starts)
    assert result.state_at_head.layer_source == source
    # The callback's last word on each layer is the start the result keeps.
    assert dict(seen) == expected


def test_layer_start_state_is_the_state_before_that_line():
    data = gcode(PRUSA)
    result = scan(data)
    offset, state = result.layer_starts[2]
    assert state.layer == 1
    assert state.last_position[2] == pytest.approx(0.2)


def test_scan_from_a_layer_start_matches_a_scan_from_zero():
    data = gcode(ORCA + "M221 S90\nG1 X30 E2\n")
    full = scan(data)
    offset, state = full.layer_starts[2]
    resumed = scan_to_offset(io.BytesIO(data), len(data), start_offset=offset,
                             start_state=state)
    assert resumed.state_at_head == full.state_at_head
    assert resumed.window == full.window[-len(resumed.window):]


def test_window_records_carry_the_layer():
    window = scan(ORCA).window
    assert [r.layer for r in window if r.toolhead_de > 0] == [1, 2]


# --------------------------------------------------------------------------
# Tools, objects, heaters, fans, factors
# --------------------------------------------------------------------------


def test_tool_changes_and_extruders():
    state = scan(
        "T0\nM104 S200\nT1\nACTIVATE_EXTRUDER EXTRUDER=extruder1\nM104 S210\n"
        "M109 T0 S180\nSET_PRESSURE_ADVANCE ADVANCE=.04\n"
        "SET_PRESSURE_ADVANCE EXTRUDER=extruder ADVANCE=.03 SMOOTH_TIME=.02"
    ).state_at_head
    assert state.active_tool == 1
    assert state.active_extruder == "extruder1"
    assert state.heater_targets == {"extruder": 180.0, "extruder1": 210.0}
    assert state.pressure_advance == {
        "extruder1": {"advance": 0.04},
        "extruder": {"advance": 0.03, "smooth_time": 0.02},
    }


def test_heaters_fans_and_limits():
    state = scan(
        "M140 S60\nM190 S65\nM141 S40\nSET_HEATER_TEMPERATURE HEATER=chamber_heater TARGET=45\n"
        "M106 S255\nM106 P2 S127.5\nM107 P2\nM106 P3 S51\n"
        "SET_FAN_SPEED FAN=exhaust SPEED=0.3\n"
        "SET_VELOCITY_LIMIT VELOCITY=300 ACCEL=5000 SQUARE_CORNER_VELOCITY=8 "
        "MINIMUM_CRUISE_RATIO=0.5\nM204 P4000 T3000"
    ).state_at_head
    assert state.heater_targets == {"heater_bed": 65.0, "chamber": 40.0,
                                    "chamber_heater": 45.0}
    assert state.fan_speeds == pytest.approx(
        {"fan": 1.0, "fan_p2": 0.0, "fan_p3": 0.2, "exhaust": 0.3})
    assert state.velocity_limits == {
        "velocity": 300.0, "accel": 3000.0, "square_corner_velocity": 8.0,
        "minimum_cruise_ratio": 0.5,
    }


def test_object_defines_are_verbatim_and_uncapped():
    names = ["part_%d" % i for i in range(200)] + ["Würfel_ü"]
    lines = ["EXCLUDE_OBJECT_DEFINE NAME=%s CENTER=1,2 POLYGON=[[0,0],[1,1]] ; c" % n
             for n in names]
    lines += ["EXCLUDE_OBJECT_START NAME=Würfel_ü", "G1 X1"]
    result = scan("\n".join(lines))
    assert len(result.object_defines) == 201
    assert result.object_defines[-1] == (
        "EXCLUDE_OBJECT_DEFINE NAME=Würfel_ü CENTER=1,2 POLYGON=[[0,0],[1,1]]")
    assert result.state_at_head.current_object == "Würfel_ü"


@pytest.mark.parametrize("quoted", ['"my part"', "'my part'"])
def test_a_quoted_object_name_is_one_value(quoted):
    """Klipper splits an extended command's parameters shell-style."""
    result = scan("EXCLUDE_OBJECT_START NAME=%s\nG1 X1" % quoted)
    assert result.state_at_head.current_object == "my part"
    result = scan("EXCLUDE_OBJECT_START NAME=%s\nEXCLUDE_OBJECT_END NAME=%s\nG1 X1"
                  % (quoted, quoted))
    assert result.state_at_head.current_object is None


def test_a_hash_ends_an_extended_commands_parameters_unless_quoted():
    """Kalico's parser reads `#` and `;` as comments (shlex commenters)."""
    state = scan("EXCLUDE_OBJECT_START NAME=box#note\n"
                 "SET_VELOCITY_LIMIT ACCEL=3000 #ACCEL=9000\nG1 X1").state_at_head
    assert state.current_object == "box"
    assert state.velocity_limits["accel"] == 3000.0
    quoted = scan('EXCLUDE_OBJECT_START NAME="a #b"\nG1 X1').state_at_head
    assert quoted.current_object == "a #b"


def test_offsets_are_bytes_not_characters():
    data = gcode(";Würfel ü ü ü\nG1 X1\nG1 X2")
    window = scan(data).window
    assert [r.offset for r in window] == [offset_of(data, "G1 X1"), offset_of(data, "G1 X2")]
    assert window[-1].end_offset == len(data)


# --------------------------------------------------------------------------
# Arcs and firmware retract
# --------------------------------------------------------------------------


def test_arcs_pass_through_with_their_geometry():
    window = scan(
        "G1 X0 Y0\nM83\nG2 X10 Y0 I5 J0 E1 F1200\nG3 X10 Y10 I0 J5 P1 E.5\nG2 X0 Y0 R5"
    ).window
    g2, g3, r_arc = window[1:]
    assert (g2.is_arc, g2.arc_cw, g2.arc_center, g2.end_xy) == (True, True, (5.0, 0.0),
                                                                (10.0, 0.0))
    assert g2.toolhead_de == pytest.approx(1.0)
    assert g2.speed == pytest.approx(20.0)
    assert (g3.arc_cw, g3.arc_center, g3.end_xy) == (False, (10.0, 5.0), (10.0, 10.0))
    assert (r_arc.is_arc, r_arc.arc_center, r_arc.end_xy) == (True, None, (0.0, 0.0))


def test_firmware_retract():
    result = scan("G1 X1 E1\nG10\nG10\nG1 X5\nG11\nG10")
    retracts = [r.retract for r in result.window]
    assert retracts == [0, -1, 0, 1, -1]
    assert result.state_at_head.retracted is True
    assert result.window[1].start_xy == (1.0, 0.0)


# --------------------------------------------------------------------------
# Lines, offsets and the window
# --------------------------------------------------------------------------


def test_comments_blank_lines_and_case():
    state = scan(
        "; header\n\n   \n  ; indented comment\n  G1 X5 ; X99\ng1 y2\n"
        "G1 X6;E9\nset_fan_speed fan=Aux speed=.5\n"
    ).state_at_head
    assert state.last_position[:2] == approx_list([6.0, 2.0])
    assert state.last_position[3] == 0.0
    assert state.fan_speeds == {"Aux": 0.5}


def test_crlf_lines():
    data = b"M83\r\nG1 X1 E1\r\n;LAYER_CHANGE\r\nG1 X2 E1\r\n"
    result = scan(data)
    assert result.state_at_head.last_position[:1] + result.state_at_head.last_position[3:] \
        == approx_list([2.0, 2.0])
    assert result.offset == len(data)
    assert result.layer_starts[1][0] == data.index(b";LAYER_CHANGE")
    assert [r.end_offset for r in result.window] == [15, len(data)]


def test_truncated_last_line_is_not_executed():
    data = b"G1 X1\nG1 X2"
    result = scan(data)
    assert result.state_at_head.last_position[0] == 1.0
    assert result.offset == 6


def test_end_offset_mid_line_stops_before_that_line():
    data = gcode("G1 X1\nG1 X2\nG1 X3")
    end = offset_of(data, "G1 X2") + 3
    result = scan(data, end=end)
    assert result.state_at_head.last_position[0] == 1.0
    assert result.offset == offset_of(data, "G1 X2")


def test_end_offset_at_a_line_boundary_includes_that_line():
    data = gcode("G1 X1\nG1 X2\nG1 X3")
    result = scan(data, end=offset_of(data, "G1 X3"))
    assert result.state_at_head.last_position[0] == 2.0


def test_start_offset_mid_line_skips_to_the_next_line():
    data = gcode("G1 X1\n;G1 X9 E5\nG1 X3")
    start = offset_of(data, ";G1 X9") + 1
    result = scan_to_offset(io.BytesIO(data), len(data), start_offset=start)
    assert result.state_at_head.last_position[0] == 3.0
    assert result.state_at_head.last_position[3] == 0.0
    assert [r.offset for r in result.window] == [offset_of(data, "G1 X3")]


def test_window_keeps_only_the_last_window_bytes(monkeypatch):
    monkeypatch.setattr(reader, "WINDOW_BYTES", 40)
    data = gcode("\n".join("G1 X%d E.1" % i for i in range(100)))
    window = scan(data, end=len(data)).window
    assert window[-1].end_offset == len(data)
    assert window[0].offset >= len(data) - 40
    assert window[0].offset - 12 < len(data) - 40
    assert all(a.end_offset == b.offset for a, b in zip(window, window[1:]))


def test_layer_starts_keep_the_window_and_the_layer_it_begins_in(monkeypatch):
    monkeypatch.setattr(reader, "WINDOW_BYTES", 60)
    body = "".join(";LAYER_CHANGE\nG1 Z%d\nG1 X1\n" % i for i in range(1, 30))
    data = gcode(body)
    result = scan(data)
    begin = len(data) - 60
    inside = [layer for layer, (off, _) in result.layer_starts.items() if off >= begin]
    before = [layer for layer, (off, _) in result.layer_starts.items() if off < begin]
    assert len(before) == 1
    assert before[0] == min(inside) - 1
    assert max(inside) == 29


def test_memory_is_constant_in_file_size(tmp_path):
    layer = "".join("G1 X%d.123 Y%d.456 E.0123\n" % (i, i) for i in range(400))
    path = tmp_path / "big.gcode"
    with open(path, "wb") as f:
        for n in range(1, 1001):
            f.write((";LAYER_CHANGE\nSET_PRINT_STATS_INFO CURRENT_LAYER=%d\nG1 Z%d\n"
                     % (n, n)).encode() + layer.encode())
    size = path.stat().st_size
    assert size > 10 * 1024 * 1024
    tracemalloc.start()
    try:
        with open(path, "rb") as f:
            result = scan_to_offset(f, size)
        peak = tracemalloc.get_traced_memory()[1]
    finally:
        tracemalloc.stop()
    assert result.state_at_head.layer == 1000
    assert peak < 6 * 1024 * 1024


# --------------------------------------------------------------------------
# Cancel and throttle
# --------------------------------------------------------------------------


def test_cancel_stops_the_scan(monkeypatch):
    monkeypatch.setattr(reader, "CHECK_INTERVAL_BYTES", 64)
    data = gcode("\n".join("G1 X%d" % i for i in range(200)))
    cancel = threading.Event()
    cancel.set()
    with pytest.raises(ScanCancelled):
        scan(data, cancel=cancel)


def test_throttle_sees_growing_byte_counts(monkeypatch):
    monkeypatch.setattr(reader, "CHECK_INTERVAL_BYTES", 64)
    data = gcode("\n".join("G1 X%d" % i for i in range(200)))
    calls = []
    scan(data, throttle=calls.append)
    assert len(calls) >= len(data) // 64 - 1
    assert calls == sorted(calls)
    assert calls[-1] <= len(data)


def test_rate_limiter_sleeps_off_the_lead():
    now = [100.0]
    slept = []
    limiter = RateLimiter(1000, clock=lambda: now[0], sleep=slept.append)
    limiter.pace(500)
    now[0] += 0.75
    limiter.pace(1000)
    now[0] += 2.0
    limiter.pace(1500)
    assert slept == approx_list([0.5, 0.25])


@pytest.mark.parametrize(
    "printing, cpus, allowed",
    [(False, 1, True), (True, 1, False), (True, None, False), (True, 2, True),
     (False, None, True)],
)
def test_whole_file_analysis_policy(printing, cpus, allowed):
    assert whole_file_analysis_allowed(printing, cpus) is allowed


@pytest.mark.skipif(not sys.platform.startswith("linux"),
                    reason="only Linux takes a thread id as a PRIO_PROCESS target")
def test_lower_thread_priority_nices_only_the_calling_thread():
    import os
    before = os.getpriority(os.PRIO_PROCESS, 0)
    out = {}

    def worker():
        out["ok"] = lower_thread_priority(5)
        out["nice"] = os.getpriority(os.PRIO_PROCESS, threading.get_native_id())

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert out["ok"] is True
    assert out["nice"] == min(19, before + 5)
    assert os.getpriority(os.PRIO_PROCESS, 0) == before


@pytest.mark.skipif(sys.platform.startswith("linux"),
                    reason="Linux nices the thread; covered above")
def test_lower_thread_priority_declines_where_a_thread_cannot_be_niced():
    import os
    before = os.getpriority(os.PRIO_PROCESS, 0)
    out = {}

    def worker():
        out["ok"] = lower_thread_priority(5)

    t = threading.Thread(target=worker)
    t.start()
    t.join()
    assert out["ok"] is False
    assert os.getpriority(os.PRIO_PROCESS, 0) == before


# --------------------------------------------------------------------------
# ModalState
# --------------------------------------------------------------------------


def test_modal_state_round_trips_and_copies_deeply():
    state = scan(ORCA + "M106 S255\nSET_PRESSURE_ADVANCE ADVANCE=.05\nT2\n").state_at_head
    again = ModalState.from_dict(state.to_dict())
    assert again == state
    clone = state.copy()
    clone.last_position[0] = 999.0
    clone.fan_speeds["fan"] = 0.0
    clone.pressure_advance["extruder"]["advance"] = 1.0
    assert state.last_position[0] != 999.0
    assert state.fan_speeds["fan"] == 1.0
    assert state.pressure_advance["extruder"]["advance"] == 0.05


# --------------------------------------------------------------------------
# Duty cycle and the layer index sidecar
# --------------------------------------------------------------------------

from helix_gcode_reader import (  # noqa: E402
    DutyCycleLimiter,
    LayerIndexWriter,
    layer_index_path,
    read_layer_index,
)


class FakeClock:
    def __init__(self):
        self.now = 0.0
        self.slept = []

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.slept.append(seconds)
        self.now += seconds


def test_duty_cycle_sleeps_in_proportion_to_work():
    clock = FakeClock()
    limiter = DutyCycleLimiter(0.2, max_bytes_per_s=1e12, clock=clock, sleep=clock.sleep)
    clock.now += 1.0          # one second of work
    limiter.pace(1000)
    clock.now += 0.5          # half a second more
    limiter.pace(2000)
    assert clock.slept == approx_list([4.0, 2.0])
    busy = 1.5
    assert busy / clock.now == pytest.approx(0.2)


def test_duty_cycle_obeys_the_byte_cap_when_it_is_stricter():
    clock = FakeClock()
    limiter = DutyCycleLimiter(0.5, max_bytes_per_s=100, clock=clock, sleep=clock.sleep)
    clock.now += 0.1
    limiter.pace(1000)        # 1000 bytes are due at t=10
    assert clock.slept == approx_list([9.9])


@pytest.mark.parametrize("fraction", [0.0, -0.1, 1.5])
def test_duty_cycle_rejects_a_bad_fraction(fraction):
    with pytest.raises(ValueError):
        DutyCycleLimiter(fraction)


IDENT = {"file_size": 1234, "file_mtime": 1700000000.5, "file_hash": "abc"}


def test_layer_index_round_trip(tmp_path):
    path = layer_index_path(tmp_path, "sub/part.gcode")
    assert path == str(tmp_path / ".helix_index" / "sub" / "part.gcode.layers.jsonl")
    states = {}
    w = LayerIndexWriter(path, dict(IDENT, job_id="x"))
    for layer, offset in ((1, 100), (2, 500), (3, 900)):
        st = ModalState()
        st.last_position[2] = layer * 0.2
        st.layer = layer - 1
        states[layer] = st
        w.append(layer, offset, st)
    w.close()
    summary = read_layer_index(path, IDENT, max_offset=600)
    assert (summary.last.layer, summary.last.offset) == (3, 900)
    assert summary.last.state == states[3]
    assert (summary.at_or_before.layer, summary.at_or_before.offset) == (2, 500)
    assert summary.complete is False
    assert read_layer_index(path, IDENT, max_offset=50).at_or_before is None


def test_layer_index_for_another_file_version_is_ignored_and_restarted(tmp_path):
    path = layer_index_path(tmp_path, "a.gcode")
    w = LayerIndexWriter(path, IDENT)
    w.append(1, 100, ModalState())
    w.close()
    changed = dict(IDENT, file_hash="def")
    assert read_layer_index(path, changed) is None
    w = LayerIndexWriter(path, changed)
    w.append(1, 50, ModalState())
    w.close()
    summary = read_layer_index(path, changed)
    assert summary.last.offset == 50
    assert read_layer_index(path, IDENT) is None


def test_layer_index_appends_and_survives_a_cut_line(tmp_path):
    path = layer_index_path(tmp_path, "a.gcode")
    w = LayerIndexWriter(path, IDENT)
    w.append(1, 100, ModalState())
    w.close()
    with open(path, "a") as f:
        f.write('{"layer": 2, "offset": 2')     # a write cut short
    w = LayerIndexWriter(path, IDENT)
    w.append(3, 300, ModalState())
    w.finish(400)
    w.close()
    summary = read_layer_index(path, IDENT)
    assert summary.last.offset == 300 and summary.complete is True


def test_missing_layer_index_is_none(tmp_path):
    assert read_layer_index(str(tmp_path / "nope.jsonl"), IDENT) is None


def test_whole_index_carries_extras_and_progress_lines_and_stays_readable(tmp_path):
    path = layer_index_path(tmp_path, "a.gcode")
    st = ModalState()
    reader.write_layer_index(
        path, IDENT,
        [(1, 10, st, {"tools": [0, 1], "print_z": 0.2, "feature": "Outer wall"}),
         (2, 90, st, None)],
        [(40, "M600"), (60, "M73 P50")], 120)
    lines = [json.loads(l) for l in open(path).read().splitlines()]
    assert lines[1]["tools"] == [0, 1] and lines[1]["print_z"] == 0.2
    assert lines[1]["feature"] == "Outer wall"
    assert "tools" not in lines[2]
    assert lines[3] == {"progress_lines": [[40, "M600"], [60, "M73 P50"]]}
    assert lines[4] == {"complete": True, "end_offset": 120}
    summary = read_layer_index(path, IDENT)
    assert summary.complete and (summary.last.layer, summary.last.offset) == (2, 90)
    assert not (tmp_path / ".helix_index" / "a.gcode.layers.jsonl.tmp").exists()


def test_extra_never_overrides_the_checkpoint_fields(tmp_path):
    path = layer_index_path(tmp_path, "a.gcode")
    w = LayerIndexWriter(path, IDENT)
    w.append(1, 10, ModalState(), {"offset": 999, "tools": [2]})
    w.close()
    summary = read_layer_index(path, IDENT)
    assert summary.last.offset == 10


def test_a_complete_index_is_never_clobbered_by_a_partial_writer(tmp_path):
    path = layer_index_path(tmp_path, "a.gcode")
    complete_starts = [(1, 10, ModalState(), {"tools": [0]}), (2, 90, ModalState(), None)]

    # A writer opening after the complete index exists writes nothing.
    reader.write_layer_index(path, IDENT, complete_starts, [], 120)
    whole = open(path, "rb").read()
    late = LayerIndexWriter(path, IDENT)
    late.append(1, 10, ModalState())
    late.finish(50)
    late.close()
    assert late.inert
    assert open(path, "rb").read() == whole

    # A writer already appending when the complete index lands keeps writing
    # to the file it opened; the complete index stays whole.
    other = layer_index_path(tmp_path, "b.gcode")
    early = LayerIndexWriter(other, IDENT)
    early.append(1, 10, ModalState())
    reader.write_layer_index(other, IDENT, complete_starts, [], 120)
    whole = open(other, "rb").read()
    early.append(2, 90, ModalState())
    early.finish(120)
    early.close()
    assert open(other, "rb").read() == whole
    assert read_layer_index(other, IDENT).complete


def index_for(root, rel, identity):
    path = layer_index_path(root, rel)
    w = LayerIndexWriter(path, identity)
    w.append(1, 0, ModalState())
    w.close()
    return path


def test_prune_removes_stale_indexes_and_keeps_current_ones(tmp_path):
    import os
    kept, removed = [], []
    for i in range(40):                      # no count cap
        f = tmp_path / ("f%d.gcode" % i)
        f.write_text("G1 X1\n")
        kept.append(index_for(tmp_path, f.name, reader.file_identity(f)))
    gone = tmp_path / "gone.gcode"
    gone.write_text("G1 X1\n")
    removed.append(index_for(tmp_path, gone.name, reader.file_identity(gone)))
    gone.unlink()
    changed = tmp_path / "changed.gcode"
    changed.write_text("G1 X1\n")
    removed.append(index_for(tmp_path, changed.name, reader.file_identity(changed)))
    changed.write_text("G1 X2 Y2\n")
    garbage = layer_index_path(tmp_path, "f0.gcode").replace("f0.gcode", "junk.gcode")
    open(garbage, "w").write("not json\n")
    removed.append(garbage)
    external = index_for(tmp_path, ".external/x.gcode", IDENT)
    removed.append(external)
    orphan = kept[0] + ".tmp"
    open(orphan, "w").write("{}\n")
    # Older than any worker for a source under 1 GB could run
    long_ago = time.time() - reader.worker_timeout(1 << 30)
    os.utime(orphan, (long_ago, long_ago))
    removed.append(orphan)
    assert sorted(reader.prune_layer_indexes(tmp_path)) == sorted(removed)
    assert all(os.path.exists(p) for p in kept)
    assert not any(os.path.exists(p) for p in removed)


def test_prune_spares_a_temp_index_a_live_writer_holds(tmp_path):
    import os
    f = tmp_path / "a.gcode"
    f.write_text("G1 X1\n")
    path = layer_index_path(tmp_path, "a.gcode")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path + ".tmp", "w").write("{}\n")
    # A writer may hold it until the worker for its source would time out.
    assert reader.prune_layer_indexes(tmp_path) == []
    held_until = time.time() - reader.worker_timeout(f.stat().st_size)
    os.utime(path + ".tmp", (held_until + 30, held_until + 30))
    assert reader.prune_layer_indexes(tmp_path) == []
    os.utime(path + ".tmp", (held_until - 30, held_until - 30))
    assert reader.prune_layer_indexes(tmp_path, [path]) == []
    assert reader.prune_layer_indexes(tmp_path, [path + ".tmp"]) == []
    assert reader.prune_layer_indexes(tmp_path) == [path + ".tmp"]


def test_prune_removes_a_temp_index_whose_source_is_gone(tmp_path):
    import os
    path = layer_index_path(tmp_path, "gone.gcode")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path + ".tmp", "w").write("{}\n")
    assert reader.prune_layer_indexes(tmp_path) == [path + ".tmp"]


def test_a_large_sources_temp_index_is_held_longer(tmp_path):
    import os
    big = tmp_path / "big.gcode"
    with open(str(big), "wb") as f:
        f.truncate(50 * 1024 * 1024)
    path = layer_index_path(tmp_path, "big.gcode")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path + ".tmp", "w").write("{}\n")
    age = reader.worker_timeout(0) + 60
    os.utime(path + ".tmp", (time.time() - age, time.time() - age))
    assert reader.prune_layer_indexes(tmp_path) == []


def test_prune_spares_protected_indexes_even_when_stale(tmp_path):
    import os
    f = tmp_path / "a.gcode"
    f.write_text("G1 X1\n")
    path = index_for(tmp_path, "a.gcode", reader.file_identity(f))
    external = index_for(tmp_path, ".external/x.gcode", IDENT)
    f.unlink()
    assert reader.prune_layer_indexes(tmp_path, [path, external]) == []
    assert os.path.exists(path) and os.path.exists(external)


# --------------------------------------------------------------------------
# ScanObserver
# --------------------------------------------------------------------------


class Recorder(reader.ScanObserver):
    def __init__(self):
        self.seen = []

    def move(self, state, start, words, offset):
        self.seen.append(("move", offset, list(start), list(state.last_position),
                          [w.decode() for w in words]))

    def command(self, state, cmd, words, body, offset):
        self.seen.append(("command", offset, cmd.decode(), state.active_tool))

    def comment(self, state, line, offset):
        self.seen.append(("comment", offset, line.decode().rstrip()))


def test_observer_sees_every_line_after_the_reducer_applied_it():
    data = gcode("""
;LAYER_CHANGE
T1
G1 X10 E1
G2 X20 I5 J0 E1
EXCLUDE_OBJECT_DEFINE NAME=a
G10
G10
""")
    rec = Recorder()
    scan(data, observer=rec)
    kinds = [(s[0], s[1]) for s in rec.seen]
    assert kinds == [
        ("comment", 0), ("command", offset_of(data, "T1")), ("move", offset_of(data, "G1")),
        ("move", offset_of(data, "G2")), ("command", offset_of(data, "EXCLUDE")),
        ("command", offset_of(data, "G10")), ("command", offset_of(data, "G10", 1)),
    ]
    assert rec.seen[1][3] == 1                       # the T1 already applied
    assert rec.seen[2][2] == [0.0, 0.0, 0.0, 0.0]    # start
    assert rec.seen[2][3] == [10.0, 0.0, 0.0, 1.0]   # end
    assert rec.seen[2][4] == ["G1", "X10", "E1"]
    assert rec.seen[3][2] == [10.0, 0.0, 0.0, 1.0]


def test_observer_does_not_change_the_scan():
    data = gcode("""
G90
M83
;LAYER_CHANGE
G1 Z0.2
G1 X5 Y5 E0.5
;LAYER_CHANGE
G1 Z0.4
G1 X1 Y1 E0.5
""")
    plain = scan(data)
    watched = scan(data, observer=Recorder())
    assert plain.state_at_head == watched.state_at_head
    assert plain.window == watched.window
    assert sorted(plain.layer_starts) == sorted(watched.layer_starts)


def test_fsync_dir_is_the_one_recovery_uses(tmp_path):
    import helix_plr
    assert helix_plr._fsync_dir is reader.fsync_dir
    reader.fsync_dir(tmp_path)
    reader.fsync_dir(tmp_path / "missing")


def test_a_writer_behind_a_complete_index_reads_it_once(tmp_path, monkeypatch):
    path = str(tmp_path / "a.layers.jsonl")
    identity = {"file_size": 10, "file_mtime": 1.0, "file_hash": "x"}
    reader.write_layer_index(path, identity, [(0, 0, reader.ModalState(), None)], [], 10)
    reads = []
    real_read = reader.read_layer_index
    monkeypatch.setattr(reader, "read_layer_index",
                        lambda *a, **k: reads.append(1) or real_read(*a, **k))
    writer = reader.LayerIndexWriter(path, identity)
    for layer in range(5):
        writer.append(layer, layer * 10, reader.ModalState())
    writer.progress_lines([])
    writer.finish(10)
    writer.close()
    assert writer.inert
    assert len(reads) == 1


class TestAtomicWrites:

    def test_write_bytes_replaces_whole_and_leaves_no_temp(self, tmp_path):
        target = tmp_path / "a.png"
        target.write_bytes(b"old")
        reader.atomic_write_bytes(str(target), b"new")
        assert target.read_bytes() == b"new"
        assert [p.name for p in tmp_path.iterdir()] == ["a.png"]

    def test_a_failed_replace_leaves_the_old_file_and_no_temp(self, tmp_path):
        target = tmp_path / "a.cfg"
        target.write_bytes(b"old")

        def crash(src, dst):
            assert open(src, "rb").read() == b"new"
            raise OSError("disk gone")

        with pytest.raises(OSError):
            reader.atomic_write_bytes(str(target), b"new", replace=crash)
        assert target.read_bytes() == b"old"
        assert [p.name for p in tmp_path.iterdir()] == ["a.cfg"]

    def test_the_temp_name_is_hidden_and_unique(self, tmp_path, monkeypatch):
        seen = []

        def spy(src, dst):
            seen.append(os.path.basename(src))
            os.replace(src, dst)

        reader.atomic_write_bytes(str(tmp_path / "a"), b"1", replace=spy)
        reader.atomic_write_bytes(str(tmp_path / "a"), b"2", replace=spy)
        assert all(name.startswith(".a.") for name in seen)
        assert seen[0] != seen[1]

    def test_the_mode_is_applied(self, tmp_path):
        reader.atomic_write_bytes(str(tmp_path / "a"), b"1", mode=0o640)
        assert (tmp_path / "a").stat().st_mode & 0o777 == 0o640

    def test_an_exclusive_publish_never_replaces(self, tmp_path):
        (tmp_path / "out").write_bytes(b"printing")
        (tmp_path / "part").write_bytes(b"other job")
        with pytest.raises(FileExistsError):
            reader.atomic_publish(str(tmp_path / "part"), str(tmp_path / "out"), exclusive=True)
        assert (tmp_path / "out").read_bytes() == b"printing"
        reader.atomic_publish(str(tmp_path / "part"), str(tmp_path / "new"), exclusive=True)
        assert (tmp_path / "new").read_bytes() == b"other job"
        assert not (tmp_path / "part").exists()

    def test_without_hard_links_an_exclusive_publish_still_refuses(self, tmp_path,
                                                                   monkeypatch):
        def no_links(src, dst):
            raise PermissionError("vfat")

        monkeypatch.setattr(reader.os, "link", no_links)
        (tmp_path / "out").write_bytes(b"printing")
        (tmp_path / "part").write_bytes(b"x")
        with pytest.raises(FileExistsError):
            reader.atomic_publish(str(tmp_path / "part"), str(tmp_path / "out"), exclusive=True)
        assert (tmp_path / "out").read_bytes() == b"printing"
        reader.atomic_publish(str(tmp_path / "part"), str(tmp_path / "new"), exclusive=True)
        assert (tmp_path / "new").read_bytes() == b"x"

    def test_the_durable_writers_all_go_through_it(self, monkeypatch, tmp_path):
        import helix_config
        calls = []
        real = reader.atomic_write_bytes
        monkeypatch.setattr(reader, "atomic_write_bytes",
                            lambda *a, **k: calls.append(a[0]) or real(*a, **k))
        helix_config.atomic_write(tmp_path / "printer.cfg", "x")
        assert calls == [str(tmp_path / "printer.cfg")]

    def test_a_whole_layer_index_is_published_through_it(self, monkeypatch, tmp_path):
        published = []
        real = reader.atomic_publish
        monkeypatch.setattr(reader, "atomic_publish",
                            lambda tmp, path, **k: published.append(path) or real(tmp, path, **k))
        path = str(tmp_path / "a.layers.jsonl")
        reader.write_layer_index(path, {"file_size": 1, "file_mtime": 1.0, "file_hash": "x"},
                                 [], [], 1)
        assert published == [path]
        assert reader.read_layer_index(path, {"file_size": 1, "file_mtime": 1.0,
                                              "file_hash": "x"}).complete
