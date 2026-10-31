// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "print_start_phase.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helix {

/// Bytes of the print file read to find its start block: past the slicer's
/// header and thumbnails.
inline constexpr size_t kStartBlockHeadBytes = 200 * 1024;

/// What a start-block line does, as the pre-print display names it.
enum class StartBlockStepKind { HomeAxes, ProbeZ, LevelGantry, TiltZ, BedMesh, CleanNozzle, Purge };

/// One operation of a start sequence the slicer writes inline in the file,
/// at the byte its line starts.
struct StartBlockStep {
    uint64_t offset = 0;
    StartBlockStepKind kind = StartBlockStepKind::HomeAxes;
    /// The command returns only once its work is done, so virtual_sdcard.
    /// file_position stands on its line for as long as it runs. A position that
    /// passed such a line without ever standing on it means the printer skipped
    /// the work. A macro or a move is queued and returns at once, so its work
    /// runs while the position stands on a later line.
    bool runs_on_its_line = false;
    /// The line's command word, upper-cased: what a profile names to say
    /// which console lines the command prints while it runs.
    std::string command;
};

/// The phase a step shows.
PrintStartPhase start_block_step_phase(StartBlockStepKind kind);

/// The steps in the start block of @p head, the first bytes of the print file,
/// in file order: every G28 (Z alone is a Z probe), QGL, Z tilt, bed-mesh
/// calibration and nozzle-clean or purge macro, then the first extruding move,
/// up to the first layer marker. @p truncated says @p head stops short of the
/// file's end, so its last, unterminated line is not read.
std::vector<StartBlockStep> scan_start_block(std::string_view head, bool truncated);

/// Follows virtual_sdcard.file_position through the start block's steps.
/// Klipper advances the position past a line only once its command returns,
/// so the position is the start of the line running now.
class StartBlockFollower {
  public:
    void set_steps(std::vector<StartBlockStep> steps);
    [[nodiscard]] bool has_steps() const {
        return !steps_.empty();
    }
    /// The steps in file order.
    [[nodiscard]] const std::vector<StartBlockStep>& steps() const {
        return steps_;
    }

    /// The step the print reached at @p position, when it is one not yet
    /// reached: the last line at or before the position that runs a step, a
    /// step that runs on its line only if the position stands on it. Every
    /// step before it is passed over for good, since the position only grows.
    std::optional<StartBlockStep> advance(uint64_t position);

  private:
    std::vector<StartBlockStep> steps_;
    size_t next_ = 0;
};

} // namespace helix
