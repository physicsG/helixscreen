# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery: where to resume
(docs/devel/POWER_LOSS_RECOVERY.md, "Where it resumes").

The snapshot says how far Klipper had read (file_position, the dispatch head)
and where the nozzle was (motion_report.live_position). Between them lie up to
a few seconds of queued moves. This module scans the file up to the head from
the nearest layer checkpoint, walks the window of lines back from the head by
the measured extruder lead to the line that was executing, checks it against
the nozzle's XY, then moves forward by resume_skip_fraction of a save interval,
because the nozzle kept going for up to one interval after the snapshot. When
the walk finds nothing it falls back to the start of the next layer.

Pure: no Moonraker imports. Python 3.8, stdlib only.
"""

from __future__ import annotations

import math
import threading
from typing import BinaryIO, List, NamedTuple, Optional, Sequence, Tuple

import helix_gcode_reader
from helix_gcode_reader import LineRecord, ModalState, ScanResult, scan_to_offset

# The executing line's XY path must pass this close to the nozzle.
MATCH_TOL_MM = 0.5

# Mesh compensation and Z-hop separate the line's Z from the nozzle's.
Z_TOL_MM = 2.0

# Extruder positions closer than this are the same position.
E_TOL_MM = 1e-3


class ResumePoint(NamedTuple):
    offset: int  # byte offset of the first line to print, at a line boundary
    state: Optional[ModalState]  # modal state just before that line
    method: str  # "measured" | "layer_start"
    retrace_estimate_mm: float  # printed again: from the resume line's start to the skip target
    skip_estimate_mm: float  # assumed printed after the snapshot: from the nozzle to the skip target
    warnings: List[str]


# --------------------------------------------------------------------------
# Scanning to the head
# --------------------------------------------------------------------------


def scan_for_resume(stream: BinaryIO, file_position: int, index_path: Optional[str],
                    identity: Optional[dict],
                    cancel: Optional[threading.Event] = None) -> ScanResult:
    """Scan to file_position, and through the one line starting there, from
    the last layer checkpoint that leaves a full window behind the head, else
    byte 0.

    The line executing at the snapshot can lie in the layer before the head's:
    a layer-change save is taken as SET_PRINT_STATS_INFO is read, seconds of
    queued moves ahead of the nozzle. A checkpoint closer to the head than a
    window would cut that line out of the window. Without a checkpoint a full
    window back, the head is within a window of the file's first layer, so
    byte 0 costs at most that much more.

    Klipper's toolhead.position includes a move that is still blocked in the
    full lookahead queue while virtual_sdcard.file_position has not passed
    its line yet; align_head() decides whether that line belongs to the head.
    """
    stream.seek(file_position)
    in_flight = stream.readline()
    end = file_position + len(in_flight) if in_flight.endswith(b"\n") else file_position
    start = None
    if index_path is not None and identity is not None:
        begin = file_position - helix_gcode_reader.WINDOW_BYTES
        summary = helix_gcode_reader.read_layer_index(index_path, identity, max_offset=begin)
        if summary is not None:
            start = summary.at_or_before
    if start is None:
        return scan_to_offset(stream, end, cancel=cancel)
    return scan_to_offset(stream, end, start.offset, start.state, cancel=cancel)


def align_head(scan: ScanResult, file_position: int, toolhead_pos: Sequence[float],
               gcode_offset: Sequence[float] = (0.0, 0.0, 0.0, 0.0)) -> ScanResult:
    """Keep the in-flight line (the one at file_position) in the window when
    toolhead.position is its end, drop it when toolhead.position is still its
    start. Decided by XY; a line with no XY change stays."""
    window = scan.window
    if not window or window[-1].offset < file_position:
        return scan
    last = window[-1]
    head = scan.state_at_head
    nozzle = (toolhead_pos[0] - (gcode_offset[0] - head.homing_position[0]),
              toolhead_pos[1] - (gcode_offset[1] - head.homing_position[1]))
    to_end = math.hypot(nozzle[0] - last.end_xy[0], nozzle[1] - last.end_xy[1])
    to_start = math.hypot(nozzle[0] - last.start_xy[0], nozzle[1] - last.start_xy[1])
    if to_end <= to_start:
        return scan
    return scan._replace(window=window[:-1])


def state_before(stream: BinaryIO, scan: ScanResult, offset: int) -> ModalState:
    """The modal state just before the line at `offset`, by rescanning from
    the nearest layer start at or before it (byte 0 when there is none)."""
    starts = [(o, st) for o, st in scan.layer_starts.values() if o <= offset]
    if starts:
        o, st = max(starts, key=lambda e: e[0])
        return scan_to_offset(stream, offset, o, st).state_at_head
    return scan_to_offset(stream, offset).state_at_head


# --------------------------------------------------------------------------
# Geometry
# --------------------------------------------------------------------------


def _seg_closest(p: Tuple[float, float], a: Tuple[float, float],
                 b: Tuple[float, float]) -> Tuple[float, float]:
    """(distance from p to segment ab, fraction along ab of the closest point)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length2 = dx * dx + dy * dy
    if length2 == 0.0:
        return math.hypot(p[0] - a[0], p[1] - a[1]), 0.0
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / length2))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy)), t


def _arc_sweep(rec: LineRecord) -> Tuple[float, float, float]:
    """(radius, start angle, signed sweep) of an I/J arc; a full circle when
    the endpoints meet."""
    cx, cy = rec.arc_center
    r = math.hypot(rec.start_xy[0] - cx, rec.start_xy[1] - cy)
    a0 = math.atan2(rec.start_xy[1] - cy, rec.start_xy[0] - cx)
    a1 = math.atan2(rec.end_xy[1] - cy, rec.end_xy[0] - cx)
    sweep = a1 - a0
    if rec.arc_cw:
        if sweep >= 0.0:
            sweep -= 2.0 * math.pi
    elif sweep <= 0.0:
        sweep += 2.0 * math.pi
    return r, a0, sweep


def _closest(rec: LineRecord, p: Tuple[float, float]) -> Tuple[float, float]:
    """(distance from p to the line's XY path, fraction along it)."""
    if rec.is_arc and rec.arc_center is not None:
        r, a0, sweep = _arc_sweep(rec)
        if r == 0.0:
            return _seg_closest(p, rec.start_xy, rec.end_xy)
        cx, cy = rec.arc_center
        ang = math.atan2(p[1] - cy, p[0] - cx) - a0
        # angle of p measured in the sweep's direction, in [0, 2pi)
        rel = (ang if sweep > 0 else -ang) % (2.0 * math.pi)
        span = abs(sweep)
        if rel <= span:
            return abs(math.hypot(p[0] - cx, p[1] - cy) - r), rel / span
        d0 = math.hypot(p[0] - rec.start_xy[0], p[1] - rec.start_xy[1])
        d1 = math.hypot(p[0] - rec.end_xy[0], p[1] - rec.end_xy[1])
        return (d0, 0.0) if d0 <= d1 else (d1, 1.0)
    return _seg_closest(p, rec.start_xy, rec.end_xy)


def path_length(rec: LineRecord) -> float:
    if rec.is_arc and rec.arc_center is not None:
        r, _, sweep = _arc_sweep(rec)
        return abs(r * sweep)
    return math.hypot(rec.end_xy[0] - rec.start_xy[0], rec.end_xy[1] - rec.start_xy[1])


# --------------------------------------------------------------------------
# The walk
# --------------------------------------------------------------------------


class ExecutedLine(NamedTuple):
    index: int  # into scan.window
    fraction: float  # how far along the line the nozzle was


def locate_executed_line(scan: ScanResult, toolhead_pos: Sequence[float],
                         live_pos: Sequence[float], extrude_factor: float,
                         retract_length: float,
                         gcode_offset: Sequence[float] = (0.0, 0.0, 0.0, 0.0),
                         ) -> Optional[ExecutedLine]:
    """The window line the nozzle was on at the snapshot, or None.

    Walks back from the head subtracting each line's toolhead-E change
    (firmware retracts count retract_length) until it reaches live_pos.e;
    lines with no E change are matched by XY. A candidate counts only when its
    XY path passes within MATCH_TOL_MM of the nozzle and its Z within Z_TOL_MM.
    Lines record the file's own frame, so the runtime gcode offset not set by
    the file is taken off the nozzle position first.
    """
    window = scan.window
    if not window:
        return None
    head = scan.state_at_head
    scale = extrude_factor / head.extrude_factor if extrude_factor and head.extrude_factor else 1.0
    shift = [gcode_offset[i] - head.homing_position[i] for i in range(3)]
    nozzle = (live_pos[0] - shift[0], live_pos[1] - shift[1])
    nozzle_z = live_pos[2] - shift[2]
    live_e = live_pos[3]
    e_end = toolhead_pos[3]
    for i in range(len(window) - 1, -1, -1):
        rec = window[i]
        de = rec.toolhead_de * scale + rec.retract * retract_length
        e_start = e_end - de
        if abs(de) > E_TOL_MM:
            hit = min(e_start, e_end) - E_TOL_MM <= live_e <= max(e_start, e_end) + E_TOL_MM
        else:
            hit = abs(e_end - live_e) <= E_TOL_MM
        if hit and abs(rec.z - nozzle_z) <= Z_TOL_MM:
            dist, frac = _closest(rec, nozzle)
            if dist <= MATCH_TOL_MM:
                return ExecutedLine(i, frac)
        e_end = e_start
    return None


def _feed(rec: LineRecord, speed_factor: float, max_velocity: float) -> float:
    v = rec.speed * (speed_factor if speed_factor > 0 else 1.0)
    if max_velocity > 0:
        v = min(v, max_velocity)
    return max(v, 1e-3)


def advance_by_time(scan: ScanResult, executed: ExecutedLine, seconds: float,
                    speed_factor: float, max_velocity: float) -> Tuple[int, float, float]:
    """Move forward from the executing point by `seconds` of motion at each
    line's feedrate. Returns (window index of the line to resume at,
    retrace_estimate_mm, skip_estimate_mm). Resuming at the start of the
    line the target falls in re-traces rather than leaves a gap."""
    window = scan.window
    i = executed.index
    length = path_length(window[i])
    remaining = length * (1.0 - executed.fraction)
    budget = max(0.0, seconds)
    skipped = 0.0
    run = remaining / _feed(window[i], speed_factor, max_velocity)
    if budget <= run:
        travelled = budget * _feed(window[i], speed_factor, max_velocity)
        return i, length * executed.fraction + travelled, travelled
    budget -= run
    skipped += remaining
    for j in range(i + 1, len(window)):
        seg = path_length(window[j])
        t = seg / _feed(window[j], speed_factor, max_velocity)
        if budget <= t:
            travelled = budget * _feed(window[j], speed_factor, max_velocity)
            return j, travelled, skipped + travelled
        budget -= t
        skipped += seg
    last = len(window) - 1
    return last, path_length(window[last]), skipped


class _NextLayerFound(Exception):
    pass


def next_layer_start(stream: BinaryIO, scan: ScanResult, layer: Optional[int],
                     cancel: Optional[threading.Event] = None) -> Optional[ResumePoint]:
    """The start of the layer after `layer` (the snapshot's, else the one at
    the head), read on past the head when the scan has not reached it.

    The fallback when the executed line cannot be found: the rest of the
    current layer is skipped, a cosmetic gap, rather than the layer printed
    again over its own plastic at the same Z, a collision. None on the last
    layer or when the file has no layer markers.
    """
    current = layer if layer is not None else scan.state_at_head.layer
    if current is None:
        return None
    later = sorted((offset, n, state) for n, (offset, state) in scan.layer_starts.items()
                   if n > current)
    if later:
        offset, _, state = later[0]
        return ResumePoint(offset, state, "layer_start", 0.0, 0.0, ["layer_start"])
    found = []

    # The scan has seen no later layer, so the first start past the head is
    # the next one.
    def on_layer(n: int, offset: int, state: ModalState) -> None:
        found.append((offset, state))
        raise _NextLayerFound()

    stream.seek(0, 2)
    end = stream.tell()
    try:
        scan_to_offset(stream, end, scan.offset, scan.state_at_head, cancel=cancel,
                       on_layer_start=on_layer)
    except _NextLayerFound:
        pass
    if not found:
        return None
    offset, state = found[0]
    return ResumePoint(offset, state, "layer_start", 0.0, 0.0, ["layer_start"])


def resume_point(stream: BinaryIO, scan: ScanResult, snapshot: dict,
                 skip_fraction: float, save_interval_s: float) -> Optional[ResumePoint]:
    """The resume point for a snapshot: measured when the walk converges,
    else the next layer's start. None when neither is found."""
    toolhead = snapshot.get("toolhead_pos")
    live = snapshot.get("live_pos")
    limits = snapshot.get("limits") or {}
    retraction = snapshot.get("retraction") or {}
    executed = None
    if toolhead and isinstance(snapshot.get("file_position"), int):
        scan = align_head(scan, snapshot["file_position"], toolhead,
                          snapshot.get("gcode_offset") or (0.0, 0.0, 0.0, 0.0))
    if toolhead and live:
        executed = locate_executed_line(
            scan, toolhead, live, snapshot.get("extrude_factor") or 1.0,
            retraction.get("length") or 0.0,
            snapshot.get("gcode_offset") or (0.0, 0.0, 0.0, 0.0))
    if executed is not None:
        index, retrace, skip = advance_by_time(
            scan, executed, skip_fraction * save_interval_s,
            snapshot.get("speed_factor") or 1.0, limits.get("velocity") or 0.0)
        offset = scan.window[index].offset
        return ResumePoint(offset, state_before(stream, scan, offset), "measured",
                           retrace, skip, [])
    return next_layer_start(stream, scan, snapshot.get("layer"))


class ExcludedObjectOpen(Exception):
    """The resume point lies in an excluded object's block, and nothing that
    is not excluded prints after it."""


class _Landing(Exception):
    pass


class _FirstPrintedExtrusion(helix_gcode_reader.ScanObserver):
    """Stops the scan at the first move that extrudes outside every excluded
    object."""

    def __init__(self, keys: set) -> None:
        self.keys = keys
        self.offset = None  # type: Optional[int]

    def move(self, state: ModalState, start: List[float], words: List[bytes],
             offset: int) -> None:
        name = state.current_object
        if name and helix_gcode_reader.object_key(name) in self.keys:
            return
        if state.last_position[3] > start[3] + E_TOL_MM:
            self.offset = offset
            raise _Landing()


def leave_excluded_object(stream: BinaryIO, point: ResumePoint,
                          excluded: Sequence[str]) -> ResumePoint:
    """`point` moved past the excluded object block it lies in, to the first
    move after it that extrudes outside every excluded object, with the modal
    state carried across the skipped lines.

    Those lines printed nothing in the original print. Resuming inside the
    block would print it: the plan's start re-registers exclude_object's
    transform, which lets its first extruding moves through (the start
    consumes them, helix_plr_render.EXCLUDE_COUNTER_MOVES), and re-entering
    the object would land the nozzle on it. The skipped travel, Z moves,
    retracts and G92 take the landing to where the next printed path starts.
    Nothing re-traced, nothing skipped that would have printed: both
    estimates are 0."""
    name = point.state.current_object if point.state is not None else None
    keys = {helix_gcode_reader.object_key(e) for e in excluded}
    if not name or helix_gcode_reader.object_key(name) not in keys:
        return point
    stream.seek(0, 2)
    end = stream.tell()
    finder = _FirstPrintedExtrusion(keys)
    try:
        scan_to_offset(stream, end, point.offset, point.state.copy(), observer=finder)
    except _Landing:
        pass
    if finder.offset is None:
        raise ExcludedObjectOpen("excluded_object_open: nothing prints after %s" % name)
    state = scan_to_offset(stream, finder.offset, point.offset, point.state.copy()).state_at_head
    return point._replace(offset=finder.offset, state=state, retrace_estimate_mm=0.0,
                          skip_estimate_mm=0.0)
