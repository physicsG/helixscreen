#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_observer_guard.h"

#include "bed_coord_mapper.h"
#include "gcode_parser.h"

#include <cmath>
#include <functional>
#include <glm/vec2.hpp>
#include <lvgl.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Forward declarations
namespace helix {
class PrinterExcludedObjectsState;
}

namespace helix::ui {

class PrintExcludeObjectManager;

class ExcludeObjectMapView {
  public:
    // Canonical value lives on helix::BedCoordMapper; aliased here so existing
    // ExcludeObjectMapView::MIN_TOUCH_TARGET_PX references keep compiling.
    static constexpr float MIN_TOUCH_TARGET_PX = helix::BedCoordMapper::MIN_TOUCH_TARGET_PX;

    using PixelRect = helix::PixelRect;
    using CoordMapper = helix::BedCoordMapper;

    enum class KeyBarMode { FullNames, Abbreviated, Summary };
    static KeyBarMode key_bar_mode(int object_count);

    ExcludeObjectMapView();
    ~ExcludeObjectMapView();

    // Non-copyable
    ExcludeObjectMapView(const ExcludeObjectMapView&) = delete;
    ExcludeObjectMapView& operator=(const ExcludeObjectMapView&) = delete;

    void create(lv_obj_t* parent, helix::PrinterExcludedObjectsState* state, float bed_w_mm,
                float bed_h_mm, PrintExcludeObjectManager* exclude_manager,
                std::shared_ptr<helix::gcode::ParsedGCodeFile> parsed_file = nullptr);
    void destroy();

    [[nodiscard]] lv_obj_t* root() const {
        return root_;
    }
    [[nodiscard]] bool is_active() const {
        return root_ != nullptr;
    }

    void set_close_callback(std::function<void()> cb) {
        close_cb_ = std::move(cb);
    }

  private:
    void build_object_rects();
    void update_visual_states();
    void build_key_bar();
    void draw_first_layer_outlines();
    lv_obj_t* create_object_rect(lv_obj_t* parent, int index, const std::string& name,
                                 const PixelRect& rect);
    lv_color_t get_object_color(int index) const;

    static void on_close_clicked(lv_event_t* e);
    static void on_object_clicked(lv_event_t* e);

    lv_obj_t* root_{nullptr};
    lv_obj_t* plate_area_{nullptr};
    lv_obj_t* key_bar_{nullptr};
    lv_obj_t* object_container_{nullptr};
    lv_obj_t* canvas_{nullptr};
    lv_draw_buf_t* canvas_buf_{nullptr};

    helix::PrinterExcludedObjectsState* state_{nullptr};
    PrintExcludeObjectManager* exclude_manager_{nullptr};
    std::shared_ptr<helix::gcode::ParsedGCodeFile> parsed_file_;

    float bed_w_mm_{235.0f};
    float bed_h_mm_{235.0f};

    std::unique_ptr<CoordMapper> mapper_;
    ObserverGuard excluded_version_obs_;
    ObserverGuard defined_version_obs_;

    std::function<void()> close_cb_;

    struct ObjectRect {
        std::string name;
        lv_obj_t* rect{nullptr};
    };
    std::vector<ObjectRect> object_rects_;
};

} // namespace helix::ui
