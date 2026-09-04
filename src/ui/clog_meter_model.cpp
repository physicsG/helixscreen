// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clog_meter_model.h"

#include "ui_update_queue.h"

#include "ams_state.h"
#include "observer_factory.h"

namespace helix::ui {

ClogMeterModel::ClogMeterModel(Callback on_change) : on_change_(std::move(on_change)) {
    auto& ams = AmsState::instance();

    // Immediate observers: the callbacks below only move geometry and set
    // styles in the renderers, never observer lifecycle (#82).
    //
    // Each writes its own field and then republishes everything, so a renderer
    // always sees a whole sample. The observers fire once on registration, so
    // sample_ is fully populated by the time this returns — but on_change_ is
    // not called for those, because the renderer that owns this model is still
    // being constructed.
    auto field = [this](int ClogMeterSample::*member, lv_subject_t* subject) {
        return observe_int_immediate<ClogMeterModel>(subject, this,
                                                     [member](ClogMeterModel* self, int v) {
                                                         self->sample_.*member = v;
                                                         self->publish();
                                                     });
    };

    mode_obs_ = field(&ClogMeterSample::mode, ams.get_clog_meter_mode_subject());
    value_obs_ = field(&ClogMeterSample::value, ams.get_clog_meter_value_subject());
    warning_obs_ = field(&ClogMeterSample::warning, ams.get_clog_meter_warning_subject());
    danger_obs_ = field(&ClogMeterSample::danger_pct, ams.get_clog_meter_danger_pct_subject());
    peak_obs_ = field(&ClogMeterSample::peak_pct, ams.get_clog_meter_peak_pct_subject());

    ready_ = true;
}

ClogMeterModel::~ClogMeterModel() {
    // Freeze the queue around teardown so a background enqueue cannot land
    // between the drain and the observer resets.
    auto freeze = UpdateQueue::instance().scoped_freeze();
    UpdateQueue::instance().drain();

    mode_obs_.reset();
    value_obs_.reset();
    warning_obs_.reset();
    danger_obs_.reset();
    peak_obs_.reset();
}

void ClogMeterModel::publish() {
    if (ready_ && on_change_) {
        on_change_(sample_);
    }
}

} // namespace helix::ui
