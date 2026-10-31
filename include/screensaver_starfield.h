// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver_sim_saver.h"
#include "screensaver_starfield_sim.h"

#include <cstddef>
#include <lvgl.h>
#include <optional>

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
 * invalidation. The rungs past full rate thin the star population (kStarfieldLevels).
 */
class StarfieldScreensaver : public helix::ui::SimSaver<helix::ui::StarfieldSim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::STARFIELD;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return helix::ui::kSaverBuildCanvasFormat;
    }

  private:
    // Test-only seam: reads and places the stars. See tests/test_helpers/screensaver_test_access.h.
    friend class StarfieldScreensaverTestAccess;

    /// Stars moved and drawn at `level`.
    size_t star_count(size_t level) const {
        return static_cast<size_t>(helix::ui::starfield_star_count(level));
    }
};

#endif // HELIX_ENABLE_SCREENSAVER
