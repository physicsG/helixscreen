// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "print_history_data.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace helix {

inline constexpr int kActivityDays = 7;

/// A bar with any activity never scales below this percentage of the tallest bar.
inline constexpr int kMinVisibleBarPct = 6;

/// Local midnights bounding seven calendar days: day i spans [starts[i], starts[i+1]).
using ActivityDayStarts = std::array<double, kActivityDays + 1>;

/// The seven local days ending with today, oldest first, plus tomorrow's midnight.
/// Each boundary comes from mktime, so a DST day is 23 or 25 hours long.
[[nodiscard]] ActivityDayStarts local_day_starts(std::time_t now);

struct WeekActivity {
    std::array<double, kActivityDays> hours_by_day{};
    double week_hours = 0.0;
    int week_prints = 0;
};

/// Sum print_duration into local calendar days by each job's job_timestamp().
/// Unlike count_trend_buckets() (rolling 24-hour windows ending now, for the
/// dashboard chart), the buckets here are calendar days of the viewer's timezone.
[[nodiscard]] WeekActivity summarize_week(const std::vector<PrintHistoryJob>& jobs,
                                          const ActivityDayStarts& starts);

/// Bar heights as 0..100, scaled to the tallest day; any non-zero day stays visible.
[[nodiscard]] std::array<int, kActivityDays>
scale_day_bars(const std::array<double, kActivityDays>& hours);

/// "0 h", "45 min", "2.5 h", "23 h"
[[nodiscard]] std::string format_print_hours(double hours);

/// "1 print", "128 prints"
[[nodiscard]] std::string format_print_count(uint64_t prints);

/// Filament length in meters ("0.1 m", "2.5 m", "412 m"); empty when there is none.
/// Separate from format::format_filament_length, which picks mm, m or km by size;
/// the stats tile compares lengths across prints and needs one unit.
[[nodiscard]] std::string format_filament_meters(double mm);

/// "128 prints · 412 h" from @p totals' jobs and time, plus " · 412 m" when
/// with_filament and there is filament.
[[nodiscard]] std::string format_lifetime_line(const PrintHistoryTotals& totals,
                                               bool with_filament);

/// First character of a weekday abbreviation, uppercased when ASCII.
[[nodiscard]] std::string weekday_initial(std::string_view abbreviation);

} // namespace helix
