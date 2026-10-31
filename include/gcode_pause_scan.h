// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file gcode_pause_scan.h
 * @brief Pure per-line scan for scheduled pauses (M600 / PAUSE / M601 / M0).
 *
 * One scanner instance per pass over a file. The caller feeds every line in
 * order with its byte offset and the layer in progress; the scanner keeps the
 * pauses and the running M73 state. It is deliberately header-only and free of
 * I/O so both gcode paths can run it on the pass they already make over the
 * file — GCodeLayerIndex::build_from_file() (streaming mode) and the viewer's
 * full-load getline loop — with no second read.
 *
 * Each pause carries BOTH progress coordinates because the progress bar can
 * fill on either of two axes (see ScheduledPause::slicer_fraction): which one
 * is live is a property of the file, reported by PauseScan::axis().
 */

#pragma once

#include "utils/decimal_parse.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace helix {
namespace gcode {

namespace pause_scan_detail {

inline char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

/// Whitespace between the words of a line, including the CR a CRLF file
/// leaves on every getline result.
inline bool is_word_separator(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// True when the code part's first word equals @p word (case-insensitive,
/// token-terminated: whitespace, ';', '\r', '\n' or end all end a word).
inline bool first_word_is(std::string_view line, std::string_view word) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    if (line.size() - i < word.size()) {
        return false;
    }
    for (size_t k = 0; k < word.size(); ++k) {
        if (ascii_lower(line[i + k]) != word[k]) {
            return false;
        }
    }
    const size_t end = i + word.size();
    if (end < line.size()) {
        const char c = line[end];
        if (!is_word_separator(c) && c != ';') {
            return false; // longer identifier (M6000, PAUSED, …)
        }
    }
    return true;
}

/// The next word of the code part at or after @p pos, advancing @p pos past
/// it; empty once the code part (up to ';' or end) is exhausted. Separators
/// and terminators are one set, so every call either consumes a word or ends.
inline std::string_view next_word(std::string_view line, size_t& pos) {
    while (pos < line.size() && is_word_separator(line[pos])) {
        ++pos;
    }
    const size_t start = pos;
    while (pos < line.size() && !is_word_separator(line[pos]) && line[pos] != ';') {
        ++pos;
    }
    return line.substr(start, pos - start);
}

} // namespace pause_scan_detail

/// Why the printer will stop at this point in the file.
enum class PauseKind : uint8_t {
    FilamentChange, ///< M600 — filament change / manual insert
    Macro,          ///< PAUSE or M601 — an explicit pause command
    Stop,           ///< M0 — what several pause-at-height post-processors emit
};

/// Which function of the file the progress bar fills on.
enum class ProgressAxis : uint8_t {
    /// virtual_sdcard.progress — file_position / file_size. The bar's axis for
    /// a file whose slicer emitted no M73 P lines.
    BytePosition,
    /// display_status.progress — the P of the last M73. Slicer-emitted M73 P
    /// lines make Klipper report this and latch it ahead of the byte axis.
    SlicerTime,
};

/// A pause command found in the file, with its position on both progress axes.
struct ScheduledPause {
    /// Byte offset of the command's line start (getline framing: the offset of
    /// the first character, '\n' not included in the line itself).
    uint64_t file_offset{0};
    /// file_offset / file_size — exact on ProgressAxis::BytePosition.
    float byte_fraction{0.0f};
    /// P/100 of the last M73 at or before file_offset — exact on
    /// ProgressAxis::SlicerTime. 0 before the file's first M73 (a pause there
    /// is at the start of the job by definition).
    float slicer_fraction{0.0f};
    /// 0-based layer in progress when the command appears; -1 in the prologue
    /// (before the first layer). Future UI ("pause at layer N") reads this;
    /// tick placement does not need it.
    int32_t layer_index{-1};
    PauseKind kind{PauseKind::Macro};
};

/// Where the file tells the firmware it reached a layer: a
/// `SET_PRINT_STATS_INFO ... CURRENT_LAYER=N` line. A pause scheduled at
/// runtime for layer N fires as that line runs (layer_pause.h), so this is
/// where its marker belongs on the progress bar.
struct LayerMark {
    int32_t layer{0}; ///< N, the slicer's own layer number
    float byte_fraction{0.0f};
    float slicer_fraction{0.0f};
};

/// Which fraction of a pause is valid for a file's axis.
inline float display_fraction(const ScheduledPause& pause, ProgressAxis axis) {
    return axis == ProgressAxis::SlicerTime ? pause.slicer_fraction : pause.byte_fraction;
}

/// Which fraction of a layer mark is valid for a file's axis.
inline float display_fraction(const LayerMark& mark, ProgressAxis axis) {
    return axis == ProgressAxis::SlicerTime ? mark.slicer_fraction : mark.byte_fraction;
}

/// Everything one pass over a file collects for the progress surfaces.
struct PauseScanResult {
    std::vector<ScheduledPause> pauses;
    /// One per distinct CURRENT_LAYER value, in file order. Empty when the
    /// slicer reports no layers.
    std::vector<LayerMark> layer_marks;
    ProgressAxis axis{ProgressAxis::BytePosition};
};

/// One scan, shared read-only by everyone who holds it (the layer index, the
/// viewer, print state): a long print's marks are not worth three copies.
using PauseScanPtr = std::shared_ptr<const PauseScanResult>;

/// The first mark for @p layer, or null when the file never reports it.
inline const LayerMark* find_layer_mark(const PauseScanResult& scan, int32_t layer) {
    for (const auto& mark : scan.layer_marks) {
        if (mark.layer == layer) {
            return &mark;
        }
    }
    return nullptr;
}

/// True when @p line's command word is one of the pause commands, with the kind.
/// Case-insensitive; the command must be the first word of the code part (a
/// mention inside a comment or an M117 message is not a scheduled pause).
inline std::optional<PauseKind> classify_pause_command(std::string_view line) {
    using pause_scan_detail::first_word_is;
    if (first_word_is(line, "m600")) {
        return PauseKind::FilamentChange;
    }
    if (first_word_is(line, "m601") || first_word_is(line, "pause")) {
        return PauseKind::Macro;
    }
    if (first_word_is(line, "m0")) {
        return PauseKind::Stop;
    }
    return std::nullopt;
}

/// N from a `SET_PRINT_STATS_INFO ... CURRENT_LAYER=N` line, or nullopt when
/// the line is not one or carries no CURRENT_LAYER. Case-insensitive.
inline std::optional<int32_t> reported_current_layer(std::string_view line) {
    using pause_scan_detail::ascii_lower;
    using pause_scan_detail::first_word_is;
    using pause_scan_detail::next_word;
    if (!first_word_is(line, "set_print_stats_info")) {
        return std::nullopt;
    }
    static constexpr std::string_view kKey = "current_layer=";
    size_t pos = 0;
    for (std::string_view word = next_word(line, pos); !word.empty(); word = next_word(line, pos)) {
        if (word.size() > kKey.size()) {
            bool match = true;
            for (size_t k = 0; k < kKey.size(); ++k) {
                if (ascii_lower(word[k]) != kKey[k]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                int32_t value = 0;
                bool digits = false;
                for (size_t k = kKey.size(); k < word.size(); ++k) {
                    const char d = word[k];
                    if (d < '0' || d > '9' || value > 100000000) {
                        return std::nullopt;
                    }
                    value = value * 10 + (d - '0');
                    digits = true;
                }
                return digits ? std::optional<int32_t>(value) : std::nullopt;
            }
        }
    }
    return std::nullopt;
}

/// The P value (0-100) of an `M73 P…` line, or nullopt when the line is not an
/// M73 carrying a P parameter. R (minutes left) and Bambu's Q (fine bar) are
/// ignored — P is the only word that feeds display_status.progress.
inline std::optional<float> m73_progress_percent(std::string_view line) {
    using pause_scan_detail::first_word_is;
    using pause_scan_detail::next_word;
    if (!first_word_is(line, "m73")) {
        return std::nullopt;
    }
    // Scan the words after "M73" for a P at word start.
    size_t pos = 0;
    for (std::string_view word = next_word(line, pos); !word.empty(); word = next_word(line, pos)) {
        if (word[0] == 'P' || word[0] == 'p') {
            float value = 0.0f;
            const auto* first = word.data() + 1;
            const auto* last = word.data() + word.size();
            const auto [ptr, ec] = parse_decimal(first, last, value);
            if (ec == std::errc{} && ptr != first) {
                return value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : value);
            }
            // 'P' without a number is not a parameter; keep scanning.
        }
    }
    return std::nullopt;
}

/**
 * @brief Running state of a pause scan over one file.
 *
 * Feed every line in file order:
 * @code
 *   PauseScan scan;
 *   scan.begin(total_bytes);
 *   while (reader.next(line))  // helix::text_io::LineReader
 *       scan.feed_line(line, offset, current_layer);
 * @endcode
 *
 * The class only accumulates; it never reads the file, so the caller owns the
 * offsets it reports (getline framing: advance the offset by line length + 1).
 */
class PauseScan {
  public:
    void begin(size_t total_bytes) {
        total_bytes_ = total_bytes;
        pauses_.clear();
        layer_marks_.clear();
        last_m73_fraction_ = 0.0f;
        has_m73_ = false;
    }

    /**
     * @brief Feed one line (getline semantics: no trailing '\n').
     * @param line_offset Byte offset of the line's first character.
     * @param layer_index Layer in progress at this line, -1 before the first.
     * @return true when this line added a pause.
     */
    bool feed_line(std::string_view line, uint64_t line_offset, int32_t layer_index) {
        // Everything this scanner looks for is a command: first word of the
        // code part. Most lines of a sliced file are G moves, so gating on the
        // first non-space character keeps the scan near-free for them without
        // every caller having to pre-filter.
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i >= line.size()) {
            return false;
        }
        const char c = line[i];
        if (c == 'S' || c == 's') {
            if (auto layer = reported_current_layer(line)) {
                if (layer_marks_.empty() || layer_marks_.back().layer != *layer) {
                    layer_marks_.push_back({*layer, fraction_of(line_offset), last_m73_fraction_});
                }
            }
            return false;
        }
        if (c != 'M' && c != 'm' && c != 'P' && c != 'p') {
            return false;
        }

        if (auto pct = m73_progress_percent(line)) {
            last_m73_fraction_ = *pct / 100.0f;
            has_m73_ = true;
        }

        if (auto kind = classify_pause_command(line)) {
            ScheduledPause pause;
            pause.file_offset = line_offset;
            pause.byte_fraction = fraction_of(line_offset);
            pause.slicer_fraction = last_m73_fraction_;
            pause.layer_index = layer_index;
            pause.kind = *kind;
            pauses_.push_back(pause);
            return true;
        }
        return false;
    }

    const std::vector<ScheduledPause>& pauses() const {
        return pauses_;
    }

    const std::vector<LayerMark>& layer_marks() const {
        return layer_marks_;
    }

    /// The whole collection, in the form the progress surfaces consume.
    PauseScanResult result() const {
        return {pauses_, layer_marks_, axis()};
    }

    /// result() moved into a shared, immutable copy; the scan is spent after.
    PauseScanPtr take_shared() {
        const ProgressAxis a = axis();
        return std::make_shared<const PauseScanResult>(
            PauseScanResult{std::move(pauses_), std::move(layer_marks_), a});
    }

    /// True when the file carries at least one M73 P line — the bar for this
    /// file will fill on slicer time once Klipper latches display_status.
    bool has_m73() const {
        return has_m73_;
    }

    /// The bar's axis for this file, derived from content rather than runtime.
    ProgressAxis axis() const {
        return has_m73_ ? ProgressAxis::SlicerTime : ProgressAxis::BytePosition;
    }

  private:
    float fraction_of(uint64_t line_offset) const {
        return total_bytes_ > 0 ? static_cast<float>(static_cast<double>(line_offset) /
                                                     static_cast<double>(total_bytes_))
                                : 0.0f;
    }

    std::vector<ScheduledPause> pauses_;
    std::vector<LayerMark> layer_marks_;
    size_t total_bytes_{0};
    float last_m73_fraction_{0.0f};
    bool has_m73_{false};
};

} // namespace gcode
} // namespace helix
