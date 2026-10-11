// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pre_print_option.h"
#include "preprint_skip_wrappers.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <map>
#include <string>

namespace helix {

/**
 * @brief Z-offset calibration strategy — determines gcode commands for calibration and save
 *
 * Different printers need different approaches to calibrate and persist Z-offset.
 * FIRMWARE_MANAGED: firmware or macros auto-persist (FlashForge, Snapmaker U1, Artillery M1,
 * ForgeX-mod). PROBE_CALIBRATE: standard Klipper PROBE_CALIBRATE -> ACCEPT -> SAVE_CONFIG. ENDSTOP:
 * Z_ENDSTOP_CALIBRATE -> ACCEPT -> Z_OFFSET_APPLY_ENDSTOP -> SAVE_CONFIG.
 */
enum class ZOffsetCalibrationStrategy {
    PROBE_CALIBRATE,  ///< Standard Klipper: PROBE_CALIBRATE -> ACCEPT -> SAVE_CONFIG
    FIRMWARE_MANAGED, ///< Firmware/macros auto-persist (FlashForge, Snapmaker U1, Artillery M1,
                      ///< ForgeX-mod)
    ENDSTOP ///< Endstop: Z_ENDSTOP_CALIBRATE -> ACCEPT -> Z_OFFSET_APPLY_ENDSTOP -> SAVE_CONFIG
};

/**
 * @brief The printer type and what it decides: the pre-print option set and the
 *        z-offset calibration strategy
 *
 * The type comes from the printer database (detection, wizard, printer manager);
 * the option set is the database's, with runtime options (timelapse) synthesized
 * on top and firmware-held defaults overlaid. Inputs owned by other domains
 * (probe, exclude_object, timelapse availability) are passed in. Main thread only.
 */
class PrinterProfileState {
  public:
    PrinterProfileState() = default;
    PrinterProfileState(const PrinterProfileState&) = delete;
    PrinterProfileState& operator=(const PrinterProfileState&) = delete;

    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    /**
     * @brief Resolve @p type against the printer database
     *
     * @param has_probe decides the strategy when the database names none
     * @return false when the type and strategy are what they already were
     */
    bool set_printer_type(const std::string& type, bool has_probe, bool exclude_object_known,
                          bool timelapse_available);

    /**
     * @brief Synthesize runtime-dependent options into the cached option set
     *
     * Some options are driven by runtime capability discovery rather than the
     * database (the timelapse toggle appears only when the moonraker-timelapse
     * plugin is installed), and the bed_mesh option goes adaptive only when the
     * firmware has [exclude_object]. Idempotent: re-running clears previously
     * synthesized options before re-adding the ones that apply now.
     *
     * @param skip_ops Leveling steps whose skip toggle is offered now
     *        (skip_wrappers::offerable()); a database option for the same
     *        step wins over the toggle.
     */
    void apply_dynamic_options(bool exclude_object_known, bool timelapse_available,
                               const std::vector<skip_wrappers::Op>& skip_ops = {});

    /**
     * @brief Record whether an installed module persists the z-offset itself
     *
     * While set the strategy resolves to FIRMWARE_MANAGED whatever the type
     * says (prestonbrown/helixscreen#1401). The caller re-resolves the type.
     * @return true when the flag changed
     */
    bool set_external_persistence(bool persisted);
    bool external_persistence() const {
        return external_persistence_;
    }

    /// Default for the synthesized timelapse option: the global
    /// moonraker-timelapse `enabled` setting (#1094).
    void set_timelapse_default_enabled(bool enabled) {
        timelapse_default_enabled_ = enabled;
    }

    /// Merge the settings a self-storing firmware holds, keyed by option id.
    /// Merges rather than replaces: Moonraker sends deltas, so a frame mentioning
    /// one setting is silent about the rest. @return true when any changed
    bool merge_firmware_option_defaults(const std::map<std::string, bool>& defaults);

    /// Forget the stored settings, when the connected machine changes. Options
    /// they overrode get their database defaults back;
    /// the caller re-runs apply_dynamic_options(). @return true when any were held
    bool clear_firmware_option_defaults();

    const std::string& printer_type() const {
        return printer_type_;
    }
    const PrePrintOptionSet& pre_print_option_set() const {
        return pre_print_option_set_;
    }
    ZOffsetCalibrationStrategy z_offset_calibration_strategy() const {
        return z_offset_calibration_strategy_;
    }
    /// The printer database says this printer type is enclosed.
    bool db_enclosed() const {
        return db_enclosed_;
    }

    /**
     * @brief String subject updated on every change to the resolved type
     *
     * Resets to "" on deinit_subjects()/re-init, and the no-change early return
     * means a soft restart repopulates it only on the next real type change.
     * Treat it as a change signal and read the value from printer_type().
     */
    lv_subject_t* get_printer_type_subject() {
        return &printer_type_subject_;
    }
    /// 1 when Save Z Offset is needed, 0 when firmware or a module auto-saves.
    lv_subject_t* get_z_offset_can_save_subject() {
        return &z_offset_can_save_;
    }

  private:
    friend class PrinterProfileStateTestAccess;

    SubjectManager subjects_;
    bool subjects_initialized_ = false;

    std::string printer_type_;
    PrePrintOptionSet pre_print_option_set_;
    ZOffsetCalibrationStrategy z_offset_calibration_strategy_ =
        ZOffsetCalibrationStrategy::PROBE_CALIBRATE;
    bool external_persistence_ = false;
    bool db_enclosed_ = false;
    bool timelapse_default_enabled_ = false;
    /// What the firmware reports for each pre-print option it stores itself.
    /// Empty on printers whose firmware stores none.
    std::map<std::string, bool> firmware_option_defaults_;

    lv_subject_t printer_type_subject_{};
    char printer_type_subject_buf_[128];
    lv_subject_t z_offset_can_save_{};
};

} // namespace helix
