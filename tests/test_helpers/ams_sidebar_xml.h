// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/// Registers ams_sidebar.xml and everything it embeds, the way the AMS panels
/// do at build time. The global component registration pass does not cover the
/// sidebar: panels register it lazily, so a test that wants the production
/// sidebar tree calls this first. Idempotent, and safe under a fixture whose
/// widget layer is already registered (the sets do not overlap).

#pragma once

#include "ui_ams_sidebar.h"
#include "ui_ams_slot.h"
#include "ui_endless_spool_arrows.h"
#include "ui_filament_path_canvas.h"
#include "ui_spool_canvas.h"

#include "helix-xml/src/xml/lv_xml.h"

namespace helix::test {

inline void register_ams_sidebar_xml() {
    static bool done = false;
    if (done) {
        return;
    }
    ui_spool_canvas_register();
    ui_ams_slot_register();
    ui_filament_path_canvas_register();
    ui_endless_spool_arrows_register();
    helix::ui::AmsOperationSidebar::register_callbacks_static();
    lv_xml_register_component_from_file("A:ui_xml/components/ams_loaded_card.xml");
    lv_xml_register_component_from_file("A:ui_xml/components/ams_sidebar.xml");
    done = true;
}

} // namespace helix::test
