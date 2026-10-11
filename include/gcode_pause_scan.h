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
        if (c != ' ' && c != '\t' && c != ';' && c != '\r' && c != '\n') {
            return false; // longer identifier (M6000, PAUSED, …)
        }
    }
    return true;
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

/// Which fraction of a pause is valid for a file's axis.
inline float display_fraction(const ScheduledPause& pause, ProgressAxis axis) {
    return axis == ProgressAxis::SlicerTime ? pause.slicer_fraction : pause.byte_fraction;
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

/// The P value (0-100) of an `M73 P…` line, or nullopt when the line is not an
/// M73 carrying a P parameter. R (minutes left) and Bambu's Q (fine bar) are
/// ignored — P is the only word that feeds display_status.progress.
inline std::optional<float> m73_progress_percent(std::string_view line) {
    using pause_scan_detail::first_word_is;
    if (!first_word_is(line, "m73")) {
        return std::nullopt;
    }
    // Scan the words after "M73" for a P at token start. ';' ends the code part.
    size_t pos = 0;
    while (pos < line.size() && line[pos] != ';') {
        if (line[pos] == ' ' || line[pos] == '\t' || line[pos] == '\r' || line[pos] == '\n') {
            ++pos;
            continue;
        }
        const size_t token_start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' && line[pos] != ';' &&
               line[pos] != '\r' && line[pos] != '\n') {
            ++pos;
        }
        if (line[token_start] == 'P' || line[token_start] == 'p') {
            float value = 0.0f;
            const auto* first = line.data() + token_start + 1;
            const auto* last = line.data() + pos;
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
        // Both things this scanner looks for are commands: first word of the
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
            pause.byte_fraction = total_bytes_ > 0
                                      ? static_cast<float>(static_cast<double>(line_offset) /
                                                           static_cast<double>(total_bytes_))
                                      : 0.0f;
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
    std::vector<ScheduledPause> pauses_;
    size_t total_bytes_{0};
    float last_m73_fraction_{0.0f};
    bool has_m73_{false};
};

} // namespace gcode
} // namespace helix
