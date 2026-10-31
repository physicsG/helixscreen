# SPDX-License-Identifier: GPL-3.0-or-later
"""
The plugin's one G-code reader.

Power-loss recovery (helix_plr_scan.py) and file analysis (helix_analysis.py)
both read files through this module; neither carries its own parser. It holds:

- one streaming pass, scan_to_offset(), that splits each line into its
  command and words and feeds the modal-state reducer, which follows a file the way Klipper's gcode_move
  does (G90/G91, M82/M83, G92, the extrude and speed factors, gcode offsets)
  plus the in-file state a resume has to restore (tool, heater targets, fans,
  pressure advance, velocity limits, firmware retract, the print_stats layer);
- layer markers (OrcaSlicer, PrusaSlicer and SuperSlicer `;LAYER_CHANGE`,
  Bambu `; CHANGE_LAYER`, Cura `;LAYER:<n>`, and `SET_PRINT_STATS_INFO`);
- the window: per-line motion records for the last WINDOW_BYTES before the
  end offset, and the layer starts inside it; memory stays constant in the
  file size;
- a ScanObserver hook, through which a whole-file reader (file analysis) sees
  every move, command and comment after the reducer applied it;
- the throttle policy every background reader follows, and WorkerProcess, the
  niced child process a whole-file reader runs in so its pass never holds the
  GIL Moonraker's event loop needs;
- the layer index sidecar, the file identity it is valid for, and the one
  prune rule for the sidecar directory.

Offsets are byte offsets. A line counts as executed when its newline is at or
before the end offset, which is how Klipper's virtual_sdcard advances
file_position, so a final line with no newline is never applied.

Positions follow gcode_move: `last_position` is the toolhead frame (what
toolhead.position and motion_report.live_position report), and the G-code
frame is last_position minus base_position, E divided by the extrude factor.
Pure: no Moonraker imports, Python 3.8 stdlib only.
"""

from __future__ import annotations

import asyncio
import collections
import math
import os
import shutil
import subprocess
import sys
import threading
import time
from typing import (
    Any,
    BinaryIO,
    Callable,
    Deque,
    Dict,
    Iterable,
    List,
    NamedTuple,
    Optional,
    Tuple,
)

# Motion records are kept for the lines this many bytes behind the end offset:
# the dispatch head leads the nozzle by up to ~120 KB on fine curved walls.
WINDOW_BYTES = 256 * 1024

# Cancel and throttle are consulted once per this many bytes, not per line.
CHECK_INTERVAL_BYTES = 64 * 1024

# Klipper's gcode_move default speed before any F word, in mm/s.
DEFAULT_SPEED_MM_S = 25.0

# A background follower reads at most this fast while a print runs. The bytes
# are the ones Klipper has just read, so they come from the page cache.
FOLLOWER_MAX_BYTES_PER_S = 4 * 1024 * 1024

# Nice increment for a background reader thread.
BACKGROUND_NICE = 10

# Share of one core a background follower may use while a print runs; it
# sleeps (1 - f) / f seconds for each second of work.
FOLLOWER_CPU_FRACTION = 0.15

_AXES = b"XYZE"


class ScanCancelled(Exception):
    """scan_to_offset stopped because its cancel event was set."""


# --------------------------------------------------------------------------
# Modal state
# --------------------------------------------------------------------------


class ModalState:
    """What a file has set up to a point: gcode_move's state plus the in-file
    settings a resume restores. Copy before keeping one: the reducer mutates."""

    __slots__ = (
        "absolute_coord",
        "absolute_extrude",
        "last_position",
        "base_position",
        "homing_position",
        "speed",
        "speed_factor",
        "extrude_factor",
        "active_tool",
        "active_extruder",
        "heater_targets",
        "fan_speeds",
        "pressure_advance",
        "velocity_limits",
        "retracted",
        "layer",
        "total_layers",
        "layer_source",
        "current_object",
    )

    def __init__(self) -> None:
        self.absolute_coord = True
        self.absolute_extrude = True
        # Toolhead frame, [x, y, z, e].
        self.last_position = [0.0, 0.0, 0.0, 0.0]
        self.base_position = [0.0, 0.0, 0.0, 0.0]
        self.homing_position = [0.0, 0.0, 0.0, 0.0]
        # Requested feedrate in mm/s, before speed_factor.
        self.speed = DEFAULT_SPEED_MM_S
        # Public factors: 1.0 is 100 %.
        self.speed_factor = 1.0
        self.extrude_factor = 1.0
        # Last `T<n>` seen; Klipper runs it as a macro.
        self.active_tool = None  # type: Optional[int]
        # Extruder that M104/M109/SET_PRESSURE_ADVANCE address with no T/EXTRUDER.
        self.active_extruder = "extruder"
        self.heater_targets = {}  # type: Dict[str, float]
        # Fan name -> 0..1. M106/M107 with no P is "fan"; with P<n> it is
        # "fan_p<n>", which the printer's macros map to a fan.
        self.fan_speeds = {}  # type: Dict[str, float]
        # extruder -> {"advance": a, "smooth_time": s}, only keys the file set.
        self.pressure_advance = {}  # type: Dict[str, Dict[str, float]]
        # velocity, accel, square_corner_velocity, minimum_cruise_ratio,
        # accel_to_decel: only the ones the file set.
        self.velocity_limits = {}  # type: Dict[str, float]
        self.retracted = False
        # The layer print_stats reports, 1 for the first; see _LayerStarts.
        self.layer = None  # type: Optional[int]
        self.total_layers = None  # type: Optional[int]
        # What numbers the layers: "print_stats" once SET_PRINT_STATS_INFO
        # CURRENT_LAYER is seen, else the first marker style ("count" or
        # "cura"), None before any.
        self.layer_source = None  # type: Optional[str]
        self.current_object = None  # type: Optional[str]

    def copy(self) -> "ModalState":
        s = ModalState.__new__(ModalState)
        s.absolute_coord = self.absolute_coord
        s.absolute_extrude = self.absolute_extrude
        s.last_position = list(self.last_position)
        s.base_position = list(self.base_position)
        s.homing_position = list(self.homing_position)
        s.speed = self.speed
        s.speed_factor = self.speed_factor
        s.extrude_factor = self.extrude_factor
        s.active_tool = self.active_tool
        s.active_extruder = self.active_extruder
        s.heater_targets = dict(self.heater_targets)
        s.fan_speeds = dict(self.fan_speeds)
        s.pressure_advance = {k: dict(v) for k, v in self.pressure_advance.items()}
        s.velocity_limits = dict(self.velocity_limits)
        s.retracted = self.retracted
        s.layer = self.layer
        s.total_layers = self.total_layers
        s.layer_source = self.layer_source
        s.current_object = self.current_object
        return s

    def gcode_position(self) -> List[float]:
        """Position in the G-code frame, as gcode_move.gcode_position reports it."""
        p = [lp - bp for lp, bp in zip(self.last_position, self.base_position)]
        p[3] /= self.extrude_factor
        return p

    def to_dict(self) -> dict:
        """JSON-safe form, for the layer_index sidecar."""
        return {name: _plain(getattr(self, name)) for name in self.__slots__}

    @classmethod
    def from_dict(cls, data: dict) -> "ModalState":
        """Inverse of to_dict; missing keys keep their defaults."""
        s = cls()
        for name in cls.__slots__:
            if name in data:
                setattr(s, name, _plain(data[name]))
        return s

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ModalState):
            return NotImplemented
        return self.to_dict() == other.to_dict()

    def __repr__(self) -> str:
        return "ModalState(%r)" % (self.to_dict(),)


def _plain(value):
    if isinstance(value, dict):
        return {k: _plain(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_plain(v) for v in value]
    return value


# --------------------------------------------------------------------------
# Scan
# --------------------------------------------------------------------------


class LineRecord(NamedTuple):
    """One motion line near the head. Positions are the toolhead frame."""

    offset: int
    end_offset: int
    start_xy: Tuple[float, float]
    end_xy: Tuple[float, float]
    z: float
    # Change in toolhead E (scaled by the extrude factor). Firmware retracts
    # carry 0 here and +1/-1 in `retract`: their length is Klipper config.
    toolhead_de: float
    layer: Optional[int]
    is_arc: bool
    arc_center: Optional[Tuple[float, float]]  # I/J arcs; None for R arcs and lines
    arc_cw: bool  # G2
    speed: float  # requested feedrate in mm/s, before speed_factor
    retract: int  # -1 for a G10 that retracts, +1 for a G11 that unretracts


class ScanResult(NamedTuple):
    state_at_head: ModalState
    window: List[LineRecord]  # oldest first, ends at the head
    object_defines: List[str]  # verbatim EXCLUDE_OBJECT_DEFINE lines
    # layer -> (offset, state at that offset), for every layer that starts
    # inside the window plus the one the window begins in.
    layer_starts: Dict[int, Tuple[int, ModalState]]
    # Line-aligned offset the scan reached: end_offset, or less when end_offset
    # is mid-line or past the end of the file.
    offset: int


LayerCallback = Callable[[int, int, ModalState], None]
ThrottleHook = Callable[[int], None]


class ScanObserver:
    """Sees every line scan_to_offset applies, after the reducer applied it.

    Override what you need; each hook gets the live state (do not mutate it)
    and the line's byte offset. `start` is the toolhead position [x, y, z, e]
    before a move; state.last_position is where it ended.
    """

    def move(self, state: ModalState, start: List[float], words: List[bytes],
             offset: int) -> None:
        pass

    def command(self, state: ModalState, cmd: bytes, words: List[bytes],
                body: bytes, offset: int) -> None:
        """Any non-motion command, EXCLUDE_OBJECT_DEFINE and G10/G11 included.
        `cmd` is upper-cased; `body` is the line up to its comment."""

    def comment(self, state: ModalState, line: bytes, offset: int) -> None:
        """A whole-line comment, leading whitespace stripped."""


def scan_to_offset(
    stream: BinaryIO,
    end_offset: int,
    start_offset: int = 0,
    start_state: Optional[ModalState] = None,
    cancel: Optional[threading.Event] = None,
    throttle: Optional[ThrottleHook] = None,
    on_layer_start: Optional[LayerCallback] = None,
    observer: Optional[ScanObserver] = None,
) -> ScanResult:
    """Reduce `stream` from start_offset to end_offset.

    start_offset/start_state resume from a layer_index entry; a start_offset
    inside a line skips to the next line. `throttle(bytes_read)` is called
    every CHECK_INTERVAL_BYTES and may sleep; `on_layer_start(layer, offset,
    state)` sees every layer start, with the state before that line (shared
    with the result; do not mutate it). Raises ScanCancelled
    when `cancel` is set. Memory stays constant in the file size: only the
    window's records and the layer starts inside it are kept.
    """
    state = start_state.copy() if start_state is not None else ModalState()
    pos = start_offset
    if start_offset > 0:
        # The byte before a line start is its predecessor's newline, so this
        # consumes exactly that newline, or the rest of a line cut in two.
        stream.seek(start_offset - 1)
        pos = start_offset - 1 + len(stream.readline())
    else:
        stream.seek(0)
    window_begin = end_offset - WINDOW_BYTES
    window = collections.deque()  # type: Deque[LineRecord]
    object_defines = []  # type: List[str]
    layers = _LayerStarts(window_begin, on_layer_start)
    next_check = pos + CHECK_INTERVAL_BYTES
    scan_begin = pos

    lp = state.last_position
    for raw in stream:
        line_start = pos
        end = pos + len(raw)
        if end > end_offset or raw[-1:] != b"\n":
            break
        pos = end
        if pos >= next_check:
            next_check = pos + CHECK_INTERVAL_BYTES
            if cancel is not None and cancel.is_set():
                raise ScanCancelled()
            if throttle is not None:
                throttle(pos - scan_begin)

        c = raw[:1]
        if c in b" \t\r\n":
            raw = raw.lstrip()
            if not raw:
                continue
            c = raw[:1]
        if c == b";":
            marker = _layer_marker(raw)
            if marker is not None:
                layers.marker(marker[0], marker[1], line_start, state)
            if observer is not None:
                observer.comment(state, raw, line_start)
            continue
        cut = raw.find(b";")
        body = raw[:cut] if cut >= 0 else raw
        words = body.split()
        if not words:
            continue
        cmd = words[0]
        if not cmd.isupper():
            # Klipper upper-cases a traditional command's whole line, and only
            # the command word and keys of an extended one.
            cmd = cmd.upper()
            if cmd[1:2].isdigit():
                words = [w.upper() for w in words]
        if (b'"' in body or b"'" in body or b"#" in body) and not cmd[1:2].isdigit():
            words = _extended_words(body, words)

        if cmd == b"G1" or cmd == b"G0":
            sx = lp[0]
            sy = lp[1]
            se = lp[3]
            if observer is not None:
                start = list(lp)
            _apply_move(state, lp, words)
            if line_start >= window_begin:
                window.append(LineRecord(
                    line_start, pos, (sx, sy), (lp[0], lp[1]), lp[2], lp[3] - se,
                    state.layer, False, None, False, state.speed, 0,
                ))
            if observer is not None:
                observer.move(state, start, words, line_start)
            continue
        if cmd == b"G2" or cmd == b"G3":
            sx = lp[0]
            sy = lp[1]
            se = lp[3]
            if observer is not None:
                start = list(lp)
            center = _apply_arc(state, lp, words, sx, sy)
            if line_start >= window_begin:
                window.append(LineRecord(
                    line_start, pos, (sx, sy), (lp[0], lp[1]), lp[2], lp[3] - se,
                    state.layer, True, center, cmd == b"G2", state.speed, 0,
                ))
            if observer is not None:
                observer.move(state, start, words, line_start)
            continue
        if cmd == b"G10" or cmd == b"G11":
            retracting = cmd == b"G10"
            if state.retracted != retracting:
                state.retracted = retracting
                if line_start >= window_begin:
                    xy = (lp[0], lp[1])
                    window.append(LineRecord(
                        line_start, pos, xy, xy, lp[2], 0.0, state.layer, False, None,
                        False, state.speed, -1 if retracting else 1,
                    ))
            if observer is not None:
                observer.command(state, cmd, words, body, line_start)
            continue
        if cmd == b"EXCLUDE_OBJECT_DEFINE":
            object_defines.append(body.strip().decode("utf-8", "replace"))
            if observer is not None:
                observer.command(state, cmd, words, body, line_start)
            continue
        layer = _apply_other(state, cmd, words)
        if layer is not None:
            layers.print_stats(layer, line_start, state)
        if observer is not None:
            observer.command(state, cmd, words, body, line_start)

    return ScanResult(state, list(window), object_defines, layers.result(), pos)


_COUNT = "count"
_CURA = "cura"
_PRINT_STATS = "print_stats"


def _layer_marker(line: bytes) -> Optional[Tuple[str, Optional[int]]]:
    """(style, number) for a slicer layer comment, else None.

    `;LAYER_CHANGE` (Orca, Prusa, SuperSlicer) and `; CHANGE_LAYER` (Bambu)
    count; Cura's `;LAYER:<n>` carries a 0-based number.
    """
    if line.startswith(b";LAYER_CHANGE") or line.startswith(b"; CHANGE_LAYER"):
        return (_COUNT, None)
    if line.startswith(b";LAYER:"):
        try:
            return (_CURA, int(line[7:].strip()))
        except ValueError:
            return None
    return None


class _LayerStarts:
    """Where each layer starts, and the starts the result keeps.

    SET_PRINT_STATS_INFO numbers the layers from the first time a file uses
    it, because print_stats, and so the snapshot, reports that number. A slicer
    marker ahead of it still marks where the layer begins: the first marker
    since the last start is the boundary, so the retract and Z move between
    the marker and SET_PRINT_STATS_INFO belong to the new layer. A file
    without it is numbered by its first marker style; another style in the
    same file only marks boundaries.

    The state kept with a start is the state before that line, so
    scan_to_offset(start_offset=offset, start_state=state) reproduces it.
    A later start for the same layer replaces the earlier one.
    """

    def __init__(self, window_begin: int, on_layer_start: Optional[LayerCallback]):
        self._window_begin = window_begin
        self._on_layer_start = on_layer_start
        self._starts = {}  # type: Dict[int, Tuple[int, ModalState]]
        # The latest start before the window, which the window begins in.
        self._before_window = None  # type: Optional[Tuple[int, int, ModalState]]
        self._boundary = None  # type: Optional[Tuple[int, ModalState]]

    def marker(self, style: str, number: Optional[int], offset: int,
               state: ModalState) -> None:
        if self._boundary is None:
            self._boundary = (offset, state.copy())
        source = state.layer_source
        if source == _PRINT_STATS or (source is not None and source != style):
            return
        state.layer_source = style
        self._start((state.layer or 0) + 1 if number is None else number + 1, state)

    def print_stats(self, layer: int, offset: int, state: ModalState) -> None:
        state.layer_source = _PRINT_STATS
        if self._boundary is None:
            if layer == state.layer:
                return
            self._boundary = (offset, state.copy())
        self._start(layer, state)

    def _start(self, layer: int, state: ModalState) -> None:
        offset, snap = self._boundary
        self._boundary = None
        state.layer = layer
        if self._on_layer_start is not None:
            self._on_layer_start(layer, offset, snap)
        if offset >= self._window_begin:
            self._starts[layer] = (offset, snap)
        else:
            self._before_window = (layer, offset, snap)

    def result(self) -> Dict[int, Tuple[int, ModalState]]:
        out = dict(self._starts)
        if self._before_window is not None:
            layer, offset, snap = self._before_window
            out.setdefault(layer, (offset, snap))
        return out


def _apply_move(state: ModalState, lp: List[float], words: List[bytes]) -> None:
    """G0/G1 the way gcode_move.cmd_G1 applies it."""
    bp = state.base_position
    absolute = state.absolute_coord
    for w in words[1:]:
        idx = _AXIS_INDEX.get(w[0])
        if idx is None:
            continue
        try:
            v = float(w[1:])
        except ValueError:
            continue
        if idx == 3:
            v *= state.extrude_factor
            if absolute and state.absolute_extrude:
                lp[3] = v + bp[3]
            else:
                lp[3] += v
        elif idx < 3:
            if absolute:
                lp[idx] = v + bp[idx]
            else:
                lp[idx] += v
        elif v > 0.0:
            state.speed = v / 60.0


# First byte of a move word -> last_position index; 4 is F.
_AXIS_INDEX = {ord("X"): 0, ord("Y"): 1, ord("Z"): 2, ord("E"): 3, ord("F"): 4}


def _apply_arc(
    state: ModalState, lp: List[float], words: List[bytes], sx: float, sy: float
) -> Optional[Tuple[float, float]]:
    """G2/G3: the endpoint moves like G1; I/J give the centre relative to the start."""
    _apply_move(state, lp, words)
    i = j = None
    for w in words[1:]:
        a = w[:1]
        if a == b"I":
            i = _bf(w[1:])
        elif a == b"J":
            j = _bf(w[1:])
    if i is None and j is None:
        return None
    return (sx + (i or 0.0), sy + (j or 0.0))


def _extended_words(body: bytes, words: List[bytes]) -> List[bytes]:
    """An extended command's words as Klipper reads them: its parser splits
    the parameters shell-style with `#` and `;` as comments
    (gcode.py#_get_extended_params, shlex), so NAME="my part" or
    NAME='my part' is one value, quotes removed, and an unquoted `#` ends
    the line. A line with unbalanced quotes, which Klipper refuses, keeps
    the plain split."""
    import shlex
    lex = shlex.shlex(body.decode("utf-8", "surrogateescape"), posix=True)
    lex.whitespace_split = True
    lex.commenters = "#;"
    try:
        parts = list(lex)
    except ValueError:
        return words
    if not parts:
        return words
    return [words[0]] + [p.encode("utf-8", "surrogateescape") for p in parts[1:]]


def object_key(name: str) -> str:
    """An exclude_object name as Klipper compares it: upper-cased."""
    return name.upper()


def _words_kv(words: List[bytes]) -> Dict[bytes, bytes]:
    out = {}
    for w in words[1:]:
        k, sep, v = w.partition(b"=")
        if sep:
            out[k.upper()] = v.strip(b'"')
    return out


def _words_letters(words: List[bytes]) -> Dict[bytes, bytes]:
    return {w[:1].upper(): w[1:] for w in words[1:] if w}


def _bf(value: Optional[bytes]) -> Optional[float]:
    if value is None:
        return None
    try:
        f = float(value)
    except ValueError:
        return None
    if math.isnan(f) or math.isinf(f):
        return None
    return f


def _extruder_for_tool(t: Optional[bytes], state: ModalState) -> str:
    if t is None:
        return state.active_extruder
    try:
        n = int(t)
    except ValueError:
        return state.active_extruder
    return "extruder" if n == 0 else "extruder%d" % n


def _apply_other(state: ModalState, cmd: bytes, words: List[bytes]) -> Optional[int]:
    """Every non-motion command the reducer follows. Returns
    SET_PRINT_STATS_INFO's CURRENT_LAYER for _LayerStarts, else None."""
    if cmd == b"SET_VELOCITY_LIMIT":
        kv = _words_kv(words)
        for key, name in _VELOCITY_KEYS:
            v = _bf(kv.get(key))
            if v is not None:
                state.velocity_limits[name] = v
        return None
    if cmd == b"M106":
        p = _words_letters(words)
        speed = _bf(p.get(b"S"))
        state.fan_speeds[_fan_name(p.get(b"P"))] = (
            1.0 if speed is None else max(0.0, min(1.0, speed / 255.0))
        )
        return None
    if cmd == b"M107":
        state.fan_speeds[_fan_name(_words_letters(words).get(b"P"))] = 0.0
        return None
    if cmd == b"G90":
        state.absolute_coord = True
        return None
    if cmd == b"G91":
        state.absolute_coord = False
        return None
    if cmd == b"M82":
        state.absolute_extrude = True
        return None
    if cmd == b"M83":
        state.absolute_extrude = False
        return None
    if cmd == b"G92":
        _apply_g92(state, _words_letters(words))
        return None
    if cmd[:1] == b"T" and cmd[1:].isdigit():
        state.active_tool = int(cmd[1:])
        return None
    if cmd == b"SET_PRINT_STATS_INFO":
        # A file that sets print_stats info numbers its layers there, even
        # when its first CURRENT_LAYER comes after a slicer marker.
        state.layer_source = _PRINT_STATS
        kv = _words_kv(words)
        total = _bf(kv.get(b"TOTAL_LAYER"))
        if total is not None:
            state.total_layers = int(total)
        current = _bf(kv.get(b"CURRENT_LAYER"))
        return None if current is None else int(current)
    if cmd in (b"M104", b"M109"):
        p = _words_letters(words)
        target = _bf(p.get(b"S"))
        if target is not None:
            state.heater_targets[_extruder_for_tool(p.get(b"T"), state)] = target
        return None
    if cmd in (b"M140", b"M190"):
        target = _bf(_words_letters(words).get(b"S"))
        if target is not None:
            state.heater_targets["heater_bed"] = target
        return None
    if cmd in (b"M141", b"M191"):
        target = _bf(_words_letters(words).get(b"S"))
        if target is not None:
            state.heater_targets["chamber"] = target
        return None
    if cmd == b"SET_HEATER_TEMPERATURE":
        kv = _words_kv(words)
        heater = kv.get(b"HEATER")
        target = _bf(kv.get(b"TARGET"))
        if heater:
            state.heater_targets[heater.decode("utf-8", "replace")] = (
                0.0 if target is None else target
            )
        return None
    if cmd == b"SET_FAN_SPEED":
        kv = _words_kv(words)
        fan = kv.get(b"FAN")
        speed = _bf(kv.get(b"SPEED"))
        if fan and speed is not None:
            state.fan_speeds[fan.decode("utf-8", "replace")] = speed
        return None
    if cmd == b"SET_PRESSURE_ADVANCE":
        kv = _words_kv(words)
        ext = kv.get(b"EXTRUDER")
        name = ext.decode("utf-8", "replace") if ext else state.active_extruder
        entry = state.pressure_advance.setdefault(name, {})
        advance = _bf(kv.get(b"ADVANCE"))
        smooth = _bf(kv.get(b"SMOOTH_TIME"))
        if advance is not None:
            entry["advance"] = advance
        if smooth is not None:
            entry["smooth_time"] = smooth
        return None
    if cmd == b"ACTIVATE_EXTRUDER":
        ext = _words_kv(words).get(b"EXTRUDER")
        if ext:
            state.active_extruder = ext.decode("utf-8", "replace")
        return None
    if cmd == b"M220":
        s = _bf(_words_letters(words).get(b"S"))
        if s is not None and s > 0.0:
            state.speed_factor = s / 100.0
        return None
    if cmd == b"M221":
        s = _bf(_words_letters(words).get(b"S"))
        if s is not None and s > 0.0:
            _set_extrude_factor(state, s / 100.0)
        return None
    if cmd == b"M204":
        p = _words_letters(words)
        s = _bf(p.get(b"S"))
        if s is None:
            pt = [v for v in (_bf(p.get(b"P")), _bf(p.get(b"T"))) if v is not None]
            s = min(pt) if len(pt) == 2 else None
        if s is not None:
            state.velocity_limits["accel"] = s
        return None
    if cmd == b"SET_GCODE_OFFSET":
        _apply_gcode_offset(state, _words_kv(words))
        return None
    if cmd == b"EXCLUDE_OBJECT_START":
        name = _words_kv(words).get(b"NAME")
        state.current_object = name.decode("utf-8", "replace") if name else None
        return None
    if cmd == b"EXCLUDE_OBJECT_END":
        state.current_object = None
        return None
    return None


_VELOCITY_KEYS = (
    (b"VELOCITY", "velocity"),
    (b"ACCEL", "accel"),
    (b"ACCEL_TO_DECEL", "accel_to_decel"),
    (b"SQUARE_CORNER_VELOCITY", "square_corner_velocity"),
    (b"MINIMUM_CRUISE_RATIO", "minimum_cruise_ratio"),
)


def _fan_name(p: Optional[bytes]) -> str:
    if p is None or p in (b"", b"0"):
        return "fan"
    return "fan_p" + p.decode("ascii", "replace")


def _apply_g92(state: ModalState, params: Dict[bytes, bytes]) -> None:
    """gcode_move.cmd_G92."""
    lp = state.last_position
    bp = state.base_position
    any_axis = False
    for idx, axis in enumerate(_AXES):
        v = _bf(params.get(bytes((axis,))))
        if v is None:
            continue
        any_axis = True
        if idx == 3:
            v *= state.extrude_factor
        bp[idx] = lp[idx] - v
    if not any_axis:
        bp[:] = lp


def _set_extrude_factor(state: ModalState, new_factor: float) -> None:
    """gcode_move.cmd_M221: keep the logical E where it is."""
    lp = state.last_position
    bp = state.base_position
    e_value = (lp[3] - bp[3]) / state.extrude_factor
    bp[3] = lp[3] - e_value * new_factor
    state.extrude_factor = new_factor


def _apply_gcode_offset(state: ModalState, kv: Dict[bytes, bytes]) -> None:
    """gcode_move.cmd_SET_GCODE_OFFSET, without MOVE=1's toolhead move."""
    hp = state.homing_position
    bp = state.base_position
    for idx, axis in enumerate(("X", "Y", "Z", "E")):
        offset = _bf(kv.get(axis.encode()))
        if offset is None:
            adjust = _bf(kv.get((axis + "_ADJUST").encode()))
            if adjust is None:
                continue
            offset = hp[idx] + adjust
        delta = offset - hp[idx]
        bp[idx] += delta
        hp[idx] = offset


# --------------------------------------------------------------------------
# Throttle policy
# --------------------------------------------------------------------------


def whole_file_analysis_allowed(printing: bool, cpu_count: Optional[int]) -> bool:
    """Whole-file analysis runs while printing only on a multi-core host.

    The printing file's own layer index is the exception: the follower builds
    it on any host, rate-capped by RateLimiter, never past file_position.
    """
    if not printing:
        return True
    return (cpu_count or 1) > 1


class RateLimiter:
    """A ThrottleHook that holds a reader to max_bytes_per_s.

    Pass `limiter.pace` as scan_to_offset's throttle. The clock and sleep are
    injectable for tests.
    """

    def __init__(
        self,
        max_bytes_per_s: float = FOLLOWER_MAX_BYTES_PER_S,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self._rate = float(max_bytes_per_s)
        self._clock = clock
        self._sleep = sleep
        self._start = clock()

    def pace(self, bytes_read: int) -> None:
        due = self._start + bytes_read / self._rate
        ahead = due - self._clock()
        if ahead > 0:
            self._sleep(ahead)


class DutyCycleLimiter:
    """A ThrottleHook holding a reader to a share of one core and to a byte rate.

    Between two pace() calls the reader was working; it then sleeps long
    enough that work is cpu_fraction of the elapsed time, or longer when the
    byte cap asks for more. Measured in wall time, which is what the printer's
    other processes lose to it.
    """

    def __init__(
        self,
        cpu_fraction: float = FOLLOWER_CPU_FRACTION,
        max_bytes_per_s: float = FOLLOWER_MAX_BYTES_PER_S,
        clock: Callable[[], float] = time.monotonic,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if not 0.0 < cpu_fraction <= 1.0:
            raise ValueError("cpu_fraction must be in (0, 1]")
        self._ratio = (1.0 - cpu_fraction) / cpu_fraction
        self._clock = clock
        self._sleep = sleep
        self._bytes = RateLimiter(max_bytes_per_s, clock, self._sleep_noted)
        self._byte_sleep = 0.0
        self._work_start = clock()

    def _sleep_noted(self, seconds: float) -> None:
        self._byte_sleep = seconds

    def pace(self, bytes_read: int) -> None:
        work = self._clock() - self._work_start
        self._byte_sleep = 0.0
        self._bytes.pace(bytes_read)
        pause = max(work * self._ratio, self._byte_sleep)
        if pause > 0:
            self._sleep(pause)
        self._work_start = self._clock()


def fsync_dir(directory: Any) -> None:
    """Make a rename or create inside the directory at `directory` durable.
    Best effort: one that cannot be opened or synced is left as it is."""
    try:
        fd = os.open(str(directory), os.O_RDONLY)
    except OSError:
        return
    try:
        os.fsync(fd)
    except OSError:
        pass
    finally:
        os.close(fd)


def atomic_publish(tmp: str, path: str, exclusive: bool = False,
                   replace: Callable[[str, str], None] = os.replace) -> None:
    """Move the finished file `tmp` to `path` in one step, durably. With
    `exclusive` an existing `path` is never replaced: FileExistsError, and
    `tmp` stays for the caller to remove. On a filesystem without hard links
    (vfat) the exclusive check and the rename are two steps."""
    if exclusive:
        try:
            os.link(tmp, path)
        except FileExistsError:
            raise
        except OSError:
            if os.path.lexists(path):
                raise FileExistsError(path)
            replace(tmp, path)
        else:
            os.unlink(tmp)
    else:
        replace(tmp, path)
    fsync_dir(os.path.dirname(path) or ".")


def read_file_text(path: str) -> str:
    """A file's text with its line endings as they are on disk."""
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def atomic_write_text(path: str, text: str,
                      replace: Callable[[str, str], None] = os.replace) -> None:
    """Write `text` to `path` through a temp file beside it, fsynced, then
    renamed over it with the file's own mode. A crash before the rename
    leaves `path` as it was."""
    try:
        mode: Optional[int] = os.stat(str(path)).st_mode & 0o7777
    except OSError:
        mode = None
    atomic_write_bytes(str(path), text.encode("utf-8"), replace=replace, mode=mode)


def atomic_write_bytes(path: str, data: bytes, fsync: bool = True,
                       replace: Callable[[str, str], None] = os.replace,
                       mode: Optional[int] = None) -> None:
    """Write `data` to `path` through a hidden, uniquely named temp file
    beside it, then rename it over `path`. A failure at any point leaves
    `path` as it was and no temp file behind."""
    import tempfile
    path = str(path)
    directory, name = os.path.split(path)
    fd, tmp = tempfile.mkstemp(dir=directory or ".", prefix="." + name + ".", suffix=".tmp")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            if fsync:
                os.fsync(f.fileno())
        os.chmod(tmp, 0o644 if mode is None else mode)
        atomic_publish(tmp, path, replace=replace)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def lower_thread_priority(increment: int = BACKGROUND_NICE) -> bool:
    """Nice the calling thread only (Linux: a thread id is a PRIO_PROCESS
    target). Returns False where that is not possible."""
    try:
        tid = threading.get_native_id()
        current = os.getpriority(os.PRIO_PROCESS, tid)
        os.setpriority(os.PRIO_PROCESS, tid, min(19, current + increment))
    except (AttributeError, OSError):
        return False
    return True


# How long a worker process gets for a file before it is killed: this much,
# plus WORKER_SECONDS_PER_MB of the file's size. Sized for a nice-19 worker on
# the slowest hosts (MIPS and Cortex-A7 boards), which share their CPU with
# Klipper and run CPython 15 to 30 times slower than a desktop.
WORKER_BASE_TIMEOUT_S = 600.0
WORKER_SECONDS_PER_MB = 60.0


def worker_timeout(size: int) -> float:
    """The time a worker gets for a file of `size` bytes."""
    return WORKER_BASE_TIMEOUT_S + WORKER_SECONDS_PER_MB * size / (1024.0 * 1024.0)


class WorkerFailed(Exception):
    """A worker process exited without its answer: crashed, killed or timed out."""


class WorkerStopped(Exception):
    """A worker process was stopped on purpose (WorkerProcess.kill / stop)."""


def worker_argv(script: str, args: Iterable[str]) -> List[str]:
    """argv running @p script under this interpreter (Moonraker's own, venv
    included), at idle I/O priority where `ionice` exists. The script nices
    its CPU priority itself (worker_nice)."""
    argv = [sys.executable or "python3", os.path.abspath(script)] + list(args)
    ionice = shutil.which("ionice")
    if ionice is not None:
        argv = [ionice, "-c", "3"] + argv
    return argv


def worker_nice() -> None:
    """Called first thing in a worker process: the lowest CPU priority."""
    try:
        os.nice(19)
    except (AttributeError, OSError):
        pass


class WorkerProcess:
    """Runs one child process at a time and answers its stdout.

    run() raises WorkerFailed for a non-zero exit, a timeout (the child is
    killed) or a start that fails, and WorkerStopped when kill() or stop()
    ended it. kill() is synchronous for event handlers; stop() also reaps.
    """

    def __init__(self) -> None:
        self.proc = None  # type: Optional[Any]
        self._stopped = False

    @property
    def running(self) -> bool:
        return self.proc is not None and self.proc.returncode is None

    async def run(self, argv: List[str], stdin: bytes, timeout: float) -> bytes:
        self._stopped = False
        try:
            proc = await asyncio.create_subprocess_exec(
                *argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
        except OSError as e:
            raise WorkerFailed("could not start: %s" % e)
        self.proc = proc
        try:
            try:
                out, err = await asyncio.wait_for(proc.communicate(stdin), timeout)
            except asyncio.TimeoutError:
                raise WorkerFailed("timed out after %.0f s" % timeout)
        finally:
            if proc.returncode is None:
                await self.stop()
            self.proc = None
        if self._stopped:
            raise WorkerStopped()
        if proc.returncode != 0:
            tail = err.decode("utf-8", "replace").strip().splitlines()[-1:] if err else []
            raise WorkerFailed("exit %s%s" % (proc.returncode, ": " + tail[0] if tail else ""))
        return out

    def kill(self) -> None:
        if self.running:
            self._stopped = True
            try:
                self.proc.kill()
            except ProcessLookupError:
                pass

    async def stop(self) -> None:
        proc = self.proc
        if proc is None or proc.returncode is not None:
            return
        self.kill()
        await proc.wait()


# --------------------------------------------------------------------------
# Layer index sidecar
# --------------------------------------------------------------------------

# A file's layer starts with the modal state at each, so a scan can begin at a
# layer instead of byte 0. JSON lines, one record each:
#
#   {"format": 1, "file_size": .., "file_mtime": .., "file_hash": ..}  header
#   {"layer": n, "offset": o, "z": z, "state": {ModalState.to_dict()}} a layer start
#   {"progress_lines": [[offset, "M73 P12 R30"], ...]}                 optional
#   {"summary": {...}}                                                 optional
#   {"complete": true, "end_offset": o}                                whole file read
#
# `z` is the toolhead Z at the offset. A layer start written by a reader that
# saw the whole layer (file analysis) also carries:
#
#   "tools": [n, ...]  the tools that extrude in the layer (0 before any T<n>)
#   "print_z": z       the toolhead Z of its first extruding move, or null
#   "feature": "..."   the slicer `;TYPE:` section in effect at the offset, or null
#
# `progress_lines` are the lines a progress bar places markers from (pause
# commands, M73, SET_PRINT_STATS_INFO), verbatim up to their comment, so the
# screen classifies them with its own rules. `summary` is what a whole-file
# reader learned that a preview frames and colors with:
#
#   "model_box": {"min": [x, y, z], "max": [x, y, z]} | null  extrusion outside
#                the start-gcode purge and the wipe tower, G-code frame
#   "palette": ["#RRGGBB" | "", ...]   per-tool colors the file declares
#   "initial_tool": n | null           the first T<n>
#   "tools": [n, ...]                  every T<n> the file selects
#
# Append-only, so a reader that stops anywhere leaves a valid prefix; an
# unreadable line (a write cut short) is skipped, and so is a record of a kind
# the reader does not know.
LAYER_INDEX_FORMAT = 1
LAYER_INDEX_DIR = ".helix_index"
LAYER_INDEX_SUFFIX = ".layers.jsonl"

# The file identity a layer index is valid for.
IDENTITY_KEYS = ("file_size", "file_mtime", "file_hash")

# file_hash reads this much from each end of the file.
HASH_SPAN_BYTES = 64 * 1024


def file_hash(path: "os.PathLike") -> str:
    """sha1 of the first and last HASH_SPAN_BYTES (the whole file when shorter)."""
    import hashlib
    h = hashlib.sha1()
    with open(str(path), "rb") as f:
        h.update(f.read(HASH_SPAN_BYTES))
        size = os.fstat(f.fileno()).st_size
        if size > HASH_SPAN_BYTES:
            f.seek(max(HASH_SPAN_BYTES, size - HASH_SPAN_BYTES))
            h.update(f.read(HASH_SPAN_BYTES))
    return h.hexdigest()


def file_identity(path: "os.PathLike") -> dict:
    """The identity a layer index for `path` is written against."""
    st = os.stat(str(path))
    return {"file_size": st.st_size, "file_mtime": st.st_mtime, "file_hash": file_hash(path)}


class LayerCheckpoint(NamedTuple):
    layer: int
    offset: int
    state: ModalState


def layer_index_path(gcodes_root: "os.PathLike", relpath: str) -> str:
    return os.path.join(str(gcodes_root), LAYER_INDEX_DIR, relpath + LAYER_INDEX_SUFFIX)


def _identity(record: dict) -> dict:
    return {k: record.get(k) for k in IDENTITY_KEYS}


class LayerIndexWriter:
    """Appends checkpoints to a file's layer index, starting it over when the
    one on disk indexes another version of the file.

    One writer per index: a complete index of this version (file analysis
    writes them whole, with write_layer_index) is never appended to; the
    writer then writes nothing. An index replaced while this writer has it
    open stays intact, since the replacement is a rename and the writer's
    appends go to the file it opened."""

    def __init__(self, path: str, identity: dict) -> None:
        self.path = path
        self.identity = _identity(identity)
        self._f = None
        self.inert = False

    def _ensure_open(self) -> None:
        """Open the index on first use. A writer that found it complete stays
        inert without reading it again."""
        if self._f is None and not self.inert:
            self._open()

    def _open(self) -> None:
        state = read_layer_index(self.path, self.identity)
        if state is not None and state.complete:
            self.inert = True
            return
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        if state is None:
            self._f = open(self.path, "w")
            self._write(dict(self.identity, format=LAYER_INDEX_FORMAT))
        else:
            self._f = open(self.path, "a")
            # A line cut short by a crash would swallow the next record.
            with open(self.path, "rb") as f:
                f.seek(0, os.SEEK_END)
                if f.tell() > 0:
                    f.seek(-1, os.SEEK_END)
                    if f.read(1) != b"\n":
                        self._f.write("\n")

    def _write(self, record: dict) -> None:
        import json
        if self.inert:
            return
        self._f.write(json.dumps(record, separators=(",", ":")) + "\n")
        self._f.flush()

    def append(self, layer: int, offset: int, state: ModalState,
               extra: Optional[dict] = None) -> None:
        """One layer start; `extra` adds the whole-layer fields (tools,
        print_z, feature) a reader that saw the layer knows."""
        self._ensure_open()
        record = {"layer": layer, "offset": offset, "z": state.last_position[2],
                  "state": state.to_dict()}
        if extra:
            record.update((k, v) for k, v in extra.items() if k not in record)
        self._write(record)

    def progress_lines(self, lines: List[Tuple[int, str]]) -> None:
        self._ensure_open()
        self._write({"progress_lines": [[o, t] for o, t in lines]})

    def summary(self, summary: dict) -> None:
        self._ensure_open()
        self._write({"summary": summary})

    def finish(self, end_offset: int) -> None:
        self._ensure_open()
        self._write({"complete": True, "end_offset": end_offset})

    def close(self) -> None:
        if self._f is not None:
            self._f.close()
            self._f = None


def write_layer_index(path: str, identity: dict,
                      starts: List[Tuple[int, int, ModalState, Optional[dict]]],
                      progress_lines: List[Tuple[int, str]], end_offset: int,
                      summary: Optional[dict] = None) -> None:
    """Write a whole file's index in one go: (layer, offset, state, extra)
    per start. It replaces the file atomically, so a follower appending to
    the one it found keeps writing to an unlinked file, and readers never see
    a half-written index."""
    tmp = path + ".tmp"
    writer = LayerIndexWriter(tmp, identity)
    try:
        if os.path.exists(tmp):
            os.remove(tmp)
        for layer, offset, state, extra in starts:
            writer.append(layer, offset, state, extra)
        writer.progress_lines(progress_lines)
        if summary is not None:
            writer.summary(summary)
        writer.finish(end_offset)
        if writer._f is not None:
            os.fsync(writer._f.fileno())
    finally:
        writer.close()
    atomic_publish(tmp, path)


class LayerIndexSummary(NamedTuple):
    last: Optional[LayerCheckpoint]  # the checkpoint furthest into the file
    at_or_before: Optional[LayerCheckpoint]  # furthest one at or before max_offset
    complete: bool


def read_layer_index(path: str, identity: dict,
                     max_offset: Optional[int] = None) -> Optional[LayerIndexSummary]:
    """Stream a layer index; None when it is missing, unreadable or indexes a
    different version of the file. Memory stays constant in its length."""
    import json
    try:
        f = open(path, "r")
    except OSError:
        return None
    last = None
    best = None
    complete = False
    with f:
        header = None
        for line in f:
            try:
                record = json.loads(line)
            except ValueError:
                continue
            if not isinstance(record, dict):
                continue
            if header is None:
                header = record
                if header.get("format") != LAYER_INDEX_FORMAT \
                        or _identity(header) != _identity(identity):
                    return None
                continue
            if record.get("complete"):
                complete = True
                continue
            layer = record.get("layer")
            offset = record.get("offset")
            if not isinstance(layer, int) or not isinstance(offset, int) \
                    or not isinstance(record.get("state"), dict):
                continue
            if last is None or offset >= last[1]:
                last = (layer, offset, record)
            if (max_offset is None or offset <= max_offset) \
                    and (best is None or offset >= best[1]):
                best = (layer, offset, record)
    if header is None:
        return None

    def checkpoint(entry):
        if entry is None:
            return None
        return LayerCheckpoint(entry[0], entry[1], ModalState.from_dict(entry[2]["state"]))

    return LayerIndexSummary(checkpoint(last), checkpoint(best), complete)


def _header_identity(path: str) -> Optional[dict]:
    import json
    try:
        with open(path, "r") as f:
            header = json.loads(f.readline())
    except (OSError, ValueError):
        return None
    return header if isinstance(header, dict) else None


def layer_index_stale(index_path: str, source_path: Optional[str]) -> bool:
    """The prune rule for one index: stale when its G-code is gone, unknown
    (an index under .external, of a file outside gcodes), or a different
    version than the index's header names (size or mtime). The content hash
    is not read: a prune walks every index, and a file rewritten to the same
    size within one mtime tick is not worth reading every file for."""
    if source_path is None:
        return True
    try:
        st = os.stat(source_path)
    except OSError:
        return True
    header = _header_identity(index_path)
    if header is None:
        return True
    return header.get("file_size") != st.st_size or header.get("file_mtime") != st.st_mtime


def temp_index_orphaned(tmp_path: str, source_path: Optional[str], now: float) -> bool:
    """Whether a whole-file write's temp index can no longer have a writer:
    its source is gone, or it is older than the longest a worker may run for
    that source (worker_timeout). An index of a file outside gcodes
    (`source_path` None) gets the timeout of an empty file."""
    try:
        age = now - os.stat(tmp_path).st_mtime
    except OSError:
        return False
    size = 0
    if source_path is not None:
        try:
            size = os.stat(source_path).st_size
        except OSError:
            return True
    return age > worker_timeout(size)


def prune_layer_indexes(gcodes_root: "os.PathLike",
                        protected: Iterable[str] = ()) -> List[str]:
    """Remove every stale layer index (layer_index_stale) except the
    @p protected paths: the index power-loss recovery is following during a
    print and the one its waiting snapshot resumes from. A whole-file write's
    `.tmp` is an orphan once no writer can still hold it (temp_index_orphaned),
    and is kept while it or the index it becomes is protected. Returns the
    removed paths."""
    root = os.path.join(str(gcodes_root), LAYER_INDEX_DIR)
    external = os.path.join(root, ".external") + os.sep
    keep = {os.path.abspath(p) for p in protected if p}
    removed = []  # type: List[str]
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            path = os.path.join(dirpath, name)
            if name.endswith(LAYER_INDEX_SUFFIX + ".tmp"):
                if os.path.abspath(path) in keep or os.path.abspath(path[:-4]) in keep:
                    continue
                source = None
                if not path.startswith(external):
                    source = os.path.join(str(gcodes_root), os.path.relpath(
                        path, root)[:-len(LAYER_INDEX_SUFFIX + ".tmp")])
                if not temp_index_orphaned(path, source, time.time()):
                    continue
                try:
                    os.remove(path)
                    removed.append(path)
                except OSError:
                    pass
                continue
            if not name.endswith(LAYER_INDEX_SUFFIX):
                continue
            if os.path.abspath(path) in keep:
                continue
            source = None
            if not path.startswith(external):
                source = os.path.join(
                    str(gcodes_root), os.path.relpath(path, root)[:-len(LAYER_INDEX_SUFFIX)])
            if not layer_index_stale(path, source):
                continue
            try:
                os.remove(path)
                removed.append(path)
            except OSError:
                pass
    return removed
