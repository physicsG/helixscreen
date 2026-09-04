// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

struct _lv_obj_t;
typedef struct _lv_obj_t lv_obj_t;

namespace helix::ui {

/**
 * @brief Sliding-rectangle buffer meter for proportional sync feedback.
 *
 * Draws two nested rectangles representing the physical buffer plunger:
 * - Neutral (bias~0): 50% overlap
 * - Tension (bias<0): inner slides up, minimal overlap
 * - Compression (bias>0): inner slides down, near-complete overlap
 *
 * Driven by `sync_feedback_bias`, whatever produces it: Happy Hare publishes
 * one directly, and an AFC_buffer configured `type: FPS_PSF` has an analog
 * filament pressure sensor that AmsBackendAfc maps onto the same -1..+1 scale.
 * A switched TurtleNeck buffer has no proportional reading and gates this out.
 *
 * (This said "Happy Hare only — AFC has no proportional sensor data", which was
 * only ever true of the switched buffer.)
 */
class UiBufferMeter {
  public:
    explicit UiBufferMeter(lv_obj_t* parent);
    ~UiBufferMeter();

    UiBufferMeter(const UiBufferMeter&) = delete;
    UiBufferMeter& operator=(const UiBufferMeter&) = delete;

    [[nodiscard]] lv_obj_t* get_root() const {
        return root_;
    }
    [[nodiscard]] bool is_valid() const {
        return root_ != nullptr;
    }

    /// Set bias value directly (for modal usage without subject binding)
    void set_bias(float bias);

    /// Resize drawing to fit container
    void resize();

  private:
    static void on_draw(lv_event_t* e);
    static void on_size_changed(lv_event_t* e);

    void draw(lv_layer_t* layer);
    void update_labels();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* canvas_obj_ = nullptr;
    lv_obj_t* direction_label_ = nullptr;

    float bias_ = 0.0f;
};

} // namespace helix::ui
