// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver_canvas.h"
#include "screensaver_pipes_sim.h"
#include "screensaver_sim_saver.h"

#include <lvgl.h>
#include <optional>

/**
 * @brief Windows NT-style 3D Pipes screensaver
 *
 * PipesSim grows shaded pipes through a node grid and rasterizes them with a z-buffer straight
 * into the canvas at the display's own depth, then dissolves the full screen and starts the next
 * one a few degrees round. A level request changes the period at once; the scale waits for the
 * next screen.
 */
class PipesScreensaver : public helix::ui::SimSaver<helix::ui::PipesSim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::PIPES_3D;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return helix::ui::kSaverBuildCanvasFormat;
    }

  private:
    // Test-only seam: reads the simulation. See tests/test_helpers/screensaver_test_access.h.
    friend class PipesScreensaverTestAccess;
};

#endif // HELIX_ENABLE_SCREENSAVER
