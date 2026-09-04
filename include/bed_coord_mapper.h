// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <glm/vec2.hpp>
#include <utility>

namespace helix {

struct PixelRect {
    float x, y, w, h;
};

/// Aspect-preserving millimetre-to-pixel mapper for a top-down bed view.
///
/// Maps the coordinate range (origin_x .. origin_x + bed_w_mm) horizontally and
/// (origin_y .. origin_y + bed_h_mm) vertically onto a viewport, using a single
/// scale factor for both axes so the plate never stretches, and centering the
/// plate in whichever axis has slack.
///
/// Y is flipped: bed y=0 (front) maps to the BOTTOM of the viewport, matching
/// how a printer bed is drawn top-down. Callers must not flip again.
///
/// `origin_x`/`origin_y` default to 0 for cartesian beds. Delta and some Voron
/// kinematics report a center origin, e.g. a 200mm delta spans -100..+100, so
/// those pass origin_x = origin_y = -100.
class BedCoordMapper {
  public:
    /// Minimum expanded size (in px) for a bbox_to_rect() touch target, so a
    /// tiny bbox still maps to a tappable on-screen target.
    static constexpr float MIN_TOUCH_TARGET_PX = 28.0f;

    BedCoordMapper(float bed_w_mm, float bed_h_mm, int viewport_w_px, int viewport_h_px,
                   float origin_x = 0.0f, float origin_y = 0.0f);

    std::pair<float, float> mm_to_px(float x_mm, float y_mm) const;
    PixelRect bbox_to_rect(glm::vec2 bbox_min, glm::vec2 bbox_max) const;

    float scale() const {
        return scale_;
    }

  private:
    float scale_{1.0f};
    float offset_x_{0.0f};
    float offset_y_{0.0f};
    float origin_x_{0.0f};
    float origin_y_{0.0f};
    int viewport_h_{0};
};

} // namespace helix
