// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_afc_fault_path.h"

#include "afc_fault_position.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "layout_manager.h"
#include "static_subject_registry.h"
#include "subject_managed_panel.h"

#include <spdlog/spdlog.h>

namespace helix::ui {

namespace {

/// Where the filament stopped, as a PathSegment. 0 (NONE) hides the graphic.
lv_subject_t g_fault_segment{};
SubjectManager g_subjects;
bool g_initialized = false;

void afc_fault_path_deinit() {
    if (!g_initialized) {
        return;
    }
    g_subjects.deinit_all();
    g_initialized = false;
    spdlog::debug("[AfcFaultPath] Subject deinitialized");
}

} // namespace

void afc_fault_path_register() {
    if (g_initialized) {
        return;
    }

    UI_MANAGED_SUBJECT_INT(g_fault_segment, 0, "afc_fault_segment", g_subjects);
    g_initialized = true;

    // Self-register cleanup — the subject must die before lv_deinit().
    StaticSubjectRegistry::instance().register_deinit("AfcFaultPath", afc_fault_path_deinit);

    // Registered here rather than alongside the modals that embed it: both
    // ams_loading_error_modal.xml (lazily registered by AmsPanel) and
    // action_prompt_modal.xml reference <afc_fault_path/>, so it has to exist
    // before either of them is parsed.
    const std::string path =
        "A:" + LayoutManager::instance().resolve_xml_path("components/afc_fault_path.xml");
    if (lv_xml_register_component_from_file(path.c_str()) != LV_RESULT_OK) {
        spdlog::error("[AfcFaultPath] Failed to register: {}", path);
    }

    spdlog::trace("[AfcFaultPath] Subject and component registered");
}

std::string afc_fault_path_apply(const std::string& message) {
    const auto segment = helix::afc::afc_fault_position(message);

    if (g_initialized) {
        lv_subject_set_int(&g_fault_segment, segment ? static_cast<int>(*segment) : 0);
    }

    if (!segment) {
        return message;
    }

    spdlog::debug("[AfcFaultPath] Fault position: {}", path_segment_to_string(*segment));
    return helix::afc::afc_strip_position_diagram(message);
}

} // namespace helix::ui
