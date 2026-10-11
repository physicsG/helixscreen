// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "camera_config_modal.h"

#include <string>
#include <vector>

/**
 * @brief Friend access to CameraConfigModal's config round trip.
 *
 * on_show() reads the widget's saved config and the printer's webcam list;
 * on_ok() writes the edited config back. Both ends are reachable without a
 * widget tree: load_config() and publish_sources() take their inputs as
 * values, and on_ok() on a never-shown modal skips the hide. What a test can
 * pin is the DATA contract - which keys survive a save - which is the whole
 * point of the modal.
 */
namespace helix {

class CameraConfigModalTestAccess {
  public:
    static void load_config(helix::CameraConfigModal& modal, const nlohmann::json& config) {
        modal.load_config(config);
    }
    static void publish_sources(helix::CameraConfigModal& modal,
                                const std::vector<WebcamInfo>& cams) {
        modal.publish_sources(cams);
    }
    static void select_source(helix::CameraConfigModal& modal, int index) {
        modal.select_source(index);
    }
    static void ok(helix::CameraConfigModal& modal) {
        modal.on_ok();
    }
    static const std::string& source(const helix::CameraConfigModal& modal) {
        return modal.source_;
    }
    /// Picker rows: Automatic + named cameras.
    static int source_count(const helix::CameraConfigModal& modal) {
        return lv_subject_get_int(const_cast<lv_subject_t*>(&modal.source_count_));
    }
    static int row_active(const helix::CameraConfigModal& modal, size_t i) {
        return lv_subject_get_int(const_cast<lv_subject_t*>(&modal.source_active_[i]));
    }
    static const char* row_name(const helix::CameraConfigModal& modal, size_t i) {
        return lv_subject_get_string(const_cast<lv_subject_t*>(&modal.source_name_[i]));
    }
    static const char* row_note(const helix::CameraConfigModal& modal, size_t i) {
        return lv_subject_get_string(const_cast<lv_subject_t*>(&modal.source_note_[i]));
    }
};

} // namespace helix

#endif // HELIX_HAS_CAMERA
