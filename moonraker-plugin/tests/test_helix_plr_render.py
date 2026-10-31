# SPDX-License-Identifier: GPL-3.0-or-later
"""
Power-loss recovery's resume scripts (helix_plr_render.py): the prepare and
commit sequences per tier, state restore, purge placement and parameter safety.

Run with: pytest tests/test_helix_plr_render.py -v
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))

from helix_gcode_reader import ModalState
from helix_plr import PlrProfile
from helix_plr_render import (
    HeatStep,
    ResumePlan,
    UnsafeValue,
    gcode_param,
    generic_purge_xy,
    lift_height,
    mesh_z_at,
    render_commit_script,
    render_heat_wait,
    nozzle_standby_c,
    render_prepare_script,
    render_start,
    required_confirmations,
    restores_summary,
)


def resume_state():
    s = ModalState()
    s.last_position = [110.5, 95.25, 12.4, 500.0]
    s.base_position = [0.0, 0.0, 0.0, 488.0]
    s.absolute_extrude = False
    s.speed = 50.0
    return s


SNAPSHOT = {
    "gcode_offset": [0.0, 0.0, 0.05, 0.0],
    "pa": {"extruder": {"advance": 0.04, "smooth_time": 0.02}},
    "limits": {"velocity": 300.0, "accel": 5000.0, "scv": 5.0, "min_cruise_ratio": None},
    "retraction": None,
    "speed_factor": 1.0,
    "extrude_factor": 0.95,
    "heaters": {"extruder": {"target": 220.0}, "heater_bed": {"target": 60.0},
                "heater_generic chamber": {"target": 45.0},
                "heater_generic dryer": {"target": 50.0},
                "temperature_fan mcu": {"target": 40.0}},
    "fans": {"fan": 1.0, "fan_generic aux": 0.5},
    "live_pos": [110.0, 95.0, 12.45, 499.0],
}


def plan(**kw):
    base = dict(
        plan_id="p1", filename="sub/part.gcode", start_name="helix_plr_resume.gcode",
        history_name="sub/part.gcode", resume_offset=123456, state=resume_state(),
        layer=62, total_layers=200, tier=2, z_physical=12.45, lift_mm=5.0, snapshot=SNAPSHOT,
        object_defines=["EXCLUDE_OBJECT_DEFINE NAME=part_a CENTER=110,95"],
        excluded=["part_b"], mesh=None, mesh_loadable="default",
        purge_xy=(5.0, 5.0, 65.0, 5.0), nozzle_target=220.0, bed_target=60.0,
        chamber=None, macro_vars={"_STATE": {"count": 3}}, method="measured",
        retrace_estimate_mm=1.2, skip_estimate_mm=10.0, warnings=[],
        requires=["part_attached", "z_confirmed"])
    base.update(kw)
    return ResumePlan(**base)


PROFILE = PlrProfile(tier=2)


def flat(steps):
    return [line for _, lines in steps for line in lines]


def test_tier2_prepare_is_lift_then_home():
    steps = render_prepare_script(plan(), PROFILE, 12.45, 5.0)
    assert steps == [
        ("lift", ["SET_KINEMATIC_POSITION Z=12.45 SET_HOMED=Z", "G91", "G1 Z5 F300", "G90"]),
        ("nozzle_standby", ["M83", "G1 E-1 F2100", "M104 S150"]),
        ("home_xy", ["G28 X Y"]),
    ]
    assert not any(l.startswith("M109") for l in flat(steps))


def test_prepare_declares_and_lifts_only_as_asked():
    declared = render_prepare_script(plan(), PROFILE, 17.45, 0.0)
    assert declared[0] == ("lift", ["SET_KINEMATIC_POSITION Z=17.45 SET_HOMED=Z"])
    homed = render_prepare_script(plan(), PROFILE, None, 3.0)
    assert homed[0] == ("lift", ["G91", "G1 Z3 F300", "G90"])
    assert [name for name, _ in render_prepare_script(plan(), PROFILE, None, 0.0)] \
        == ["nozzle_standby", "home_xy"]


@pytest.mark.parametrize("target, drop, expected", [
    (220.0, 70.0, 150.0),        # the print temperature less the drop
    (200.0, 70.0, 140.0),        # floored
    (130.0, 70.0, 130.0),        # never above the print temperature
    (260.0, 0.0, 260.0),         # no drop: stays at print temperature
])
def test_nozzle_standby_temperature(target, drop, expected):
    assert nozzle_standby_c(target, drop) == expected


def test_heat_wait_sets_then_waits_without_blocking_gcode():
    steps = render_heat_wait(plan(chamber=("chamber", 45.0)))
    assert steps == [
        HeatStep("bed_wait", ["M140 S60"], "heater_bed", 59.0, True),
        HeatStep("chamber_wait", ["SET_HEATER_TEMPERATURE HEATER=chamber TARGET=45"],
                 "heater_generic chamber", 44.0, True),
    ]
    assert not any(l.startswith(("M190", "TEMPERATURE_WAIT")) for s in steps for l in s.lines)


def test_tier1_prepare_stops_above_the_resume_point():
    steps = render_prepare_script(plan(tier=1), PROFILE, 12.45, 5.0)
    assert steps[-1] == ("position", ["G90", "G1 X110.5 Y95.25 F6000"])


def test_skipping_the_chamber_wait_still_sets_the_chamber():
    steps = render_heat_wait(plan(chamber=("chamber", 45.0)), skip_chamber_wait=True)
    assert steps[1].lines == ["SET_HEATER_TEMPERATURE HEATER=chamber TARGET=45"]
    assert steps[1].wait is False and steps[0].wait is True


def test_prepare_uses_the_profiles_xy_homing():
    steps = dict(render_prepare_script(plan(),
                                       PlrProfile(tier=2, xy_home_gcode="G28 X\nG28 Y\n"),
                                       12.45, 5.0))
    assert steps["home_xy"] == ["G28 X", "G28 Y"]


def test_tier2_commit_golden():
    steps = render_commit_script(plan(), PROFILE, "trusted")
    assert [name for name, _ in steps] == ["restore", "heat_nozzle", "purge", "move_to_resume"]
    restore, heat, purge, ret = (lines for _, lines in steps)
    assert restore == [
        "BED_MESH_PROFILE LOAD=default",
        "SET_GCODE_OFFSET X=0 Y=0 Z=0.05",
        "SET_PRESSURE_ADVANCE EXTRUDER=extruder ADVANCE=0.04 SMOOTH_TIME=0.02",
        "SET_VELOCITY_LIMIT VELOCITY=300 ACCEL=5000 SQUARE_CORNER_VELOCITY=5",
        "M220 S100",
        "M221 S95",
        "SET_HEATER_TEMPERATURE HEATER=chamber TARGET=45",
        "SET_HEATER_TEMPERATURE HEATER=dryer TARGET=50",
        "SET_TEMPERATURE_FAN_TARGET TEMPERATURE_FAN=mcu TARGET=40",
        "SET_GCODE_VARIABLE MACRO=_STATE VARIABLE=count VALUE=3",
    ]
    assert heat == ["G90", "G1 X5 Y5 F6000", "G1 Z0.3 F300", "M104 S220"]
    assert purge == [
        "M83", "G1 X65 Y5 E12 F1200", "G1 E-0.8 F2100", "G1 X5 Y5 F6000", "G1 Z17.4 F300",
    ]
    assert ret == [
        "G90", "G1 Z17.4 F300", "G1 X110.5 Y95.25 F6000", "G1 Z12.4 F300",
        "M83", "G1 E0.8 F2100", "G90", "M83", "G92 E12", "G1 F3000",
        "M106 S255", "SET_FAN_SPEED FAN=aux SPEED=0.5",
    ]


def test_commit_never_heats_the_nozzle_over_the_part():
    lines = flat(render_commit_script(plan(), PROFILE, "trusted"))
    assert "M109 S220" not in lines                  # the plugin polls the nozzle instead
    heat = lines.index("M104 S220")
    assert lines[heat - 1] == "G1 Z0.3 F300" and lines[heat - 2] == "G1 X5 Y5 F6000"


def test_tier1_commit_redeclares_z_at_the_part_top():
    mesh = {"points": [[0.1, 0.1], [0.1, 0.1]], "min": [0.0, 0.0], "max": [200.0, 200.0]}
    steps = render_commit_script(plan(tier=1, mesh=mesh), PROFILE, "part_top")
    assert steps[0] == ("z_reference", ["SET_KINEMATIC_POSITION Z=12.55 SET_HOMED=Z", "G91",
                                        "G1 Z5 F300", "G90"])


def test_commit_restores_the_chamber_once_and_the_retract_state():
    s = resume_state()
    s.retracted = True
    s.absolute_coord = False
    lines = flat(render_commit_script(plan(state=s, chamber=("chamber", 45.0)), PROFILE, "trusted"))
    assert not any("HEATER=chamber" in l for l in lines)
    assert lines[-1] == "G10"
    assert "G91" in lines[-9:]


def test_purge_macro_replaces_the_generic_purge():
    steps = dict(render_commit_script(plan(), PlrProfile(tier=2, purge_gcode="PURGE_LINE\nWIPE"),
                                      "trusted"))
    assert steps["heat_nozzle"] == ["M104 S220"] and steps["purge"] == ["PURGE_LINE", "WIPE"]


def test_no_purge_location_primes_at_the_home_position():
    steps = dict(render_commit_script(plan(purge_xy=None), PROFILE, "trusted"))
    assert steps["heat_nozzle"] == ["M104 S220"]
    assert not any(l.startswith("G1 X") for l in steps["purge"])


def test_restore_without_a_mesh_or_layers():
    restore = dict(render_commit_script(
        plan(mesh_loadable=None, layer=None, total_layers=None, macro_vars={}, excluded=[]),
        PROFILE, "trusted"))["restore"]
    assert not any(l.startswith(("BED_MESH", "SET_PRINT_STATS_INFO", "SET_GCODE_VARIABLE",
                                 "EXCLUDE_OBJECT NAME")) for l in restore)


def test_include_file_mesh_loads_the_plr_profile():
    restore = dict(render_commit_script(plan(mesh_loadable="helix_plr"), PROFILE,
                                        "trusted"))["restore"]
    assert restore[0] == "BED_MESH_PROFILE LOAD=helix_plr"


def test_start_selects_seeks_and_starts():
    assert render_start(plan()).split("\n") == [
        "M23 helix_plr_resume.gcode",
        "M26 S123456",
        "EXCLUDE_OBJECT_DEFINE NAME=part_a CENTER=110,95",
        "EXCLUDE_OBJECT NAME=part_b",
        "SAVE_GCODE_STATE NAME=HELIX_PLR_EXCLUDE",
        "M83",
        "G1 E0.001 F2100", "G1 E-0.001 F2100", "G1 E0.001 F2100",
        "G1 E-0.001 F2100", "G1 E0.001 F2100", "G1 E-0.001 F2100",
        "RESTORE_GCODE_STATE NAME=HELIX_PLR_EXCLUDE",
        "SET_PRINT_STATS_INFO TOTAL_LAYER=200 CURRENT_LAYER=62",
        "M24",
    ]
    assert render_start(plan(start_name="part.gcode")).startswith("M23 part.gcode\n")
    bare = plan(object_defines=[], excluded=[], layer=None, total_layers=None)
    assert render_start(bare) == "M23 helix_plr_resume.gcode\nM26 S123456\nM24"


# exclude_object.py#_register_transform: initial_extrusion_moves = 5.
KLIPPER_INITIAL_EXTRUSION_MOVES = 5


def test_an_exclusion_is_followed_by_moves_that_use_up_klippers_count():
    """The first exclusion after M23 registers exclude_object's transform,
    which prints its first five extruding moves whatever object they are in."""
    lines = render_start(plan(excluded=["part_b", "part_c"])).split("\n")
    last = max(i for i, l in enumerate(lines) if l.startswith("EXCLUDE_OBJECT NAME="))
    between = lines[last + 1:lines.index("M24")]
    e_moves = [float(l.split()[1][1:]) for l in between if l.startswith("G1 E")]
    assert len(e_moves) >= KLIPPER_INITIAL_EXTRUSION_MOVES
    assert sum(e_moves) == pytest.approx(0.0, abs=1e-12)
    assert between[0].startswith("SAVE_GCODE_STATE") and between[1] == "M83"
    assert between[2 + len(e_moves)].startswith("RESTORE_GCODE_STATE")
    assert "MOVE=" not in between[2 + len(e_moves)]
    unexcluded = render_start(plan(excluded=[])).split("\n")
    assert not any(l.startswith(("G1", "SAVE_GCODE_STATE", "M83")) for l in unexcluded)


def test_selecting_the_file_is_followed_by_the_state_it_clears():
    """M23 resets exclude_object and print_stats' layer info, so nothing the
    commit steps send may carry them."""
    lines = flat(render_commit_script(plan(), PROFILE, "trusted"))
    assert not any(l.startswith(("EXCLUDE_OBJECT", "SET_PRINT_STATS_INFO")) for l in lines)


def test_start_reenters_the_object_the_resume_point_is_in():
    s = resume_state()
    s.current_object = "box.stl_id_0_copy_0"
    lines = render_start(plan(state=s)).split("\n")
    assert lines[-3:] == ["SET_PRINT_STATS_INFO TOTAL_LAYER=200 CURRENT_LAYER=62",
                          "EXCLUDE_OBJECT_START NAME=box.stl_id_0_copy_0", "M24"]


def test_restores_summary():
    assert restores_summary(plan()) == [
        "bed_mesh", "gcode_offset", "pressure_advance", "velocity_limits", "speed_factor",
        "extrude_factor", "objects", "excluded_objects", "fans", "macro_vars"]


@pytest.mark.parametrize(
    "tier, warnings, cold, expected",
    [
        (1, [], False, ["part_attached", "nozzle_clear", "z_jogged"]),
        (2, [], False, ["part_attached", "z_confirmed"]),
        (2, ["mesh_unavailable"], True,
         ["part_attached", "z_confirmed", "accept_no_mesh", "accept_cold_bed"]),
        (2, ["layer_start"], False, ["part_attached", "z_confirmed", "accept_next_layer"]),
    ],
)
def test_required_confirmations(tier, warnings, cold, expected):
    assert required_confirmations(tier, warnings, cold) == expected


@pytest.mark.parametrize(
    "value, expected",
    [("part_a", "part_a"), ("my part", '"my part"'), ("Würfel", "Würfel")],
)
def test_gcode_param(value, expected):
    assert gcode_param(value) == expected


@pytest.mark.parametrize("value", ["a;b", "a#b", "a*b", 'a"b', "a\nM104 S300", ""])
def test_gcode_param_refuses_what_klipper_would_cut(value):
    with pytest.raises(UnsafeValue):
        gcode_param(value)


def test_unsafe_names_from_status_refuse_the_render():
    with pytest.raises(UnsafeValue):
        render_start(plan(excluded=["evil\nG28"]))
    s = resume_state()
    s.current_object = "evil;G28"
    with pytest.raises(UnsafeValue):
        render_start(plan(state=s))


def test_lift_height():
    assert lift_height(5.0, 12.0, 250.0) == 5.0
    assert lift_height(5.0, 247.0, 250.0) == pytest.approx(2.0)
    assert lift_height(5.0, 250.0, 250.0) == 0.0
    assert lift_height(5.0, 12.0, None) == 5.0


def obj(x0, y0, x1, y1):
    return {"name": "o", "polygon": [[x0, y0], [x1, y0], [x1, y1], [x0, y1]]}


def test_purge_picks_the_first_clear_edge():
    bed = ((0.0, 0.0), (220.0, 220.0))
    assert generic_purge_xy([obj(80, 80, 140, 140)], *bed) == (5.0, 5.0, 65.0, 5.0)
    # Front edge covered: the back is next.
    assert generic_purge_xy([obj(0, 0, 220, 30)], *bed) == (5.0, 215.0, 65.0, 215.0)
    # Front and back covered: the left edge.
    assert generic_purge_xy([obj(30, 0, 220, 220)], *bed) == (5.0, 5.0, 5.0, 65.0)


def test_purge_none_when_the_bed_is_full_or_the_part_unknown():
    bed = ((0.0, 0.0), (220.0, 220.0))
    assert generic_purge_xy([obj(0, 0, 220, 220)], *bed) is None
    assert generic_purge_xy([], *bed) is None


def test_purge_uses_an_object_centre_without_a_polygon():
    bed = ((0.0, 0.0), (220.0, 220.0))
    assert generic_purge_xy([{"name": "o", "center": [30, 5]}], *bed) == (5.0, 215.0, 65.0, 215.0)


def test_mesh_z_at_interpolates_and_clamps():
    mesh = {"points": [[0.0, 0.2], [0.4, 0.6]], "min": [0.0, 0.0], "max": [100.0, 100.0]}
    assert mesh_z_at(mesh, 50.0, 50.0) == pytest.approx(0.3)
    assert mesh_z_at(mesh, 100.0, 0.0) == pytest.approx(0.2)
    assert mesh_z_at(mesh, -50.0, 500.0) == pytest.approx(0.4)
    assert mesh_z_at(None, 1.0, 1.0) is None


# --------------------------------------------------------------------------
# Tier-1 part-top Z, mesh fade, heating away from the part
# --------------------------------------------------------------------------

from helix_plr_render import mesh_adjustment  # noqa: E402

FLAT_MESH = {"points": [[0.1, 0.1], [0.1, 0.1]], "min": [0.0, 0.0], "max": [200.0, 200.0]}


def z_ref(p):
    return dict(render_commit_script(p, PROFILE, "part_top"))["z_reference"][0]


def test_part_top_uses_the_printed_layer_not_a_hop():
    # The resume line is a Z-hop travel at 12.4; the plastic under the nozzle is at 12.0.
    p = plan(tier=1, part_top_z=12.0, mesh_loadable=None)
    assert z_ref(p) == "SET_KINEMATIC_POSITION Z=12.05 SET_HOMED=Z"


def test_part_top_adds_the_mesh_only_when_it_will_be_loaded():
    with_mesh = plan(tier=1, part_top_z=12.0, mesh=FLAT_MESH, mesh_loadable="default")
    assert z_ref(with_mesh) == "SET_KINEMATIC_POSITION Z=12.15 SET_HOMED=Z"
    lost = plan(tier=1, part_top_z=12.0, mesh=FLAT_MESH, mesh_loadable=None)
    assert z_ref(lost) == "SET_KINEMATIC_POSITION Z=12.05 SET_HOMED=Z"


def test_part_top_applies_the_mesh_fade():
    faded = plan(tier=1, part_top_z=12.0, mesh=FLAT_MESH, mesh_loadable="default",
                 mesh_fade={"fade_start": 1.0, "fade_end": 10.0, "fade_target": 0.0})
    assert z_ref(faded) == "SET_KINEMATIC_POSITION Z=12.05 SET_HOMED=Z"


def test_mesh_adjustment_follows_bed_mesh_fade():
    mesh = {"points": [[0.0, 0.4], [0.0, 0.4]], "min": [0.0, 0.0], "max": [100.0, 100.0]}
    assert mesh_adjustment(mesh, 50, 50, 5.0, None) == pytest.approx(0.2)
    fade = {"fade_start": 1.0, "fade_end": 10.0, "fade_target": 0.0}
    assert mesh_adjustment(mesh, 50, 50, 0.5, fade) == pytest.approx(0.2)        # below start
    assert mesh_adjustment(mesh, 50, 50, 20.0, fade) == pytest.approx(0.0)       # past end
    mid = mesh_adjustment(mesh, 50, 50, 5.5, fade)
    assert mid == pytest.approx((10.0 + 0.0 - 5.5) / (9.0 - 0.2) * 0.2)
    averaged = {"fade_start": 1.0, "fade_end": 10.0}
    assert mesh_adjustment(mesh, 50, 50, 20.0, averaged) == pytest.approx(0.2)   # mesh average


def test_the_lifted_height_clears_the_part_top():
    steps = dict(render_commit_script(plan(part_top_z=13.0), PROFILE, "trusted"))
    assert "G1 Z18 F300" in steps["move_to_resume"]


def test_without_a_purge_line_the_nozzle_heats_at_the_park_position():
    purge = dict(render_commit_script(plan(tier=1, part_top_z=12.0, purge_xy=None,
                                           park_xy=(220.0, 220.0)), PROFILE,
                                      "part_top"))["heat_nozzle"]
    assert purge == ["G90", "G1 Z17.4 F300", "G1 X220 Y220 F6000", "M104 S220"]


def test_a_purge_macro_heats_at_the_park_position_too():
    steps = dict(render_commit_script(plan(park_xy=(0.0, 0.0)),
                                      PlrProfile(tier=2, purge_gcode="PURGE"), "trusted"))
    assert steps["heat_nozzle"] == ["G90", "G1 Z17.4 F300", "G1 X0 Y0 F6000", "M104 S220"]
    assert steps["purge"] == ["PURGE"]
