// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_ENABLE_SCREENSAVER

#include "screensaver.h"
#include "screensaver_clock.h"
#include "screensaver_clock_saver.h"
#include "screensaver_highfly_sim.h"

#include <cstdint>
#include <lvgl.h>
#include <optional>
#include <vector>

namespace helix::ui {

/**
 * @brief High Fly: over a sunlit cloud deck with a thin clock at the cloud line
 *
 * helix::ui::HighFlySim draws the sky, clouds and numerals; this class draws the date block
 * and AM/PM with lv_draw_label, so the canvas takes kLvDrawCanvasFormat.
 */
class HighFlyScreensaver : public ClockSaver<HighFlySim> {
  public:
    ScreensaverType type() const override {
        return ScreensaverType::HIGHFLY;
    }

  protected:
    std::optional<lv_color_format_t> canvas_format() const override {
        return kLvDrawCanvasFormat;
    }
    /// The date block and AM/PM, put back wherever `under` repainted beneath them.
    void draw_clock_text(const std::vector<DirtyRect>& under,
                         std::vector<DirtyRect>& dirty) override;
    void reset_clock_text() override;

  private:
    friend class HighFlyScreensaverTestAccess;

    SaverLabel date_line_;
    SaverLabel day_line_;
    SaverLabel meridiem_;
};

} // namespace helix::ui

#endif // HELIX_ENABLE_SCREENSAVER
