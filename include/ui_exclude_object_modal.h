// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_modal.h"

#include <functional>
#include <string>

/**
 * @file ui_exclude_object_modal.h
 * @brief Confirmation dialog for excluding an object during print
 *
 * Uses Modal for RAII lifecycle - dialog auto-hides when object is destroyed.
 * Shows a warning with the object name and confirm/cancel options.
 * After 5 seconds, the exclusion becomes permanent.
 *
 * @example
 *   exclude_modal_.set_object_name("Cube_1");
 *   exclude_modal_.set_on_confirm([this]() { execute_exclude(); });
 *   exclude_modal_.show(lv_screen_active());
 */

/**
 * @brief Confirmation modal for excluding an object during print
 *
 * Derives from Modal base class for RAII lifecycle management.
 * Shows a warning with the object name and confirm/cancel options.
 */
class ExcludeObjectModal : public Modal {
  public:
    using Callback = std::function<void()>;

    /**
     * @brief Get human-readable name for logging
     * @return "Exclude Object"
     */
    const char* get_name() const override {
        return "Exclude Object";
    }

    /**
     * @brief Get XML component name for lv_xml_create()
     * @return "exclude_object_modal"
     */
    const char* component_name() const override {
        return "exclude_object_modal";
    }

    /**
     * @brief Set the object name to display in the modal
     * @param name Name of the object to exclude
     */
    void set_object_name(const std::string& name) {
        object_name_ = name;
    }

    /**
     * @brief Set callback to invoke when user confirms exclusion
     * @param cb Callback function
     */
    void set_on_confirm(Callback cb) {
        on_confirm_cb_ = std::move(cb);
    }

    /**
     * @brief Set callback to invoke when the dialog closes without Confirm
     *
     * Fires once per showing, for the Cancel button, ESC, a backdrop tap, a
     * hot-reload rebuild and a programmatic hide() alike.
     * @param cb Callback function
     */
    void set_on_cancel(Callback cb) {
        on_cancel_cb_ = std::move(cb);
    }

  protected:
    /**
     * @brief Called after modal is created and visible
     *
     * Wires up the OK and Cancel buttons.
     */
    void on_show() override;

    /**
     * @brief Called when user clicks OK button
     *
     * Invokes the confirm callback if set, then hides the modal.
     */
    void on_ok() override {
        if (on_confirm_cb_) {
            on_confirm_cb_();
        }
        hide(ModalCloseReason::ButtonPress);
    }

    /**
     * @brief Called when user clicks Cancel button, and on ESC
     *
     * Invokes the cancel callback if set, then hides the modal.
     */
    void on_cancel() override {
        if (on_cancel_cb_) {
            on_cancel_cb_();
        }
        hide(ModalCloseReason::ButtonPress);
    }

    /**
     * @brief A close no button answered resolves as a cancel
     *
     * The caller holds a pending-exclusion guard that only a resolution
     * clears, so a backdrop tap, hot-reload rebuild or programmatic hide()
     * must report like Cancel does.
     */
    void on_dismissed() override {
        if (on_cancel_cb_) {
            on_cancel_cb_();
        }
    }

  private:
    std::string object_name_;
    Callback on_confirm_cb_;
    Callback on_cancel_cb_;
};
