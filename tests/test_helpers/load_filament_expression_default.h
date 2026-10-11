// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix::test {

/// A LOAD_FILAMENT gcode_macro body exactly as configfile.settings reports it on
/// an OpenCentauri COSMOS printer. Its only parameter, EXTRUDER_TEMP, defaults to
/// a printer-side variable lookup (global.extruder_temp.loading), not a number.
inline constexpr const char* LOAD_FILAMENT_EXPRESSION_DEFAULT = R"GCODE(
{% set global = printer['gcode_macro _global_var'] %}
{% set target = params.EXTRUDER_TEMP | default(global.extruder_temp.loading) | int %}
{% set settings = printer['gcode_macro _COSMOS_SETTINGS']|default({}) %}
{% set toolhead_led = settings.toolhead_led|default('false')|lower == 'true' %}
_CLOSE_PROMPT

RESPOND TYPE=command MSG="action:prompt_begin Load Filament"
RESPOND TYPE=command MSG="action:prompt_text Wait for hotend to heat up"
RESPOND TYPE=command MSG="action:prompt_show"

{% if toolhead_led %}
SET_LED LED=hotend WHITE=1
{% endif %}

M104 S{global.extruder_temp.probing}

_CG28 AXIS=XY

MOVE_TO_TRAY
M104 S{target}
TEMPERATURE_WAIT SENSOR=extruder MINIMUM={target} MAXIMUM={target+5}
M107
M400

RESPOND TYPE=command MSG="action:prompt_begin Load Filament"
RESPOND TYPE=command MSG="action:prompt_text Insert filament all the way into extruder before continuing"
RESPOND TYPE=command MSG="action:prompt_footer_button LOAD|_LOAD_FILAMENT_STEP_PUSH"
RESPOND TYPE=command MSG="action:prompt_footer_button CANCEL|_LOAD_FILAMENT_CANCEL"
RESPOND TYPE=command MSG="action:prompt_show")GCODE";

} // namespace helix::test
