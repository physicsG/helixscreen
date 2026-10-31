# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery: the generic resume sequence as G-code
(docs/devel/POWER_LOSS_RECOVERY.md, "The resume sequence").

The plugin renders and sends this sequence itself. It runs in two
phases around the app's confirmations:

- prepare: bed, chamber and nozzle to temperature with the nozzle where it
  stopped, declare the physical Z the snapshot measured, lift, drop the nozzle
  to standby, home X and Y, and for tier 1 stop above the resume point so the
  user can jog down to the part.
- commit: settle the Z reference, restore the print's state, heat and purge
  the nozzle away from the part, return to the resume point, then the single
  start command (M23 / M26 / M24).

Pure: every function takes a ResumePlan and returns lines. Python 3.8, stdlib only.
"""

from __future__ import annotations

from typing import Any, Dict, List, NamedTuple, Optional, Sequence, Tuple

from helix_gcode_reader import ModalState

# Kept between the lifted nozzle and the top of the Z travel.
SAG_ALLOWANCE_MM = 1.0

# The least cold lift that clears the part before XY homing drags across it.
# Less room than this under the top of travel and PLR is not offered.
MIN_LIFT_MM = 2.0

# mm/min
Z_FEED = 300
TRAVEL_FEED = 6000
PURGE_FEED = 1200
RETRACT_FEED = 2100

# Generic purge: a line along a bed edge clear of every object.
PURGE_MARGIN_MM = 5.0
PURGE_LINE_MM = 60.0
PURGE_Z_MM = 0.3
PURGE_E_MM = 12.0
PURGE_RETRACT_MM = 0.8
# Primes the nozzle at the resume point after the purge's retract.
PRIME_E_MM = PURGE_RETRACT_MM
# Heat-up prime when no purge location is known: at the XY home position.
HOME_PRIME_E_MM = 5.0
# Pulled back when the nozzle drops to standby, still at print temperature,
# so it oozes less while it waits; the purge or prime gives it back.
STANDBY_RETRACT_MM = 1.0
# The standby temperature never goes below this.
NOZZLE_STANDBY_MIN_C = 140.0

# exclude_object lets the first initial_extrusion_moves (5) extruding moves
# through its freshly registered transform before it skips anything
# (exclude_object.py#_register_transform, _normal_move); the start sends one
# more than that, alternating so the extruder ends where it began.
EXCLUDE_COUNTER_MOVES = 6
EXCLUDE_COUNTER_E_MM = 0.001
EXCLUDE_COUNTER_STATE = "HELIX_PLR_EXCLUDE"

CONFIRMATIONS = ("part_attached", "nozzle_clear", "z_confirmed", "z_jogged",
                 "accept_no_mesh", "accept_cold_bed", "accept_next_layer")

# Klipper ends an extended command's arguments at any of these.
_UNSAFE_PARAM_CHARS = set("#*;\n\r\"")


class UnsafeValue(ValueError):
    """A value from a file or status that cannot be placed in a command."""


def gcode_param(value: Any) -> str:
    """A value as an extended-command parameter: quoted when it has spaces,
    refused when it carries a character Klipper's parser would cut at."""
    text = str(value)
    if not text or any(c in _UNSAFE_PARAM_CHARS for c in text):
        raise UnsafeValue("unsafe parameter %r" % text)
    return '"%s"' % text if any(c.isspace() for c in text) else text


def _f(value: float) -> str:
    text = "%.4f" % value
    text = text.rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


class ResumePlan(NamedTuple):
    plan_id: str
    filename: str  # gcodes-relative path of the file being resumed
    start_name: str  # what M23 selects: the file itself, or a root-level link to it
    history_name: Optional[str]  # name history should carry when start_name is a link
    resume_offset: int
    state: ModalState  # modal state just before the resume line
    layer: Optional[int]
    total_layers: Optional[int]
    tier: int
    z_physical: float  # live_pos.z at the snapshot: kinematic frame
    lift_mm: float
    snapshot: Dict[str, Any]
    object_defines: List[str]
    excluded: List[str]
    mesh: Optional[Dict[str, Any]]  # the job record's mesh
    mesh_loadable: Optional[str]  # profile BED_MESH_PROFILE LOAD can use, else None
    purge_xy: Optional[Tuple[float, float, float, float]]
    nozzle_target: float
    bed_target: float
    chamber: Optional[Tuple[str, float]]  # (heater name, target)
    macro_vars: Dict[str, Dict[str, Any]]
    method: str
    retrace_estimate_mm: float
    skip_estimate_mm: float
    warnings: List[str]
    requires: List[str]
    # G-code Z of the last extruding move before the resume point: the top of
    # the plastic a tier-1 nozzle is jogged onto.
    part_top_z: Optional[float] = None
    # bed_mesh fade_start, fade_end, fade_target (None = the mesh's average).
    mesh_fade: Optional[Dict[str, Any]] = None
    # A part-free XY (the XY homing position) to heat the nozzle over when
    # there is no purge line.
    park_xy: Optional[Tuple[float, float]] = None
    backend: str = "generic"  # "generic" | "qidi_print_state"


# --------------------------------------------------------------------------
# Geometry helpers
# --------------------------------------------------------------------------


def lift_height(z_lift_mm: float, z_physical: float, position_max_z: Optional[float]) -> float:
    """The cold lift: the profile's height, kept under the top of travel."""
    if position_max_z is None:
        return max(0.0, z_lift_mm)
    return max(0.0, min(z_lift_mm, position_max_z - z_physical - SAG_ALLOWANCE_MM))


def _object_boxes(objects: Sequence[Any]) -> List[Tuple[float, float, float, float]]:
    boxes = []
    for obj in objects or ():
        if not isinstance(obj, dict):
            continue
        poly = obj.get("polygon")
        if isinstance(poly, list) and poly:
            xs = [p[0] for p in poly if isinstance(p, (list, tuple)) and len(p) >= 2]
            ys = [p[1] for p in poly if isinstance(p, (list, tuple)) and len(p) >= 2]
            if xs and ys:
                boxes.append((min(xs), min(ys), max(xs), max(ys)))
                continue
        center = obj.get("center")
        if isinstance(center, (list, tuple)) and len(center) >= 2:
            boxes.append((center[0], center[1], center[0], center[1]))
    return boxes


def generic_purge_xy(objects: List[dict], bed_min: Tuple[float, float],
                     bed_max: Tuple[float, float],
                     margin_mm: float = PURGE_MARGIN_MM) -> Optional[Tuple[float, float, float, float]]:
    """A purge line (x0, y0, x1, y1) along a bed edge, `margin_mm` from the
    edge and from every object's bounding box: front, back, left, right in
    that order. None when no edge is clear or no object is known (the part's
    footprint is then unknown)."""
    boxes = _object_boxes(objects)
    if not boxes:
        return None
    x0, y0 = bed_min[0] + margin_mm, bed_min[1] + margin_mm
    x1, y1 = bed_max[0] - margin_mm, bed_max[1] - margin_mm
    if x1 <= x0 or y1 <= y0:
        return None
    lx = min(PURGE_LINE_MM, x1 - x0)
    ly = min(PURGE_LINE_MM, y1 - y0)
    candidates = [
        (x0, y0, x0 + lx, y0),  # front
        (x0, y1, x0 + lx, y1),  # back
        (x0, y0, x0, y0 + ly),  # left
        (x1, y0, x1, y0 + ly),  # right
    ]
    for cx0, cy0, cx1, cy1 in candidates:
        lo_x, hi_x = min(cx0, cx1) - margin_mm, max(cx0, cx1) + margin_mm
        lo_y, hi_y = min(cy0, cy1) - margin_mm, max(cy0, cy1) + margin_mm
        if all(b[2] < lo_x or b[0] > hi_x or b[3] < lo_y or b[1] > hi_y for b in boxes):
            return (cx0, cy0, cx1, cy1)
    return None


def mesh_adjustment(mesh: Optional[Dict[str, Any]], x: float, y: float, z: float,
                    fade: Optional[Dict[str, Any]]) -> Optional[float]:
    """What bed_mesh adds to a move to (x, y, z), fade included
    (bed_mesh.py#BedMesh.move: past fade_end only fade_target remains)."""
    raw = mesh_z_at(mesh, x, y)
    if raw is None:
        return None
    fade = fade or {}
    start, end = fade.get("fade_start", 1.0), fade.get("fade_end", 0.0)
    if not isinstance(start, (int, float)) or not isinstance(end, (int, float)) \
            or end - start <= 0.0:
        return raw
    target = fade.get("fade_target")
    if not isinstance(target, (int, float)):
        points = [v for row in mesh["points"] for v in row]
        target = sum(points) / len(points)
    adj = raw - target
    if max(z, z - adj) >= start:
        # 0 past fade_end, so only fade_target remains there.
        factor = max(0.0, min(1.0, (end + target - z) / ((end - start) - adj)))
    else:
        factor = 1.0
    return factor * adj + target


def mesh_z_at(mesh: Optional[Dict[str, Any]], x: float, y: float) -> Optional[float]:
    """Bilinear height of the snapshot's probed mesh at (x, y), clamped to its
    bounds; None without a usable mesh."""
    if not isinstance(mesh, dict):
        return None
    points = mesh.get("points")
    lo, hi = mesh.get("min"), mesh.get("max")
    if not points or not lo or not hi or not isinstance(points[0], list):
        return None
    rows, cols = len(points), len(points[0])
    if rows < 1 or cols < 1:
        return None

    def axis(v, a, b, n):
        if n == 1 or b <= a:
            return 0, 0, 0.0
        t = (min(max(v, a), b) - a) / (b - a) * (n - 1)
        i = min(int(t), n - 2)
        return i, i + 1, t - i

    i0, i1, fx = axis(x, lo[0], hi[0], cols)
    j0, j1, fy = axis(y, lo[1], hi[1], rows)
    top = points[j0][i0] * (1 - fx) + points[j0][i1] * fx
    bottom = points[j1][i0] * (1 - fx) + points[j1][i1] * fx
    return top * (1 - fy) + bottom * fy


# --------------------------------------------------------------------------
# Scripts
# --------------------------------------------------------------------------


def _resume_xyz(state: ModalState) -> Tuple[float, float, float]:
    gx, gy, gz, _ = state.gcode_position()
    return gx, gy, gz


class HeatStep(NamedTuple):
    """One heater of the heat wait: the set command, then the plugin waits for
    `sensor` (a Klipper object) to reach `minimum`. The wait is not a G-code
    (M190, TEMPERATURE_WAIT): those hold Klipper's G-code lock, so an abort's
    heaters-off could not run until the target was reached."""
    step: str
    lines: List[str]
    sensor: str
    minimum: float
    wait: bool


# A heater counts as at temperature this close under its target.
HEAT_TOLERANCE_C = 1.0


def render_heat_wait(plan: ResumePlan, skip_chamber_wait: bool = False,
                     nozzle_in_place: bool = False) -> List[HeatStep]:
    """Bed, chamber and, with `nozzle_in_place`, the nozzle where it stands.
    helix_plr.py sets them all at once, then waits for each. After a
    cut the nozzle is frozen into the plastic: lifting or homing it cold
    tears the part off the bed, and a reheated bed is the part's best chance
    to grip again, so no Z move happens before both are hot. Every prepare
    and every commit heats all three: prepare leaves the nozzle at standby,
    and commit brings it back before anything moves."""
    steps = []  # type: List[HeatStep]
    if plan.bed_target > 0:
        steps.append(HeatStep("bed_wait", ["M140 S%s" % _f(plan.bed_target)], "heater_bed",
                              plan.bed_target - HEAT_TOLERANCE_C, True))
    if plan.chamber is not None:
        name, target = plan.chamber
        steps.append(HeatStep(
            "chamber_wait",
            ["SET_HEATER_TEMPERATURE HEATER=%s TARGET=%s" % (gcode_param(name), _f(target))],
            "heater_generic " + name, target - HEAT_TOLERANCE_C, not skip_chamber_wait))
    if nozzle_in_place:
        steps.append(HeatStep("nozzle_in_place", ["M104 S%s" % _f(plan.nozzle_target)],
                              "extruder", plan.nozzle_target - HEAT_TOLERANCE_C, True))
    return steps


def nozzle_standby_c(target: float, drop_c: float) -> float:
    """The standby temperature: `drop_c` under the print temperature, no
    lower than NOZZLE_STANDBY_MIN_C and never above the print temperature."""
    return min(target, max(NOZZLE_STANDBY_MIN_C, target - drop_c))


def nozzle_standby_lines(plan: ResumePlan, profile: Any) -> List[str]:
    """R30: the nozzle has let go of the part; retract a little while it is
    still at print temperature, then drop it to standby until commit."""
    return ["M83", "G1 E-%s F%d" % (_f(STANDBY_RETRACT_MM), RETRACT_FEED),
            "M104 S%s" % _f(nozzle_standby_c(plan.nozzle_target, profile.nozzle_standby_drop_c))]


def render_prepare_script(plan: ResumePlan, profile: Any, declare_z: Optional[float],
                          lift_mm: float) -> List[Tuple[str, List[str]]]:
    """The generic prepare's motion after the heat wait (bed and nozzle hot):
    declare Z when Klipper has none (`declare_z`), lift by `lift_mm`, drop
    the nozzle to standby, home XY, and at tier 1 stop above the resume
    point. helix_plr.py decides both inputs
    from Klipper's homed axes and the lift record.

    SET_HOMED=Z leaves X and Y unhomed, so an XY homing macro that homes only
    unhomed axes still homes them (Klipper builds whose SET_KINEMATIC_POSITION
    has SET_HOMED; helix_plr.py checks the result)."""
    steps = []  # type: List[Tuple[str, List[str]]]
    lift = []  # type: List[str]
    if declare_z is not None:
        lift.append("SET_KINEMATIC_POSITION Z=%s SET_HOMED=Z" % _f(declare_z))
    if lift_mm > 0:
        lift += ["G91", "G1 Z%s F%d" % (_f(lift_mm), Z_FEED), "G90"]
    if lift:
        steps.append(("lift", lift))
    steps.append(("nozzle_standby", nozzle_standby_lines(plan, profile)))
    steps.append(("home_xy", [line for line in profile.xy_home_gcode.splitlines() if line.strip()]))
    if plan.tier == 1:
        x, y, _ = _resume_xyz(plan.state)
        steps.append(("position", ["G90", "G1 X%s Y%s F%d" % (_f(x), _f(y), TRAVEL_FEED)]))
    return steps


def _restore_lines(plan: ResumePlan) -> List[str]:
    snap = plan.snapshot
    out = []
    if plan.mesh_loadable:
        out.append("BED_MESH_PROFILE LOAD=%s" % gcode_param(plan.mesh_loadable))
    offset = snap.get("gcode_offset")
    if offset:
        out.append("SET_GCODE_OFFSET X=%s Y=%s Z=%s" % (_f(offset[0]), _f(offset[1]), _f(offset[2])))
    for extruder, pa in sorted((snap.get("pa") or {}).items()):
        parts = ["SET_PRESSURE_ADVANCE EXTRUDER=%s" % gcode_param(extruder)]
        if pa.get("advance") is not None:
            parts.append("ADVANCE=%s" % _f(pa["advance"]))
        if pa.get("smooth_time") is not None:
            parts.append("SMOOTH_TIME=%s" % _f(pa["smooth_time"]))
        if len(parts) > 1:
            out.append(" ".join(parts))
    limits = snap.get("limits") or {}
    parts = ["SET_VELOCITY_LIMIT"]
    for key, name in (("velocity", "VELOCITY"), ("accel", "ACCEL"),
                      ("scv", "SQUARE_CORNER_VELOCITY"), ("min_cruise_ratio", "MINIMUM_CRUISE_RATIO")):
        if limits.get(key) is not None:
            parts.append("%s=%s" % (name, _f(limits[key])))
    if len(parts) > 1:
        out.append(" ".join(parts))
    retraction = snap.get("retraction")
    if retraction:
        parts = ["SET_RETRACTION"]
        for key, name in (("length", "RETRACT_LENGTH"), ("speed", "RETRACT_SPEED"),
                          ("extra", "UNRETRACT_EXTRA_LENGTH"), ("unretract_speed", "UNRETRACT_SPEED")):
            if retraction.get(key) is not None:
                parts.append("%s=%s" % (name, _f(retraction[key])))
        if len(parts) > 1:
            out.append(" ".join(parts))
    if snap.get("speed_factor") is not None:
        out.append("M220 S%s" % _f(snap["speed_factor"] * 100.0))
    if snap.get("extrude_factor") is not None:
        out.append("M221 S%s" % _f(snap["extrude_factor"] * 100.0))
    for name, heater in sorted((snap.get("heaters") or {}).items()):
        target = heater.get("target") if isinstance(heater, dict) else None
        if not target:
            continue
        if plan.chamber is not None and name.endswith(" " + plan.chamber[0]):
            continue
        if name.startswith("temperature_fan "):
            out.append("SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN=%s TARGET=%s"
                       % (gcode_param(name.split(" ", 1)[1]), _f(target)))
        elif name.startswith("heater_generic "):
            out.append("SET_HEATER_TEMPERATURE HEATER=%s TARGET=%s"
                       % (gcode_param(name.split(" ", 1)[1]), _f(target)))
    for macro, values in sorted(plan.macro_vars.items()):
        for var, value in sorted(values.items()):
            out.append("SET_GCODE_VARIABLE MACRO=%s VARIABLE=%s VALUE=%s" % (
                gcode_param(macro), gcode_param(var), gcode_param(_macro_value(value))))
    return out


def _macro_value(value: Any) -> str:
    import json
    return json.dumps(value, separators=(",", ":")) if not isinstance(value, str) \
        else json.dumps(value)


def _safe_z(plan: ResumePlan) -> float:
    """The lifted height in the restored G-code frame, above both the resume
    line's Z and the top of the plastic."""
    rz = _resume_xyz(plan.state)[2]
    top = plan.part_top_z if plan.part_top_z is not None else rz
    return max(rz, top) + plan.lift_mm


def _park_lines(plan: ResumePlan) -> List[str]:
    """Travel, lifted, to the part-free park position before the nozzle heats."""
    if plan.park_xy is None:
        return []
    offset = plan.snapshot.get("gcode_offset") or [0.0, 0.0]
    return ["G90", "G1 Z%s F%d" % (_f(_safe_z(plan)), Z_FEED),
            "G1 X%s Y%s F%d" % (_f(plan.park_xy[0] - offset[0]),
                                _f(plan.park_xy[1] - offset[1]), TRAVEL_FEED)]


def _heat_nozzle_lines(plan: ResumePlan, profile: Any) -> List[str]:
    """Go where the nozzle may heat, then set its target. The wait is the
    plugin's poll, not M109, so an abort lands before the purge."""
    target = "M104 S%s" % _f(plan.nozzle_target)
    if plan.purge_xy is None or profile.purge_gcode:
        # Lifted, at the XY homing position: never over the resume point a
        # tier-1 jog left the nozzle at.
        return _park_lines(plan) + [target]
    x0, y0, _, _ = plan.purge_xy
    return ["G90", "G1 X%s Y%s F%d" % (_f(x0), _f(y0), TRAVEL_FEED),
            "G1 Z%s F%d" % (_f(PURGE_Z_MM), Z_FEED), target]


def _purge_lines(plan: ResumePlan, profile: Any) -> List[str]:
    if profile.purge_gcode:
        return [l for l in profile.purge_gcode.splitlines() if l.strip()]
    if plan.purge_xy is None:
        return ["M83", "G1 E%s F%d" % (_f(HOME_PRIME_E_MM), PURGE_FEED // 4),
                "G1 E-%s F%d" % (_f(PURGE_RETRACT_MM), RETRACT_FEED)]
    x0, y0, x1, y1 = plan.purge_xy
    return [
        "M83",
        "G1 X%s Y%s E%s F%d" % (_f(x1), _f(y1), _f(PURGE_E_MM), PURGE_FEED),
        "G1 E-%s F%d" % (_f(PURGE_RETRACT_MM), RETRACT_FEED),
        "G1 X%s Y%s F%d" % (_f(x0), _f(y0), TRAVEL_FEED),
        "G1 Z%s F%d" % (_f(_safe_z(plan)), Z_FEED),
    ]


def render_commit_script(plan: ResumePlan, profile: Any,
                         z_reference: str) -> List[Tuple[str, List[str]]]:
    """Steps of the commit phase, up to but not including the start command.
    z_reference is "trusted" (tier 2: keep the frame prepare declared) or
    "part_top" (tier 1: the nozzle was jogged onto the part)."""
    steps = []  # type: List[Tuple[str, List[str]]]
    rx, ry, rz = _resume_xyz(plan.state)
    if z_reference == "part_top":
        # The nozzle rests on the plastic: declare the height that plastic was
        # printed at, through the offset and the mesh the resume runs with.
        offset = plan.snapshot.get("gcode_offset") or [0.0, 0.0, 0.0]
        top = plan.part_top_z if plan.part_top_z is not None else rz
        z = top + offset[2]
        if plan.mesh_loadable:
            z += mesh_adjustment(plan.mesh, rx + offset[0], ry + offset[1], z,
                                 plan.mesh_fade) or 0.0
        lines = ["SET_KINEMATIC_POSITION Z=%s SET_HOMED=Z" % _f(z)]
        lines += ["G91", "G1 Z%s F%d" % (_f(plan.lift_mm), Z_FEED), "G90"]
        steps.append(("z_reference", lines))
    steps.append(("restore", _restore_lines(plan)))
    # helix_plr.py polls the extruder to temperature after this step.
    steps.append(("heat_nozzle", _heat_nozzle_lines(plan, profile)))
    steps.append(("purge", _purge_lines(plan, profile)))
    state = plan.state
    ret = ["G90",
           "G1 Z%s F%d" % (_f(_safe_z(plan)), Z_FEED),
           "G1 X%s Y%s F%d" % (_f(rx), _f(ry), TRAVEL_FEED),
           "G1 Z%s F%d" % (_f(rz), Z_FEED),
           "M83",
           "G1 E%s F%d" % (_f(PRIME_E_MM), RETRACT_FEED),
           "G90" if state.absolute_coord else "G91",
           "M82" if state.absolute_extrude else "M83",
           "G92 E%s" % _f(state.gcode_position()[3]),
           "G1 F%s" % _f(state.speed * 60.0)]
    for name, speed in sorted((plan.snapshot.get("fans") or {}).items()):
        if speed is None:
            continue
        if name == "fan":
            ret.append("M106 S%s" % _f(round(speed * 255.0, 1)))
        elif name.startswith("fan_generic "):
            ret.append("SET_FAN_SPEED FAN=%s SPEED=%s" % (gcode_param(name.split(" ", 1)[1]), _f(speed)))
    if state.retracted:
        ret.append("G10")
    steps.append(("move_to_resume", ret))
    return steps


def _file_state_lines(plan: ResumePlan) -> List[str]:
    """What selecting the file clears: M23 resets exclude_object (objects,
    exclusions, the current object) and print_stats' layer info, so these
    run between M23 and M24.

    The first exclusion registers exclude_object's transform, which counts
    its first extruding moves instead of skipping them: tiny moves that net
    to zero use them up, so the file's first moves of an excluded object are
    skipped. SAVE/RESTORE_GCODE_STATE puts the extrude mode back and
    re-bases E, so the file's E stays where it was.

    The resume offset can fall inside an object's EXCLUDE_OBJECT_START/END
    block; that object (never an excluded one: the plan resumes past those)
    is entered again, so the block's END matches."""
    out = list(plan.object_defines)
    for name in plan.excluded:
        out.append("EXCLUDE_OBJECT NAME=%s" % gcode_param(name))
    if plan.excluded:
        out += ["SAVE_GCODE_STATE NAME=%s" % EXCLUDE_COUNTER_STATE, "M83"]
        for i in range(EXCLUDE_COUNTER_MOVES):
            sign = "" if i % 2 == 0 else "-"
            out.append("G1 E%s%s F%d" % (sign, _f(EXCLUDE_COUNTER_E_MM), RETRACT_FEED))
        out.append("RESTORE_GCODE_STATE NAME=%s" % EXCLUDE_COUNTER_STATE)
    info = ["SET_PRINT_STATS_INFO"]
    if plan.total_layers is not None:
        info.append("TOTAL_LAYER=%d" % plan.total_layers)
    if plan.layer is not None:
        info.append("CURRENT_LAYER=%d" % plan.layer)
    if len(info) > 1:
        out.append(" ".join(info))
    if plan.state.current_object:
        out.append("EXCLUDE_OBJECT_START NAME=%s" % gcode_param(plan.state.current_object))
    return out


def render_start(plan: ResumePlan) -> str:
    """The single start command: select, seek, the state selecting cleared, start."""
    return "\n".join(["M23 %s" % plan.start_name, "M26 S%d" % plan.resume_offset]
                     + _file_state_lines(plan) + ["M24"])


def required_confirmations(tier: int, warnings: Sequence[str], bed_cold: bool) -> List[str]:
    out = ["part_attached", "nozzle_clear", "z_jogged"] if tier == 1 \
        else ["part_attached", "z_confirmed"]
    if "mesh_unavailable" in warnings:
        out.append("accept_no_mesh")
    if bed_cold:
        out.append("accept_cold_bed")
    if "layer_start" in warnings:
        # The rest of the current layer is skipped.
        out.append("accept_next_layer")
    return out


def restores_summary(plan: ResumePlan) -> List[str]:
    """What commit restores, as short names for the app to list."""
    snap = plan.snapshot
    out = []
    if plan.mesh_loadable:
        out.append("bed_mesh")
    if snap.get("gcode_offset"):
        out.append("gcode_offset")
    if snap.get("pa"):
        out.append("pressure_advance")
    if any(v is not None for v in (snap.get("limits") or {}).values()):
        out.append("velocity_limits")
    if snap.get("retraction"):
        out.append("retraction")
    if snap.get("speed_factor") is not None:
        out.append("speed_factor")
    if snap.get("extrude_factor") is not None:
        out.append("extrude_factor")
    if plan.object_defines:
        out.append("objects")
    if plan.excluded:
        out.append("excluded_objects")
    if snap.get("fans"):
        out.append("fans")
    if plan.macro_vars:
        out.append("macro_vars")
    return out
