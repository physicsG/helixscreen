// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "print_history_data.h"
#include "screensaver_canvas.h"
#include "screensaver_maze_sim.h"
#include "screensaver_sim_saver.h"

#include <cstdint>
#include <functional>
#include <lvgl.h>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace helix::ui {

/// Wall art: at most this many recent prints, other files on the printer, and lookups spent
/// finding the files' (most files on a printer were never shown, so their thumbnails are not
/// in the cache).
inline constexpr size_t kMazeArtRecent = 5;
inline constexpr size_t kMazeArtFiles = 6;
inline constexpr size_t kMazeArtFileProbes = 48;
/// A picture larger than this a side is not decoded, so start-up never holds a full-size
/// image: at most one decode of this size runs past the time budget.
inline constexpr int kMazeArtMaxSourceSide = 512;
/// Each picture is kept at most this size a side until its texture is built.
inline constexpr int kMazeArtPictureSide = 64;
/// No new decode starts once start-up has spent this long on art.
inline constexpr uint32_t kMazeArtDecodeBudgetMs = 120;

/// Where wall art comes from: LVGL paths of cached thumbnails, and the printer picture.
struct MazeArtSources {
    std::vector<std::string> recent;
    std::vector<std::string> files;
    std::string printer;
};

/// One file's thumbnail keys (paths from the gcodes root), best first.
using MazeArtCandidate = std::vector<std::string>;
/// The LVGL path of a key's cached thumbnail, or empty when it is not cached.
using MazeArtLookup = std::function<std::string(const std::string& key)>;

/// Up to `max_jobs` files from print history, newest first and each once, with their
/// thumbnails resolved from the gcodes root: the largest that fit
/// kMazeArtMaxSourceSide first, then the larger ones, smallest first.
std::vector<MazeArtCandidate> history_art_candidates(const std::vector<PrintHistoryJob>& jobs,
                                                     size_t max_jobs);

/**
 * @brief The first cached thumbnail of each candidate, in order
 *
 * Stops at `max` paths or after `max_probes` lookups; a path already in the result or in
 * `taken` is passed over.
 */
std::vector<std::string> cached_art_paths(const std::vector<MazeArtCandidate>& candidates,
                                          const MazeArtLookup& lookup, size_t max,
                                          size_t max_probes,
                                          const std::vector<std::string>& taken = {});

/// Recent, file and printer art interleaved one of each in turn, without repeats or empty
/// paths, up to `max`.
std::vector<std::string> mix_maze_art(const MazeArtSources& sources, size_t max);

/**
 * @brief The LVGL path of a thumbnail already in the local cache for the connected printer
 *
 * `key` is a thumbnail path as a listing gives it. Prefers the pre-scaled card copy, then the
 * full PNG (ThumbnailCache::peek). Reads only: it never fetches, and never touches an entry's
 * age or freshness.
 */
std::string maze_cached_thumbnail(const std::string& key);

/// Wall art the app already holds: cached thumbnails of recent prints and of other files in
/// the print-select list (shuffled with `rng`), and the printer picture for `screen_w`.
MazeArtSources maze_art_sources_from_app(int screen_w, std::minstd_rand& rng);

/**
 * @brief Windows 95-style 3D Maze screensaver
 *
 * MazeSim walks a random maze and raycasts it Wolfenstein-style straight into the
 * canvas at the display's own depth; every frame repaints the whole canvas. The Helix icon
 * stands where the original placed OpenGL logos, and picture walls show the printer's image.
 */
class MazeScreensaver : public SimSaver<MazeSim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::MAZE_3D;
    }
    /// Replaces where on_start() finds its art; for tests.
    void set_art_source(std::function<MazeArtSources()> source) {
        art_source_ = std::move(source);
    }
    /// Replaces how long on_start() may spend decoding art; for tests, which must not
    /// depend on how fast the machine decodes.
    void set_art_decode_budget_ms(uint32_t budget_ms) {
        art_decode_budget_ms_ = budget_ms;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return kSaverBuildCanvasFormat;
    }
    /// Decodes the art, then starts the sim with it.
    bool on_start() override;

  private:
    std::function<MazeArtSources()> art_source_;
    uint32_t art_decode_budget_ms_ = kMazeArtDecodeBudgetMs;
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
