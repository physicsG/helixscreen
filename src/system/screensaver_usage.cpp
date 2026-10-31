// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_usage.h"

#include <string>

namespace helix {

namespace {

int64_t elapsed_ms(SaverUsage::Clock::time_point from, SaverUsage::Clock::time_point to) {
    if (to <= from) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}

} // namespace

bool SaverUsageWindow::empty() const {
    return shown_sec.empty() && starts.empty() && woke_by_touch.empty() && stopped_other.empty() &&
           too_heavy.empty() && settled_levels.empty();
}

void SaverUsage::begin(const std::string& name, Clock::time_point now) {
    if (open_) {
        end(SaverRunEnd::OTHER, std::nullopt, now);
    }
    open_ = OpenRun{name, now};
    counts_.starts[name]++;
}

void SaverUsage::end(SaverRunEnd how, std::optional<size_t> level, Clock::time_point now) {
    if (!open_) {
        return;
    }
    const std::string& name = open_->name;
    shown_ms_[name] += elapsed_ms(open_->since, now);
    switch (how) {
    case SaverRunEnd::TOUCH:
        counts_.woke_by_touch[name]++;
        break;
    case SaverRunEnd::OTHER:
        counts_.stopped_other[name]++;
        break;
    case SaverRunEnd::TOO_HEAVY:
        counts_.too_heavy[name]++;
        break;
    }
    if (level) {
        counts_.settled_levels[name + ":" + std::to_string(*level)]++;
    }
    open_.reset();
}

SaverUsageWindow SaverUsage::window(Clock::time_point now) const {
    SaverUsageWindow out = counts_;
    std::map<std::string, int64_t> ms = shown_ms_;
    if (open_) {
        ms[open_->name] += elapsed_ms(open_->since, now);
    }
    for (const auto& [name, total] : ms) {
        if (total >= 1000) {
            out.shown_sec[name] = static_cast<int>(total / 1000);
        }
    }
    return out;
}

void SaverUsage::reset_window(Clock::time_point now) {
    if (open_) {
        shown_ms_[open_->name] += elapsed_ms(open_->since, now);
        open_->since = now;
    }
    for (auto it = shown_ms_.begin(); it != shown_ms_.end();) {
        it->second %= 1000;
        it = it->second == 0 ? shown_ms_.erase(it) : std::next(it);
    }
    counts_ = SaverUsageWindow{};
}

} // namespace helix
