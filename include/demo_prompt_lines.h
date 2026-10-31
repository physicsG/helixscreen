// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

namespace helix::demo {

/**
 * @brief Z-Mod's color picker, line for line as its CHANGE_ZCOLOR macro sends it
 *
 * One text line, three groups of eight label-less swatches and a footer Cancel
 * (prestonbrown/helixscreen#1806). Shared by `ctl demo action-prompt-zmod` and
 * the action prompt layout tests, so both show the same prompt.
 */
inline std::vector<std::string> zmod_color_picker_lines() {
    static constexpr const char* kSlotCmd = "CHANGE_ZCOLOR SLOT=1 TYPE=PETG-CF HEX=";
    static constexpr const char* const kGroups[3][8] = {
        {"ffffff", "fff245", "def578", "21cc3d", "167a4b", "156682", "24e4a0", "7bd9f0"},
        {"4caaf8", "2e54dd", "48358c", "a341f7", "f435f6", "d584de", "fa6173", "f82d29"},
        {"805003", "f9903b", "fcebd7", "d5c5a1", "b17c38", "8c8c89", "bebebe", "1b1b1b"},
    };
    std::vector<std::string> lines = {
        "// action:prompt_begin Выберите цвет",
        "// action:prompt_text Катушка 1: PETG-CF/",
    };
    for (const auto& group : kGroups) {
        lines.emplace_back("// action:prompt_button_group_start");
        for (const char* hex : group) {
            lines.push_back(std::string("// action:prompt_button _ |") + kSlotCmd + hex +
                            " HIDE=0|" + hex);
        }
        lines.emplace_back("// action:prompt_button_group_end");
    }
    lines.emplace_back(
        "// action:prompt_footer_button Отмена|RESPOND TYPE=command MSG=action:prompt_end");
    lines.emplace_back("// action:prompt_show");
    return lines;
}

/**
 * @brief AFC_CALIBRATION's unit picker on a six-unit AFC, line for line
 *
 * One button per unit in alternating primary/secondary, then the "All Lanes"
 * and Cancel footer buttons, as AFC_functions.py#cmd_AFC_CALIBRATION sends
 * them. Shared by `ctl demo action-prompt-afc-cal` and the action prompt
 * layout tests.
 */
inline std::vector<std::string> afc_calibration_lines() {
    static constexpr const char* const kUnits[] = {"Turtle_1",        "Vivid_1", "EMU_1",
                                                   "HTLF_claymore_1", "AMS_1",   "Ace2_1"};
    std::vector<std::string> lines = {
        "// action:prompt_begin AFC Calibration",
        "// action:prompt_text The following prompts will lead you through the calibration of "
        "your AFC unit(s). First, select a unit to calibrate. *All values will be automatically "
        "updated in the appropriate config sections.",
    };
    int index = 0;
    for (const char* unit : kUnits) {
        lines.push_back(std::string("// action:prompt_button ") + unit + "|UNIT_CALIBRATION UNIT=" +
                        unit + "|" + (index++ % 2 == 0 ? "primary" : "secondary"));
    }
    lines.emplace_back(
        "// action:prompt_footer_button All Lanes in all units|ALL_CALIBRATION|secondary");
    lines.emplace_back(
        "// action:prompt_footer_button Cancel|RESPOND TYPE=command MSG=action:prompt_end|warning");
    lines.emplace_back("// action:prompt_show");
    return lines;
}

/**
 * @brief Buttons colored by hex in the third field, as Fluidd, Mainsail and
 * Z-Mod send them, then two rows of label-less swatch tiles and a Back footer
 * (prestonbrown/helixscreen#1806). Shared by `ctl demo action-prompt-hex` and
 * the action prompt layout tests.
 */
inline std::vector<std::string> hex_color_prompt_lines() {
    static constexpr const char* const kPalette[2][6] = {
        {"ffffff", "fef043", "0acc38", "21cc3d", "2750e0", "a03cf7"},
        {"f330f9", "f72224", "7c4b00", "898989", "161616", "#0de2a0"},
    };
    std::vector<std::string> lines = {
        "// action:prompt_begin Select color",
        "// action:prompt_text Pick a color for slot 1.",
        "// action:prompt_button Standard|GCODE_CMD_1|error",
        "// action:prompt_button HEX Dark (White Text)|GCODE_CMD_2|ff5722",
        "// action:prompt_button HEX Light (Black Text)|GCODE_CMD_3|e0e0e0",
    };
    for (const auto& row : kPalette) {
        lines.emplace_back("// action:prompt_button_group_start");
        for (const char* hex : row) {
            lines.push_back(std::string("// action:prompt_button _ |CHANGE_ZCOLOR SLOT=1 HEX=") +
                            hex + "|" + hex);
        }
        lines.emplace_back("// action:prompt_button_group_end");
    }
    lines.emplace_back("// action:prompt_footer_button Back||secondary");
    lines.emplace_back("// action:prompt_show");
    return lines;
}

} // namespace helix::demo
