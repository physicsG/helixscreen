# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery on newer QIDI firmware (the /home/qidi layout: Q2, Max 4).

QIDI's own Moonraker resumes a print from rows in its SQLite `config` table,
section `print_state`, which only QIDI's stock screen writes. This module
writes those rows from the generic snapshot (helix_plr.py's record, schema 1)
and resumes through QIDI's endpoint, so the screen that replaces QIDI's still
leaves a resumable print behind (prestonbrown/helixscreen#1716).

QIDI's resume path, the contract every format here is written against
(QIDI sources copied from a Q2 on 01.01.02.01):

  printer.print.power_off_print
    -> klippy_apis.py#KlippyAPI.power_off_print
    -> database.py#MoonrakerDatabase.generate_power_off_print_gcode
       (reads every print_state row, json.loads the objects and the three
       position rows, base64-packs the rest as strings)
    -> SDCARD_POWER_OFF_CONTINUES_PINT_FILE STATE=<b64>
    -> virtual_sdcard.py#VirtualSD.cmd_SDCARD_POWER_OFF_CONTINUES_PINT_FILE
       (file_position = max(int(float(row)) - 4096, 0))
    -> virtual_sdcard.py#VirtualSD._set_printer_state_internal
       (float() of targets, fans and factors; "1"/"true"/"yes" booleans;
       SET_KINEMATIC_POSITION and the final G1 Z both from gcode_position.z)

helix_plr.py stays vendor-free: it asks qidi_print_state_present() which
backend applies, subscribes EXTRA_STATUS_FIELDS, and drives a
QidiPrintStateAdapter from its snapshot loop and its prepare/commit/discard
endpoints. Pure functions first; the adapter is the only part that touches
Moonraker. Python 3.8 stdlib only.
"""

from __future__ import annotations

import glob
import json
import logging
import os
import time
from typing import Any, Awaitable, Callable, Dict, List, Mapping, Optional, Sequence

PRINT_STATE_SECTION = "print_state"

# virtual_sdcard.py#VirtualSD.cmd_SDCARD_POWER_OFF_CONTINUES_PINT_FILE starts
# the print this many bytes before the stored file_position.
QIDI_REWIND_BYTES = 4096

QIDI_RESUME_COMMAND = "SDCARD_POWER_OFF_CONTINUES_PINT_FILE"

# Where QIDI's Moonraker copies every job (klippy_apis.py#KlippyAPI.start_print)
# and where its resume opens `glob(".cache/*")[0]` (virtual_sdcard.py, the
# same command). QIDI's Klipper hardcodes this path.
QIDI_CACHE_DIR = "/home/qidi/printer_data/.cache"

# Status fields QIDI adds to gcode_move and virtual_sdcard. helix_plr.py
# subscribes them beside its own and passes them back as `qidi_status`.
EXTRA_STATUS_FIELDS = {
    "gcode_move": ["speed_2", "speed_factor_2", "base_position"],
    "virtual_sdcard": ["plate_index"],
}

# Klipper objects behind QIDI's temperature and fan rows (the Q2's printer.cfg
# names; the row names are QIDI's PRINT_STATE_TEMPLATE in database.py).
_TARGET_ROWS = (
    ("extruder_target", "extruder"),
    ("heater_bed_target", "heater_bed"),
    ("chamber_target", "heater_generic chamber"),
)
_FAN_ROWS = (
    ("cooling_fan_speed", "fan_generic cooling_fan"),
    ("auxiliary_cooling_fan_speed", "fan_generic auxiliary_cooling_fan"),
    ("chamber_circulation_fan_speed", "fan_generic chamber_circulation_fan"),
)

# Every row the resume reads. A missing row falls back to the parser's
# default (bed 0, speed_factor 0.016667, positions [0,0,0,0]), so all of them
# are written at job start.
PRINT_STATE_KEYS = (
    "file_position",
    "plate_index",
    "gcode_position",
    "homing_position",
    "base_position",
    "speed",
    "speed_factor",
    "extrude_factor",
    "absolute_coord",
    "absolute_extrude",
    "extruder_target",
    "heater_bed_target",
    "chamber_target",
    "cooling_fan_speed",
    "auxiliary_cooling_fan_speed",
    "chamber_circulation_fan_speed",
    "all_objects",
    "exclude_objects",
    "current_layer",
)

# Changed rows are written at most this often, matching QIDI's screen.
WRITE_INTERVAL_S = 1.0

# QIDI's Moonraker deletes the section when a job starts while history still
# holds a current job (database.py#MoonrakerDatabase._on_job_state_changed,
# STARTED). The job's first full write waits this long after the start so it
# lands after that delete.
FIRST_WRITE_DELAY_S = 2.0


# ---------------------------------------------------------------------------
# Capability
# ---------------------------------------------------------------------------


def qidi_print_state_present(
    gcode_help: Optional[Mapping[str, Any]], gcode_move_status: Optional[Mapping[str, Any]]
) -> bool:
    """Whether Klipper is QIDI's fork with the print_state resume.

    `gcode_help` is /printer/gcode/help's reply (command -> description); it
    lists SDCARD_POWER_OFF_CONTINUES_PINT_FILE because QIDI registers it with
    desc= (virtual_sdcard.py#VirtualSD.__init__). `gcode_move_status` is
    gcode_move's status, where QIDI adds `speed_factor_2`. Either answers
    yes; neither is a path, since /home/mks links to /home/qidi on the Q2.
    """
    if gcode_help and QIDI_RESUME_COMMAND in gcode_help:
        return True
    if gcode_move_status and "speed_factor_2" in gcode_move_status:
        return True
    return False


def moonraker_resumes_print_state(klippy_apis: Any, database: Any) -> bool:
    """Whether this Moonraker carries QIDI's resume endpoint and the config
    table the rows live in: the code the adapter calls, probed by name."""
    return (
        hasattr(klippy_apis, "power_off_print")
        and hasattr(database, "update_value_from_config")
        and hasattr(database, "delete_value_from_config")
    )


# ---------------------------------------------------------------------------
# Row formats
# ---------------------------------------------------------------------------


def _fmt_float(value: Any) -> str:
    """'%f', the screen's float format; float() reads it back."""
    return "%f" % float(value)


# How _set_printer_state_internal turns a row into the integer it sends:
# int() truncates, so a plain '%f' of 0.9/60 sends M220 S89.
def _qidi_m220(row: str) -> int:
    return int(float(row) * 60 * 100)  # 'sf': speed_factor * 60; M220 S{int(sf * 100)}


def _qidi_m221(row: str) -> int:
    return int(float(row) * 100)  # M221 S{int(ef * 100)}


def _qidi_m106(row: str) -> int:
    return int(float(row) * 255)  # M106 S{int(f * 255)}


def _fmt_truncated(value: Any, scale: float, parse: Callable[[str], int]) -> str:
    """'%f' of `value`, raised by the fewest millionths that make QIDI's
    truncating parse send round(value x scale)."""
    v = float(value)
    want = int(round(v * scale))
    for step in range(8):
        row = _fmt_float(v + step * 1e-6)
        if parse(row) == want:
            return row
    return _fmt_float(v)


def _fmt_int(value: Any) -> str:
    return str(int(value))


def _fmt_target(value: Any) -> str:
    """Integer string for a whole-degree target, '%f' otherwise; the parser
    float()s either."""
    v = float(value)
    if v == int(v):
        return str(int(v))
    return _fmt_float(v)


def _fmt_bool(value: Any) -> str:
    return "1" if value else "0"


def _fmt_vec4(values: Sequence[Any]) -> str:
    """A 4-vector as JSON text. The row must be a string: Moonraker binds it
    straight into SQLite, and the resume json.loads it."""
    vec = [float(v) for v in list(values)[:4]]
    vec += [0.0] * (4 - len(vec))
    return json.dumps(vec, separators=(",", ":"))


def _vec(value: Any) -> Optional[List[float]]:
    if isinstance(value, (list, tuple)) and len(value) >= 3:
        try:
            return [float(v) for v in value]
        except (TypeError, ValueError):
            return None
    return None


def frame_z(snapshot: Mapping[str, Any]) -> float:
    """The Z to store in gcode_position.

    QIDI declares `SET_KINEMATIC_POSITION Z=<z>`, applies the gcode offset and
    the mesh, and then moves `G1 Z<z>`. The declared value becomes the frame
    every later line prints in, so the remaining print is off by
    (physical Z at the cut) - <z>. The physical Z is the kinematic one, which
    includes the gcode offset and the mesh: `live_pos.z`, the executing move,
    then `toolhead_pos.z`. The gcode-frame Z QIDI's own screen stores misses
    by offset + mesh for the rest of the print.

    What no value can fix: the declaration and the landing move share this
    field, so the rest of the resumed layer, until the file's next Z move,
    prints off by D = offset + mesh(x, y at the cut): above the plastic when D
    is positive, pressed into it when D is negative (a negative Z offset, a
    low mesh point). helix_plr.py refuses a QIDI resume whose D is below
    -QIDI_LANDING_TOL_MM.
    """
    for key in ("live_pos", "toolhead_pos"):
        pos = _vec(snapshot.get(key))
        if pos is not None:
            return pos[2]
    gcode_pos = _vec(snapshot.get("gcode_pos")) or [0.0, 0.0, 0.0, 0.0]
    offset = _vec(snapshot.get("gcode_offset")) or [0.0, 0.0, 0.0, 0.0]
    return gcode_pos[2] + offset[2]


def _or(value: Any, default: Any) -> Any:
    """A snapshot field, or its default when it has not arrived (None)."""
    return default if value is None else value


def _internal_speed_factor(snapshot: Mapping[str, Any], qidi_status: Mapping[str, Any]) -> float:
    """gcode_move's internal speed factor, 1/60 at 100 %: QIDI's
    `speed_factor_2` when subscribed, else the public factor / 60
    (gcode_move.py: the public `speed_factor` is the internal one x 60)."""
    gm = qidi_status.get("gcode_move") or {}
    raw = gm.get("speed_factor_2")
    if raw is not None:
        return float(raw)
    return float(_or(snapshot.get("speed_factor"), 1.0)) / 60.0


def _internal_speed(
    snapshot: Mapping[str, Any], qidi_status: Mapping[str, Any], internal_sf: float
) -> float:
    """gcode_move's internal `speed` (mm/s, factor applied), which the resume
    assigns back to gcode_move.speed: QIDI's `speed_2`, else the public speed
    (mm/min) x the internal factor."""
    gm = qidi_status.get("gcode_move") or {}
    raw = gm.get("speed_2")
    if raw is not None:
        return float(raw)
    return float(_or(snapshot.get("speed"), 0.0)) * internal_sf


def print_state_rows(
    snapshot: Mapping[str, Any], qidi_status: Optional[Mapping[str, Any]] = None
) -> Dict[str, str]:
    """QIDI's print_state rows for a generic snapshot (schema 1), every value
    a string in the format QIDI's resume parses.

    `qidi_status` is {object: {field: value}} for EXTRA_STATUS_FIELDS; any of
    it may be missing.
    """
    qs = qidi_status or {}
    gm = qs.get("gcode_move") or {}
    vsd = qs.get("virtual_sdcard") or {}

    gcode_pos = _vec(snapshot.get("gcode_pos")) or [0.0, 0.0, 0.0, 0.0]
    offset = _vec(snapshot.get("gcode_offset")) or [0.0, 0.0, 0.0, 0.0]
    position = list(gcode_pos[:4]) + [0.0] * (4 - len(gcode_pos[:4]))
    position[2] = frame_z(snapshot)
    base = _vec(gm.get("base_position")) or offset

    internal_sf = _internal_speed_factor(snapshot, qs)
    heaters = snapshot.get("heaters") or {}
    fans = snapshot.get("fans") or {}

    rows = {
        "file_position": _fmt_int(_or(snapshot.get("file_position"), 0)),
        "plate_index": str(vsd.get("plate_index", "1")),
        "gcode_position": _fmt_vec4(position),
        "homing_position": _fmt_vec4(offset),
        "base_position": _fmt_vec4(base),
        "speed": _fmt_float(_internal_speed(snapshot, qs, internal_sf)),
        "speed_factor": _fmt_truncated(internal_sf, 6000.0, _qidi_m220),
        "extrude_factor": _fmt_truncated(_or(snapshot.get("extrude_factor"), 1.0), 100.0,
                                         _qidi_m221),
        "absolute_coord": _fmt_bool(_or(snapshot.get("abs_coord"), True)),
        "absolute_extrude": _fmt_bool(_or(snapshot.get("abs_extrude"), True)),
        "all_objects": json.dumps(list(snapshot.get("objects") or [])),
        "exclude_objects": json.dumps(list(snapshot.get("excluded") or [])),
        "current_layer": _fmt_int(snapshot.get("layer") or 0),
    }
    for row, heater in _TARGET_ROWS:
        rows[row] = _fmt_target(_or((heaters.get(heater) or {}).get("target"), 0.0))
    for row, fan in _FAN_ROWS:
        rows[row] = _fmt_truncated(_or(fans.get(fan), 0.0), 255.0, _qidi_m106)
    return rows


def resume_file_position_row(resume_offset: int, file_size: int) -> int:
    """The file_position row that makes QIDI's `max(row - 4096, 0)` land on
    `resume_offset`. The offset is clamped into the file first; the row may
    then exceed the file size, which QIDI does not check, so a resume point
    in the last 4 KiB still lands exactly."""
    offset = max(0, min(int(resume_offset), int(file_size)))
    return offset + QIDI_REWIND_BYTES


def resume_rows(
    snapshot: Mapping[str, Any],
    resume_offset: int,
    file_size: int,
    resume_state: Any = None,
    qidi_status: Optional[Mapping[str, Any]] = None,
) -> Dict[str, str]:
    """The rows commit writes before calling QIDI's endpoint: the snapshot's
    rows with the resume point's file position and, from `resume_state` (a
    helix_gcode_reader.ModalState just before the resume line), the X/Y/E
    QIDI travels to and restores, the coordinate modes and the feedrate.

    Z stays frame_z(snapshot): it is the frame declaration, not the resume
    line's height.
    """
    rows = print_state_rows(snapshot, qidi_status)
    rows["file_position"] = _fmt_int(resume_file_position_row(resume_offset, file_size))
    if resume_state is not None:
        pos = list(resume_state.gcode_position())
        pos[2] = frame_z(snapshot)
        rows["gcode_position"] = _fmt_vec4(pos)
        rows["absolute_coord"] = _fmt_bool(resume_state.absolute_coord)
        rows["absolute_extrude"] = _fmt_bool(resume_state.absolute_extrude)
        internal_sf = _internal_speed_factor(snapshot, qidi_status or {})
        # ModalState.speed is F in mm/s before the factor; internal = F/60 x
        # public factor = (F/60) x internal factor x 60.
        rows["speed"] = _fmt_float(resume_state.speed * internal_sf * 60.0)
    return rows


def changed_rows(written: Mapping[str, str], rows: Mapping[str, str]) -> Dict[str, str]:
    """The rows whose value differs from what was last written."""
    return {k: v for k, v in rows.items() if written.get(k) != v}


def qidi_resume_filename(cache_dir: str = QIDI_CACHE_DIR) -> Optional[str]:
    """The name print_stats, and so history, gives the resumed job: QIDI's
    resume opens `glob(<cache>/*)[0]` and passes that path on as the
    filename (virtual_sdcard.py#VirtualSD._load_file ->
    print_stats.set_current_file)."""
    found = glob.glob(os.path.join(cache_dir, "*"))
    return found[0] if found else None


# ---------------------------------------------------------------------------
# Adapter
# ---------------------------------------------------------------------------


class QidiPrintStateError(Exception):
    pass


class QidiPrintStateAdapter:
    """Writes print_state from the snapshot loop and resumes through QIDI.

    helix_plr.py calls, from its loop and endpoints:
      on_job_start()            a job started (job_state started)
      on_snapshot(snap, status) after each snapshot it persists; force=True
                                for an event save
      on_job_end()              a job completed or was cancelled
      prepare(heat_wait)        prepare: the heat wait, nothing else
      commit(..., heat_wait)    commit: heat wait, rows for the resume
                                point, marker, power_off_print
      abort()                   abort: rows kept
      discard()                 discard: delete the section
      has_resume_rows()         the offer's extra rule
    """

    def __init__(
        self,
        database: Any,
        klippy_apis: Any,
        mark: Optional[Callable[[str, str], None]] = None,
        unmark: Optional[Callable[[str], Any]] = None,
        clock: Callable[[], float] = time.monotonic,
        cache_dir: str = QIDI_CACHE_DIR,
    ) -> None:
        self._db = database
        self._kapis = klippy_apis
        self._mark = mark
        self._unmark = unmark
        self._clock = clock
        self._cache_dir = cache_dir
        self._written: Dict[str, str] = {}
        self._last_write: Optional[float] = None
        self._job_started_at: Optional[float] = None

    # --- save side -------------------------------------------------------

    def on_job_start(self) -> None:
        self._written = {}
        self._last_write = None
        self._job_started_at = self._clock()

    def on_job_end(self) -> None:
        self._written = {}
        self._last_write = None
        self._job_started_at = None

    async def on_snapshot(
        self,
        snapshot: Mapping[str, Any],
        qidi_status: Optional[Mapping[str, Any]] = None,
        force: bool = False,
    ) -> Dict[str, str]:
        """Write the rows that changed since the last write; every row on the
        job's first write. Returns what was written."""
        now = self._clock()
        if not self._written:
            if self._job_started_at is not None and now - self._job_started_at < FIRST_WRITE_DELAY_S:
                return {}
        elif not force and self._last_write is not None and now - self._last_write < WRITE_INTERVAL_S:
            return {}
        try:
            rows = print_state_rows(snapshot, qidi_status)
        except (TypeError, ValueError):
            logging.exception("HelixPLR[qidi]: snapshot not writable as print_state rows")
            return {}
        todo = changed_rows(self._written, rows)
        if not todo:
            return {}
        if await self._write(todo):
            self._written.update(todo)
            self._last_write = now
            return todo
        return {}

    async def _write(self, rows: Mapping[str, str]) -> bool:
        """One transaction through QIDI's batch upsert when it exists, else
        one upsert per key."""
        try:
            if hasattr(self._db, "update_value_from_config_batch"):
                records = [(PRINT_STATE_SECTION, k, v) for k, v in rows.items()]
                await self._db.update_value_from_config_batch(records)
            else:
                for k, v in rows.items():
                    await self._db.update_value_from_config(PRINT_STATE_SECTION, k, v)
        except Exception:
            logging.exception("HelixPLR[qidi]: print_state write failed")
            return False
        return True

    # --- resume side -----------------------------------------------------

    async def has_resume_rows(self) -> bool:
        try:
            cur = await self._db.get_value_from_config(PRINT_STATE_SECTION, "file_position")
            row = await cur.fetchone()
        except Exception:
            logging.exception("HelixPLR[qidi]: print_state read failed")
            return False
        return bool(row)

    async def prepare(self, heat_wait: Callable[[], Awaitable[Any]]) -> None:
        """prepare for this backend: the heat wait only (helix_plr.py's bed,
        chamber and nozzle where it stands, set together)."""
        await heat_wait()

    async def commit(
        self,
        snapshot: Mapping[str, Any],
        resume_offset: int,
        file_size: int,
        heat_wait: Callable[[], Awaitable[Any]],
        resume_state: Any = None,
        qidi_status: Optional[Mapping[str, Any]] = None,
    ) -> Any:
        """commit for this backend: the heat wait, the rows for the resume
        point, the marker, then QIDI's resume.

        The wait runs again here because QIDI's sequence sends M140 and M104
        without waiting, then lifts Z and homes X and Y before its M109: a
        nozzle that cooled while the screen asked its confirmations would be
        dragged out of the plastic cold. At temperature it returns at once.
        The marker is set immediately before the endpoint call and withdrawn
        if the call fails.
        """
        await heat_wait()
        rows = resume_rows(snapshot, resume_offset, file_size, resume_state, qidi_status)
        if not await self._write(rows):
            raise QidiPrintStateError("print_state write failed")
        self._written = dict(rows)
        name = qidi_resume_filename(self._cache_dir)
        if name is None:
            raise QidiPrintStateError("no job file in QIDI's .cache")
        if self._mark is not None:
            self._mark(name, "plr_resume")
        try:
            return await self._kapis.power_off_print(None)
        except Exception:
            if self._unmark is not None:
                self._unmark(name)
            raise

    def abort(self) -> None:
        """abort keeps the snapshot: the rows stay, so the offer stands.
        Rows a commit already rewrote point at the same resume point."""

    async def discard(self) -> None:
        await self._db.delete_value_from_config(PRINT_STATE_SECTION, None)
        self._written = {}
