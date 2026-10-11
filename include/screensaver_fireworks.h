// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_base.h"
#include "screensaver_canvas.h"
#include "screensaver_fireworks_sim.h"

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
class FireworksScreensaver : public SaverBase {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::FIREWORKS;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return SAVER_BUILD_CANVAS_FORMAT;
    }
    bool on_start() override;
    void on_frame(uint32_t dt_ms, std::vector<DirtyRect>& dirty) override;
    void on_stop() override;
    size_t ladder_size() const override {
        return FIREWORKS_LEVEL_COUNT;
    }
    uint32_t ladder_period_ms(size_t level) const override {
        return FIREWORKS_LEVELS[level].period_ms;
    }
    void on_level_request(size_t level) override {
        sim_.request_level(level);
    }

  private:
    FireworksSim sim_;
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
