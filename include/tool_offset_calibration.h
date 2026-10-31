// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Automatic tool offset calibration.
//
// klipper-toolchanger ships a CALIBRATE_TOOL_OFFSETS macro
// (examples/calibrate-offsets.cfg) that does the whole job in one blocking
// gcode: heats every nozzle, locates the calibration sensor with the first
// tool, then for every other tool selects it, probes it against the sensor and
// writes the result into that tool's gcode_x/y/z_offset with SET_TOOL_PARAMETER
// + SAVE_TOOL_PARAMETER. Nothing in the macro persists: the staged parameters
// wait for a SAVE_CONFIG, which is exactly what the per-tool offset save path
// (helix::tool_offsets, ToolState's dirty tracking) already handles.
//
// This module is the ONLY place that decides which printers calibrate their
// tool offsets and how. resolve_procedure() walks one table of procedures, in
// precedence order: a printer's own multi-step guided flow (a Snapmaker U1's
// measurement, an INDX's seat-by-hand), the macro above run by the tool offset
// panel, and a changer firmware's macro that only opens a printer-side
// action:prompt, which the entry sends without opening the panel. Callers ask
// that one question and never name the firmware. There is no per-tool entry
// point: every procedure is all-or-nothing.
//
// Nothing is parsed off the console. Everything the screen needs is already in
// subscribed status: which tool is on the carriage (`toolchanger`.tool_number,
// ToolState's active tool), which tools have been measured (the macro's
// SET_TOOL_PARAMETER writes land on the `tool T<n>` objects and reach
// ToolState through helix::tool_offsets), and when the run is over (the rpc
// completes). The panel shows the run's status line and marks the row for the
// tool currently on the carriage; it infers no per-tool progress beyond that,
// because the macro reports none.

#include <string>

namespace helix {
class PrinterDiscovery;
}

namespace helix::tool_offset_calibration {

/// What the Tool Offsets entry does on a printer.
enum class ProcedureKind {
    None,          ///< No automatic calibration: the entry is hidden
    Panel,         ///< The tool offset panel runs a blocking macro (gcode)
    GuidedFlow,    ///< A guided device flow runs (flow_id; ui_guided_device_flow.h)
    PrinterPrompt, ///< gcode opens the printer's own action:prompt, which confirms
                   ///< and runs the measurement; its rpc answers before anything
                   ///< moves, so nothing on this side can follow the run
};

struct Procedure {
    ProcedureKind kind = ProcedureKind::None;
    /// Which row matched, for logs. Empty when none did.
    const char* name = "";
    /// Panel: the blocking macro, its completion the run's. PrinterPrompt: the
    /// prompt-opening macro. Empty otherwise.
    std::string gcode;
    /// GuidedFlow: the flow to start. A printer's own flow may be a setup step
    /// its offsets depend on rather than a measurement (an INDX seats a tool by
    /// hand for its first Z home).
    std::string flow_id;
    /// What the entry does, untranslated (pass it through lv_tr()).
    const char* entry_description = "";
};

/// The procedure the Tool Offsets entry runs on this printer.
Procedure resolve_procedure(const PrinterDiscovery& hw);

/// Whether the printer offers any tool offset calibration.
bool supported(const PrinterDiscovery& hw);

} // namespace helix::tool_offset_calibration
