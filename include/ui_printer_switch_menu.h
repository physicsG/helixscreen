// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_context_menu.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace helix::ui {

/**
 * @brief Context menu for switching between configured printers
 *
 * Shows a popup near the navbar printer badge listing all configured printers
 * with a checkmark on the active one, plus an "Add Printer" button that
 * launches the setup wizard.
 */
class PrinterSwitchMenu : public ContextMenu {
    HELIX_CONTEXT_MENU_KIND(PrinterSwitchMenu)

  public:
    enum class MenuAction {
        SWITCH,
        ADD_PRINTER,
        CANCELLED,
    };

    using SwitchCallback = std::function<void(MenuAction action, const std::string& printer_id)>;

    PrinterSwitchMenu() = default;

    void show(lv_obj_t* parent, lv_obj_t* near_widget);

    void set_switch_callback(SwitchCallback callback) {
        switch_callback_ = std::move(callback);
    }

    static void register_callbacks();

  protected:
    const char* xml_component_name() const override {
        return "printer_switch_menu";
    }
    void on_created(lv_obj_t* menu) override;
    void on_backdrop_clicked() override;

    /// As wide as the longest printer name, up to half a landscape screen or the whole
    /// width of a portrait one less its margins; past that, names ellipsize.
    CardWidth card_width() const override {
        return {50, 160, 0, true, 100};
    }
    int32_t content_width() const override {
        return widest_row_;
    }

  private:
    void populate_printer_list();
    /// Sets widest_row_ from the printer rows, the header and the Add button, laid out once
    void measure_rows(lv_obj_t* printer_list,
                      const std::vector<std::pair<lv_obj_t*, std::string>>& rows);
    void handle_printer_selected(const std::string& printer_id);
    void handle_add_printer();
    void dispatch_switch_action(MenuAction action, const std::string& printer_id = "");

    SwitchCallback switch_callback_;

    /// Widest printer row populate_printer_list() built, inside the card's padding
    int32_t widest_row_ = 0;

    static bool s_callbacks_registered_;

    /// The menu on screen as a PrinterSwitchMenu, or nullptr. Thin wrapper over
    /// ContextMenu::active_as() that also logs the unexpected empty case.
    static PrinterSwitchMenu* get_active_instance();
    static void on_add_printer_cb(lv_event_t* e);
    static void on_printer_row_cb(lv_event_t* e);
};

} // namespace helix::ui
