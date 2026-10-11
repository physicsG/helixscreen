# Belt Tension (resonance comparison)

Developer guide for the belt tension check: two Klipper resonance sweeps, one per CoreXY
diagonal, compared as curves. The predecessor tool (hand-plucked belts on a live
accelerometer stream) is `BELT_TUNER.md`.

**User guide**: `../user/guide/calibration.md` § Belt Tension

---

## Overview

The check runs Klipper's `TEST_RESONANCES` down each belt path, reads the CSV Klipper
writes, and overlays both frequency responses. The two curves are compared the way
Shake&Tune compares belts: a band-limited Pearson similarity as the headline verdict, plus
each path's peaks paired by frequency with unpaired peaks flagged. A re-test sweeps one
path and keeps the other, ghosting the replaced curve.

Path A is the `AXIS=1,-1` diagonal and Path B the `AXIS=1,1` one. The letters follow the
Voron motor names Shake&Tune uses: a 1,1 move turns only `stepper_x`, which on a Voron is
the B motor, so a user comparing this screen with Shake&Tune or the Voron documentation
sees the same letters on the same belts.

Start is gated, in `src/calibration/belt_gating.cpp#evaluate_belt_gate`:

| Gate leg | Meaning |
|----------|---------|
| `NOT_CONNECTED` | no Moonraker, or klippy not READY (a dead klippy refuses gcode, so a sweep would only stall) |
| `NO_ACCELEROMETER` | no accelerometer in the configfile config |
| `NOT_COREXY` | kinematics is not `corexy`; the two diagonals the comparison needs exist only there |
| `NOT_COLOCATED` | Klipper's unix socket is not reachable: HelixScreen is not running on the printer's own computer, so it cannot read the CSV |
| `PRINTING` | a print is active; the toolhead is not free to move |

The panel computes the gate in one place, `src/ui/ui_panel_belt_tension.cpp#refresh_gate`,
plus a calibrator-idle leg; `bt_can_start` binds the Start button's disabled state, so the
gate holds however the panel was reached (row click, `ctl navigate`, a demo entry, an
accelerometer that drops out after entry).

Start and Re-test also pass through `BeltTensionPanel::run_after_ram_check`: on a host with
less than `RESONANCE_LOW_RAM_WARN_MB` (200 MB) it shows input shaper's shared
`show_low_ram_resonance_warning` first. Klipper analyses each sweep on the printer's own
host, which is this one (the co-location gate), and on a 128 MB board that analysis can be
OOM-killed, leaving klippy wedged until it is restarted.

Z belts are deliberately unsupported; see the header comment on
`include/belt_tension_types.h` for the measurement that closed that door.

## Key files

| File | Role |
|------|------|
| `src/ui/ui_panel_belt_tension.cpp`, `include/ui_panel_belt_tension.h`, `ui_xml/panel_belt_tension.xml` | The panel: four view states (START, RUNNING, RESULTS, ERROR), subjects, gate wiring, run queue, re-test and ghost bookkeeping |
| `src/ui/ui_belt_path_sketch.cpp` | The START-screen sketch widget drawing the two diagonals |
| `src/calibration/belt_tension_calibrator.cpp` | Measures one path per call: home if needed, sweep, cancel, `emergency_abort()` |
| `src/calibration/belt_tension_types.cpp` | `compare_belt_paths` (band similarity, paired peaks) with `band_similarity` / `detect_belt_peaks` / `pair_belt_peaks` / `verdict_for_similarity` |
| `src/calibration/resonance_console.cpp` | `[resonance_tester]` config query and console-line parsing shared with input shaper |
| `src/calibration/shaper_csv_parser.cpp` | `parse_resonance_csv`: reads the CSV, returns `(freq, psd_xyz)` pairs or a classified error |
| `src/api/moonraker_advanced_api.cpp` | `BeltResonanceCollector` (console follow + CSV read) and `test_belt_resonance` (entry point); `detect_belt_hardware` for kinematics |
| `src/ui/ui_frequency_response_chart.cpp` | Shared chart: series styles, muted ghost series, sweep cursor, numbered and hollow markers, percent gridline labels |
| `src/calibration/belt_gating.cpp` | The pure Start-gate predicate |
| `src/api/moonraker_client_mock.cpp` | `dispatch_test_resonances_response`: the `--test` simulation of a sweep |

## Flow

```
BeltTensionPanel (LVGL thread)
  |  handle_start_clicked / handle_retest_clicked
  v
BeltTensionCalibrator::measure_path(path)              belt_tension_calibrator.cpp
  |  ensure_homed_then(...)                            toolhead_homing.h
  v
IAdvancedAPI::test_belt_resonance(axis, name, ...)     moonraker_advanced_api.cpp
  |  1. BeltResonanceCollector::start()                (console subscription)
  |  2. query_resonance_tester_config(client)          resonance_console.cpp
  |     -> collector->set_config(cfg)  [progress hold]
  |  3. execute_gcode TEST_RESONANCES AXIS=<1,-1|1,1>
  |                      OUTPUT=resonances NAME=helix_belt_{a,b} SWEEPING_PERIOD=0
  v
Klipper
  |  "Testing frequency %.0f Hz"          -> on_progress (percent + cursor)
  |  "Resonances data written to <path>"  -> read_result(path)
  v
parse_resonance_csv(path)                              shaper_csv_parser.cpp
  |  (freq, psd_xyz) pairs | classified error
  v  on_complete / on_error  (marshalled to the LVGL thread by lifetime_.bg_cb)
BeltTensionPanel::on_sweep_complete -> next path, then finish_run()
  |
  v
compare_belt_paths(curve_a, curve_b, sweep min, sweep max)  belt_tension_types.cpp
  -> BeltComparison -> populate_results(): similarity, verdict chip, peak facts, markers
```

Two threading rules shape the middle of that flow. Every collector callback arrives on the
WebSocket thread; the calibrator wraps each one in `lifetime_.bg_cb(...)` so the panel only
ever hears on the LVGL thread, and a run cancelled mid-flight expires the tokens so late
callbacks die at the guard. And the sweep's G-code rides the long calibration timeout with
`caller_surfaces_errors=true`; a transport loss is ignored (Klipper keeps sweeping and the
console lines keep arriving), everything else completes the collector with an error.

Progress needs the printer's own range: `sweep_percent` scales the current "Testing
frequency" line against the `[resonance_tester]` config queried alongside the sweep, and
the collector holds progress lines until `set_config` lands so the first percent is not
computed against the 5-135 Hz default when the printer sweeps something else. The config
query always answers (defaults on error), so the hold is finite.

The panel owns stall detection: `BeltTensionPanel::STALL_TIMEOUT_MS` (120 s, an
`OperationTimeoutGuard`) fires when no progress line arrived, because Klipper prints one
every second. `BeltResonanceCollector` carries no deadline of its own. The stall message
names the likeliest cause on a small board, Klipper's analysis running out of memory, and
tells the user to power-cycle (or restart Klipper over SSH): an OOM-killed analysis leaves
klippy wedged, the Moonraker firmware-restart RPC cannot reach it, and on the CC1 and AD5M
Moonraker's service manager does not list Klipper at all.

Start refuses outright when `MemAvailable + SwapFree` is under
`RESONANCE_MIN_HEADROOM_MB` (64 MB), and asks first on a board under 200 MB total
(`include/memory_utils.h#resonance_memory_check`, shared with input shaper). On the CC1
the OOM killer takes HelixScreen before the analysis, so the stall error never shows
there: the check has to happen before Start.

**Stop** is not a pause. A sweep cannot be resumed, so `handle_stop_clicked` calls
`BeltTensionCalibrator::emergency_abort`, which is
`include/calibration_abort.h#emergency_stop_and_restart`: M112, then FIRMWARE_RESTART to
clear the movement state the aborted sweep left behind.

## Klipper facts the implementation leans on

- `TEST_RESONANCES AXIS=1,-1` and `AXIS=1,1` excite the two CoreXY diagonals. Which
  physical belt each diagonal loads depends on the printer's geometry, which is why the UI
  reports paths A and B, never belt names.
- `SWEEPING_PERIOD=0` forces the pulse-only excitation. Mainline Klipper's
  `[resonance_tester]` defaults to the sweeping test (1.2 s), Kalico's to pulse-only, and
  firmware older than the sweeping test has no such parameter and ignores it. Shake&Tune's
  own guidance is that the sweep smooths over mechanical faults (loose belts, binding), which
  is what a belt comparison looks for.
- `OUTPUT=resonances` makes Klipper write `resonances_<axis>_<name>.csv`, usually in `/tmp`
  (the Snapmaker U1 writes to `/userdata/gcodes/shaper_calibrate/`). Only the path
  announced on the console in THIS run is read, so a stale file from an earlier run cannot
  be mistaken for a result (`BeltResonanceCollector::on_gcode_response`).
- The CSV's `psd_xyz` column is the combined response. Per-chip columns instead of
  `psd_xyz` mean more than one accelerometer reported; that is `ResonanceCsvError::MULTI_CHIP`,
  surfaced as "More than one accelerometer reported. Belt Tension supports one."
- `TEST_RESONANCES` cannot sweep Z at all, and a toolhead accelerometer barely sees a Z
  belt; see `include/belt_tension_types.h`.
- Klipper defaults for `[resonance_tester]` are 5-135 Hz at 1 Hz/s: two sweeps of about
  130 s each. The START screen shows the printer's actual range as
  "N-M Hz · about K min" from `query_hw_facts`.

## Verdicts, and why provisional

`include/belt_tension_types.h` (`belt_verdict` namespace). Every calculation uses only
bins inside the printer's `[resonance_tester]` sweep range: Klipper and Kalico write bins
past the sweep ceiling, and those carry no excitation.

| Verdict | Condition |
|---------|-----------|
| MATCHED ("Good match") | similarity >= 90% |
| CLOSE ("Fair match") | similarity >= 75% |
| ADJUST ("Poor match") | anything worse |

Similarity is the Pearson correlation of the two in-band PSDs (B interpolated onto A's
bins) x100, clamped to 0..100. Alongside it the comparison pairs local maxima by
frequency, Shake&Tune's `_pair_peaks`: a pair forms when two peaks sit within
min(median + 1.5 * IQR of all peak distances, 10 Hz), greedily closest-first, and a peak
with no partner is reported unpaired rather than differenced against the other path's
tallest peak: the two tallest peaks can be different modes entirely (a frame mode on one
path, a belt hump on the other).

The comparison is invalid when either curve has fewer than three in-band bins. The
constants are provisional. Five printers captured so far sort plausibly under them (fixtures
in `tests/fixtures/belt_sweeps/`, named by axis):

| Printer | Band (Hz) | Similarity | Verdict |
|---------|-----------|-----------:|---------|
| K1C | 5-133.3 | 98.9% | Good match |
| AD5M | 5-100 | 97.4% | Good match |
| K2 Plus | 20-120 | 92.2% | Good match |
| U1 | 5-100 | 81.7% | Fair match (main pair 3 Hz apart) |
| Voron 2.4 (Kalico) | 5-135 | 48.3% | Poor match (uneven belts, gantry due for re-racking) |

Belt modes sit where the machine puts them (40-60 Hz on the small factory printers,
110-130 Hz on the Voron), so no fixed "belt band" would work across the fleet. No capture
showed anything above 150 Hz stronger than 2% of its peak.

The panel shows no direction ("A tighter" / "B tighter"): a signed difference between two
single peaks read the Voron's 36 Hz frame mode against its 120 Hz belt hump. Direction
returns only if captures show a dependable signal (#1721).

Shake&Tune reports a higher similarity for the same printer (71% against 48% on the Voron):
it correlates 0-200 Hz, where both curves are nearly flat and so agree, while this check
correlates only the swept band.

## The RESULTS screen

- Centre: `bt_similarity` ("48%") and the verdict chip.
- Sides: the path labels with their `bt_note_a` / `bt_note_b` status ("just now", "sweeping",
  "N min ago"); after a single-path re-test the note carries the similarity the comparison
  showed before (" · was 48%").
- Under the chart: `bt_facts`, up to `MAX_LISTED_PEAKS` pairs strongest first as A/B
  ("Peaks 36/35 · 132/133 Hz"), and `bt_unpaired` ("Only on B: 120, 129 Hz"), hidden by
  `bt_has_unpaired` when empty.
- Chart: the listed pairs carry the same number on both curves; the listed unpaired peaks
  are hollow rings. Both curves (and the re-test ghosts) are trimmed to the sweep band and
  scaled by the tallest in-band point on screen, so the y axis reads 0-100%.

## Mock knobs

The mock simulates the whole console sequence: "Testing frequency" lines, the terminal
written-file line, and a CSV with a Lorentzian peak over a noise floor at the requested
frequency (`dispatch_test_resonances_response` in `src/api/moonraker_client_mock.cpp`,
`write_mock_belt_csv` for the file). Full reference: `MOCK_ENVIRONMENT_VARIABLES.md`,
one `###` entry per variable.

| Variable | Effect |
|----------|--------|
| `HELIX_MOCK_BELT_A_HZ` / `HELIX_MOCK_BELT_B_HZ` | The two paths' simulated peaks. Defaults 110/98: 12 Hz apart, deliberately in "Poor match" territory. Re-measuring one path walks its peak toward the other's by up to 4 Hz per run, so the re-test loop converges the way cranking a tensioner does |
| `HELIX_MOCK_BELT_FAIL` | `stall` (no progress lines), `nofile` (CSV missing), `multichip` (per-chip columns), `error` (adxl error line), `kalico` (dialect the parser rejects) |
| `HELIX_MOCK_KINEMATICS` | Overrides the kinematics the mock reports; `cartesian` on the default Voron persona exercises the NOT_COREXY gate leg |
| `HELIX_MOCK_BELT_CSV_A` / `HELIX_MOCK_BELT_CSV_B` | Replay a real capture as that path's result instead of the synthetic curve (e.g. the fixtures in `tests/fixtures/belt_sweeps/`) |
| `HELIX_MOCK_BELT_RANGE` | `<min>-<max>`: the `[resonance_tester]` sweep range the mock reports and sweeps, to match a replayed capture's band |

## Tests

| File | Covers |
|------|--------|
| `tests/unit/test_belt_tension_calibrator.cpp` | The calibrator: homing order, sweep lifecycle, cancel-mid-run generation guard, emergency abort |
| `tests/unit/test_belt_tension_panel_states.cpp` | Panel view states, gate-to-button binding, similarity/facts/markers, ghost series on re-test, low-RAM warning |
| `tests/unit/test_belt_compare.cpp` | `compare_belt_paths` / `pair_belt_peaks` / `band_similarity` against five printers' real captures (`tests/fixtures/belt_sweeps/`) and synthetic curves |
| `tests/unit/test_belt_gating.cpp` | Every gate leg of `evaluate_belt_gate` |
| `tests/unit/test_mock_test_resonances.cpp` | The mock's sweep simulation, including each `HELIX_MOCK_BELT_FAIL` mode |
| `tests/unit/test_frequency_response_chart_style.cpp` (`[belt]` cases) | Series styling, muted ghost, sweep cursor, markers |

Run them with `make t F='[belt]'` (the `[belt]` tag also collects the pluck-tuner units
kept from `BELT_TUNER.md`; the panel and calibrator suites additionally carry
`[belt_tension]`).
