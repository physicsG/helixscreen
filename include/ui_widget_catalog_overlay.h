// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"
#include "panel_widget_registry.h"

#include <functional>
#include <string>
#include <vector>

namespace helix {

class PanelWidgetConfig;

/// Callback invoked when the user selects a widget from the catalog.
/// Receives the widget definition ID (e.g. "temperature", "network").
using WidgetSelectedCallback = std::function<void(const std::string& widget_id)>;

/// Callback invoked when the catalog overlay is closed (selection or back navigation).
using CatalogClosedCallback = std::function<void()>;

/// Shows a half-width overlay listing the widget categories available for grid
/// placement. Picking a category dives into a sub-page listing that category's
/// widgets, using the same header/back contract as the settings sub-pages.
/// Widgets already placed are shown dimmed with a "Placed" badge.
/// On selection, the callback fires and both levels close.
class WidgetCatalogOverlay {
  public:
    /// Open the catalog overlay.
    /// @param parent_screen  Screen to parent the overlay on
    /// @param config         Current widget config (to determine which are already placed)
    /// @param on_select      Called with the chosen widget ID when user taps a row
    /// @param on_close       Called when the overlay is closed for any reason
    static void show(lv_obj_t* parent_screen, const PanelWidgetConfig& config,
                     WidgetSelectedCallback on_select, CatalogClosedCallback on_close = nullptr);

    /// Widget defs belonging to @p category, in registry order.
    ///
    /// Pure — reads the registry and nothing else — so the grouping can be
    /// asserted without standing up an overlay.
    static std::vector<const PanelWidgetDef*> widgets_in_category(WidgetCategory category);

    /// Root of the open catalog overlay, or nullptr when it is closed.
    static lv_obj_t* active_root();

    /// Root of the open category sub-page, or nullptr when no dive is active.
    static lv_obj_t* active_category_root();

  private:
    /// Push the sub-page listing one category's widgets on top of the catalog.
    static void show_category(WidgetCategory category);

    /// Click dispatch for the top-level category rows.
    static void on_category_row_clicked(lv_event_t* e);

    /// Populate the category list with one row per registered category
    static void populate_category_rows(lv_obj_t* group);

    /// Populate a sub-page's scroll container with one row per widget in @p category
    static void populate_rows(lv_obj_t* scroll, const PanelWidgetConfig& config,
                              WidgetCategory category);

    /// Create a single catalog row widget
    static lv_obj_t* create_row(lv_obj_t* parent, const char* name, const char* icon,
                                const char* description, int colspan, int rowspan,
                                bool already_placed, bool hardware_gated);
};

} // namespace helix
