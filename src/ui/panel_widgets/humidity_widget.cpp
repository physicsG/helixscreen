// SPDX-License-Identifier: GPL-3.0-or-later

#include "humidity_widget.h"

#include "ui_fonts.h"

#include "format_utils.h"
#include "humidity_sensor_manager.h"
#include "observer_factory.h"
#include "panel_widget_registry.h"
#include "panel_widget_size.h"
#include "static_subject_registry.h"
#include "subject_debug_registry.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

// File-static subject: formatted chamber humidity text for XML binding
static lv_subject_t s_chamber_humidity_text;
static char s_chamber_humidity_text_buf[8]; // "45%" or "--"
static bool s_subjects_initialized = false;
static ObserverGuard s_humidity_observer;

static void humidity_widget_init_subjects() {
    if (s_subjects_initialized) {
        return;
    }

    lv_subject_init_string(&s_chamber_humidity_text, s_chamber_humidity_text_buf, nullptr,
                           sizeof(s_chamber_humidity_text_buf), "--");
    lv_xml_register_subject(nullptr, "chamber_humidity_text", &s_chamber_humidity_text);
    SubjectDebugRegistry::instance().register_subject(&s_chamber_humidity_text,
                                                      "chamber_humidity_text",
                                                      LV_SUBJECT_TYPE_STRING, __FILE__, __LINE__);

    // Observe raw humidity int and format for display
    auto* raw_subj =
        helix::sensors::HumiditySensorManager::instance().get_chamber_humidity_subject();
    if (raw_subj) {
        s_humidity_observer = helix::ui::observe_int_sync<lv_subject_t>(
            raw_subj, &s_chamber_humidity_text, [](lv_subject_t* target, int humidity_x10) {
                char buf[8];
                if (humidity_x10 >= 0) {
                    helix::format::format_humidity(humidity_x10, buf, sizeof(buf));
                } else {
                    snprintf(buf, sizeof(buf), "--");
                }
                if (strcmp(lv_subject_get_string(target), buf) != 0) {
                    lv_subject_copy_string(target, buf);
                }
            });
    }

    s_subjects_initialized = true;

    StaticSubjectRegistry::instance().register_deinit("HumidityWidgetSubjects", []() {
        if (s_subjects_initialized && lv_is_initialized()) {
            // Release observer — raw subject from HumiditySensorManager may already
            // be destroyed during reverse-order deinit [L073]
            s_humidity_observer.release();
            lv_subject_deinit(&s_chamber_humidity_text);
            s_subjects_initialized = false;
            spdlog::trace("[HumidityWidget] Subjects deinitialized");
        }
    });

    spdlog::debug("[HumidityWidget] Subjects initialized (chamber_humidity_text)");
}

namespace helix {
void register_humidity_widget() {
    register_widget_factory("humidity",
                            [](const std::string&) { return std::make_unique<HumidityWidget>(); });
    register_widget_subjects("humidity", humidity_widget_init_subjects);
}
} // namespace helix

using namespace helix;

HumidityWidget::~HumidityWidget() {
    detach();
}

void HumidityWidget::attach(lv_obj_t* widget_obj, lv_obj_t* /*parent_screen*/) {
    widget_obj_ = widget_obj;
    if (widget_obj_)
        lv_obj_set_user_data(widget_obj_, this);
}

void HumidityWidget::detach() {
    if (widget_obj_) {
        lv_obj_set_user_data(widget_obj_, nullptr);
        widget_obj_ = nullptr;
    }
}

void HumidityWidget::on_size_changed(int /*colspan*/, int /*rowspan*/, int width_px,
                                     int height_px) {
    if (!widget_obj_)
        return;

    bool wide = (width_px >= widget_size::w_normal());
    bool tall = (height_px >= widget_size::h_tall());

    // Scale icon when tall or wide
    const lv_font_t* icon_font = (tall || wide) ? &mdi_icons_32 : &mdi_icons_24;

    // Scale text when wide
    const char* label_token = wide ? "font_body" : "font_xs";
    const char* value_token = wide ? "font_body" : "font_xs";
    const lv_font_t* label_font = theme_manager_get_font(label_token);
    const lv_font_t* value_font = theme_manager_get_font(value_token);
    if (!label_font || !value_font)
        return;

    // Icon inside humidity_indicator
    lv_obj_t* indicator = lv_obj_find_by_name(widget_obj_, "humidity_indicator");
    if (indicator) {
        // First child of indicator is the icon (lv_label with MDI font)
        lv_obj_t* icon = lv_obj_get_child(indicator, 0);
        if (icon)
            lv_obj_set_style_text_font(icon, icon_font, 0);
    }

    // Percentage value label (named in humidity_indicator.xml)
    lv_obj_t* value_label = lv_obj_find_by_name(widget_obj_, "humidity_value");
    if (value_label)
        lv_obj_set_style_text_font(value_label, value_font, 0);

    // Bottom "Humidity" label — second child of the widget view
    uint32_t wcount = lv_obj_get_child_count(widget_obj_);
    if (wcount >= 2) {
        lv_obj_t* label = lv_obj_get_child(widget_obj_, 1);
        if (label)
            lv_obj_set_style_text_font(label, label_font, 0);
    }
}
