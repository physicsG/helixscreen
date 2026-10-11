// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_status_layout_fitter.h"

#include "ui_next_tick.h"
#include "ui_overlay_temp_graph.h"

#include "layout_manager.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "print_status_layout_decision.h"
#include "temp_graph_controller.h"

#include <spdlog/spdlog.h>

#include <cstdlib>

namespace helix::ui {

PrintStatusLayoutFitter::PrintStatusLayoutFitter(std::string log_tag, PrinterState& printer_state,
                                                 Subjects subjects, Host host)
    : log_tag_(std::move(log_tag)), printer_state_(printer_state), subjects_(subjects),
      host_(std::move(host)) {}

void PrintStatusLayoutFitter::resume_temp_graph() {
    if (temp_graph_controller_) {
        temp_graph_controller_->resume();
    }
}

void PrintStatusLayoutFitter::pause_temp_graph() {
    if (temp_graph_controller_) {
        temp_graph_controller_->pause();
    }
}

void PrintStatusLayoutFitter::on_tree_destroyed() {
    destroy_temp_graph();
    preview_slack_h_ = 0;
    if (host_.subjects_ready()) {
        lv_subject_set_int(subjects_.graph_fits, 0);
    }
}

void PrintStatusLayoutFitter::recompute_aux_composites() {
    bool aux_present = host_.aux_present();
    int density = lv_subject_get_int(subjects_.fan_row_density);
    lv_subject_set_int(subjects_.aux_icon_visible, (aux_present && density == 0) ? 1 : 0);
    lv_subject_set_int(subjects_.aux_full_visible, (aux_present && density != 2) ? 1 : 0);
    lv_subject_set_int(subjects_.aux_short_visible, (aux_present && density == 2) ? 1 : 0);
}

void PrintStatusLayoutFitter::recompute_aux_composites_for_measurement(int density,
                                                                       bool aux_present) {
    lv_subject_set_int(subjects_.aux_icon_visible, (aux_present && density == 0) ? 1 : 0);
    lv_subject_set_int(subjects_.aux_full_visible, (aux_present && density != 2) ? 1 : 0);
    lv_subject_set_int(subjects_.aux_short_visible, (aux_present && density == 2) ? 1 : 0);
}

void PrintStatusLayoutFitter::recompute_fans_density() {
    spdlog::debug("[{}] recompute_fans_density: entry", log_tag_);
    if (!host_.root()) {
        spdlog::debug("[{}] recompute_fans_density: no widget tree", log_tag_);
        return;
    }
    lv_obj_t* fan_row = lv_obj_find_by_name(host_.root(), "print_status_fan_row");
    lv_obj_t* controls = lv_obj_find_by_name(host_.root(), "controls_section");
    if (!fan_row || !controls) {
        spdlog::debug("[{}] recompute_fans_density: fan_row={} controls={}", log_tag_,
                      fmt::ptr(fan_row), fmt::ptr(controls));
        return;
    }

    // First-time measurement: force each density tier and measure the row's
    // natural CONTENT width. The row has width="100%" so `lv_obj_get_width()`
    // returns the column width (useless). We must temporarily set width to
    // LV_SIZE_CONTENT so flex sums child widths.
    if (fan_row_natural_width_[0] == 0) {
        bool was_hidden = lv_obj_has_flag(fan_row, LV_OBJ_FLAG_HIDDEN);
        if (was_hidden)
            lv_obj_remove_flag(fan_row, LV_OBJ_FLAG_HIDDEN);

        int saved_density = lv_subject_get_int(subjects_.fan_row_density);
        lv_obj_set_width(fan_row, LV_SIZE_CONTENT);

        for (int d = 0; d < 3; ++d) {
            lv_subject_set_int(subjects_.fan_row_density, d);
            recompute_aux_composites_for_measurement(d, /*aux_present=*/true);
            lv_obj_update_layout(fan_row);
            fan_row_natural_width_[d] = lv_obj_get_width(fan_row);
        }

        // Restore 100% width and original density
        lv_obj_set_width(fan_row, lv_pct(100));
        lv_subject_set_int(subjects_.fan_row_density, saved_density);
        recompute_aux_composites();
        lv_obj_update_layout(fan_row);

        if (was_hidden)
            lv_obj_add_flag(fan_row, LV_OBJ_FLAG_HIDDEN);

        spdlog::debug("[{}] fan_row natural widths: full={} med={} compact={}", log_tag_,
                      fan_row_natural_width_[0], fan_row_natural_width_[1],
                      fan_row_natural_width_[2]);

        if (fan_row_natural_width_[0] <= 0) {
            spdlog::info("[{}] widths zero — retrying on next tick", log_tag_);
            auto token = lifetime_.token();
            token.defer("PrintStatusLayoutFitter::recompute_fans_density_retry",
                        [this]() { recompute_fans_density(); });
            return;
        }
    }

    int controls_w = lv_obj_get_content_width(controls);
    // Slack accounts for measurement-vs-render discrepancy (font metric rounding,
    // gap accounting differences, etc.). Too tight clips; too loose forces a
    // lower-density tier than necessary. 8px is the largest value that still
    // lets density 0 win when the hotend label is at its widest realistic
    // value ("100%") — the visible label changes when the mock's auto heater
    // fan trips, so the cached natural width can be measured against either
    // "0%" or "100%" and we need both to fit.
    constexpr int DENSITY_SLACK = 8;
    int next_density = 2;
    if (controls_w >= fan_row_natural_width_[0] + DENSITY_SLACK)
        next_density = 0;
    else if (controls_w >= fan_row_natural_width_[1] + DENSITY_SLACK)
        next_density = 1;

    int current = lv_subject_get_int(subjects_.fan_row_density);
    spdlog::debug("[{}] fan_row_density check: current={} next={} controls_w={} widths=[{},{},{}]",
                  log_tag_, current, next_density, controls_w, fan_row_natural_width_[0],
                  fan_row_natural_width_[1], fan_row_natural_width_[2]);
    if (next_density != current) {
        lv_subject_set_int(subjects_.fan_row_density, next_density);
        recompute_aux_composites();
    }
}

// DECLARATIVE_OK: the ceiling is a function of the card's MEASURED width, which
// no style attribute can express — the measured-layout structural exception.
void PrintStatusLayoutFitter::apply_preview_height_cap() {
    if (!host_.root()) {
        return;
    }
    // Portrait only. The landscape card sits in a row at roughly 380x392
    // (aspect ~1.03), so it could never reach a 1.30 ceiling anyway — but the
    // landscape XML has no absorber to size either, so bail before touching it.
    // There is no slack in landscape by construction, which is exactly what the
    // graph gate needs to hear: report zero rather than leaving a portrait
    // reading latched after a rotation.
    if (!helix::is_portrait_layout(helix::LayoutManager::instance().type())) {
        note_preview_slack(0);
        return;
    }
    lv_obj_t* card = lv_obj_find_by_name(host_.root(), "thumbnail_section");
    lv_obj_t* strip = lv_obj_find_by_name(host_.root(), "metadata_clip");
    lv_obj_t* slack = lv_obj_find_by_name(host_.root(), "preview_slack");
    if (!card || !strip || !slack) {
        return;
    }

    lv_obj_t* content = lv_obj_find_by_name(host_.root(), "overlay_content");
    lv_obj_update_layout(content ? content : card);

    // The band is the card's CONTENT width; the card's own border/padding is
    // chrome and rides along with the strip in the ceiling.
    const int32_t band_w = lv_obj_get_content_width(card);
    const int32_t chrome_h =
        lv_obj_get_height(strip) + (lv_obj_get_height(card) - lv_obj_get_content_height(card));
    const int32_t max_h = helix::ui::portrait_preview_card_max_height(band_w, chrome_h);
    if (max_h <= 0) {
        return; // not measurable yet; leave the layout alone
    }

    const char* space_md_str = lv_xml_get_const(nullptr, "space_md");
    const int32_t gap = space_md_str ? std::atoi(space_md_str) : 8;

    // Space the card and the absorber share. Invariant under the absorber's own
    // state, which is what makes re-running this a fixed point: when the
    // absorber is visible it costs its height plus one gap, and both come back.
    const bool shown = !lv_obj_has_flag(slack, LV_OBJ_FLAG_HIDDEN);
    const int32_t avail = lv_obj_get_height(card) + (shown ? lv_obj_get_height(slack) + gap : 0);

    const int32_t want = helix::ui::portrait_preview_slack(max_h, avail, gap);

    // The absorber's visibility is not application state, it is the same measured
    // layout decision as its height: hidden is how a fixed-size flex child costs
    // ZERO, because LVGL's flex pass skips hidden children's size AND their gap.
    // A subject here would be a second name for `want > 0` with no other reader.
    if (want != (shown ? lv_obj_get_height(slack) : 0)) {
        if (want > 0) {
            lv_obj_set_height(slack, want);
            // DECLARATIVE_OK: measured-layout absorber; visibility is `want > 0`.
            lv_obj_remove_flag(slack, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_set_height(slack, 0);
            // DECLARATIVE_OK: measured-layout absorber; hidden is how it costs zero.
            lv_obj_add_flag(slack, LV_OBJ_FLAG_HIDDEN);
        }
        // Settle the absorber before measuring its content width below — the
        // graph's ceiling is a function of that width, and a just-unhidden child
        // has no resolved percentage size until the layout pass runs.
        lv_obj_update_layout(content ? content : slack);
    }

    // Cap the graph, not the absorber. The absorber must keep the WHOLE slack or
    // the preview card grows straight back into it; the leftover under the graph
    // is transparent and reads as background between the graph and the controls.
    // Sized BEFORE note_preview_slack() publishes the fit subject, so the
    // container is never un-hidden at a height nobody chose.
    const int32_t graph_h = helix::ui::portrait_graph_height(lv_obj_get_content_width(slack), want);
    if (graph_h > 0) {
        if (lv_obj_t* graph = lv_obj_find_by_name(slack, "temp_graph_container")) {
            // DECLARATIVE_OK: measured-layout ceiling, same reason as the absorber.
            lv_obj_set_height(graph, graph_h);
        }
    }

    note_preview_slack(want);
    spdlog::debug("[{}] preview cap: band_w={} chrome_h={} max_h={} avail={} slack={} graph_h={}",
                  log_tag_, band_w, chrome_h, max_h, avail, want, graph_h);
}

void PrintStatusLayoutFitter::note_preview_slack(int32_t slack_h) {
    preview_slack_h_ = slack_h;
    recompute_graph_fits();
}

void PrintStatusLayoutFitter::recompute_graph_fits() {
    if (!host_.subjects_ready()) {
        return;
    }
    const int current = lv_subject_get_int(subjects_.graph_fits);
    const bool next = helix::ui::portrait_graph_fits(preview_slack_h_, current == 1);

    // Build BEFORE publishing, never after: the subject is what un-hides the
    // container, so flipping it first would show an empty box for however many
    // frames the controller takes to draw its first trace.
    if (next) {
        ensure_temp_graph();
    }

    if (static_cast<int>(next) != current) {
        spdlog::debug("[{}] graph_fits {} -> {} (slack={}, needed={})", log_tag_, current,
                      static_cast<int>(next), preview_slack_h_,
                      helix::ui::MIN_TEMP_GRAPH_HEIGHT_PX);
        lv_subject_set_int(subjects_.graph_fits, next ? 1 : 0);
    }
}

void PrintStatusLayoutFitter::ensure_temp_graph() {
    if (temp_graph_controller_) {
        return; // already live — keep the backfilled trace
    }
    if (!host_.root()) {
        return;
    }
    lv_obj_t* container = lv_obj_find_by_name(host_.root(), "temp_graph_container");
    if (!container) {
        return; // landscape variant has no absorber, so no container either
    }

    helix::TempGraphControllerConfig cfg;
    // 180 points, not the 1200-point default. This graph is on screen DURING a
    // print, which is the worst moment to spend redraw time — 1200 points across
    // N series is what froze the K2 Plus touch UI in #979. At 1 Hz sampling 180
    // points is still three minutes of trace, more than the band can resolve.
    cfg.point_count = 180;
    cfg.axis_size = "xs";
    // Lines and target lines only. The band is ~300px wide: axis labels would eat
    // most of the plot, a legend would eat the rest, and gradients are pure fill
    // cost for a strip this short.
    cfg.initial_features = TEMP_GRAPH_FEATURE_LINES | TEMP_GRAPH_FEATURE_TARGET_LINES;

    helix::TempGraphSeriesSpec nozzle;
    nozzle.klipper_name = printer_state_.temperature_state().active_extruder_name();
    nozzle.display_name = lv_tr("Nozzle");
    nozzle.color = helix::TEMP_GRAPH_SERIES_COLORS[0];
    nozzle.show_target = true;

    helix::TempGraphSeriesSpec bed;
    bed.klipper_name = "heater_bed";
    bed.display_name = lv_tr("Bed");
    bed.color = helix::TEMP_GRAPH_SERIES_COLORS[1];
    bed.show_target = true;

    cfg.series = {std::move(nozzle), std::move(bed)};

    // Chamber only when the printer actually has one. printer_has_chamber is the
    // union of heater and sensor.
    //
    // The name must be the DISCOVERED Klipper object ("heater_generic chamber"),
    // not the literal "chamber": TempGraphController::setup_observers() routes to
    // the chamber subjects on the `heater_generic` / `temperature_fan` prefix, and
    // anything else falls through to a TemperatureSensorManager lookup that finds
    // nothing — a series that resolves to no subject renders as a blank line with
    // no error. Prefer the heater (it has a target to draw); fall back to the
    // sensor so sensor-only chambers still graph.
    lv_subject_t* chamber_gate = lv_xml_get_subject(nullptr, "printer_has_chamber");
    if (chamber_gate && lv_subject_get_int(chamber_gate) != 0) {
        const auto& temp_state = printer_state_.temperature_state();
        const std::string& heater = temp_state.chamber_heater_name();
        const std::string& klipper = temp_state.chamber_temperature_source();
        if (!klipper.empty()) {
            helix::TempGraphSeriesSpec chamber;
            chamber.klipper_name = klipper;
            chamber.display_name = lv_tr("Chamber");
            chamber.color = helix::TEMP_GRAPH_SERIES_COLORS[2];
            chamber.show_target = !heater.empty();
            cfg.series.push_back(std::move(chamber));
        }
    }

    temp_graph_container_ = container;
    temp_graph_controller_ = std::make_unique<helix::TempGraphController>(container, cfg);
    // The controller backfills from TemperatureHistoryManager in its constructor,
    // so the trace is populated the moment the container un-hides rather than
    // growing from blank at the next sample.
    spdlog::debug("[{}] Temperature mini-graph created ({} series, {} points)", log_tag_,
                  cfg.series.size(), cfg.point_count);
}

void PrintStatusLayoutFitter::destroy_temp_graph(bool defer_delete) {
    temp_graph_container_ = nullptr;
    if (!temp_graph_controller_) {
        return;
    }

    // Detach observers SYNCHRONOUSLY, then defer only the deallocation. The
    // widget tree is already queued for async deletion by the time this runs, so
    // the controller's destructor will find its chart gone (chart_delete_cb nulls
    // it) — but its observers still point at live subjects and must come off
    // before anything else can fire them (#726). Deferring the delete itself
    // keeps it out of the current UpdateQueue batch (#696).
    temp_graph_controller_->detach();
    auto* old = temp_graph_controller_.release();
    if (defer_delete && lv_is_initialized()) {
        helix::ui::run_next_tick([old]() { delete old; });
    } else {
        delete old;
    }
    spdlog::debug("[{}] Temperature mini-graph torn down", log_tag_);
}

void PrintStatusLayoutFitter::recompute_fans_fit() {
    spdlog::debug("[{}] recompute_fans_fit: entry", log_tag_);
    if (!host_.root()) {
        spdlog::debug("[{}] recompute_fans_fit: no widget tree", log_tag_);
        return;
    }
    // Cap the preview before measuring: the fan-row budget reads overlay_content
    // and the controls, and both must be measured against the settled column.
    apply_preview_height_cap();

    lv_obj_t* controls = lv_obj_find_by_name(host_.root(), "controls_section");
    lv_obj_t* fan_row = lv_obj_find_by_name(host_.root(), "print_status_fan_row");
    if (!controls || !fan_row) {
        spdlog::debug("[{}] recompute_fans_fit: controls={} fan_row={}", log_tag_,
                      fmt::ptr(controls), fmt::ptr(fan_row));
        return;
    }

    lv_obj_update_layout(controls);

    if (fan_row_natural_height_ == 0) {
        bool was_hidden = lv_obj_has_flag(fan_row, LV_OBJ_FLAG_HIDDEN);
        if (was_hidden)
            lv_obj_remove_flag(fan_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(fan_row);
        fan_row_natural_height_ = lv_obj_get_height(fan_row);
        if (was_hidden)
            lv_obj_add_flag(fan_row, LV_OBJ_FLAG_HIDDEN);
        spdlog::debug("[{}] fan_row natural height={}", log_tag_, fan_row_natural_height_);
        if (fan_row_natural_height_ <= 0) {
            auto token = lifetime_.token();
            token.defer("PrintStatusLayoutFitter::recompute_fans_fit_retry",
                        [this]() { recompute_fans_fit(); });
            return;
        }
    }

    // Portrait stacks overlay_content into a column and sizes controls_section
    // to its content, which removes the slack the landscape formula measures
    // against. See helix::ui::fan_row_budget().
    const bool portrait = helix::is_portrait_layout(helix::LayoutManager::instance().type());
    lv_obj_t* content = lv_obj_find_by_name(host_.root(), "overlay_content");
    if (portrait && content) {
        lv_obj_update_layout(content);
    }

    int controls_h = lv_obj_get_height(controls);
    int content_h = content ? lv_obj_get_height(content) : 0;
    int used = 0;
    int visible_count = 0;

    auto add_child_height = [&](const char* name) {
        lv_obj_t* o = lv_obj_find_by_name(host_.root(), name);
        if (!o || lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN))
            return;
        used += lv_obj_get_height(o);
        ++visible_count;
    };
    add_child_height("temp_card");
    // Portrait merged the filament/AMS cluster INTO speed_flow_row, so the next
    // line already counts it; landscape never had it as a controls child at all.
    add_child_height("speed_flow_row");

    // button_grid is flex_grow=1 so its OWN height is stretched. Sum the
    // visible button-row children directly to get the natural content height.
    lv_obj_t* btn_grid = lv_obj_find_by_name(host_.root(), "button_grid");
    if (btn_grid && !lv_obj_has_flag(btn_grid, LV_OBJ_FLAG_HIDDEN)) {
        int btn_grid_used = 0;
        int btn_rows_visible = 0;
        uint32_t n = lv_obj_get_child_count(btn_grid);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t* row = lv_obj_get_child(btn_grid, i);
            if (!row || lv_obj_has_flag(row, LV_OBJ_FLAG_HIDDEN))
                continue;
            btn_grid_used += lv_obj_get_height(row);
            ++btn_rows_visible;
        }
        const char* space_sm_str = lv_xml_get_const(nullptr, "space_sm");
        int space_sm = space_sm_str ? std::atoi(space_sm_str) : 4;
        if (btn_rows_visible > 1)
            btn_grid_used += (btn_rows_visible - 1) * space_sm;
        used += btn_grid_used;
        ++visible_count;
    }
    add_child_height("print_status_extras");

    // Account for inter-child gaps: (N-1) gaps between N visible children.
    // The fan row would add one more visible child, so include +1 in gap count.
    const char* space_md_str = lv_xml_get_const(nullptr, "space_md");
    int space_md = space_md_str ? std::atoi(space_md_str) : 8;
    if (visible_count >= 1)
        used += visible_count * space_md; // (visible_count - 1) for existing + 1 for fan row

    int available = helix::ui::fan_row_budget(portrait, controls_h, content_h, used);
    int current = lv_subject_get_int(subjects_.fans_fit);
    int next = current;
    if (current == 1) {
        if (available < fan_row_natural_height_)
            next = 0;
    } else {
        if (available >= fan_row_natural_height_ + 4)
            next = 1;
    }
    if (next != current) {
        spdlog::debug("[{}] fans_fit {} -> {} (portrait={}, controls_h={}, content_h={}, used={}, "
                      "available={}, needed={})",
                      log_tag_, current, next, portrait, controls_h, content_h, used, available,
                      fan_row_natural_height_);
        lv_subject_set_int(subjects_.fans_fit, next);
    }
}

} // namespace helix::ui
