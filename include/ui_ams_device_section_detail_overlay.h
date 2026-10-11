// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_ams_device_section_detail_overlay.h
 * @brief AMS Device Section Detail overlay for progressive disclosure
 *
 * This overlay displays controls for a single device section (e.g., "calibration"
 * or "speed") in the AMS device operations progressive disclosure pattern.
 * It is opened from the device operations overlay when the user taps a section
 * row, showing only the actions belonging to that section.
 *
 * @pattern Overlay (lazy init, singleton)
 * @threading Main thread only
 */

#pragma once

#include "ams_types.h"
#include "overlay_base.h"
#include "static_panel_registry.h"

#include <lvgl/lvgl.h>

#include <string>
#include <vector>

// Forward declarations
namespace helix {
class AmsBackend;
} // namespace helix

namespace helix::ui {

/**
 * @class AmsDeviceSectionDetailOverlay
 * @brief Overlay for a single AMS device section's actions
 *
 * Shows the actions for one section (identified by section_id) from the
 * backend's get_device_actions() list. The title is set to the section label
 * passed in via show().
 *
 * ## Usage:
 *
 * @code
 * auto& overlay = helix::ui::get_ams_device_section_detail_overlay();
 * overlay.show(parent_screen, "calibration", "Calibration");
 * @endcode
 */
class AmsDeviceSectionDetailOverlay : public OverlayBase {
  public:
    /**
     * @brief Destructor
     */
    ~AmsDeviceSectionDetailOverlay() override;

    //
    // === OverlayBase Interface ===
    //

    /**
     * @brief Initialize subjects for reactive binding
     *
     * Registers subjects for:
     * - ams_section_detail_title: Title text for the overlay header
     */
    void init_subjects() override;

    /**
     * @brief Get human-readable overlay name
     * @return "Section Detail"
     */
    const char* get_name() const override {
        return "Section Detail";
    }

    const char* xml_component() const override {
        return "ams_device_section_detail";
    }

    lv_obj_t* create(lv_obj_t* parent) override;

    //
    // === Public API ===
    //

    /**
     * @brief Show the overlay for a specific section
     *
     * Sets the title to the section label, builds controls for the backend
     * actions matching section_id, and pushes the overlay.
     *
     * @param parent_screen The parent screen for overlay creation
     * @param section_id The section ID to filter actions by (e.g., "calibration")
     * @param section_label Human-readable label for the overlay title
     */
    void show(lv_obj_t* parent_screen, const std::string& section_id,
              const std::string& section_label);

    /**
     * @brief Refresh the overlay from backend
     *
     * Re-queries backend and recreates controls for the current section.
     */
    void refresh();

  protected:
    void before_show() override;

  private:
    //
    // === Internal Methods ===
    //

    /**
     * @brief Create control for a single device action (non-BUTTON types)
     *
     * Creates the appropriate control based on action type (TOGGLE,
     * INFO, SLIDER, DROPDOWN). BUTTON actions are handled by
     * create_button_in_row() instead.
     *
     * @param parent Container to add control to
     * @param action Action metadata
     */
    void create_action_control(lv_obj_t* parent, const helix::printer::DeviceAction& action);

    /**
     * @brief Create a button inside a 2-column row container
     *
     * @param row The flex row container (2 buttons per row)
     * @param action Action metadata for the button
     */
    void create_button_in_row(lv_obj_t* row, const helix::printer::DeviceAction& action);

    //
    // === Static Callbacks ===
    //

    /**
     * @brief Callback for dynamic action button click
     */
    static void on_action_clicked(lv_event_t* e);

    /**
     * @brief Callback for dynamic toggle value change
     */
    static void on_toggle_changed(lv_event_t* e);

    /**
     * @brief Callback for slider value change — updates label only (no backend call)
     */
    static void on_slider_changed(lv_event_t* e);

    /**
     * @brief Callback for slider release — executes backend action once
     */
    static void on_slider_released(lv_event_t* e);

    /**
     * @brief Callback for text input submit — validates and applies numeric value
     */
    static void on_value_input_ready(lv_event_t* e);

    /**
     * @brief Callback for dynamic dropdown value change
     */
    static void on_dropdown_changed(lv_event_t* e);

    //
    // === State ===
    //

    /// Alias for overlay_root_ to match existing pattern
    lv_obj_t*& overlay_ = overlay_root_;

    /// Container for section action controls
    lv_obj_t* actions_container_ = nullptr;

    /// The section ID this overlay is currently showing
    std::string section_id_;

    /// Its human-readable label, shown in the header
    std::string section_label_;

    /// Cached actions from backend
    std::vector<helix::printer::DeviceAction> cached_actions_;

    /// Action IDs for callback lookup (index stored in user_data)
    std::vector<std::string> action_ids_;
};

inline AmsDeviceSectionDetailOverlay& get_ams_device_section_detail_overlay() {
    return lazy_global<AmsDeviceSectionDetailOverlay>("AmsDeviceSectionDetailOverlay");
}

} // namespace helix::ui
