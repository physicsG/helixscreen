// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver_canvas.h"
#include "screensaver_fireworks_sim.h"
#include "screensaver_sim_saver.h"

#include <cstdint>
#include <lvgl.h>
#include <optional>
#include <vector>

namespace helix::ui {

/**
 * @brief Fireworks over hills under a night sky
 *
 * helix::ui::FireworksSim draws each frame straight into the canvas at the display's own
 * depth, RGB565 or XRGB8888, and returns one dirty box per rocket or burst. A level request
 * waits for the next shell launch, and so does the frame period that comes with it.
 */
class FireworksScreensaver : public SimSaver<FireworksSim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::FIREWORKS;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return kSaverBuildCanvasFormat;
    }
    void on_frame(uint32_t dt_ms, std::vector<DirtyRect>& dirty) override;
    void on_level_request(size_t level) override {
        sim_.request_level(level);
    }
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
