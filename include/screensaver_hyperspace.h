// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver_canvas.h"
#include "screensaver_hyperspace_sim.h"
#include "screensaver_sim_saver.h"

#include <lvgl.h>
#include <optional>

namespace helix::ui {

/**
 * @brief A flight through glowing rainbow space, after Hyperspace
 *
 * helix::ui::HyperspaceSim redraws the whole canvas every frame at the display's own depth,
 * RGB565 or XRGB8888, and reports one box over all of it. A level request applies at once.
 */
class HyperspaceScreensaver : public SimSaver<HyperspaceSim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::HYPERSPACE;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return kSaverBuildCanvasFormat;
    }
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
