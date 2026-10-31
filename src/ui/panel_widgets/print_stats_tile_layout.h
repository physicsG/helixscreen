// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure content and layout decision for the print stats home tile.
//
// Free of LVGL so it is unit-testable without a display. PrintStatsWidget measures the
// minimum box each rung draws in and feeds it here; this header picks the content, the
// rung and the flags the XML binds. The tile offers Strip, Cell, Wide and Full.

#include "src/ui/panel_widgets/size_rung.h"

#include <cstdint>

namespace helix {

/// What the tile has to say. The enum values are the print_stats_content subject ints.
enum class PrintStatsContent : int {
    Loading = 0,   ///< the history cache has not answered yet
    NoHistory = 1, ///< the printer has never printed
    Activity = 2,  ///< this week's hours, with whatever lifetime figures are known
};

/// @p lifetime_prints is the server's lifetime job count, negative while unknown.
[[nodiscard]] constexpr PrintStatsContent print_stats_content(bool history_loaded, int cached_jobs,
                                                              int64_t lifetime_prints) {
    if (!history_loaded)
        return PrintStatsContent::Loading;
    if (cached_jobs == 0 && lifetime_prints <= 0)
        return PrintStatsContent::NoHistory;
    return PrintStatsContent::Activity;
}

/// How much of the lifetime totals the support line carries.
enum class PrintStatsSupport : int {
    None = 0,       ///< no line
    PrintsOnly = 1, ///< the lifetime print count
    Full = 2,       ///< prints, hours and filament
};

/// How the Strip's one line is set, largest first: the hero and label fonts, and whether
/// the label is the week's full wording or its short form ("/wk"). The values are the
/// print_stats_strip_text subject ints. Every other rung draws HeadingWeek.
enum class PrintStatsStripText : int {
    HeadingWeek = 0, ///< hero font_heading, label font_small
    BodyWeek = 1,    ///< hero font_body, label font_small
    BodyXsWeek = 2,  ///< hero font_body, label font_xs
    SmallXsWeek = 3, ///< hero font_small, label font_xs
    HeadingShort = 4,
    BodyShort = 5,
    BodyXsShort = 6,
    SmallXsShort = 7,
};
inline constexpr int kPrintStatsStripTexts = 8;

struct PrintStatsTileLayoutInput {
    int content_w = 0; ///< the box the content draws in, chrome already subtracted
    int content_h = 0;
    PrintStatsContent content = PrintStatsContent::Loading;
    bool lifetime_known = false; ///< the server's totals have arrived
    RungNeeds needs;             ///< offers strip, cell, wide and full only
    /// The support line's widths as they would draw now, and the height it adds under
    /// the Cell's stacked hero. The line never ellipsizes: a level that does not fit
    /// whole steps down to the shorter one, then to none.
    int count_line_w = 0;
    int full_line_w = 0;
    int support_line_h = 0;
    /// The Strip line's width, hero, gap and label, as each PrintStatsStripText would
    /// draw it now.
    int strip_line_w[kPrintStatsStripTexts] = {};
};

/// The published verdict. `rung` is SizeRung's int; the flags are 0/1 subjects.
struct PrintStatsTileVerdict {
    SizeRung rung = SizeRung::Strip;
    bool fits = false;
    bool show_bars = false;
    bool show_day_labels = false;
    bool side_by_side = false; ///< hero beside the bars rather than above them
    PrintStatsSupport support = PrintStatsSupport::None;
    PrintStatsStripText strip_text = PrintStatsStripText::HeadingWeek;
};

/// Strip: the hours and "this week" on one line, in the largest fonts that hold it whole,
/// then the short label the same way; the smallest short form when nothing fits. Cell: the hours
/// over "this week", with the lifetime print count under them where it fits. Wide: the hours beside
/// the seven bars, over the lifetime line. Full: the hours over the bars, a weekday initial under
/// each, then the lifetime line.
[[nodiscard]] constexpr PrintStatsTileVerdict
decide_print_stats_tile_layout(const PrintStatsTileLayoutInput& in) {
    PrintStatsTileVerdict v;
    v.rung = choose_size_rung(in.content_w, in.content_h, in.needs);
    v.fits = holds_any_rung(in.content_w, in.content_h, in.needs);
    const int r = static_cast<int>(v.rung);
    const bool activity = in.content == PrintStatsContent::Activity;
    v.show_bars = r >= static_cast<int>(SizeRung::Wide) && activity;
    v.show_day_labels = v.show_bars && v.rung == SizeRung::Full;
    v.side_by_side = v.rung == SizeRung::Wide;
    if (v.rung == SizeRung::Strip) {
        v.strip_text = PrintStatsStripText::SmallXsShort;
        for (int i = 0; i < kPrintStatsStripTexts; ++i) {
            if (in.strip_line_w[i] <= in.content_w) {
                v.strip_text = static_cast<PrintStatsStripText>(i);
                break;
            }
        }
    }
    if (v.rung == SizeRung::Strip || !in.lifetime_known || !activity)
        return v;
    const bool count_fits = in.count_line_w <= in.content_w;
    if (v.rung == SizeRung::Cell) {
        if (count_fits && in.content_h >= in.needs.cell.h + in.support_line_h)
            v.support = PrintStatsSupport::PrintsOnly;
    } else if (in.full_line_w <= in.content_w) {
        v.support = PrintStatsSupport::Full;
    } else if (count_fits) {
        v.support = PrintStatsSupport::PrintsOnly;
    }
    return v;
}

} // namespace helix
