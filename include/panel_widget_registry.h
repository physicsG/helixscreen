// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace helix {

class PanelWidget;

using WidgetFactory = std::function<std::unique_ptr<PanelWidget>(const std::string& instance_id)>;
using SubjectInitFn = std::function<void()>;

/// Grouping used to organize the Add Widget catalog.
///
/// Enumerator order is the display order in the catalog, ordered by how often
/// people reach for a group rather than alphabetically: the print you are
/// watching first, the machine housekeeping you rarely touch last.
///
/// These are deliberately coarser than the section list in
/// docs/user/guide/home-panel.md § "Available Widgets". That list is a
/// reference index you read top to bottom; this is a menu you navigate on a
/// 480px panel, where a category holding two widgets costs two taps to reach
/// two things and earns nothing.
enum class WidgetCategory {
    PrintStatus,
    Temperature,
    Filament,
    Controls,
    System,
};

struct WidgetCategoryDef {
    WidgetCategory id;
    const char* display_name;
    const char* translation_tag; // For i18n
    const char* icon;            // Icon name from ui_icon_codepoints.h
};

struct PanelWidgetDef {
    const char* id;                    // Stable string for JSON config
    const char* display_name;          // For settings overlay UI
    const char* icon;                  // Icon name
    const char* description;           // Short description for settings overlay
    const char* hardware_gate_subject; // nullptr = always available
    const char* hardware_gate_hint; // Human-readable reason, e.g., "Requires AMS or MMU hardware"

    /// Catalog grouping. Deliberately has no default: it sits before the
    /// defaulted fields so every table entry must name one positionally, and a
    /// new widget cannot silently land in whichever category happens to be
    /// first.
    WidgetCategory category;

    bool default_enabled = true; // Whether enabled in fresh/default config
    int colspan = 1;             // Default grid columns spanned
    int rowspan = 1;             // Default grid rows spanned
    int min_colspan = 0;         // Minimum columns (0 = use colspan)
    int min_rowspan = 0;         // Minimum rows (0 = use rowspan)
    int max_colspan = 0;         // Maximum columns (0 = use colspan, i.e. not scalable)
    int max_rowspan = 0;         // Maximum rows (0 = use rowspan, i.e. not scalable)
    bool multi_instance = false; // Allows dynamic instance creation with base_id:N IDs

    /// True when this widget may occupy half a cell on that axis (#1126).
    ///
    /// The home grid lays out half-cell tracks, so an authored colspan of 1 is
    /// GridLayout::TRACKS_PER_CELL tracks wide. A widget with the flag set may
    /// also be placed and sized at odd track counts; edit mode snaps everything
    /// else to even boundaries so a whole-cell widget can never straddle two.
    ///
    /// Set it on an axis when the widget's content is CONTINUOUS along it - a
    /// chart, an aspect-fit image, wrapping text, a scrolling strip, stacked
    /// readout rows, or a layout chosen by measurement (active_spool's
    /// compact/wide switch, decide_nozzle_layout()). Half a cell of extra room
    /// shows more there. Leave it off for a centred fixed glyph with a short
    /// label - network, led, filament, humidity, the heater tiles - where the
    /// intermediate size buys whitespace and nothing else, and costs a drag
    /// snap that is twice as fussy on a 34px track.
    ///
    /// The minimum on every axis is a whole cell, so this only ever ADDS sizes
    /// above a size the content already fits. It never makes a widget smaller.
    bool supports_half_col = false;
    bool supports_half_row = false;

    /// True when this widget wants the home panel's shared card background
    /// drawn behind it, fused with its neighbours'.
    ///
    /// Most home widgets paint nothing — `extends="lv_obj"` inherits
    /// StyleRole::ObjBase, which is fully transparent — and would render on the
    /// bare panel background without this. The exceptions are the few that
    /// bring their own surface: print_status and camera extend ui_card,
    /// nozzle_temps hand-rolls one, and ams nests a card in its spool view.
    /// Fusing behind those would stack two cards and show a box inside a box.
    ///
    /// printer_image and tips opt out for looks rather than duplication: the
    /// printer render is meant to float on the panel background and tips is a
    /// left accent rule, not a tile.
    ///
    /// This used to be inferred from the span — a widget exactly one cell
    /// square merged, anything else was assumed to paint its own background.
    /// That conflated two unrelated questions and was wrong in both directions:
    /// a 2x1 fan_stack got no background at all, and a one-cell ams got two.
    bool merges_into_card = true;

    WidgetFactory factory = nullptr;       // nullptr = pure XML or externally managed
    SubjectInitFn init_subjects = nullptr; // Called once before XML creation

    // Resolved accessors (0 = "use default colspan/rowspan")
    int effective_min_colspan() const {
        return min_colspan > 0 ? min_colspan : colspan;
    }
    int effective_min_rowspan() const {
        return min_rowspan > 0 ? min_rowspan : rowspan;
    }
    int effective_max_colspan() const {
        return max_colspan > 0 ? max_colspan : colspan;
    }
    int effective_max_rowspan() const {
        return max_rowspan > 0 ? max_rowspan : rowspan;
    }
    bool is_scalable() const {
        return effective_max_colspan() > effective_min_colspan() ||
               effective_max_rowspan() > effective_min_rowspan();
    }
};

/// Categories in catalog display order.
const std::vector<WidgetCategoryDef>& get_widget_categories();
/// nullptr when `id` is not one of the enumerators.
const WidgetCategoryDef* find_widget_category(WidgetCategory id);

const std::vector<PanelWidgetDef>& get_all_widget_defs();
const PanelWidgetDef* find_widget_def(std::string_view id);
size_t widget_def_count();
void register_widget_factory(std::string_view id, WidgetFactory factory);
void register_widget_subjects(std::string_view id, SubjectInitFn init_fn);
// Internal — called once from PanelWidgetManager::init_widget_subjects().
// Do not call directly; widget factories require runtime context (singletons, shared resources).
void init_widget_registrations();

} // namespace helix
