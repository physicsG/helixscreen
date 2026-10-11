// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_leader_line.h"

#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_parser.h"
#include "helix-xml/src/xml/lv_xml_widget.h"
#include "helix-xml/src/xml/parsers/lv_xml_obj_parser.h"
#include "lvgl/lvgl.h"

namespace {

void* leader_line_create(lv_xml_parser_state_t* state, const char** /*attrs*/) {
    return lv_line_create(static_cast<lv_obj_t*>(lv_xml_state_get_parent(state)));
}

} // namespace

void helix::ui::register_leader_line_widget() {
    lv_xml_register_widget("leader_line", leader_line_create, lv_xml_obj_apply);
}
