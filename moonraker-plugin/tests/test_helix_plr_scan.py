# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery's resume point (helix_plr_scan.py): the walk back from the
dispatch head by the measured extruder lead, the XY check, the skip-ahead and
the layer-start fallback.

Each case builds a file, places the nozzle on a known line (live_pos) with the
end-of-queue E (toolhead_pos) as Klipper would report it, and checks which
line the walk picks.

Run with: pytest tests/test_helix_plr_scan.py -v
"""

import io
import math
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

import helix_gcode_reader
from helix_gcode_reader import LayerIndexWriter, layer_index_path, scan_to_offset
from helix_plr_scan import (
    ExecutedLine,
    advance_by_time,
    next_layer_start,
    locate_executed_line,
    path_length,
    resume_point,
    scan_for_resume,
    state_before,
)

HEAD_E = 1000.0


def gcode(lines):
    return ("\n".join(lines) + "\n").encode()


def scan(data):
    return scan_to_offset(io.BytesIO(data), len(data))


def nozzle_on(result, index, fraction=0.5, retract_length=0.0, scale=1.0):
    """(toolhead_pos, live_pos) with the nozzle `fraction` along window line `index`."""
    window = result.window
    e = HEAD_E
    for rec in window[index + 1:]:
        e -= rec.toolhead_de * scale + rec.retract * retract_length
    rec = window[index]
    e -= (rec.toolhead_de * scale + rec.retract * retract_length) * (1.0 - fraction)
    if rec.is_arc and rec.arc_center is not None:
        from helix_plr_scan import _arc_sweep
        r, a0, sweep = _arc_sweep(rec)
        a = a0 + sweep * fraction
        xy = (rec.arc_center[0] + r * math.cos(a), rec.arc_center[1] + r * math.sin(a))
    else:
        xy = (rec.start_xy[0] + (rec.end_xy[0] - rec.start_xy[0]) * fraction,
              rec.start_xy[1] + (rec.end_xy[1] - rec.start_xy[1]) * fraction)
    return [0.0, 0.0, rec.z, HEAD_E], [xy[0], xy[1], rec.z, e]


def wall(n, z=0.4, de=".02", x0=0.0):
    """A curved outer wall: n segments of ~0.5 mm on a circle."""
    out = []
    for i in range(1, n + 1):
        a = i * 0.05
        out.append("G1 X%.4f Y%.4f E%s" % (x0 + 10 * math.cos(a), 10 * math.sin(a), de))
    return out


def head_index(result, offset):
    return [r.offset for r in result.window].index(offset)


def walk(result, idx, **kw):
    toolhead, live = nozzle_on(result, idx, kw.pop("fraction", 0.5),
                               kw.get("retract_length", 0.0), kw.pop("scale", 1.0))
    return locate_executed_line(result, toolhead, live, kw.pop("extrude_factor", 1.0),
                                kw.pop("retract_length", 0.0), **kw)


# --------------------------------------------------------------------------
# Resume-point fixtures
# --------------------------------------------------------------------------


def test_orca_walls_relative_e():
    data = gcode(["M83", "G1 Z.4", "G1 X10 Y0 F3000"] + wall(200))
    r = scan(data)
    target = len(r.window) - 40          # ~18 mm of E-bearing wall behind the head
    assert walk(r, target).index == target


def test_orca_infill_absolute_e_with_g92_reset():
    lines = ["M82", "G1 Z.2", "G92 E0", "G1 X0 Y0 F6000"]
    e = 0.0
    for i in range(1, 30):
        e += 1.5
        lines.append("G1 X%d Y%d E%.4f" % (100 if i % 2 else 0, i * 0.5, e))
    data = gcode(lines)
    r = scan(data)
    found = walk(r, len(r.window) - 6)
    assert found.index == len(r.window) - 6
    offset = r.window[found.index].offset
    state = state_before(io.BytesIO(data), r, offset)
    assert state.gcode_position()[3] == pytest.approx(1.5 * 23)


def test_prusa_travel_run_matches_by_xy():
    lines = ["M83", "G1 Z.2", "G1 X0 Y0 E1"] + ["G1 X%d Y%d F9000" % (i * 5, i * 3)
                                                 for i in range(1, 12)] + ["G1 X60 Y40 E2"]
    r = scan(gcode(lines))
    for idx in (2, 5, 9):
        assert walk(r, idx).index == idx


def test_retract_mid_window_sums_signed_e():
    lines = (["M83", "G1 Z.4", "G1 X10 Y0"] + wall(60) + ["G1 E-.8 F2100", "G1 X0 Y0 F9000",
             "G1 E.8 F2100"] + wall(40, x0=0.0))
    r = scan(gcode(lines))
    target = 50
    assert walk(r, target).index == target


def test_firmware_retract_uses_retract_length():
    # The head is retracted and travelling: only the G10 lies between it and the nozzle.
    lines = ["M83", "G1 Z.4", "G1 X10 Y0"] + wall(40) + ["G10", "G1 X0 Y0 F9000"]
    r = scan(gcode(lines))
    target = 30
    assert walk(r, target, retract_length=0.8).index == target
    toolhead, live = nozzle_on(r, target, retract_length=0.8)
    wrong = locate_executed_line(r, toolhead, live, 1.0, 0.0)
    assert wrong is None or wrong.index != target


def test_arcs_match_with_arc_geometry():
    lines = ["M83", "G1 Z.4", "G1 X10 Y0"]
    for k in range(20):
        lines.append("G3 X-10 Y0 I-10 J0 E.5" if k % 2 == 0 else "G3 X10 Y0 I10 J0 E.5")
    r = scan(gcode(lines))
    assert r.window[5].is_arc
    found = walk(r, 5, fraction=0.5)
    assert found.index == 5 and found.fraction == pytest.approx(0.5, abs=1e-6)
    assert path_length(r.window[5]) == pytest.approx(math.pi * 10)


def test_vase_mode_every_line_carries_z():
    lines = ["M83", "G1 X10 Y0"]
    z = 0.2
    for i in range(1, 300):
        z += 0.001
        a = i * 0.05
        lines.append("G1 X%.4f Y%.4f Z%.4f E.02" % (10 * math.cos(a), 10 * math.sin(a), z))
    r = scan(gcode(lines))
    target = len(r.window) - 30
    assert walk(r, target).index == target


def test_zhop_across_a_layer_change_stays_in_the_current_layer():
    lines = ["M83", ";LAYER_CHANGE", "G1 Z.2", "G1 X10 Y0"] + wall(60, z=0.2) + [
        "G1 E-.8", "G1 Z.8", ";LAYER_CHANGE", "G1 Z.4", "G1 X10 Y0", "G1 E.8"] + wall(40)
    r = scan(gcode(lines))
    target = len(r.window) - 20
    found = walk(r, target)
    assert found.index == target and r.window[found.index].layer == 2


def test_extrude_factor_change_at_runtime_scales_the_walk():
    lines = ["M83", "G1 Z.4", "G1 X10 Y0", "M221 S95"] + wall(120)
    r = scan(gcode(lines))
    target = len(r.window) - 30
    # File says 95 %, the user set 100 % on the printer: toolhead E deltas are 100/95 larger.
    scale = 1.0 / 0.95
    assert walk(r, target, extrude_factor=1.0, scale=scale).index == target


def test_runtime_gcode_offset_is_taken_off_the_nozzle():
    lines = ["M83", "G1 Z.4", "G1 X10 Y0"] + wall(80)
    r = scan(gcode(lines))
    toolhead, live = nozzle_on(r, 40)
    live = [live[0] + 2.0, live[1], live[2] + 0.1, live[3]]
    assert locate_executed_line(r, toolhead, live, 1.0, 0.0) is None
    found = locate_executed_line(r, toolhead, live, 1.0, 0.0,
                                 gcode_offset=[2.0, 0.0, 0.1, 0.0])
    assert found.index == 40


def three_layers():
    return gcode(["M83", ";LAYER_CHANGE", "G1 Z.2"] + wall(30, z=0.2) +
                 [";LAYER_CHANGE", "G1 Z.4"] + wall(30) +
                 [";LAYER_CHANGE", "G1 Z.6"] + wall(30, z=0.6))


def test_no_convergence_falls_back_to_the_next_layer_start():
    """Ruling (2026-10-21): skip the rest of the layer (a gap), never print it
    again over itself at the same Z (a collision)."""
    data = three_layers()
    head = data.index(b";LAYER_CHANGE\nG1 Z.6")             # Klipper is still in layer 2
    r = scan_to_offset(io.BytesIO(data), head - 40)
    snapshot = {"toolhead_pos": [0, 0, 0.4, HEAD_E], "live_pos": [150, 150, 0.4, HEAD_E - 0.1],
                "layer": 2, "extrude_factor": 1.0}
    point = resume_point(io.BytesIO(data), r, snapshot, 0.5, 1.0)
    assert point.method == "layer_start" and point.warnings == ["layer_start"]
    assert point.offset == head
    assert point.state.layer == 2 and point.state.last_position[2] == pytest.approx(0.4)


def test_next_layer_start_already_inside_the_scan():
    data = three_layers()
    r = scan(data)
    point = next_layer_start(io.BytesIO(data), r, 2)
    assert point.offset == data.index(b";LAYER_CHANGE\nG1 Z.6")
    assert next_layer_start(io.BytesIO(data), r, 1).offset == data.index(b";LAYER_CHANGE\nG1 Z.4")


def test_head_near_the_file_start_has_a_short_window():
    data = gcode(["M83", "G1 Z.2", "G1 X10 Y0"] + wall(10))
    r = scan(data)
    assert len(r.window) == 12            # every motion line from byte 0
    assert walk(r, 3).index == 3


def test_next_layer_start_without_a_layer_or_a_next_layer():
    data = three_layers()
    mid = scan_to_offset(io.BytesIO(data), data.index(b"G1 Z.4") + 20)
    assert next_layer_start(io.BytesIO(data), mid, None).offset == \
        data.index(b";LAYER_CHANGE\nG1 Z.6")                    # unknown: after the head's layer
    assert next_layer_start(io.BytesIO(data), scan(data), 3) is None   # the last layer
    no_markers = gcode(["G1 X1", "G1 X2"])
    assert next_layer_start(io.BytesIO(no_markers), scan(no_markers), None) is None


# --------------------------------------------------------------------------
# Skip ahead
# --------------------------------------------------------------------------


def straight(n, feed=600):
    return ["M83", "G1 Z.2", "G1 X0 Y0", "G1 F%d" % feed] + ["G1 X%d Y0 E.05" % i
                                                              for i in range(1, n + 1)]


def test_skip_zero_retraces_from_the_line_start():
    r = scan(gcode(straight(40)))
    i = 20
    idx, retrace, skip = advance_by_time(r, ExecutedLine(i, 0.4), 0.0, 1.0, 300.0)
    assert (idx, retrace, skip) == (i, pytest.approx(0.4), 0.0)


def test_skip_half_an_interval_moves_by_feedrate():
    r = scan(gcode(straight(40)))       # 1 mm segments at 10 mm/s
    i = 10
    idx, retrace, skip = advance_by_time(r, ExecutedLine(i, 0.5), 0.5, 1.0, 300.0)
    assert skip == pytest.approx(5.0)
    assert r.window[idx].end_xy[0] - r.window[i].end_xy[0] == pytest.approx(5.0)
    assert retrace == pytest.approx(0.5)


def test_skip_honours_speed_factor_and_velocity_cap():
    r = scan(gcode(straight(60)))
    _, _, fast = advance_by_time(r, ExecutedLine(5, 0.0), 0.5, 2.0, 300.0)
    _, _, capped = advance_by_time(r, ExecutedLine(5, 0.0), 0.5, 2.0, 4.0)
    assert fast == pytest.approx(10.0) and capped == pytest.approx(2.0)


def test_skip_past_the_head_stops_at_the_last_line():
    r = scan(gcode(straight(5)))
    idx, _, _ = advance_by_time(r, ExecutedLine(len(r.window) - 2, 0.0), 100.0, 1.0, 300.0)
    assert idx == len(r.window) - 1


def test_measured_resume_point_end_to_end():
    data = gcode(straight(40))
    r = scan(data)
    toolhead, live = nozzle_on(r, 20, fraction=0.5)
    snapshot = {"toolhead_pos": toolhead, "live_pos": live, "extrude_factor": 1.0,
                "speed_factor": 1.0, "limits": {"velocity": 300.0}, "layer": None}
    point = resume_point(io.BytesIO(data), r, snapshot, 0.5, 1.0)
    assert point.method == "measured" and point.warnings == []
    assert point.skip_estimate_mm == pytest.approx(5.0)
    resumed = r.window[[w.offset for w in r.window].index(point.offset)]
    assert resumed.start_xy[0] == pytest.approx(r.window[20].end_xy[0] + 4)
    assert point.state.last_position[0] == pytest.approx(resumed.start_xy[0])


# --------------------------------------------------------------------------
# Starting from a checkpoint
# --------------------------------------------------------------------------


class CountingStream(io.BytesIO):
    def __init__(self, data):
        super().__init__(data)
        self.read_bytes = 0

    def __next__(self):
        line = super().__next__()
        self.read_bytes += len(line)
        return line


def layered(layers=60, lines=200):
    out = ["M83"]
    for n in range(1, layers + 1):
        out += [";LAYER_CHANGE", "SET_PRINT_STATS_INFO CURRENT_LAYER=%d" % n, "G1 Z%.1f" % (n * .2)]
        out += ["G1 X%d Y%d E.05" % (i % 50, n) for i in range(lines)]
    return gcode(out)


def test_scan_for_resume_starts_at_a_checkpoint(tmp_path, monkeypatch):
    monkeypatch.setattr(helix_gcode_reader, "WINDOW_BYTES", 8 * 1024)
    data = layered()
    ident = {"file_size": len(data), "file_mtime": 1.0, "file_hash": "h"}
    index = layer_index_path(tmp_path, "f.gcode")
    w = LayerIndexWriter(index, ident)
    scan_to_offset(io.BytesIO(data), len(data), on_layer_start=w.append)
    w.close()
    head = data.index(b"\n", len(data) - 100) + 1
    through = data.index(b"\n", head) + 1      # the in-flight line is scanned too
    full = scan_to_offset(io.BytesIO(data), through)
    stream = CountingStream(data)
    fast = scan_for_resume(stream, head, index, ident)
    assert fast.state_at_head == full.state_at_head
    assert fast.window == full.window
    assert stream.read_bytes < 3 * 8 * 1024
    assert scan_for_resume(io.BytesIO(data), head, None, None).state_at_head == full.state_at_head
    assert full.window[-1].offset == head


Q2_LAYER_CHANGE = Path(__file__).parent / "fixtures" / "plr_q2_layer_change.gcode"


def test_a_layer_change_save_finds_the_line_still_printing_the_layer_below(tmp_path):
    """The save a layer change triggers: print_stats already reports layer 17,
    file_position is past SET_PRINT_STATS_INFO, and the nozzle is still on
    layer 16's last lines. The followed index holds a layer-17 checkpoint
    behind the head; the scan must still reach the lines before it."""
    data = Q2_LAYER_CHANGE.read_bytes()
    ident = {"file_size": len(data), "file_mtime": 1.0, "file_hash": "h"}
    index = layer_index_path(tmp_path, "q2.gcode")
    w = LayerIndexWriter(index, ident)
    scan_to_offset(io.BytesIO(data), len(data), on_layer_start=w.append)
    w.close()
    layer17 = data.index(b"SET_PRINT_STATS_INFO CURRENT_LAYER=17")
    marker17 = data.rindex(b";LAYER_CHANGE", 0, layer17)
    head = data.index(b"\n", layer17) + 1
    full = scan_to_offset(io.BytesIO(data), head)
    executing = data.rindex(b"G1 X116.357 Y153.643 E1.74912", 0, marker17)
    toolhead, live = nozzle_on(full, head_index(full, executing))
    snapshot = {"toolhead_pos": toolhead, "live_pos": live, "file_position": head,
                "layer": 17, "extrude_factor": 1.0, "speed_factor": 1.0,
                "limits": {"velocity": 300.0}}
    stream = io.BytesIO(data)
    r = scan_for_resume(stream, head, index, ident)
    point = resume_point(stream, r, snapshot, 0.5, 1.0)
    assert point.method == "measured"
    assert executing <= point.offset < layer17


def test_scan_for_resume_ignores_an_index_of_another_file(tmp_path):
    data = layered(10, 20)
    index = layer_index_path(tmp_path, "f.gcode")
    w = LayerIndexWriter(index, {"file_size": 1, "file_mtime": 1.0, "file_hash": "x"})
    st = helix_gcode_reader.ModalState()
    st.last_position[0] = 999.0
    w.append(5, len(data) // 2, st)
    w.close()
    ident = {"file_size": len(data), "file_mtime": 1.0, "file_hash": "h"}
    result = scan_for_resume(io.BytesIO(data), len(data), index, ident)
    assert result.state_at_head == scan_to_offset(io.BytesIO(data), len(data)).state_at_head


def test_a_line_at_another_height_is_not_the_executing_one():
    r = scan(gcode(["M83", "G1 Z.4", "G1 X10 Y0"] + wall(60)))
    toolhead, live = nozzle_on(r, 30)
    live[2] += 3.0
    assert locate_executed_line(r, toolhead, live, 1.0, 0.0) is None


def test_a_travel_needs_the_extruder_where_it_was():
    lines = ["M83", "G1 Z.2", "G1 X0 Y0 E1"] + ["G1 X%d Y%d F9000" % (i * 5, i * 3)
                                                 for i in range(1, 12)] + ["G1 X60 Y40 E2"]
    r = scan(gcode(lines))
    toolhead, live = nozzle_on(r, 5)
    live[3] -= 0.5                # E mid-way through the extrusion before the travels
    found = locate_executed_line(r, toolhead, live, 1.0, 0.0)
    assert found is None or found.index != 5


def test_clockwise_arcs():
    lines = ["M83", "G1 Z.4", "G1 X10 Y0"]
    for k in range(20):
        lines.append("G2 X-10 Y0 I-10 J0 E.5" if k % 2 == 0 else "G2 X10 Y0 I10 J0 E.5")
    r = scan(gcode(lines))
    rec = r.window[6]                    # G2 from (10, 0) to (-10, 0) around (0, 0)
    assert rec.arc_cw and rec.start_xy == (10.0, 0.0)
    toolhead, live = nozzle_on(r, 6, fraction=0.25)
    live[0], live[1] = 0.0, -10.0        # clockwise passes below the centre
    found = locate_executed_line(r, toolhead, live, 1.0, 0.0)
    assert found.index == 6 and found.fraction == pytest.approx(0.5, abs=1e-6)


def test_state_before_rescans_from_the_nearest_layer_start():
    data = layered(30, 50)
    r = scan(data)
    offset = r.window[-5].offset
    stream = CountingStream(data)
    state = state_before(stream, r, offset)
    assert state == scan_to_offset(io.BytesIO(data), offset).state_at_head
    nearest = max(o for o, _ in r.layer_starts.values() if o <= offset)
    assert stream.read_bytes <= offset - nearest + 64


def long_infill(n=30):
    """M82 infill: 100 mm lines, 1.5 mm of E each."""
    lines = ["M82", "G1 Z.4", "G92 E0", "G1 X0 Y0 F6000"]
    e = 0.0
    for i in range(1, n + 1):
        e += 1.5
        lines.append("G1 X%d Y%d E%.4f" % (100 if i % 2 else 0, i * 2, e))
    return gcode(lines)


@pytest.mark.parametrize("blocked", [True, False])
def test_the_in_flight_line_is_aligned_with_toolhead_position(blocked):
    """With the lookahead queue full, toolhead.position is the END of the line
    at file_position although file_position has not passed it."""
    data = long_infill()
    # Klipper has read up to line 20 (0-based in the window below) and is blocked in it.
    full = scan(data)
    in_flight = full.window[3 + 20]
    file_position = in_flight.offset
    head_rec = in_flight if blocked else full.window[3 + 19]
    target = 3 + 17
    e_head = HEAD_E
    live_e = e_head - sum(r.toolhead_de for r in full.window[target + 1:full.window.index(head_rec) + 1]) \
        - full.window[target].toolhead_de * 0.5
    rec = full.window[target]
    live = [(rec.start_xy[0] + rec.end_xy[0]) / 2, (rec.start_xy[1] + rec.end_xy[1]) / 2, 0.4, live_e]
    toolhead = [head_rec.end_xy[0], head_rec.end_xy[1], 0.4, e_head]
    snapshot = {"toolhead_pos": toolhead, "live_pos": live, "file_position": file_position,
                "extrude_factor": 1.0, "speed_factor": 1.0, "limits": {"velocity": 300.0}}
    stream = io.BytesIO(data)
    r = scan_for_resume(stream, file_position, None, None)
    point = resume_point(stream, r, snapshot, 0.0, 1.0)
    assert point.method == "measured"
    assert point.offset == rec.offset


# --------------------------------------------------------------------------
# Leaving an excluded object's block
# --------------------------------------------------------------------------

from helix_plr_scan import ExcludedObjectOpen, ResumePoint, leave_excluded_object  # noqa: E402


def point_at(data, marker, method="measured"):
    """A resume point at the line starting with `marker`."""
    offset = data.index(marker)
    state = scan_to_offset(io.BytesIO(data), offset).state_at_head
    return ResumePoint(offset, state, method, 1.5, 2.5, [])


OBJECTS = [
    "M82", "G1 F600", "M104 S210", "M106 S255",
    "EXCLUDE_OBJECT_START NAME=box", "G1 Z0.2", "G1 X1 Y1 E1", "G1 X2 Y1 E2",
    "G1 X3 Y1 E3", "EXCLUDE_OBJECT_END NAME=box",
    "EXCLUDE_OBJECT_START NAME=peg", "G1 X10 Y10 E4", "G1 X11 Y10 E5",
    "EXCLUDE_OBJECT_END NAME=peg",
    "G1 E4.2 F2100", "G1 Z0.6", "G1 X20 Y20 F6000", "EXCLUDE_OBJECT_START NAME=Cube",
    "G1 E5 F2100", "G1 X21 Y20 E6", "EXCLUDE_OBJECT_END NAME=Cube",
]


def test_a_point_outside_an_excluded_block_stays():
    data = gcode(OBJECTS)
    point = point_at(data, b"G1 X2 Y1")
    assert leave_excluded_object(io.BytesIO(data), point, []) is point
    assert leave_excluded_object(io.BytesIO(data), point, ["PEG"]) is point


def test_an_excluded_object_right_after_the_one_cut_in_is_skipped_too():
    """box and peg are both excluded: the landing is Cube's unretract, at
    the end of the travel to Cube, with the retract and the Z move carried."""
    data = gcode(OBJECTS)
    moved = leave_excluded_object(io.BytesIO(data), point_at(data, b"G1 X2 Y1"), ["BOX", "PEG"])
    assert moved.offset == data.index(b"G1 E5 F2100")
    assert moved.state.last_position[:3] == [20.0, 20.0, 0.6]
    assert moved.state.last_position[3] == pytest.approx(4.2)
    assert moved.state.current_object == "Cube"
    assert (moved.retrace_estimate_mm, moved.skip_estimate_mm) == (0.0, 0.0)
    assert moved.method == "measured"


def test_only_the_cut_object_excluded_lands_at_the_next_printed_move():
    data = gcode(OBJECTS)
    moved = leave_excluded_object(io.BytesIO(data), point_at(data, b"G1 X2 Y1"), ["box"])
    assert moved.offset == data.index(b"G1 X10 Y10 E4")
    assert moved.state.current_object == "peg"
    assert moved.state.last_position[:2] == [3.0, 1.0]


def test_state_changed_inside_the_skipped_block_is_carried():
    lines = list(OBJECTS)
    at = lines.index("G1 X3 Y1 E3")
    lines[at:at] = ["M83", "G92 E0", "G1 Z0.4", "M104 S230", "M106 S128",
                    "SET_PRINT_STATS_INFO CURRENT_LAYER=2"]
    data = gcode(lines)
    moved = leave_excluded_object(io.BytesIO(data), point_at(data, b"G1 X2 Y1"), ["BOX", "PEG"])
    state = moved.state
    assert state.absolute_extrude is False
    assert state.heater_targets["extruder"] == 230.0
    assert state.fan_speeds["fan"] == pytest.approx(128 / 255.0)
    assert state.layer == 2
    # Relative from G92 E0: box's last line and peg's two. In M83 the
    # following `G1 E4.2` extrudes outside any object, so it is the landing.
    assert state.gcode_position()[3] == pytest.approx(3.0 + 4.0 + 5.0)
    assert moved.offset == data.index(b"G1 E4.2")
    assert state.last_position[2] == 0.4


def test_nothing_printed_after_the_excluded_block_refuses():
    data = gcode(OBJECTS)
    with pytest.raises(ExcludedObjectOpen):
        leave_excluded_object(io.BytesIO(data), point_at(data, b"G1 X2 Y1"),
                              ["BOX", "PEG", "CUBE"])
