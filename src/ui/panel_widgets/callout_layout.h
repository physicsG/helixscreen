// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the printer image widget's live callouts: which
// mode the widget draws in, where the image sits, and where every chip and
// leader line goes. No LVGL, so every boundary is unit-testable. The widget
// measures chip widths in the fonts the chips render and feeds them here.

#include "printer_image_regions.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

namespace helix {

enum class CalloutKind { Nozzle = 0, Bed = 1, Chamber = 2, Fan = 3, Light = 4, Toolhead = 5 };

/// The `printer_callout_mode` subject ints; XML ref_values read these.
enum class CalloutMode { ImageOnly = 0, Pinned = 1, Docked = 2, BothSides = 3, OneSide = 4 };

struct CalloutRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct CalloutChipIn {
    CalloutKind kind = CalloutKind::Nozzle;
    int w = 0;                       ///< measured width including padding and comfort margin
    std::optional<NormPoint> anchor; ///< tagged point; nullopt when the image has none for it
};

struct CalloutChipOut {
    CalloutKind kind = CalloutKind::Nozzle;
    CalloutRect rect;
    bool has_line = false;
    int line_x0 = 0, line_y0 = 0; ///< the tagged point
    int line_xm = 0, line_ym = 0; ///< elbow: 45 degrees from the point, then straight on
    int line_x1 = 0, line_y1 = 0; ///< the chip's inner edge, at its centre
};

struct CalloutPoint {
    int x = 0;
    int y = 0;
};

struct CalloutLayoutInput {
    int area_w = 0; ///< the image container's content box
    int area_h = 0;
    bool single_cell = false; ///< the grid granted one cell on both axes
    int image_w = 0;          ///< natural image size; only the aspect is used
    int image_h = 0;
    bool tagged = false; ///< the image has a regions entry
    int chip_h = 0;      ///< every chip is one text line tall
    int gap = 0;         ///< between stacked chips, and from the area edge
    int min_line = 0;    ///< shortest leader run worth drawing
    /// Worst-case chips this printer can ever show. Decides the MODE, so the
    /// image never moves when a chip comes or goes. Empty rules out the line
    /// modes: a tagged image then draws Pinned.
    std::vector<CalloutChipIn> budget;
    /// Chips showing now. Only these get positions.
    std::vector<CalloutChipIn> active;
    /// Nozzle+fan combined, for pinned mode when both are active.
    std::optional<CalloutChipIn> toolhead;
};

struct CalloutLayout {
    CalloutMode mode = CalloutMode::ImageOnly;
    CalloutRect image;
    std::vector<CalloutChipOut> chips;
    bool toolhead_merged = false;
};

/// Where a tagged image puts `k`'s chip: its part's point, the nozzle for the
/// toolhead chip, and the middle of the bed's near edge for the bed.
[[nodiscard]] inline std::optional<NormPoint> region_anchor(const ImageRegions& r, CalloutKind k) {
    switch (k) {
    case CalloutKind::Nozzle:
    case CalloutKind::Toolhead:
        return r.nozzle;
    case CalloutKind::Fan:
        return r.part_fan;
    case CalloutKind::Bed:
        return NormPoint{(r.bed_left.x + r.bed_right.x) / 2, (r.bed_left.y + r.bed_right.y) / 2};
    case CalloutKind::Chamber:
        return r.chamber;
    case CalloutKind::Light:
        return r.light;
    }
    return std::nullopt;
}

/// Contain-fit the image into the area, centred. Zero rect when either is empty.
[[nodiscard]] inline CalloutRect fit_image(int area_w, int area_h, int img_w, int img_h) {
    if (area_w <= 0 || area_h <= 0 || img_w <= 0 || img_h <= 0)
        return {};
    CalloutRect r;
    if (int64_t(area_w) * img_h <= int64_t(area_h) * img_w) {
        r.w = area_w;
        r.h = int(int64_t(area_w) * img_h / img_w);
    } else {
        r.h = area_h;
        r.w = int(int64_t(area_h) * img_w / img_h);
    }
    r.x = (area_w - r.w) / 2;
    r.y = (area_h - r.h) / 2;
    return r;
}

/// The point of `img` (a fit_image() rect) under area pixel (x, y), or nullopt
/// when that pixel is in the letterbox rather than on the image.
[[nodiscard]] inline std::optional<NormPoint> image_point_at(const CalloutRect& img, int x, int y) {
    if (img.w <= 0 || img.h <= 0 || x < img.x || y < img.y || x >= img.x + img.w ||
        y >= img.y + img.h)
        return std::nullopt;
    return NormPoint{float(x - img.x) / float(img.w), float(y - img.y) / float(img.h)};
}

/// Resolve overlaps along one axis. `start` holds each item's ideal start,
/// sorted ascending; items are pushed apart by `gap`, then pulled back so the
/// last ends by `hi`. The caller has already checked the items fit in [lo, hi].
inline void spread_1d(std::vector<int>& start, const std::vector<int>& size, int lo, int hi,
                      int gap) {
    for (size_t i = 0; i < start.size(); ++i) {
        const int min_s = i ? start[i - 1] + size[i - 1] + gap : lo;
        start[i] = std::max(start[i], min_s);
    }
    for (size_t i = start.size(); i-- > 0;) {
        const int max_s = (i + 1 < start.size() ? start[i + 1] - gap : hi) - size[i];
        start[i] = std::min(start[i], max_s);
    }
}

namespace callout_detail {

/// Where a leader from point `a` to chip edge `c` bends: 45 degrees toward the
/// chip, then straight into it. `horizontal` leaders (side bands) end in a level
/// run, the others (bands above and below) in a vertical one. When 45 degrees
/// leaves less than `min_line` of straight run, the diagonal steepens to keep
/// that stub; it never runs back past the point.
inline CalloutPoint leader_elbow(CalloutPoint a, CalloutPoint c, int min_line, bool horizontal) {
    const int a_run = horizontal ? a.x : a.y, c_run = horizontal ? c.x : c.y;
    const int run = std::abs(c_run - a_run);
    const int cross = std::abs(horizontal ? c.y - a.y : c.x - a.x);
    const int diag = cross <= run - min_line ? cross : std::max(0, run - min_line);
    const int bend = a_run + (c_run >= a_run ? diag : -diag);
    return horizontal ? CalloutPoint{bend, c.y} : CalloutPoint{c.x, bend};
}

inline int px(float n, int origin, int extent) {
    return origin + int(n * float(extent));
}

inline CalloutRect clamp_into(CalloutRect r, int area_w, int area_h) {
    r.w = std::min(r.w, area_w);
    r.h = std::min(r.h, area_h);
    r.x = std::clamp(r.x, 0, std::max(0, area_w - r.w));
    r.y = std::clamp(r.y, 0, std::max(0, area_h - r.h));
    return r;
}

/// Chips with no usable point: a column in the widest side band if one fits
/// them, else a bottom-edge row filled right to left, wrapping upward.
inline void place_docked(const CalloutLayoutInput& in, const CalloutRect& img,
                         const std::vector<CalloutChipIn>& chips,
                         std::vector<CalloutChipOut>& out) {
    if (chips.empty())
        return;
    int widest = 0;
    for (const auto& c : chips)
        widest = std::max(widest, c.w);
    const int band_x = img.x + img.w;
    const int band_w = in.area_w - band_x;
    const int stack_h = int(chips.size()) * in.chip_h + int(chips.size() - 1) * in.gap;
    if (band_w >= widest + 2 * in.gap && stack_h <= in.area_h - 2 * in.gap) {
        int y = (in.area_h - stack_h) / 2;
        for (const auto& c : chips) {
            out.push_back({c.kind, {band_x + in.gap, y, c.w, in.chip_h}});
            y += in.chip_h + in.gap;
        }
        return;
    }
    int x = in.area_w - in.gap;
    int y = in.area_h - in.gap - in.chip_h;
    for (const auto& c : chips) {
        if (x - c.w < in.gap && x != in.area_w - in.gap) {
            x = in.area_w - in.gap;
            y -= in.chip_h + in.gap;
        }
        x -= c.w;
        out.push_back({c.kind, clamp_into({x, y, c.w, in.chip_h}, in.area_w, in.area_h)});
        x -= in.gap;
    }
}

/// The first `n` of `chips` grouped into columns: chips whose x-ranges intersect,
/// transitively. Each column's indices are sorted top to bottom.
inline std::vector<std::vector<size_t>> x_columns(const std::vector<CalloutChipOut>& chips,
                                                  size_t n) {
    std::vector<size_t> by_x(n);
    for (size_t i = 0; i < n; ++i)
        by_x[i] = i;
    std::sort(by_x.begin(), by_x.end(),
              [&](size_t a, size_t b) { return chips[a].rect.x < chips[b].rect.x; });
    std::vector<std::vector<size_t>> cols;
    for (size_t b = 0; b < by_x.size();) {
        size_t e = b + 1;
        int right = chips[by_x[b]].rect.x + chips[by_x[b]].rect.w;
        for (; e < by_x.size() && chips[by_x[e]].rect.x < right; ++e)
            right = std::max(right, chips[by_x[e]].rect.x + chips[by_x[e]].rect.w);
        std::vector<size_t> col(by_x.begin() + long(b), by_x.begin() + long(e));
        std::sort(col.begin(), col.end(),
                  [&](size_t p, size_t q) { return chips[p].rect.y < chips[q].rect.y; });
        cols.push_back(std::move(col));
        b = e;
    }
    return cols;
}

/// Which chip a column too tall to stack gives up to the docked row first.
/// The nozzle and the toolhead never do.
inline int dock_rank(CalloutKind k) {
    switch (k) {
    case CalloutKind::Light:
        return 0;
    case CalloutKind::Fan:
        return 1;
    case CalloutKind::Chamber:
        return 2;
    case CalloutKind::Bed:
        return 3;
    case CalloutKind::Nozzle:
    case CalloutKind::Toolhead:
        break;
    }
    return 4;
}

/// Pinned chips (the first `pinned` of `chips`) sit on their points and can land
/// on each other. Each x-column is moved apart vertically by spread_1d, above the
/// docked row (the rest of `chips`); a lone chip moves only to clear that row.
/// Returns the index of the chip a column too tall to stack gives up to the
/// docked row, before moving anything, or nullopt once every column fits.
inline std::optional<size_t> slide_apart(const CalloutLayoutInput& in,
                                         std::vector<CalloutChipOut>& chips, size_t pinned) {
    int hi = in.area_h - in.gap;
    for (size_t i = pinned; i < chips.size(); ++i)
        hi = std::min(hi, chips[i].rect.y - in.gap);
    const auto cols = x_columns(chips, pinned);
    for (const auto& col : cols) {
        int total = in.gap * int(col.size() - 1);
        for (size_t i : col)
            total += chips[i].rect.h;
        if (total <= hi - in.gap)
            continue;
        std::optional<size_t> victim;
        for (size_t i : col)
            if (dock_rank(chips[i].kind) < 4 &&
                (!victim || dock_rank(chips[i].kind) < dock_rank(chips[*victim].kind)))
                victim = i;
        if (victim)
            return victim;
    }
    for (const auto& col : cols) {
        if (col.size() == 1) {
            auto& r = chips[col[0]].rect;
            r.y = std::min(r.y, hi - r.h);
        } else {
            std::vector<int> start, size;
            for (size_t i : col) {
                start.push_back(chips[i].rect.y);
                size.push_back(chips[i].rect.h);
            }
            spread_1d(start, size, in.gap, hi, in.gap);
            for (size_t k = 0; k < col.size(); ++k)
                chips[col[k]].rect.y = start[k];
        }
        for (size_t i : col)
            chips[i].rect = clamp_into(chips[i].rect, in.area_w, in.area_h);
    }
    return std::nullopt;
}

inline CalloutChipOut chip_at(const CalloutChipIn& c, int x, int y, int h, int ax, int ay) {
    CalloutChipOut o{c.kind, {x, y, c.w, h}, true};
    o.line_x0 = ax;
    o.line_y0 = ay;
    return o;
}

/// Stack `chips` (sorted by the point's position along the stacking axis) in a
/// column at x = band_pos (horizontal bands) or a row at y = band_pos (vertical
/// bands). `band_pos` is the chips' near edge when `left_or_top`, else their far edge.
/// Returns false if they do not fit the axis.
inline bool stack(const CalloutLayoutInput& in, const CalloutRect& img, bool horizontal_band,
                  bool left_or_top, int band_pos, std::vector<CalloutChipIn> chips,
                  std::vector<CalloutChipOut>& out) {
    if (chips.empty())
        return true;
    const auto key = [&](const CalloutChipIn& c) {
        return horizontal_band ? c.anchor->y : c.anchor->x;
    };
    std::sort(chips.begin(), chips.end(), [&](auto& a, auto& b) { return key(a) < key(b); });
    std::vector<int> start, size;
    for (const auto& c : chips) {
        const int a =
            horizontal_band ? px(c.anchor->y, img.y, img.h) : px(c.anchor->x, img.x, img.w);
        const int s = horizontal_band ? in.chip_h : c.w;
        start.push_back(a - s / 2);
        size.push_back(s);
    }
    const int extent = horizontal_band ? in.area_h : in.area_w;
    int total = in.gap * int(chips.size() - 1);
    for (int s : size)
        total += s;
    if (total > extent - 2 * in.gap)
        return false;
    spread_1d(start, size, in.gap, extent - in.gap, in.gap);
    for (size_t i = 0; i < chips.size(); ++i) {
        const auto& c = chips[i];
        const int ax = px(c.anchor->x, img.x, img.w), ay = px(c.anchor->y, img.y, img.h);
        CalloutChipOut o;
        if (horizontal_band) {
            const int x = left_or_top ? band_pos : band_pos - c.w;
            o = chip_at(c, x, start[i], in.chip_h, ax, ay);
            o.line_x1 = left_or_top ? x + c.w : x;
            o.line_y1 = start[i] + in.chip_h / 2;
        } else {
            const int y = left_or_top ? band_pos : band_pos - in.chip_h;
            o = chip_at(c, start[i], y, in.chip_h, ax, ay);
            o.line_x1 = start[i] + c.w / 2;
            o.line_y1 = left_or_top ? y + in.chip_h : y;
        }
        const CalloutPoint e =
            leader_elbow({ax, ay}, {o.line_x1, o.line_y1}, in.min_line, horizontal_band);
        o.line_xm = e.x;
        o.line_ym = e.y;
        out.push_back(o);
    }
    return true;
}

/// How far below its contain-fit size the image may shrink to make room for a
/// leader-line column; past this, the picture loses more than the lines give.
inline constexpr int kMaxImageShrinkPct = 8;

/// The side band's axis (true: bands left and right) and the room one column of
/// budget chips needs across it, for the image at `img`.
inline std::pair<bool, int> line_column(const CalloutLayoutInput& in, const CalloutRect& img) {
    const bool horiz = (in.area_w - img.w) >= (in.area_h - img.h);
    int widest = 0;
    for (const auto& c : in.budget)
        widest = std::max(widest, c.w);
    return {horiz, (horiz ? widest : in.chip_h) + in.gap + in.min_line};
}

/// Both sides, else one side, with the image centred at `centred`.
inline bool try_line_modes_at(const CalloutLayoutInput& in, const CalloutRect& centred,
                              CalloutLayout& out) {
    // Plain locals, not a structured binding: clang before 16 rejects a lambda
    // capturing a binding.
    const auto column = line_column(in, centred);
    const bool horiz = column.first;
    const int col = column.second;
    const int far_edge = (horiz ? in.area_w : in.area_h) - in.gap;
    const auto split = [&](const std::vector<CalloutChipIn>& v, bool near_half) {
        std::vector<CalloutChipIn> r;
        for (const auto& c : v)
            if (((horiz ? c.anchor->x : c.anchor->y) < 0.5f) == near_half)
                r.push_back(c);
        return r;
    };

    std::vector<CalloutChipOut> scratch;
    const int band = horiz ? centred.x : centred.y;
    if (band >= col && stack(in, centred, horiz, true, in.gap, split(in.budget, true), scratch) &&
        stack(in, centred, horiz, false, far_edge, split(in.budget, false), scratch)) {
        out.image = centred;
        out.chips.clear();
        stack(in, centred, horiz, true, in.gap, split(in.active, true), out.chips);
        stack(in, centred, horiz, false, far_edge, split(in.active, false), out.chips);
        out.mode = CalloutMode::BothSides;
        return true;
    }

    const int free_extent = horiz ? in.area_w - centred.w : in.area_h - centred.h;
    CalloutRect moved = centred;
    (horiz ? moved.x : moved.y) = 0;
    scratch.clear();
    if (free_extent >= col && stack(in, moved, horiz, false, far_edge, in.budget, scratch)) {
        out.image = moved;
        out.chips.clear();
        stack(in, moved, horiz, false, far_edge, in.active, out.chips);
        out.mode = CalloutMode::OneSide;
        return true;
    }
    return false;
}

/// Line modes at the contain-fit size, else with the image shrunk by up to
/// kMaxImageShrinkPct: just enough for one side, then for both. Fit is tested
/// against the BUDGET and only the ACTIVE chips are placed, so the image never
/// moves as chips come and go. A part with no tagged point cannot have a line:
/// the pinned path docks it.
inline bool try_line_modes(const CalloutLayoutInput& in, CalloutLayout& out) {
    if (in.budget.empty())
        return false;
    for (const auto* v : {&in.budget, &in.active})
        for (const auto& c : *v)
            if (!c.anchor)
                return false;

    const CalloutRect fitted = out.image;
    if (try_line_modes_at(in, fitted, out))
        return true;
    const auto [horiz, col] = line_column(in, fitted);
    const int area = horiz ? in.area_w : in.area_h;
    const int full = horiz ? fitted.w : fitted.h;
    const int min_extent = full - full * kMaxImageShrinkPct / 100;
    for (const int columns : {1, 2}) {
        const int extent = area - columns * col;
        if (extent < min_extent || extent >= full)
            continue;
        CalloutRect shrunk;
        shrunk.w = horiz ? extent : int(int64_t(fitted.w) * extent / full);
        shrunk.h = horiz ? int(int64_t(fitted.h) * extent / full) : extent;
        shrunk.x = (in.area_w - shrunk.w) / 2;
        shrunk.y = (in.area_h - shrunk.h) / 2;
        if (try_line_modes_at(in, shrunk, out))
            return true;
    }
    return false;
}

} // namespace callout_detail

[[nodiscard]] inline CalloutLayout compute_callout_layout(const CalloutLayoutInput& in) {
    using namespace callout_detail;
    CalloutLayout out;
    out.image = fit_image(in.area_w, in.area_h, in.image_w, in.image_h);
    if (in.single_cell || out.image.w <= 0 || out.image.h < 3 * in.chip_h) {
        out.mode = CalloutMode::ImageOnly;
        return out;
    }

    std::vector<CalloutChipIn> chips = in.active;
    if (in.tagged) {
        if (try_line_modes(in, out))
            return out;
        const auto has = [&](CalloutKind k) {
            return std::any_of(chips.begin(), chips.end(),
                               [k](const CalloutChipIn& c) { return c.kind == k; });
        };
        if (in.toolhead && has(CalloutKind::Nozzle) && has(CalloutKind::Fan)) {
            chips.erase(std::remove_if(chips.begin(), chips.end(),
                                       [](const CalloutChipIn& c) {
                                           return c.kind == CalloutKind::Nozzle ||
                                                  c.kind == CalloutKind::Fan;
                                       }),
                        chips.end());
            chips.push_back(*in.toolhead);
            out.toolhead_merged = true;
        }
        out.mode = CalloutMode::Pinned;
        std::vector<CalloutChipIn> pinned, docked;
        for (const auto& c : chips)
            (c.anchor ? pinned : docked).push_back(c);
        // Each pass places the pinned chips on their points and the rest in the
        // docked row, until every column of pinned chips stacks above that row.
        for (;;) {
            out.chips.clear();
            for (const auto& c : pinned) {
                const int cx = px(c.anchor->x, out.image.x, out.image.w);
                const int cy = px(c.anchor->y, out.image.y, out.image.h);
                out.chips.push_back(
                    {c.kind, clamp_into({cx - c.w / 2, cy - in.chip_h / 2, c.w, in.chip_h},
                                        in.area_w, in.area_h)});
            }
            // The docked row is the bottom edge: the side bands belong to the image here.
            place_docked(in, CalloutRect{0, 0, in.area_w, 0}, docked, out.chips);
            const auto victim = slide_apart(in, out.chips, pinned.size());
            if (!victim)
                return out;
            docked.push_back(pinned[*victim]);
            pinned.erase(pinned.begin() + long(*victim));
        }
    }

    out.mode = CalloutMode::Docked;
    place_docked(in, out.image, chips, out.chips);
    return out;
}

/// Chip widths for review_callout_layout(), indexed by CalloutKind.
using CalloutChipWidths = std::array<int, 6>;

/// The tagger's review: a chip for every tagged part of `r`, placed over an
/// `area_w` x `area_h` view of the image the way the home widget pins them
/// (sliding apart, the toolhead merge, the overflow dock).
[[nodiscard]] inline CalloutLayout review_callout_layout(const ImageRegions& r, int area_w,
                                                         int area_h, const CalloutChipWidths& w,
                                                         int chip_h, int gap) {
    CalloutLayoutInput in;
    in.area_w = area_w;
    in.area_h = area_h;
    in.image_w = r.src_w;
    in.image_h = r.src_h;
    in.tagged = true;
    in.chip_h = chip_h;
    in.gap = gap;
    for (const CalloutKind k : {CalloutKind::Nozzle, CalloutKind::Bed, CalloutKind::Chamber,
                                CalloutKind::Fan, CalloutKind::Light}) {
        if (const auto a = region_anchor(r, k))
            in.active.push_back({k, w[size_t(k)], a});
    }
    const auto toolhead = size_t(CalloutKind::Toolhead);
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, w[toolhead], r.nozzle};
    return compute_callout_layout(in);
}

} // namespace helix
