// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file belt_tune_session.cpp
 * @brief Zone candidates, in-plane zone analysis, advice and round outcomes for
 *        the guided belt-tuning loop
 */

#include "belt_tune_session.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace helix::calibration {

using namespace belt_tune;

namespace {

/// The zone two peaks define, seen from the adjusted path.
BeltTuneZone make_zone(float a_hz, float b_hz, BeltZoneSource source, BeltPath adjusted,
                       float band_min_hz, float band_max_hz) {
    BeltTuneZone z;
    z.source = source;
    z.adj_hz = adjusted == BeltPath::PATH_A ? a_hz : b_hz;
    z.ref_hz = adjusted == BeltPath::PATH_A ? b_hz : a_hz;
    const float mid = (a_hz + b_hz) / 2.0f;
    const float half = std::max(kZoneHalfMinHz, std::abs(a_hz - b_hz) / 2.0f + kZoneMarginHz);
    z.window.freq_start_hz = std::max(band_min_hz, mid - half);
    z.window.freq_end_hz = std::min(band_max_hz, mid + half);
    return z;
}

bool near_matched(float centre_hz, const std::vector<float>& matched_centres_hz) {
    return std::any_of(matched_centres_hz.begin(), matched_centres_hz.end(),
                       [centre_hz](float m) { return std::abs(centre_hz - m) <= kZoneHalfMinHz; });
}

/// One path's tallest peak, and whether it belongs to a pair.
struct Tallest {
    std::optional<BeltPeak> peak;
    bool paired = false;

    void offer(const BeltPeak& p, bool is_paired) {
        if (!peak || p.amplitude > peak->amplitude) {
            peak = p;
            paired = is_paired;
        }
    }
};

/// Each path's peaks, tallest peak and the zone amplitude floor.
struct PeakCensus {
    std::vector<BeltPeak> a;
    std::vector<BeltPeak> b;
    Tallest tallest_a;
    Tallest tallest_b;
    float floor = 0.0f;

    explicit PeakCensus(const BeltComparison& cmp) {
        const BeltPeakPairing& pk = cmp.peaks;
        for (const auto& pair : pk.pairs) {
            a.push_back(pair.a);
            b.push_back(pair.b);
            tallest_a.offer(pair.a, true);
            tallest_b.offer(pair.b, true);
        }
        for (const auto& p : pk.unpaired_a) {
            a.push_back(p);
            tallest_a.offer(p, false);
        }
        for (const auto& p : pk.unpaired_b) {
            b.push_back(p);
            tallest_b.offer(p, false);
        }
        // A comparison built without curves carries no band maximum; its
        // tallest peak stands in.
        const float top =
            std::max({cmp.band_max_psd, tallest_a.peak ? tallest_a.peak->amplitude : 0.0f,
                      tallest_b.peak ? tallest_b.peak->amplitude : 0.0f});
        floor = kZoneMinAmplitudeFraction * top;
    }

    [[nodiscard]] bool unpaired_above_floor(const Tallest& t) const {
        return t.peak && !t.paired && t.peak->amplitude >= floor;
    }

    /// The taller of the two tallest peaks that are unpaired and above the
    /// floor, with the path it belongs to.
    [[nodiscard]] std::optional<std::pair<BeltPeak, BeltPath>> unpaired_tallest() const {
        const bool ua = unpaired_above_floor(tallest_a);
        const bool ub = unpaired_above_floor(tallest_b);
        if (ua && (!ub || tallest_a.peak->amplitude >= tallest_b.peak->amplitude)) {
            return std::make_pair(*tallest_a.peak, BeltPath::PATH_A);
        }
        if (ub) {
            return std::make_pair(*tallest_b.peak, BeltPath::PATH_B);
        }
        return std::nullopt;
    }
};

/// The frequency of the window's edge bin when it is the window's maximum and
/// rises above its inward neighbour: the peak sits at or past the edge.
/// `window` holds only the in-window bins.
std::optional<float> edge_maximum(const BeltCurve& window) {
    if (window.size() < 2) {
        return std::nullopt;
    }
    const auto top =
        std::max_element(window.begin(), window.end(),
                         [](const auto& l, const auto& r) { return l.second < r.second; });
    if (top == window.begin() && window[0].second > window[1].second) {
        return window.front().first;
    }
    const size_t last = window.size() - 1;
    if (top == window.begin() + static_cast<std::ptrdiff_t>(last) &&
        window[last].second > window[last - 1].second) {
        return window.back().first;
    }
    return std::nullopt;
}

float max_psd(const BeltCurve& curve) {
    float top = 0.0f;
    for (const auto& bin : curve) {
        top = std::max(top, bin.second);
    }
    return top;
}

/// The value of the curve's bin nearest freq_hz; nullopt for an empty curve.
std::optional<float> nearest_bin_value(const BeltCurve& curve, float freq_hz) {
    if (curve.empty()) {
        return std::nullopt;
    }
    const auto it =
        std::min_element(curve.begin(), curve.end(), [freq_hz](const auto& l, const auto& r) {
            return std::abs(l.first - freq_hz) < std::abs(r.first - freq_hz);
        });
    return it->second;
}

} // namespace

// ============================================================================
// Free functions
// ============================================================================

bool tallest_peak_unpaired(const BeltComparison& cmp) {
    return cmp.valid && PeakCensus(cmp).unpaired_tallest().has_value();
}

const BeltCurve& tuning_curve(const BeltResponse& r) {
    return r.in_plane.empty() ? r.xyz : r.in_plane;
}

float in_plane_fraction(const BeltResponse& r, float freq_hz) {
    if (r.in_plane.empty()) {
        return 1.0f;
    }
    const auto xyz = nearest_bin_value(r.xyz, freq_hz);
    if (!xyz || *xyz <= 0.0f) {
        return 0.0f;
    }
    return nearest_bin_value(r.in_plane, freq_hz).value_or(0.0f) / *xyz;
}

std::vector<BeltZoneCandidate> tune_zone_candidates(const BeltResponse& a, const BeltResponse& b,
                                                    float band_min_hz, float band_max_hz) {
    const BeltComparison cmp =
        compare_belt_paths(tuning_curve(a), tuning_curve(b), band_min_hz, band_max_hz);
    if (!cmp.valid) {
        return {};
    }
    const PeakCensus census(cmp);
    const auto eligible = [&](const BeltResponse& r, const BeltPeak& p) {
        return p.amplitude >= census.floor &&
               in_plane_fraction(r, p.freq_hz) >= kInPlaneMinFraction;
    };

    std::vector<BeltZoneCandidate> found;
    const auto add = [&](const BeltPeak& pa, const BeltPeak& pb, BeltZoneSource source) {
        found.push_back(
            {make_zone(pa.freq_hz, pb.freq_hz, source, BeltPath::PATH_A, band_min_hz, band_max_hz),
             pa.amplitude + pb.amplitude});
    };
    for (const auto& pair : cmp.peaks.pairs) {
        if (eligible(a, pair.a) && eligible(b, pair.b)) {
            add(pair.a, pair.b, BeltZoneSource::PAIRED);
        }
    }

    // The taller of the two unpaired tallest peaks steers against the other
    // path's nearest eligible peak, but never across two unrelated modes.
    const BeltPeak* tallest = nullptr;
    BeltPath tallest_path = BeltPath::PATH_A;
    for (const auto& [t, path] : {std::make_pair(&census.tallest_a, BeltPath::PATH_A),
                                  std::make_pair(&census.tallest_b, BeltPath::PATH_B)}) {
        const BeltResponse& r = path == BeltPath::PATH_A ? a : b;
        if (t->peak && !t->paired && eligible(r, *t->peak) &&
            (!tallest || t->peak->amplitude > tallest->amplitude)) {
            tallest = &*t->peak;
            tallest_path = path;
        }
    }
    if (tallest) {
        const bool on_a = tallest_path == BeltPath::PATH_A;
        const BeltPeak* partner = nullptr;
        for (const auto& o : on_a ? census.b : census.a) {
            const float d = std::abs(o.freq_hz - tallest->freq_hz);
            if (d <= kZoneFallbackMaxHz && eligible(on_a ? b : a, o) &&
                (!partner || d < std::abs(partner->freq_hz - tallest->freq_hz))) {
                partner = &o;
            }
        }
        if (partner) {
            add(on_a ? *tallest : *partner, on_a ? *partner : *tallest,
                BeltZoneSource::TALLEST_UNPAIRED);
        }
    }

    std::stable_sort(found.begin(), found.end(),
                     [](const BeltZoneCandidate& l, const BeltZoneCandidate& r) {
                         return l.strength > r.strength;
                     });
    std::vector<BeltZoneCandidate> out;
    std::vector<float> centres;
    for (const auto& c : found) {
        const float centre = (c.zone.adj_hz + c.zone.ref_hz) / 2.0f;
        if (near_matched(centre, centres)) {
            continue;
        }
        centres.push_back(centre);
        out.push_back(c);
        if (out.size() == static_cast<size_t>(kMaxZoneCandidates)) {
            break;
        }
    }
    return out;
}

BeltSweepWindow zone_sweep_window(BeltSweepWindow zone, float band_min_hz, float band_max_hz) {
    BeltSweepWindow w;
    w.freq_start_hz = std::ceil(std::max(band_min_hz, zone.freq_start_hz - kZoneSweepMarginHz));
    w.freq_end_hz = std::floor(std::min(band_max_hz, zone.freq_end_hz + kZoneSweepTopMarginHz));
    return w;
}

std::optional<float> zone_centroid_hz(const BeltCurve& curve, BeltSweepWindow zone) {
    const BeltCurve x = curve_in_band(curve, zone.freq_start_hz, zone.freq_end_hz);
    if (x.size() < 3) {
        return std::nullopt;
    }
    double power = 0.0;
    double moment = 0.0;
    for (const auto& [f, v] : x) {
        power += v;
        moment += static_cast<double>(f) * v;
    }
    if (power <= 0.0) {
        return std::nullopt;
    }
    return static_cast<float>(moment / power);
}

float zone_power(const BeltCurve& curve, BeltSweepWindow zone) {
    double sum = 0.0;
    for (const auto& bin : curve_in_band(curve, zone.freq_start_hz, zone.freq_end_hz)) {
        sum += bin.second;
    }
    return static_cast<float>(sum);
}

std::optional<BeltPowerHint> zone_power_hint(const BeltCurve& a, const BeltCurve& b,
                                             BeltSweepWindow zone) {
    const float pa = zone_power(a, zone);
    const float pb = zone_power(b, zone);
    if (pa <= 0.0f || pb <= 0.0f) {
        return std::nullopt;
    }
    BeltPowerHint h;
    h.stronger = pa >= pb ? BeltPath::PATH_A : BeltPath::PATH_B;
    h.ratio = pa >= pb ? pa / pb : pb / pa;
    if (h.ratio <= kPowerHintRatio) {
        return std::nullopt;
    }
    h.freq_hz = zone_centroid_hz(h.stronger == BeltPath::PATH_A ? a : b, zone)
                    .value_or((zone.freq_start_hz + zone.freq_end_hz) / 2.0f);
    return h;
}

std::optional<float> unexcited_floor_psd(const BeltCurve& curve, BeltSweepWindow swept,
                                         float band_min_hz, float band_max_hz) {
    std::vector<float> outside;
    for (const auto& bin : curve_in_band(curve, band_min_hz, band_max_hz)) {
        if (bin.first < swept.freq_start_hz || bin.first > swept.freq_end_hz) {
            outside.push_back(bin.second);
        }
    }
    if (outside.size() < static_cast<size_t>(kUnexcitedMinBins)) {
        return std::nullopt;
    }
    std::sort(outside.begin(), outside.end());
    const size_t mid = outside.size() / 2;
    return outside.size() % 2 == 1 ? outside[mid] : (outside[mid - 1] + outside[mid]) / 2.0f;
}

bool window_holds_peak(const BeltCurve& curve, BeltSweepWindow zone, BeltSweepWindow swept,
                       float band_min_hz, float band_max_hz) {
    const BeltCurve x = curve_in_band(curve, zone.freq_start_hz, zone.freq_end_hz);
    if (x.empty()) {
        return false;
    }
    const float top = max_psd(x);
    const float base =
        unexcited_floor_psd(curve, swept, band_min_hz, band_max_hz)
            .value_or(std::min_element(x.begin(), x.end(), [](const auto& l, const auto& r) {
                          return l.second < r.second;
                      })->second);
    return top > 0.0f && top >= kPeakMinProminence * base;
}

float matched_tolerance_hz(const BeltCurve& curve) {
    if (curve.size() < 2) {
        return kMatchedMinHz;
    }
    const float bin_hz =
        (curve.back().first - curve.front().first) / static_cast<float>(curve.size() - 1);
    return std::max(kMatchedMinHz, bin_hz);
}

// ============================================================================
// BeltTuneSession
// ============================================================================

namespace {

/// A SCREW amount to the nearest kTurnStep, within [min_turns, kTurnMax].
float clamp_turns(float turns, float min_turns) {
    return std::clamp(std::round(turns / kTurnStep) * kTurnStep, min_turns, kTurnMax);
}

} // namespace

BeltTuneSession::BeltTuneSession(const BeltResponse& a, const BeltResponse& b,
                                 const BeltComparison& round0, float band_min_hz, float band_max_hz,
                                 BeltTensionerKind kind)
    : a0_(a), b0_(b), a_full_(a), b_full_(b), band_min_hz_(band_min_hz), band_max_hz_(band_max_hz),
      kind_(kind),
      tolerance_hz_(std::max(matched_tolerance_hz(a.xyz), matched_tolerance_hz(b.xyz))),
      shape_warning_(tallest_peak_unpaired(round0)),
      candidates_(tune_zone_candidates(a, b, band_min_hz, band_max_hz)) {
    if (candidates_.empty()) {
        stage_ = BeltTuneStage::NO_ZONE;
        spdlog::debug("[BeltTune] No zone candidates");
        return;
    }
    // The looser path by the strongest candidate's centroids is the one a
    // calibration turn tightens.
    const BeltTuneZone& strongest = candidates_.front().zone;
    float a_hz = strongest.adj_hz;
    float b_hz = strongest.ref_hz;
    const auto ca = zone_centroid_hz(tuning_curve(a), strongest.window);
    const auto cb = zone_centroid_hz(tuning_curve(b), strongest.window);
    if (ca && cb) {
        a_hz = *ca;
        b_hz = *cb;
    }
    adjusted_ = a_hz <= b_hz ? BeltPath::PATH_A : BeltPath::PATH_B;
    adj_hz_ = adjusted_ == BeltPath::PATH_A ? a_hz : b_hz;
    ref_hz_ = adjusted_ == BeltPath::PATH_A ? b_hz : a_hz;
    spdlog::debug("[BeltTune] {} candidates; strongest {:.1f}-{:.1f} Hz, A {:.2f} B {:.2f} Hz",
                  candidates_.size(), strongest.window.freq_start_hz, strongest.window.freq_end_hz,
                  a_hz, b_hz);

    if (std::abs(gap_hz()) <= std::max(tolerance_hz_, kCalibrationMinMoveHz)) {
        stage_ = BeltTuneStage::ALREADY_MATCHED;
        return;
    }
    start_unmatched();
}

void BeltTuneSession::start_unmatched() {
    if (kind_ != BeltTensionerKind::SPRING) {
        stage_ = BeltTuneStage::CALIBRATE;
        return;
    }
    registered_ = candidates_;
    zone_index_ = 0;
    enter_zone(registered_.front(), adjusted_);
}

void BeltTuneSession::enter_zone(const BeltZoneCandidate& candidate, BeltPath path) {
    if (path != adjusted_) {
        clear_belt_history();
    }
    adjusted_ = path;
    zone_ = candidate.zone;
    if (path == BeltPath::PATH_B) {
        std::swap(zone_->adj_hz, zone_->ref_hz);
    }
    const BeltCurve& a = tuning_curve(a_full_);
    const BeltCurve& b = tuning_curve(b_full_);
    if (!read_centroids(a, b, zone_->window)) {
        adj_hz_ = zone_->adj_hz;
        ref_hz_ = zone_->ref_hz;
    }
    last_a_ = a;
    last_b_ = b;
    stage_ = BeltTuneStage::BASELINE;
    baseline_first_.reset();
    previous_gap_hz_.reset();
    window_widened_ = false;
    rounds_.clear();
    spdlog::debug("[BeltTune] Zone {:.1f}-{:.1f} Hz ({}), adjusting {}: {:.2f} vs {:.2f} Hz",
                  zone_->window.freq_start_hz, zone_->window.freq_end_hz,
                  zone_->source == BeltZoneSource::PAIRED ? "paired" : "tallest unpaired",
                  adjusted_ == BeltPath::PATH_A ? "A" : "B", adj_hz_, ref_hz_);
}

bool BeltTuneSession::read_centroids(const BeltCurve& a, const BeltCurve& b, BeltSweepWindow zone) {
    const auto ca = zone_centroid_hz(a, zone);
    const auto cb = zone_centroid_hz(b, zone);
    if (!ca || !cb) {
        return false;
    }
    adj_hz_ = adjusted_ == BeltPath::PATH_A ? *ca : *cb;
    ref_hz_ = adjusted_ == BeltPath::PATH_A ? *cb : *ca;
    return true;
}

void BeltTuneSession::clear_belt_history() {
    rate_samples_.clear();
    unmeasured_turns_ = 0.0f;
    pending_turns_.reset();
}

void BeltTuneSession::book_turns(float turns) {
    if (kind_ != BeltTensionerKind::SCREW) {
        return;
    }
    (adjusted_ == BeltPath::PATH_A ? total_turns_a_ : total_turns_b_) += turns;
    unmeasured_turns_ += std::abs(turns);
}

BeltTuneStage BeltTuneSession::stage() const {
    return stage_;
}

const std::vector<BeltZoneCandidate>& BeltTuneSession::candidates() const {
    return candidates_;
}

void BeltTuneSession::calibrate_anyway() {
    if (stage_ == BeltTuneStage::ALREADY_MATCHED) {
        start_unmatched();
    }
}

const std::optional<BeltTuneZone>& BeltTuneSession::zone() const {
    return zone_;
}

std::optional<BeltSweepWindow> BeltTuneSession::sweep_window() const {
    if (!zone_) {
        return std::nullopt;
    }
    return zone_sweep_window(zone_->window, band_min_hz_, band_max_hz_);
}

BeltPath BeltTuneSession::adjusted_path() const {
    return adjusted_;
}

bool BeltTuneSession::can_swap_adjusted_path() const {
    return stage_ != BeltTuneStage::CALIBRATE || calibration_steps_ == 0;
}

bool BeltTuneSession::swap_adjusted_path() {
    if (!can_swap_adjusted_path()) {
        return false;
    }
    adjusted_ = adjusted_ == BeltPath::PATH_A ? BeltPath::PATH_B : BeltPath::PATH_A;
    if (zone_) {
        std::swap(zone_->adj_hz, zone_->ref_hz);
    }
    std::swap(adj_hz_, ref_hz_);
    clear_belt_history();
    previous_gap_hz_.reset();
    rounds_.clear();
    return true;
}

float BeltTuneSession::gap_hz() const {
    return adj_hz_ - ref_hz_;
}

BeltTuneAdvice BeltTuneSession::advice() const {
    BeltTuneAdvice out;
    out.path = adjusted_;
    if (stage_ == BeltTuneStage::CALIBRATE) {
        // Toward the other belt: the looser one tightens, the tighter one loosens.
        out.direction = gap_hz() > 0.0f ? BeltTurnDirection::LOOSEN : BeltTurnDirection::TIGHTEN;
        if (kind_ == BeltTensionerKind::SCREW) {
            out.turns = kCalibrationTurns;
        }
        return out;
    }
    out.direction = gap_hz() > 0.0f ? BeltTurnDirection::LOOSEN : BeltTurnDirection::TIGHTEN;
    if (stage_ == BeltTuneStage::UNREPEATABLE) {
        return out;
    }
    out.spring_reseat = kind_ == BeltTensionerKind::SPRING;
    if (kind_ != BeltTensionerKind::SCREW) {
        return out;
    }
    const float gap = std::abs(gap_hz());
    const float min_turns = gap < kNearMatchFactor * tolerance_hz_ ? kTurnMinNear : kTurnMin;
    if (const auto rate = hz_per_turn()) {
        out.turns = clamp_turns(gap / *rate, min_turns);
        return out;
    }
    // Nothing learned yet. If the last turn did nothing, try a bigger one;
    // repeat checks in between change nothing.
    out.turns = kFirstTurn;
    for (auto it = rounds_.rbegin(); it != rounds_.rend(); ++it) {
        if (it->no_turn) {
            continue;
        }
        if (it->outcome == BeltTuneOutcome::NO_MOVEMENT && it->turns_measured > 0.0f) {
            out.turns = clamp_turns(2.0f * it->turns_measured, min_turns);
        }
        break;
    }
    return out;
}

void BeltTuneSession::record_turns(float turns) {
    if (kind_ == BeltTensionerKind::SCREW) {
        pending_turns_ = std::abs(turns);
    }
}

BeltCalibrationResult BeltTuneSession::add_calibration_sweep(const BeltResponse& a_full,
                                                             const BeltResponse& b_full) {
    BeltCalibrationResult out;
    if (stage_ != BeltTuneStage::CALIBRATE) {
        return out;
    }
    const float advised = advice().direction == BeltTurnDirection::TIGHTEN ? 1.0f : -1.0f;
    book_turns(advised * kCalibrationTurns);
    ++calibration_steps_;
    out.turns = static_cast<float>(calibration_steps_) * kCalibrationTurns;

    // Each candidate's gap, adjusted - reference, now against round 0. A
    // tightening turn raises the adjusted belt's side of the gap and a
    // loosening one lowers it; a move that way is positive.
    const auto gap_over = [this](const BeltResponse& a, const BeltResponse& b,
                                 BeltSweepWindow w) -> std::optional<float> {
        const auto ca = zone_centroid_hz(tuning_curve(a), w);
        const auto cb = zone_centroid_hz(tuning_curve(b), w);
        if (!ca || !cb) {
            return std::nullopt;
        }
        return adjusted_ == BeltPath::PATH_A ? *ca - *cb : *cb - *ca;
    };
    std::vector<std::pair<float, const BeltZoneCandidate*>> moved;
    std::optional<float> best;
    for (const auto& c : candidates_) {
        const auto before = gap_over(a0_, b0_, c.zone.window);
        const auto now = gap_over(a_full, b_full, c.zone.window);
        if (!before || !now) {
            continue;
        }
        const float move = advised * (*now - *before);
        spdlog::debug("[BeltTune] Calibration step {}: zone {:.1f}-{:.1f} Hz moved {:+.2f} Hz",
                      calibration_steps_, c.zone.window.freq_start_hz, c.zone.window.freq_end_hz,
                      move);
        best = best ? std::max(*best, move) : move;
        if (move >= kCalibrationMinMoveHz) {
            moved.emplace_back(move, &c);
        }
    }
    out.move_hz = best.value_or(0.0f);

    if (moved.empty()) {
        if (calibration_steps_ >= kCalibrationMaxSteps) {
            stage_ = BeltTuneStage::NO_ZONE;
            out.gave_up = true;
        }
        return out;
    }
    std::stable_sort(moved.begin(), moved.end(),
                     [](const auto& l, const auto& r) { return l.first > r.first; });
    std::vector<BeltZoneCandidate> registered;
    for (const auto& m : moved) {
        registered.push_back(*m.second);
    }
    registered_ = std::move(registered);
    zone_index_ = 0;
    a_full_ = a_full;
    b_full_ = b_full;
    enter_zone(registered_.front(), adjusted_);
    if (kind_ == BeltTensionerKind::SCREW) {
        rate_samples_.push_back(moved.front().first / out.turns);
    }
    out.registered = true;
    return out;
}

const BeltTuneRound& BeltTuneSession::add_round(const BeltResponse& a_swept,
                                                const BeltResponse& b_swept) {
    const BeltTuneAdvice advised = advice();
    BeltTuneRound r;
    if (stage_ == BeltTuneStage::TUNE) {
        ++total_rounds_;
    }
    r.ref_hz = ref_hz_;
    r.adj_hz = adj_hz_;
    r.gap_hz = gap_hz();

    const auto finish = [&](BeltTuneOutcome outcome) -> const BeltTuneRound& {
        r.outcome = outcome;
        // A pair round that measured nothing breaks the pair: the next round opens a fresh one.
        if (outcome == BeltTuneOutcome::NO_PEAK && stage_ == BeltTuneStage::BASELINE) {
            baseline_first_.reset();
        }
        rounds_.push_back(r);
        spdlog::debug("[BeltTune] Round {}: ref {:.2f} adj {:.2f} gap {:+.2f} Hz, outcome {}",
                      total_rounds_, r.ref_hz, r.adj_hz, r.gap_hz, static_cast<int>(outcome));
        return rounds_.back();
    };

    const std::optional<float> pending = pending_turns_;
    pending_turns_.reset();
    if (!zone_ || stage_ == BeltTuneStage::NO_ZONE) {
        return finish(BeltTuneOutcome::NO_PEAK);
    }
    if (stage_ == BeltTuneStage::UNREPEATABLE) {
        // A Re-measure after a disagreement starts a fresh baseline pair.
        stage_ = BeltTuneStage::BASELINE;
        baseline_first_.reset();
    }
    const bool baseline = stage_ == BeltTuneStage::BASELINE;
    if (kind_ == BeltTensionerKind::SCREW) {
        // Only a recorded turn counts.
        if (pending && *pending > 0.0f) {
            r.turns_made = pending;
            book_turns(advised.direction == BeltTurnDirection::TIGHTEN ? *pending : -*pending);
        }
        // A repeatability check only when nothing was turned since the last
        // measured round: a turn whose round measured nothing still moved the belt.
        r.no_turn = unmeasured_turns_ <= 0.0f && !baseline;
    }

    const BeltSweepWindow window = zone_->window;
    const BeltSweepWindow swept = *sweep_window();
    const BeltCurve& a_curve = tuning_curve(a_swept);
    const BeltCurve& b_curve = tuning_curve(b_swept);
    const bool a_adjusted = adjusted_ == BeltPath::PATH_A;
    const BeltCurve& ref_curve = a_adjusted ? b_curve : a_curve;
    const BeltCurve& adj_curve = a_adjusted ? a_curve : b_curve;
    const bool ref_holds = window_holds_peak(ref_curve, window, swept, band_min_hz_, band_max_hz_);
    const bool adj_holds = window_holds_peak(adj_curve, window, swept, band_min_hz_, band_max_hz_);
    if (!ref_holds || !adj_holds) {
        r.reference_lost = !ref_holds && adj_holds;
        return finish(BeltTuneOutcome::NO_PEAK);
    }

    const auto ca = zone_centroid_hz(a_curve, window);
    const auto cb = zone_centroid_hz(b_curve, window);
    const auto gap_of = [a_adjusted](float a_hz, float b_hz) {
        return a_adjusted ? a_hz - b_hz : b_hz - a_hz;
    };
    const float turns_behind = unmeasured_turns_;
    const auto adopt = [&]() {
        window_widened_ = false;
        unmeasured_turns_ = 0.0f;
        ref_hz_ = a_adjusted ? *cb : *ca;
        adj_hz_ = a_adjusted ? *ca : *cb;
        last_a_ = a_curve;
        last_b_ = b_curve;
        r.ref_hz = ref_hz_;
        r.adj_hz = adj_hz_;
        r.gap_hz = gap_hz();
        r.turns_measured = turns_behind;
    };

    // The pair's second round answers repeatability before anything else: a
    // zone whose maximum hops to its edge between two untouched rounds does
    // not repeat either.
    if (baseline && baseline_first_ && ca && cb) {
        const auto& [first_a, first_b] = *baseline_first_;
        const auto power_ratio = [](float p1, float p2) {
            const float lo = std::min(p1, p2);
            return lo > 0.0f ? std::max(p1, p2) / lo : std::numeric_limits<float>::infinity();
        };
        const float first_gap = gap_of(first_a.centroid_hz, first_b.centroid_hz);
        const bool repeats =
            std::abs(gap_of(*ca, *cb) - first_gap) <= kRepeatMaxGapHz &&
            power_ratio(first_a.power, zone_power(a_curve, window)) <= kRepeatMaxPowerRatio &&
            power_ratio(first_b.power, zone_power(b_curve, window)) <= kRepeatMaxPowerRatio;
        if (!repeats) {
            spdlog::debug("[BeltTune] Baseline pair disagrees: gap {:+.2f} then {:+.2f} Hz",
                          first_gap, gap_of(*ca, *cb));
            adopt();
            baseline_first_.reset();
            previous_gap_hz_.reset();
            stage_ = BeltTuneStage::UNREPEATABLE;
            return finish(BeltTuneOutcome::UNREPEATABLE);
        }
    }

    // The reference was not turned, so its maximum on the zone edge means its
    // peak left the zone rather than that the zone should follow it.
    if (edge_maximum(curve_in_band(ref_curve, window.freq_start_hz, window.freq_end_hz))) {
        r.reference_lost = true;
        return finish(BeltTuneOutcome::NO_PEAK);
    }
    if (const auto edge =
            edge_maximum(curve_in_band(adj_curve, window.freq_start_hz, window.freq_end_hz))) {
        // The zone moves once per zone until a round is measured again: a
        // peak still past the edge is not chased to the band's end.
        if (window_widened_) {
            return finish(BeltTuneOutcome::NO_PEAK);
        }
        // Re-centre on the reference, reaching past the edge the adjusted
        // path's maximum ran into, so the next sweep covers it.
        const float centre = zone_centroid_hz(ref_curve, window).value_or(ref_hz_);
        const float half = std::max((window.freq_end_hz - window.freq_start_hz) / 2.0f,
                                    std::abs(*edge - centre) + kZoneMarginHz);
        zone_->window.freq_start_hz = std::max(band_min_hz_, centre - half);
        zone_->window.freq_end_hz = std::min(band_max_hz_, centre + half);
        window_widened_ = true;
        // A baseline round over the old zone compares with nothing over the new one.
        baseline_first_.reset();
        return finish(BeltTuneOutcome::OUT_OF_WINDOW);
    }

    if (!ca || !cb) {
        r.reference_lost = a_adjusted ? (!cb && ca) : (!ca && cb);
        return finish(BeltTuneOutcome::NO_PEAK);
    }
    const float prev_gap = gap_hz();
    const float gap = gap_of(*ca, *cb);
    // Only the adjusted belt was turned, so the gap's change is what the turn
    // did: drift that moves both paths together cancels out of it.
    const float move = gap - prev_gap;

    if (!baseline) {
        const auto rate = hz_per_turn();
        const float bound =
            rate ? std::max(kImplausibleMinHz, kImplausibleRateFactor * turns_behind * *rate)
                 : kImplausibleMinHz * std::max(1.0f, turns_behind);
        if (std::abs(move) > bound) {
            spdlog::debug("[BeltTune] Gap moved {:+.2f} Hz, past the {:.2f} Hz bound", move, bound);
            return finish(BeltTuneOutcome::IMPLAUSIBLE);
        }
    }

    adopt();
    if (baseline) {
        previous_gap_hz_.reset();
        if (!baseline_first_) {
            baseline_first_ = std::make_pair(ZoneReading{*ca, zone_power(a_curve, window)},
                                             ZoneReading{*cb, zone_power(b_curve, window)});
            return finish(BeltTuneOutcome::BASELINE);
        }
        // The pair repeated (checked above).
        baseline_first_.reset();
        stage_ = BeltTuneStage::TUNE;
        return finish(std::abs(gap) <= tolerance_hz_ ? BeltTuneOutcome::MATCHED
                                                     : BeltTuneOutcome::BASELINE);
    }

    previous_gap_hz_ = prev_gap;
    const float expected = advised.direction == BeltTurnDirection::TIGHTEN ? 1.0f : -1.0f;
    BeltTuneOutcome outcome;
    if (std::abs(gap) <= tolerance_hz_) {
        outcome = BeltTuneOutcome::MATCHED;
    } else if (r.no_turn) {
        // Nothing was turned, so however far the gap moved, it says how well a
        // reading repeats, not what a turn did.
        outcome = BeltTuneOutcome::NO_MOVEMENT;
    } else if (std::abs(move) < kMinMoveHz) {
        outcome = BeltTuneOutcome::NO_MOVEMENT;
    } else if (move * expected < 0.0f) {
        outcome = BeltTuneOutcome::WRONG_WAY;
    } else if (gap * prev_gap < 0.0f) {
        outcome = BeltTuneOutcome::OVERSHOT;
    } else {
        // The gap moved the advised way without crossing zero. From a gap of
        // exactly zero that is away from the match.
        outcome = std::abs(gap) < std::abs(prev_gap) ? BeltTuneOutcome::CLOSER
                                                     : BeltTuneOutcome::NO_MOVEMENT;
    }
    // A gap that moved against the advice says the turn went elsewhere (the
    // other belt, the other way), so it teaches nothing about this belt.
    if (kind_ == BeltTensionerKind::SCREW && !r.no_turn && outcome != BeltTuneOutcome::WRONG_WAY &&
        std::abs(move) >= kMinMoveHz && turns_behind > 0.0f) {
        rate_samples_.push_back(std::abs(move) / turns_behind);
    }
    return finish(outcome);
}

bool BeltTuneSession::zone_matched() const {
    return !rounds_.empty() && rounds_.back().outcome == BeltTuneOutcome::MATCHED;
}

bool BeltTuneSession::next_zone() {
    if (!has_next_zone()) {
        return false;
    }
    ++zone_index_;
    enter_zone(registered_[zone_index_], adjusted_);
    return true;
}

bool BeltTuneSession::has_next_zone() const {
    return peek_next_zone().has_value();
}

std::optional<BeltTuneZone> BeltTuneSession::peek_next_zone() const {
    if (!zone_ || zone_index_ + 1 >= registered_.size()) {
        return std::nullopt;
    }
    return registered_[zone_index_ + 1].zone;
}

bool BeltTuneSession::needs_baseline() const {
    return stage_ == BeltTuneStage::BASELINE;
}

std::optional<float> BeltTuneSession::hz_per_turn() const {
    if (rate_samples_.empty()) {
        return std::nullopt;
    }
    std::vector<float> sorted = rate_samples_;
    std::sort(sorted.begin(), sorted.end());
    const size_t mid = sorted.size() / 2;
    return sorted.size() % 2 == 1 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2.0f;
}

bool BeltTuneSession::show_mechanical_hint() const {
    if (shape_warning_) {
        return true;
    }
    // Baseline rounds, repeat checks and dropped readings judge no turn.
    const auto judges_a_turn = [](const BeltTuneRound& r) {
        return !r.no_turn && r.outcome != BeltTuneOutcome::IMPLAUSIBLE &&
               r.outcome != BeltTuneOutcome::BASELINE && r.outcome != BeltTuneOutcome::UNREPEATABLE;
    };
    if (kind_ == BeltTensionerKind::SPRING) {
        // A re-seat that does not improve the match points at the mechanics.
        int without_improvement = 0;
        for (auto it = rounds_.rbegin(); it != rounds_.rend(); ++it) {
            if (it->outcome == BeltTuneOutcome::CLOSER || it->outcome == BeltTuneOutcome::MATCHED ||
                it->outcome == BeltTuneOutcome::BASELINE ||
                it->outcome == BeltTuneOutcome::UNREPEATABLE) {
                break;
            }
            if (judges_a_turn(*it)) {
                ++without_improvement;
            }
        }
        return without_improvement >= kSpringStallRounds;
    }
    const auto unmatched = std::count_if(rounds_.begin(), rounds_.end(), [&](const auto& r) {
        return r.outcome != BeltTuneOutcome::MATCHED && judges_a_turn(r);
    });
    return unmatched >= kStallRounds;
}

std::optional<BeltPowerHint> BeltTuneSession::power_hint() const {
    if (!zone_) {
        return std::nullopt;
    }
    return zone_power_hint(last_a_, last_b_, zone_->window);
}

const std::vector<BeltTuneRound>& BeltTuneSession::rounds() const {
    return rounds_;
}

BeltTensionerKind BeltTuneSession::tensioner() const {
    return kind_;
}

float BeltTuneSession::tolerance_hz() const {
    return tolerance_hz_;
}

float BeltTuneSession::reference_hz() const {
    return ref_hz_;
}

float BeltTuneSession::adjusted_hz() const {
    return adj_hz_;
}

std::optional<float> BeltTuneSession::previous_gap_hz() const {
    return previous_gap_hz_;
}

float BeltTuneSession::total_turns(BeltPath path) const {
    return path == BeltPath::PATH_A ? total_turns_a_ : total_turns_b_;
}

int BeltTuneSession::total_rounds() const {
    return total_rounds_;
}

std::string format_belt_turns(float turns) {
    static constexpr const char* kQuarters[] = {"", "¼", "½", "¾"};
    const long quarters = std::lround(std::abs(turns) * 4.0f);
    const long whole = quarters / 4;
    const char* frac = kQuarters[quarters % 4];
    if (whole == 0) {
        return *frac ? frac : "0";
    }
    return std::to_string(whole) + frac;
}

} // namespace helix::calibration
