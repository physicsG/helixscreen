// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "belt_tension_types.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

/**
 * @file belt_tune_session.h
 * @brief The guided belt-tuning loop's logic: zones, calibration, tracking and advice
 *
 * Pure: no LVGL, no I/O. The Belt Tension panel feeds it the round-0 sweeps,
 * the calibration sweeps and each round's two narrow sweeps, and renders what
 * it says. Tuning reads the in-plane curves (psd_x + psd_y). A higher
 * frequency means a tighter belt, and clockwise tightens (#1736).
 */

namespace helix::calibration {

namespace belt_tune {
/// Peaks under this fraction of the stronger path's in-band in-plane maximum
/// never define a zone.
inline constexpr float kZoneMinAmplitudeFraction = 0.20f;
/// A peak defines a zone only where psd_x + psd_y is at least this share of psd_xyz.
inline constexpr float kInPlaneMinFraction = 0.5f;
inline constexpr float kZoneHalfMinHz = 10.0f; ///< narrowest half-width of a zone
inline constexpr float kZoneMarginHz = 5.0f;   ///< zone reach beyond each defining peak
/// An unpaired tallest peak steers only against a peak this close on the other path.
inline constexpr float kZoneFallbackMaxHz = 3.0f * belt_verdict::kPairMaxHz;
inline constexpr int kMaxZoneCandidates = 4;
/// A narrow sweep starts this far below the zone, so its start transient
/// lands in bins the analysis never reads.
inline constexpr float kZoneSweepMarginHz = 10.0f;
/// ...and ends this far above it: the last 15 Hz of a narrow sweep can read
/// many times the zone's own power.
inline constexpr float kZoneSweepTopMarginHz = 20.0f;
inline constexpr float kCalibrationTurns = 1.0f;     ///< one full turn per calibration step
inline constexpr int kCalibrationMaxSteps = 2;       ///< steps before the loop gives up
inline constexpr float kCalibrationMinMoveHz = 1.5f; ///< a smaller gap move is run-to-run wander
inline constexpr float kMatchedMinHz = 1.0f;         ///< matched tolerance floor
inline constexpr float kMinMoveHz = 0.8f;            ///< smaller gap moves are noise
inline constexpr float kFirstTurn = 1.0f;            ///< the suggestion with no rate learned
inline constexpr float kTurnStep = 0.25f;
inline constexpr float kTurnMin = 0.5f;
inline constexpr float kTurnMinNear = 0.25f; ///< the smallest turn when nearly matched
/// "Nearly matched": |gap| under this multiple of the matched tolerance.
inline constexpr float kNearMatchFactor = 1.5f;
inline constexpr float kTurnMax = 2.0f;
/// A round's gap move above max(kImplausibleMinHz, kImplausibleRateFactor x
/// turns x Hz/turn) is a measurement fault. Without a rate the bound is
/// kImplausibleMinHz x max(1, turns).
inline constexpr float kImplausibleMinHz = 6.0f;
inline constexpr float kImplausibleRateFactor = 3.0f;
/// Zone in-plane power differing by more than this between paths points at
/// the mechanics, not tension.
inline constexpr float kPowerHintRatio = 3.0f;
/// A baseline pair whose centroid gaps differ by more than this does not repeat.
inline constexpr float kRepeatMaxGapHz = 1.5f;
/// ...nor one where either path's zone power differs by more than this factor.
inline constexpr float kRepeatMaxPowerRatio = 2.0f;
inline constexpr int kStallRounds = 8;       ///< unmatched rounds before the mechanical hint
inline constexpr int kSpringStallRounds = 2; ///< spring rounds in a row without improvement
/// A zone holds a peak when its maximum reaches this multiple of the unexcited
/// floor (or, without one, of the zone's minimum).
inline constexpr float kPeakMinProminence = 1.5f;
/// In-band bins outside the swept range needed to measure the unexcited floor.
inline constexpr int kUnexcitedMinBins = 8;
inline constexpr float kGaugeWarnHz = 3.0f; ///< gap gauge: warning colour up to here
} // namespace belt_tune

enum class BeltZoneSource {
    PAIRED,           ///< a peak pair
    TALLEST_UNPAIRED, ///< an unpaired tallest peak and the other path's nearest peak
};

struct BeltTuneZone {
    BeltSweepWindow window; ///< the analysed bins; a sweep reaches past it (zone_sweep_window)
    BeltZoneSource source = BeltZoneSource::PAIRED;
    float ref_hz = 0.0f; ///< reference path's peak that defined the zone
    float adj_hz = 0.0f; ///< adjusted path's peak that defined the zone
};

/// A zone the calibration turn tests. Its zone is seen from PATH_A: adj_hz is
/// A's peak, ref_hz B's.
struct BeltZoneCandidate {
    BeltTuneZone zone;
    float strength = 0.0f; ///< the two defining peaks' in-plane amplitudes, summed
};

/// What the zone's power says when it differs too much between the paths.
struct BeltPowerHint {
    BeltPath stronger = BeltPath::PATH_A;
    float freq_hz = 0.0f; ///< the stronger path's zone centroid
    float ratio = 1.0f;   ///< stronger power over weaker, > kPowerHintRatio
};

/// The curve tuning reads: in_plane, or xyz when the sweep had no per-axis columns.
[[nodiscard]] const BeltCurve& tuning_curve(const BeltResponse& r);

/// psd_x + psd_y over psd_xyz at the bin nearest freq_hz. 1 when in_plane is
/// empty (nothing says otherwise); 0 when psd_xyz there is not positive.
[[nodiscard]] float in_plane_fraction(const BeltResponse& r, float freq_hz);

/// True when either path's tallest peak is unpaired and at or above the zone
/// amplitude floor: the curves differ in shape, and the mechanical-causes
/// hint shows.
[[nodiscard]] bool tallest_peak_unpaired(const BeltComparison& cmp);

/// The zones a calibration turn tests, strongest first, at most
/// kMaxZoneCandidates. Built from the in-plane comparison over the band:
/// every pair whose two peaks reach kZoneMinAmplitudeFraction of the
/// in-band in-plane maximum and kInPlaneMinFraction in-plane share
/// (PAIRED), and an unpaired tallest peak meeting both, steered against the
/// other path's nearest such peak within kZoneFallbackMaxHz
/// (TALLEST_UNPAIRED). A matched pair is a candidate: the turn tests movement,
/// not the gap. A candidate whose centre lies within kZoneHalfMinHz of a
/// stronger one's is dropped. Each window is the two peaks' midpoint
/// +- max(kZoneHalfMinHz, gap / 2 + kZoneMarginHz), clamped to the band.
[[nodiscard]] std::vector<BeltZoneCandidate> tune_zone_candidates(const BeltResponse& a,
                                                                  const BeltResponse& b,
                                                                  float band_min_hz,
                                                                  float band_max_hz);

/// What a narrow sweep of the zone sends: kZoneSweepMarginHz below it and
/// kZoneSweepTopMarginHz above it, in whole Hz inside the band (start
/// rounded up, end rounded down), so FREQ_START/FREQ_END never leave the
/// printer's [resonance_tester] range.
[[nodiscard]] BeltSweepWindow zone_sweep_window(BeltSweepWindow zone, float band_min_hz,
                                                float band_max_hz);

/// Power-weighted mean frequency of the curve's bins inside the zone
/// (inclusive). nullopt with fewer than 3 such bins or no positive power.
[[nodiscard]] std::optional<float> zone_centroid_hz(const BeltCurve& curve, BeltSweepWindow zone);

/// Sum of the curve's values over the zone's bins (inclusive).
[[nodiscard]] float zone_power(const BeltCurve& curve, BeltSweepWindow zone);

/// The hint when one path's zone power exceeds the other's by more than
/// kPowerHintRatio; nullopt otherwise, or when either power is not positive.
[[nodiscard]] std::optional<BeltPowerHint> zone_power_hint(const BeltCurve& a, const BeltCurve& b,
                                                           BeltSweepWindow zone);

/// What the belt reads unexcited: the median of the curve's in-band bins
/// outside `swept`, which the sweep did not drive. nullopt with fewer than
/// kUnexcitedMinBins such bins.
[[nodiscard]] std::optional<float> unexcited_floor_psd(const BeltCurve& curve,
                                                       BeltSweepWindow swept, float band_min_hz,
                                                       float band_max_hz);

/// Whether the zone's maximum reaches kPeakMinProminence times the unexcited
/// floor outside `swept`, or, without one, times the zone's minimum.
[[nodiscard]] bool window_holds_peak(const BeltCurve& curve, BeltSweepWindow zone,
                                     BeltSweepWindow swept, float band_min_hz, float band_max_hz);

/// A turn count to the nearest belt_tune::kTurnStep, in fraction glyphs:
/// "¼", "¾", "1", "1¼". The sign is dropped.
[[nodiscard]] std::string format_belt_turns(float turns);

/// max(kMatchedMinHz, the curve's mean bin spacing).
[[nodiscard]] float matched_tolerance_hz(const BeltCurve& curve);

enum class BeltTuneStage {
    /// Round 0's strongest candidate already matches: its centroid gap is
    /// within max(tolerance_hz(), kCalibrationMinMoveHz). Nothing is asked
    /// of the user until calibrate_anyway().
    ALREADY_MATCHED,
    CALIBRATE, ///< waiting on a calibration turn and its full sweep of both paths
    /// The zone is known; it is measured twice, with no turn between, before
    /// any verdict. Neither round of the pair gives one.
    BASELINE,
    TUNE,    ///< each round's verdict reads the zone's gap move
    NO_ZONE, ///< no candidate, or no candidate moved after kCalibrationMaxSteps turns
    /// The baseline pair disagreed (kRepeatMaxGapHz, kRepeatMaxPowerRatio):
    /// no advice. The next round starts a fresh baseline pair.
    UNREPEATABLE,
};

enum class BeltTuneOutcome {
    CLOSER,    ///< the gap shrank and kept its sign
    OVERSHOT,  ///< the gap changed sign
    WRONG_WAY, ///< the gap moved against the advice by kMinMoveHz or more
    /// The gap moved less than kMinMoveHz, or the round turned nothing.
    /// Outcomes read the gap's change, not one path's centroid: only the
    /// adjusted belt was turned, so drift common to both paths cancels.
    NO_MOVEMENT,
    MATCHED,       ///< |gap| within the matched tolerance
    NO_PEAK,       ///< a path's zone holds no clear peak; nothing measured
    OUT_OF_WINDOW, ///< the adjusted path's maximum sits on the zone edge; the zone moved
                   ///< (once per zone until a round is measured again; then NO_PEAK)
    BASELINE,      ///< a round of the zone's baseline pair: no gap history yet
    /// The gap moved past the sanity bound: a measurement fault. The reading
    /// is dropped, the turns stay pending, and nothing is learned.
    IMPLAUSIBLE,
    /// The second round of a baseline pair disagreed with the first: the stage
    /// is UNREPEATABLE.
    UNREPEATABLE,
};

enum class BeltTurnDirection { TIGHTEN, LOOSEN };

struct BeltTuneAdvice {
    BeltPath path = BeltPath::PATH_A;
    BeltTurnDirection direction = BeltTurnDirection::TIGHTEN;
    std::optional<float> turns; ///< SCREW only, a multiple of kTurnStep
    bool spring_reseat = false; ///< SPRING: release, settle, lock
};

/// One calibration sweep's verdict.
struct BeltCalibrationResult {
    bool registered = false; ///< a candidate's gap moved kCalibrationMinMoveHz the advised way
    float move_hz = 0.0f;    ///< the best candidate's gap move, positive = the advised way
    float turns = 0.0f;      ///< calibration turns so far; the move is measured over all of them
    bool gave_up = false;    ///< kCalibrationMaxSteps reached without a move: stage is NO_ZONE
};

struct BeltTuneRound {
    float ref_hz = 0.0f;             ///< reference path's zone centroid
    float adj_hz = 0.0f;             ///< adjusted path's zone centroid
    float gap_hz = 0.0f;             ///< adjusted - reference
    std::optional<float> turns_made; ///< what the stepper recorded before this round
    /// The turns this round's reading measured: its own plus any carried from
    /// NO_PEAK, OUT_OF_WINDOW or IMPLAUSIBLE rounds since the last measured
    /// one. 0 on a round that measured nothing.
    float turns_measured = 0.0f;
    BeltTuneOutcome outcome = BeltTuneOutcome::NO_PEAK;
    /// NO_PEAK with a peak on the adjusted path only: the reference's peak
    /// left the zone, which a turn of the wrong belt does.
    bool reference_lost = false;
    /// SCREW: nothing was turned since the last measured round, so the round
    /// checks repeatability rather than a turn. It never counts toward the
    /// stall and teaches nothing. Never set on a baseline round.
    bool no_turn = false;
};

/// One tuning run. Round 0's candidates are tested by a calibration turn; the
/// zone that moved is tuned a round at a time, each round a pair of narrow
/// sweeps. The gap is always adjusted - reference from the same round, so
/// drift common to both paths cancels; negative means the adjusted belt is
/// looser.
class BeltTuneSession {
  public:
    /// `round0` is the psd_xyz comparison RESULTS showed; candidates come from
    /// the responses' in-plane curves. Any kind may start ALREADY_MATCHED.
    /// Otherwise SCREW and UNKNOWN start at CALIBRATE, and SPRING skips
    /// calibration: the strongest candidate becomes the zone, adjusting its
    /// looser path, at BASELINE.
    BeltTuneSession(const BeltResponse& a, const BeltResponse& b, const BeltComparison& round0,
                    float band_min_hz, float band_max_hz, BeltTensionerKind kind);

    [[nodiscard]] BeltTuneStage stage() const;
    [[nodiscard]] const std::vector<BeltZoneCandidate>& candidates() const;
    /// ALREADY_MATCHED only: go on as an unmatched session would (CALIBRATE,
    /// or BASELINE for SPRING). Elsewhere it changes nothing.
    void calibrate_anyway();
    /// The zone being tuned, seen from the adjusted path; nullopt until
    /// calibration registers (and at NO_ZONE). SPRING has one from the start.
    [[nodiscard]] const std::optional<BeltTuneZone>& zone() const;
    /// The range a round sweeps: zone_sweep_window() of the zone. nullopt without a zone.
    [[nodiscard]] std::optional<BeltSweepWindow> sweep_window() const;
    /// ALREADY_MATCHED and CALIBRATE: the looser path by the strongest
    /// candidate's centroid gap (the other after a swap), the one the
    /// calibration turn moves. Later: the path being tuned.
    [[nodiscard]] BeltPath adjusted_path() const;
    /// Swaps reference and adjusted, for this and later zones. The zone stays;
    /// the learned rate and this zone's rounds belong to the other belt, so
    /// they clear. At CALIBRATE it changes which belt the turn moves, and is
    /// refused (false, nothing changes) once a calibration step is in: every
    /// step is measured against round 0 on the same belt.
    bool swap_adjusted_path();
    [[nodiscard]] bool can_swap_adjusted_path() const;
    /// Signed, adjusted - reference. ALREADY_MATCHED and CALIBRATE: round 0's
    /// strongest candidate's centroid gap.
    [[nodiscard]] float gap_hz() const;
    /// CALIBRATE: kCalibrationTurns (SCREW) of the adjusted path, toward the
    /// other: tighten the looser belt, or loosen the tighter one after a swap.
    /// TUNE: from the gap and the learned rate. UNREPEATABLE: no amount and
    /// no re-seat; the direction is not advice. Meaningless at
    /// ALREADY_MATCHED, BASELINE and NO_ZONE.
    [[nodiscard]] BeltTuneAdvice advice() const;
    /// The stepper's value for the next round; SCREW only. A SCREW round with
    /// nothing turned since the last measured round (no recorded turn, or a
    /// recorded zero, and none carried from a NO_PEAK, OUT_OF_WINDOW or
    /// IMPLAUSIBLE round) is a no-turn round: it adopts the reading, teaches
    /// no rate and reads NO_MOVEMENT unless MATCHED, NO_PEAK, OUT_OF_WINDOW or
    /// IMPLAUSIBLE.
    void record_turns(float turns);
    /// The full sweep of both paths after a calibration turn. Each candidate's
    /// centroid gap is compared with round 0's, so a second step measures both
    /// turns together. Outside CALIBRATE it changes nothing and returns a
    /// result with registered false.
    BeltCalibrationResult add_calibration_sweep(const BeltResponse& a_full,
                                                const BeltResponse& b_full);
    /// Both paths swept over sweep_window().
    const BeltTuneRound& add_round(const BeltResponse& a_swept, const BeltResponse& b_swept);
    [[nodiscard]] bool zone_matched() const;
    /// Marks this zone matched and moves to the next candidate the calibration
    /// registered (every candidate, for SPRING), keeping the adjusted path.
    /// The new zone needs a baseline pair. False, changing nothing, when none
    /// remains.
    bool next_zone();
    [[nodiscard]] bool has_next_zone() const;
    /// The zone next_zone() would move to, seen from PATH_A; nullopt when none.
    [[nodiscard]] std::optional<BeltTuneZone> peek_next_zone() const;
    /// stage() == BASELINE.
    [[nodiscard]] bool needs_baseline() const;
    [[nodiscard]] std::optional<float> hz_per_turn() const;
    /// tallest_peak_unpaired() for round 0, or this zone has stalled.
    [[nodiscard]] bool show_mechanical_hint() const;
    /// zone_power_hint() over the zone for the latest curves measured across
    /// it; nullopt without a zone.
    [[nodiscard]] std::optional<BeltPowerHint> power_hint() const;
    [[nodiscard]] const std::vector<BeltTuneRound>& rounds() const; ///< current zone

    [[nodiscard]] BeltTensionerKind tensioner() const;
    [[nodiscard]] float tolerance_hz() const; ///< the matched band, +- around zero
    [[nodiscard]] float reference_hz() const; ///< latest measured reference centroid
    [[nodiscard]] float adjusted_hz() const;
    [[nodiscard]] std::optional<float> previous_gap_hz() const; ///< hollow gauge needle
    /// Signed sum, tighten positive, of the turns recorded while `path` was
    /// the adjusted one, calibration turns included. SCREW only.
    [[nodiscard]] float total_turns(BeltPath path) const;
    /// Tuning rounds across zones; calibration sweeps and baseline rounds excluded.
    [[nodiscard]] int total_rounds() const;

  private:
    /// One path's reading of the zone in one round.
    struct ZoneReading {
        float centroid_hz = 0.0f;
        float power = 0.0f;
    };

    /// The stage an unmatched session starts in: CALIBRATE, or the strongest
    /// candidate at BASELINE for SPRING.
    void start_unmatched();
    /// Enters the candidate, adjusting `path`, with its centroids read from
    /// the latest full sweep.
    void enter_zone(const BeltZoneCandidate& candidate, BeltPath path);
    void clear_belt_history();
    /// SCREW: `turns` (signed, tighten positive) onto the adjusted belt's
    /// total, and their magnitude onto the turns the next reading measures.
    void book_turns(float turns);
    /// ref_hz_ / adj_hz_ from both paths' centroids over the zone; false when
    /// either has none.
    bool read_centroids(const BeltCurve& a, const BeltCurve& b, BeltSweepWindow zone);

    BeltResponse a0_;
    BeltResponse b0_;
    /// The latest full sweep: round 0, then each calibration sweep.
    BeltResponse a_full_;
    BeltResponse b_full_;
    float band_min_hz_;
    float band_max_hz_;
    BeltTensionerKind kind_;
    float tolerance_hz_;
    bool shape_warning_;

    BeltTuneStage stage_ = BeltTuneStage::NO_ZONE;
    std::vector<BeltZoneCandidate> candidates_;
    /// Candidates the calibration registered, best move first (all of them for SPRING).
    std::vector<BeltZoneCandidate> registered_;
    size_t zone_index_ = 0; ///< into registered_
    std::optional<BeltTuneZone> zone_;
    int calibration_steps_ = 0;

    BeltPath adjusted_ = BeltPath::PATH_A;
    bool window_widened_ = false; ///< OUT_OF_WINDOW since the last measured round
    float ref_hz_ = 0.0f;
    float adj_hz_ = 0.0f;
    std::optional<float> previous_gap_hz_;
    /// The baseline pair's first round, per path (A, B).
    std::optional<std::pair<ZoneReading, ZoneReading>> baseline_first_;
    std::vector<BeltTuneRound> rounds_;
    int total_rounds_ = 0;
    /// The latest in-plane curves measured across the zone, for power_hint().
    BeltCurve last_a_;
    BeltCurve last_b_;

    std::optional<float> pending_turns_;
    /// Turns since the last measured round, as magnitudes. Only a measured
    /// round can flip the advised direction, so they all went one way.
    float unmeasured_turns_ = 0.0f;
    std::vector<float> rate_samples_; ///< |gap move| / turns, Hz per turn
    float total_turns_a_ = 0.0f;
    float total_turns_b_ = 0.0f;
};

} // namespace helix::calibration
