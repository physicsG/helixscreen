// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HELIX_ENABLE_SCREENSAVER

#include "ui_screensaver.h"

#include "config.h"
#include "display_settings_manager.h"
#include "helix_version.h"
#include "platform_capabilities.h"
#include "refresh_period_hold.h"
#include "screen_hide_hold.h"
#include "screensaver.h"
#include "screensaver_base.h"
#include "screensaver_bounce.h"
#include "screensaver_fireworks.h"
#include "screensaver_level_store.h"
#include "screensaver_pipes.h"
#include "screensaver_starfield.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace helix {

ScreensaverType screensaver_type_from_env(const std::string& value, ScreensaverType configured) {
    if (value == "toasters") {
        return ScreensaverType::FLYING_TOASTERS;
    }
    if (value == "starfield") {
        return ScreensaverType::STARFIELD;
    }
    if (value == "pipes") {
        return ScreensaverType::PIPES_3D;
    }
    if (value == "bounce") {
        return ScreensaverType::BOUNCING_PRINTER;
    }
    return (configured != ScreensaverType::OFF) ? configured : ScreensaverType::FLYING_TOASTERS;
}

} // namespace helix

using helix::ui::CpuSample;
using helix::ui::GateDecision;
using helix::ui::SaverLevelEntry;

ScreensaverManager& ScreensaverManager::instance() {
    static ScreensaverManager mgr;
    return mgr;
}

ScreensaverManager::ScreensaverManager() : cpu_clock_(helix::ui::read_process_cpu_clock) {
    screensavers_.push_back(std::make_unique<FlyingToasterScreensaver>());
    screensavers_.push_back(std::make_unique<PipesScreensaver>());
    screensavers_.push_back(std::make_unique<StarfieldScreensaver>());
    screensavers_.push_back(std::make_unique<helix::BouncingPrinterScreensaver>());
    screensavers_.push_back(std::make_unique<helix::ui::FireworksScreensaver>());

    for (const char* name : savers_missing_for_build_depth()) {
        spdlog::error("[ScreensaverManager] {} is listed as drawing at this colour depth but no "
                      "instance is registered; choosing it starts nothing",
                      name);
    }
}

std::vector<const char*> ScreensaverManager::savers_missing_for_build_depth() const {
    std::vector<const char*> missing;
    for (const helix::ui::ScreensaverInfo& row : helix::ui::SCREENSAVERS) {
        if ((row.depths & helix::ui::SAVER_BUILD_DEPTH) != 0U && find(row.type) == nullptr) {
            missing.push_back(row.name);
        }
    }
    return missing;
}

ScreensaverManager::~ScreensaverManager() = default;

void ScreensaverManager::set_host(helix::ui::SaverHost host) {
    host_ = std::move(host);
}

void ScreensaverManager::start(ScreensaverType type) {
    if (type == ScreensaverType::OFF) {
        stop();
        return;
    }

    const bool already_running =
        (active_ && active_->type() == type && active_->is_active()) || black_screen_type_ == type;
    if (already_running) {
        return;
    }

    // The screen hold stays out until the new saver has started or failed, so the panel is
    // never uncovered between the two.
    end_current();

    helix::ui::SaverBase* saver = find(type);
    const helix::ui::ScreensaverInfo* info = helix::ui::find_screensaver(type);
    if (!saver || !info) {
        spdlog::warn("[ScreensaverManager] No screensaver registered for type {}",
                     static_cast<int>(type));
        release_screen();
        release_refresh_period();
        return;
    }

    const StartPlan plan = plan_start(*saver, *info);
    if (plan.too_heavy) {
        spdlog::warn("[ScreensaverManager] {} is stored as too heavy for board {} on {}; showing "
                     "a black screen",
                     info->name, board_, version_);
        release_refresh_period();
        show_black_screen(type);
        hold_screen();
        return;
    }

    // The saver moves the held display refresh to its level's period as it starts.
    hold_refresh_period();
    saver->set_start_level(plan.level);
    saver->start();
    if (!saver->is_active()) {
        spdlog::warn("[ScreensaverManager] Screensaver type {} did not start",
                     static_cast<int>(type));
        release_screen();
        release_refresh_period();
        return;
    }

    active_ = saver;
    active_info_ = info;
    begin_gate(plan);
    hold_screen();
    spdlog::info("[ScreensaverManager] Started screensaver type {}", static_cast<int>(type));
}

void ScreensaverManager::stop() {
    end_current();
    // The idle stretch the next run subtracts starts now.
    baseline_.reset();
    sampled_ = false;
    release_screen();
    release_refresh_period();
}

void ScreensaverManager::end_current() {
    if (active_) {
        active_->stop();
        spdlog::info("[ScreensaverManager] Stopped screensaver type {}",
                     static_cast<int>(active_->type()));
        active_ = nullptr;
        active_info_ = nullptr;
    }
    if (black_screen_type_ != ScreensaverType::OFF) {
        black_screen_.destroy();
        spdlog::info("[ScreensaverManager] Stopped screensaver type {} (black screen)",
                     static_cast<int>(black_screen_type_));
        black_screen_type_ = ScreensaverType::OFF;
    }
    gate_enabled_ = false;
}

bool ScreensaverManager::is_active() const {
    return (active_ && active_->is_active()) || black_screen_type_ != ScreensaverType::OFF;
}

ScreensaverType ScreensaverManager::configured_type() {
    const int type_int = helix::DisplaySettingsManager::instance().get_screensaver_type();
    return static_cast<ScreensaverType>(helix::ui::clamp_screensaver_type(type_int));
}

helix::ui::SaverBase* ScreensaverManager::find(ScreensaverType type) const {
    for (const auto& saver : screensavers_) {
        if (saver->type() == type) {
            return saver.get();
        }
    }
    return nullptr;
}

helix::ui::BoardFacts
ScreensaverManager::board_facts(const helix::PlatformCapabilities& caps) const {
    lv_display_t* disp = lv_display_get_default();
    helix::ui::BoardFacts facts;
    facts.display_backend = host_.display_backend.empty() ? "unknown" : host_.display_backend;
    facts.cores = caps.cpu_cores;
    facts.bogomips = caps.bogomips;
    facts.width = disp ? lv_display_get_horizontal_resolution(disp) : 0;
    facts.height = disp ? lv_display_get_vertical_resolution(disp) : 0;
    facts.color_depth = LV_COLOR_DEPTH;
    return facts;
}

ScreensaverManager::StartPlan
ScreensaverManager::plan_start(const helix::ui::SaverBase& saver,
                               const helix::ui::ScreensaverInfo& info) {
    const helix::PlatformCapabilities caps = helix::PlatformCapabilities::detect();
    cores_ = caps.cpu_cores;
    version_ = helix_version_full();
    board_ = helix::ui::board_fingerprint(board_facts(caps));

    const helix::ui::SaverEnvOverrides env = helix::ui::saver_env_overrides();
    budget_override_.reset();
    if (env.budget_pct) {
        budget_override_ = static_cast<double>(*env.budget_pct) / 100.0;
    }
    if (env.level) {
        if (*env.level < saver.level_count()) {
            spdlog::info("[ScreensaverManager] HELIX_SCREENSAVER_LEVEL={} runs {} at that level "
                         "with the gate off",
                         *env.level, info.name);
            return {*env.level, false, false};
        }
        spdlog::warn("[ScreensaverManager] Ignoring HELIX_SCREENSAVER_LEVEL={}: {} has levels 0 "
                     "to {}",
                     *env.level, info.name, saver.level_count() - 1);
    }

    std::optional<SaverLevelEntry> stored;
    const helix::Config* config = helix::Config::get_instance();
    stored = helix::ui::load_level_entry(*config, info.name);

    const SaverLevelEntry entry =
        helix::ui::start_entry(stored, version_, board_, saver.level_count());
    return {entry.level, entry.too_heavy, true};
}

void ScreensaverManager::begin_gate(const StartPlan& plan) {
    gate_enabled_ = plan.gated;
    gated_level_ = active_->level();
    requested_level_ = gated_level_;
    const CpuSample now = cpu_clock_();
    session_.begin(now, baseline_.rate());
    last_sample_ns_ = now.wall_ns;
    sampled_ = true;
}

void ScreensaverManager::on_idle_check_tick() {
    const CpuSample now = cpu_clock_();
    if (sampled_ && now.wall_ns - last_sample_ns_ < SAMPLE_INTERVAL_NS) {
        return;
    }
    last_sample_ns_ = now.wall_ns;
    sampled_ = true;

    if (!active_ || !active_->is_active()) {
        if (black_screen_type_ == ScreensaverType::OFF) {
            baseline_.add(now);
        }
        return;
    }
    if (!gate_enabled_) {
        return;
    }
    if (active_->level() != gated_level_) {
        // The saver applied a lower level: measure that level on its own.
        gated_level_ = active_->level();
        session_.restart_window(now);
        return;
    }

    const std::optional<double> share = session_.add(now);
    // While a step-down waits for the saver's next natural break, windows only keep closing.
    if (!share || requested_level_ != gated_level_) {
        return;
    }

    const bool printing = host_.is_printing && host_.is_printing();
    const double budget =
        budget_override_.value_or(helix::ui::saver_budget_share(cores_, printing));
    switch (helix::ui::decide_saver_level(*share, budget, gated_level_, active_->level_count())) {
    case GateDecision::KEEP:
        return;
    case GateDecision::STEP_DOWN:
        requested_level_ = gated_level_ + 1;
        spdlog::info("[ScreensaverManager] {} used {:.1f}% of a core against a {:.1f}% budget{}; "
                     "stepping down to level {}",
                     active_info_->name, *share * 100.0, budget * 100.0,
                     printing ? " while printing" : "", requested_level_);
        record_level(requested_level_, false);
        active_->request_level(requested_level_);
        return;
    case GateDecision::TOO_HEAVY: {
        spdlog::warn("[ScreensaverManager] {} used {:.1f}% of a core at its lowest level against a "
                     "{:.1f}% budget{}; showing a black screen until the app version or board "
                     "changes",
                     active_info_->name, *share * 100.0, budget * 100.0,
                     printing ? " while printing" : "");
        record_level(gated_level_, true);
        const ScreensaverType type = active_->type();
        active_->stop();
        active_ = nullptr;
        active_info_ = nullptr;
        release_refresh_period();
        show_black_screen(type);
        return;
    }
    }
}

void ScreensaverManager::show_black_screen(ScreensaverType type) {
    black_screen_.create();
    black_screen_type_ = type;
    gate_enabled_ = false;
}

void ScreensaverManager::record_level(size_t level, bool too_heavy) {
    helix::Config* config = helix::Config::get_instance();
    if (!active_info_) {
        return;
    }
    helix::ui::save_level_entry(*config, active_info_->name, {level, too_heavy, version_, board_});
}

void ScreensaverManager::hold_screen() {
    if (!holds_screen_) {
        helix::active_screen_hide_hold().acquire(lv_screen_active());
        holds_screen_ = true;
    }
}

void ScreensaverManager::release_screen() {
    if (holds_screen_) {
        holds_screen_ = false;
        helix::active_screen_hide_hold().release();
    }
}

void ScreensaverManager::hold_refresh_period() {
    if (!holds_refresh_period_) {
        helix::active_refresh_period_hold().acquire();
        holds_refresh_period_ = true;
    }
}

void ScreensaverManager::release_refresh_period() {
    if (holds_refresh_period_) {
        holds_refresh_period_ = false;
        helix::active_refresh_period_hold().release();
    }
}

#endif // HELIX_ENABLE_SCREENSAVER
