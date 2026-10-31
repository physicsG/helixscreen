// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thermal_rate_model.h"

#include "config.h"
#include "printer_detector.h"
#include "spdlog/spdlog.h"

#include <algorithm>

void ThermalRateModel::record_sample(float temp_c, uint32_t tick_ms) {
    // Initialize timing on first sample
    if (start_tick_ == 0) {
        start_tick_ = tick_ms;
        last_tick_ = tick_ms;
        last_temp_ = temp_c;
        high_temp_ = temp_c;
        last_rise_tick_ = tick_ms;
        holding_ = false;
        return;
    }

    // With a target reported, only a step that starts under a positive target
    // is the heater heating; a heater warming beside its neighbour at target
    // 0, or above its target, would add its seconds to the climb for good.
    if (target_known_ && !(target_c_ > 0.0f && last_temp_ < target_c_)) {
        last_temp_ = temp_c;
        last_tick_ = tick_ms;
        high_temp_ = temp_c;
        last_rise_tick_ = tick_ms;
        holding_ = false;
        return;
    }

    if (temp_c > high_temp_ + kRiseEpsilonC) {
        high_temp_ = temp_c;
        last_rise_tick_ = tick_ms;
        holding_ = false;
    } else if (holding_ ||
               (tick_ms > last_rise_tick_ && tick_ms - last_rise_tick_ >= kStallReanchorMs)) {
        // Holding, not climbing: the next step starts from this reading.
        holding_ = true;
        last_temp_ = temp_c;
        last_tick_ = tick_ms;
        high_temp_ = std::min(high_temp_, temp_c + kRiseEpsilonC);
        return;
    }

    // The step that arrives counts up to the target, its slowest degrees
    // included; an overshoot past it is not heating toward it.
    const float counted_temp = target_known_ ? std::min(temp_c, target_c_) : temp_c;
    float delta_from_last = counted_temp - last_temp_;

    if (delta_from_last >= kMinDeltaFromLast && tick_ms > last_tick_) {
        const float step_s = static_cast<float>(tick_ms - last_tick_) / 1000.0f;
        climb_seconds_ += step_s;
        climb_degrees_ += delta_from_last;
        const float inst_rate = step_s / delta_from_last;

        if (has_measured_heat_rate_) {
            // EMA: blend new instantaneous rate with running estimate
            measured_heat_rate_ = kEmaNewWeight * inst_rate + kEmaOldWeight * measured_heat_rate_;
        } else if (climb_degrees_ >= kMinTotalMovement) {
            // First usable measurement — seed from the climb so far
            measured_heat_rate_ = climb_seconds_ / climb_degrees_;
            has_measured_heat_rate_ = true;
        }

        // Update last sample point when we have a significant delta
        last_temp_ = temp_c;
        last_tick_ = tick_ms;
    }
}

void ThermalRateModel::note_target(float target_c, float temp_c, uint32_t tick_ms) {
    const bool raised = target_c > target_c_ + kRiseEpsilonC;
    target_c_ = target_c;
    target_known_ = true;
    if (!raised || start_tick_ == 0) {
        return;
    }
    last_temp_ = temp_c;
    last_tick_ = tick_ms;
    high_temp_ = temp_c;
    last_rise_tick_ = tick_ms;
    holding_ = false;
}

float ThermalRateModel::estimate_seconds(float current, float target) const {
    if (current >= target)
        return 0.0f;

    float remaining_degrees = target - current;
    return std::max(0.0f, remaining_degrees * climb_rate_for(remaining_degrees));
}

std::optional<float> ThermalRateModel::measured_rate() const {
    if (!has_measured_heat_rate_)
        return std::nullopt;
    return measured_heat_rate_;
}

float ThermalRateModel::best_rate() const {
    if (has_measured_heat_rate_)
        return measured_heat_rate_;
    if (has_history_ && hist_heat_rate_ > 0)
        return hist_heat_rate_;
    return default_rate_;
}

float ThermalRateModel::climb_rate_for(float span_c) const {
    const float measured = climb_rate();
    if (!has_measured_heat_rate_ || climb_degrees_ <= 0.0f || span_c <= 0.0f) {
        return measured;
    }
    float known = 0.0f;
    if (has_history_ && hist_heat_rate_ > 0) {
        known = hist_heat_rate_;
    } else if (default_measured_) {
        known = default_rate_;
    }
    if (known <= 0.0f) {
        return measured;
    }
    const float share = std::min(climb_degrees_ / span_c, 1.0f);
    return share * measured + (1.0f - share) * known;
}

float ThermalRateModel::climb_rate() const {
    if (has_measured_heat_rate_ && climb_degrees_ > 0.0f)
        return climb_seconds_ / climb_degrees_;
    if (has_history_ && hist_heat_rate_ > 0)
        return hist_heat_rate_;
    return default_rate_;
}

void ThermalRateModel::load_history(float rate_s_per_deg) {
    hist_heat_rate_ = rate_s_per_deg;
    has_history_ = true;
}

float ThermalRateModel::blended_rate_for_save() const {
    if (!has_measured_heat_rate_ || climb_degrees_ <= 0.0f)
        return 0.0f;
    const float measured = climb_rate();
    if (has_history_ && hist_heat_rate_ > 0)
        return kSaveNewWeight * measured + kSaveOldWeight * hist_heat_rate_;
    return measured;
}

void ThermalRateModel::set_default_rate(float rate_s_per_deg, bool measured) {
    default_rate_ = rate_s_per_deg;
    default_measured_ = measured;
}

void ThermalRateModel::reset(float start_temp) {
    measured_heat_rate_ = 0.0f;
    has_measured_heat_rate_ = false;
    last_temp_ = start_temp;
    last_tick_ = 0;
    start_tick_ = 0;
    high_temp_ = start_temp;
    last_rise_tick_ = 0;
    holding_ = false;
    climb_seconds_ = 0.0f;
    climb_degrees_ = 0.0f;
    target_c_ = 0.0f;
    target_known_ = false;
}

// --- ThermalRateManager ---

ThermalRateManager& ThermalRateManager::instance() {
    static ThermalRateManager inst;
    return inst;
}

ThermalRateModel& ThermalRateManager::get_model(const std::string& heater_name) {
    return models_[heater_name];
}

float ThermalRateManager::estimate_heating_seconds(const std::string& heater_name,
                                                   float current_temp, float target_temp) const {
    auto it = models_.find(heater_name);
    if (it != models_.end()) {
        return it->second.estimate_seconds(current_temp, target_temp);
    }
    // No model exists yet — use a temporary with default rate
    ThermalRateModel tmp;
    return tmp.estimate_seconds(current_temp, target_temp);
}

void ThermalRateManager::load_from_config(helix::Config& config) {
    for (const char* heater : kPersistedHeaters) {
        std::string path = std::string("/thermal/rates/") + heater + "/heat_rate";
        float rate = config.get<float>(path, 0.0f);
        if (rate > 0.0f) {
            models_[heater].load_history(rate);
            spdlog::info("thermal: loaded {} rate {:.3f} s/°C from config", heater, rate);
        }
    }
}

void ThermalRateManager::save_to_config(helix::Config& config) {
    for (auto& [name, model] : models_) {
        float rate = model.blended_rate_for_save();
        if (rate > 0.0f) {
            std::string path = "/thermal/rates/" + name + "/heat_rate";
            config.set<float>(path, rate);
            spdlog::info("thermal: saved {} rate {:.3f} s/°C to config", name, rate);
        }
    }
    config.save();
}

void ThermalRateManager::apply_archetype_defaults(float bed_x_max,
                                                  const std::string& printer_type) {
    // Extruder: most hotends heat at 0.2-0.3 s/°C (e.g. AD5M does 183°C in ~40s)
    models_["extruder"].set_default_rate(0.25f);

    // Bed: size-dependent defaults (larger beds heat slower)
    float bed_rate;
    if (bed_x_max >= 350.0f) {
        bed_rate = 2.0f; // Large beds (350mm+)
    } else if (bed_x_max >= 250.0f) {
        bed_rate = 1.5f; // Medium beds (250-350mm)
    } else if (bed_x_max > 0.0f) {
        bed_rate = 1.0f; // Small beds (<250mm, e.g. AD5M ~235mm)
    } else {
        // Unknown size
        bed_rate = 1.2f;
    }
    models_["heater_bed"].set_default_rate(bed_rate);

    spdlog::info("thermal: archetype defaults — extruder=0.25, bed={:.1f} (bed_x_max={:.0f})",
                 bed_rate, bed_x_max);

    for (const auto& [heater, rate] : PrinterDetector::get_thermal_rates(printer_type)) {
        models_[heater].set_default_rate(rate, /*measured=*/true);
        spdlog::info("thermal: {} default {:.2f} s/°C from the database entry for '{}'", heater,
                     rate, printer_type);
    }
}

void ThermalRateManager::reset() {
    models_.clear();
}
