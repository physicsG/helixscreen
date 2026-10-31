// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_offset_calibration.h"

#include "indx_contract.h"
#include "printer_discovery.h"
#include "toolchanger_addon.h"
#include "translation_loader.h"

#if HELIX_HAS_SNAPMAKER
#include "snapmaker_nozzle_offset.h"
#endif

namespace helix::tool_offset_calibration {

namespace {

constexpr const char* kMeasureDescription =
    TR_NOOP("Measure every tool's XYZ offset automatically");

/// klipper-toolchanger's example macro. The command alias is always the
/// uppercased name whatever case printer.cfg spells the section in
/// (klippy/extras/gcode_macro.py), so the constant is the right spelling to
/// send - unlike a SET_GCODE_VARIABLE MACRO= mux key.
constexpr const char* kPanelMacro = "CALIBRATE_TOOL_OFFSETS";

/// Z-Mod's Creator 5 calibration (c5pro_generic.cfg): opens "remove the build
/// plate" in an action:prompt whose OK runs _T_CALIBRATE_EXTRUDERS.
constexpr const char* kPromptMacro = "CALIBRATE_EXTRUDERS";

/// One way a printer calibrates its tool offsets.
struct Row {
    const char* name;
    bool (*detect)(const PrinterDiscovery& hw);
    ProcedureKind kind;
    /// The panel or prompt macro, or the guided flow id.
    const char* target;
    const char* entry_description;
};

/// A tool changer in any dialect: klipper-toolchanger, or a changer firmware
/// toolchanger_addon claims.
bool is_tool_changer(const PrinterDiscovery& hw) {
    return hw.has_tool_changer() || toolchanger_addon::present(hw);
}

bool panel_detect(const PrinterDiscovery& hw) {
    return hw.has_tool_changer() && hw.has_macro(kPanelMacro);
}

bool prompt_detect(const PrinterDiscovery& hw) {
    return is_tool_changer(hw) && hw.has_macro(kPromptMacro);
}

/// Precedence order: the first row that detects wins.
const Row kRows[] = {
#if HELIX_HAS_SNAPMAKER
    {"Snapmaker nozzle offset", snapmaker::nozzle_offset::supported, ProcedureKind::GuidedFlow,
     snapmaker::nozzle_offset::kGuidedFlowId, kMeasureDescription},
#endif
    {"INDX seat by hand", indx::supports_seat_by_hand, ProcedureKind::GuidedFlow,
     indx::kSeatByHandFlowId, TR_NOOP("Seat a tool by hand for the first Z home")},
    {"klipper-toolchanger", panel_detect, ProcedureKind::Panel, kPanelMacro, kMeasureDescription},
    {"changer firmware prompt", prompt_detect, ProcedureKind::PrinterPrompt, kPromptMacro,
     kMeasureDescription},
};

} // namespace

Procedure resolve_procedure(const PrinterDiscovery& hw) {
    for (const auto& row : kRows) {
        if (!row.detect(hw)) {
            continue;
        }
        Procedure p;
        p.kind = row.kind;
        p.name = row.name;
        p.entry_description = row.entry_description;
        if (row.kind == ProcedureKind::GuidedFlow) {
            p.flow_id = row.target;
        } else {
            p.gcode = row.target;
        }
        return p;
    }
    return {};
}

bool supported(const PrinterDiscovery& hw) {
    return resolve_procedure(hw).kind != ProcedureKind::None;
}

} // namespace helix::tool_offset_calibration
