// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file print_status_layout_fitter.h
 * @brief Measured layout decisions for the print-status panel.
 *
 * Some of the panel's layout depends on what the widgets actually measure, which
 * no style attribute can express: how much of the fan row fits in the controls
 * column, how tall the portrait preview card may grow, and whether the slack that
 * leaves is tall enough for the temperature mini-graph. This measures the live
 * widget tree and publishes each decision as a subject the XML binds to.
 *
 * @threading Main thread only.
 */

#pragma once

#include "async_lifetime_guard.h"
#include "lvgl.h"
#include "printer_state.h"
#include "temp_graph_controller.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace helix::ui {

class PrintStatusLayoutFitter {
  public:
    /// The panel's subjects this publishes into; the panel owns them.
    struct Subjects {
        lv_subject_t* fan_row_density;
        lv_subject_t* aux_icon_visible;
        lv_subject_t* aux_full_visible;
        lv_subject_t* aux_short_visible;
        lv_subject_t* fans_fit;
        lv_subject_t* graph_fits;
    };

    /// What the panel answers on the fitter's behalf.
    struct Host {
        /// The overlay root to measure, or null while there is no widget tree.
        std::function<lv_obj_t*()> root;
        /// Are the subjects initialised? Nothing is published before they are.
        std::function<bool()> subjects_ready;
        /// Does the printer have an aux fan?
        std::function<bool()> aux_present;
    };

    PrintStatusLayoutFitter(std::string log_tag, PrinterState& printer_state, Subjects subjects,
                            Host host);

    PrintStatusLayoutFitter(const PrintStatusLayoutFitter&) = delete;
    PrintStatusLayoutFitter& operator=(const PrintStatusLayoutFitter&) = delete;

    /// Height-based fan row visibility (fans_fit). Also caps the portrait preview
    /// card, since the fan row budget is measured against the settled column.
    void recompute_fans_fit();
    /// Width-based fan row content tier (fan_row_density).
    void recompute_fans_density();
    /// Derive the three aux_*_visible subjects from aux presence and density.
    void recompute_aux_composites();
    /// The same derivation for an assumed density and aux presence, used while
    /// measuring each tier.
    void recompute_aux_composites_for_measurement(int density, bool aux_present);

    /// Resume the mini-graph and backfill what landed while off-screen.
    void resume_temp_graph();
    /// Stop feeding the mini-graph while it is off-screen.
    void pause_temp_graph();

    /// Detach the mini-graph's observers synchronously, then release the
    /// controller. Must run BEFORE the container is freed.
    /// @param defer_delete Hand the deallocation to run_next_tick instead of
    ///        running it here. True on the close path, which may be inside an
    ///        UpdateQueue batch. False from the owner's destructor, where
    ///        nothing will ever drain the async queue again and a deferred
    ///        delete would leak the observers along with the object.
    void destroy_temp_graph(bool defer_delete = true);

    /// The widget tree is going away: drop the graph and the fit decision with
    /// it. A rebuilt tree starts with no controller, so leaving graph_fits at 1
    /// would un-hide an empty container until the first recompute.
    void on_tree_destroyed();

  private:
    /// Portrait: cap thumbnail_section's aspect and park the leftover in the
    /// preview_slack absorber between the card and the controls. No-op in
    /// landscape and at every size where the cap does not bind.
    void apply_preview_height_cap();
    /// Record the absorber height the cap just applied and re-decide whether the
    /// temperature mini-graph fits in it. Called from every exit path of
    /// apply_preview_height_cap(), including the ones that leave the layout alone.
    void note_preview_slack(int32_t slack_h);
    void recompute_graph_fits();
    /// Build the mini-graph controller into temp_graph_container if it is not
    /// already live. Idempotent; no-op when the widget tree is gone.
    void ensure_temp_graph();

    std::string log_tag_;
    PrinterState& printer_state_;
    Subjects subjects_;
    Host host_;
    AsyncLifetimeGuard lifetime_;

    /// Height apply_preview_height_cap() last parked in the preview_slack
    /// absorber, in px. The single input to recompute_graph_fits(), cached rather
    /// than re-measured so the fit decision cannot disagree with the layout that
    /// produced it.
    int32_t preview_slack_h_ = 0;
    /// Natural height of the fan row, measured while forced visible. Used by
    /// recompute_fans_fit() as the `needed` value.
    int fan_row_natural_height_ = 0;
    /// Natural widths per density tier: 0=full, 1=medium, 2=compact. 0 means not
    /// yet measured.
    int fan_row_natural_width_[3] = {0, 0, 0};

    /// Owns the graph widget, its series observers and history backfill. Built
    /// on demand once the slack band is big enough, then kept alive across
    /// show/hide: recreating it would discard the backfilled trace.
    std::unique_ptr<helix::TempGraphController> temp_graph_controller_;
    /// The XML container the controller drew into. Nulled when the tree goes so a
    /// rebuilt one is never populated through a stale pointer.
    lv_obj_t* temp_graph_container_ = nullptr;
};

} // namespace helix::ui
