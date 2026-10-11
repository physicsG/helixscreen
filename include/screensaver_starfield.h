// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_base.h"
#include "screensaver_starfield_sim.h"

#include <cstdint>
#include <lvgl.h>
#include <optional>
#include <vector>

/**
 * @brief Windows 95-style Starfield screensaver
 *
 * Stars fly outward from the center of the screen. Each star starts small
 * and dim near the center, growing larger and brighter as it approaches
 * the edges.
 *
 * helix::ui::StarfieldSim draws each frame with direct pixel writes, erasing only where
 * stars were, and moves the stars by the time since the previous frame; each frame
 * invalidates one small box per star it moved, so a scattered sky stays a partial
 * invalidation.
 */
class StarfieldScreensaver : public helix::ui::SaverBase {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::STARFIELD;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return helix::ui::SAVER_BUILD_CANVAS_FORMAT;
    }
    bool on_start() override;
    void on_frame(uint32_t dt_ms, std::vector<helix::ui::DirtyRect>& dirty) override;
    void on_stop() override;
    size_t ladder_size() const override {
        return sizeof(LEVEL_PERIODS_MS) / sizeof(LEVEL_PERIODS_MS[0]);
    }
    uint32_t ladder_period_ms(size_t level) const override {
        return LEVEL_PERIODS_MS[level];
    }
    /// Applies a level at once; the rungs past full rate thin the star population.
    void on_level_request(size_t level) override;

  private:
    // Test-only seam: reads and places the stars. See tests/test_helpers/screensaver_test_access.h.
    friend class StarfieldScreensaverTestAccess;

    /// Frame period per level. Past full rate the rungs thin the star population instead
    /// of slowing further, because a slower frame that still flies every star saves little
    /// of a scattered sky's redraw: see LEVEL_STAR_COUNTS, indexed by the same level.
    static constexpr uint32_t LEVEL_PERIODS_MS[] = {helix::ui::SAVER_FAST_PERIOD, 33, 33, 33};
    /// Stars flown at `level`.
    static constexpr int LEVEL_STAR_COUNTS[] = {helix::ui::StarfieldSim::NUM_STARS, 96, 64, 32};

    /// Stars moved and drawn at `level`.
    size_t star_count(size_t level) const;

    helix::ui::StarfieldSim sim_;
};

#endif // HELIX_ENABLE_SCREENSAVER
