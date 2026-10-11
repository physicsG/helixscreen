// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_context_menu.h"
#include "ui_observer_guard.h"

#include "ams_error.h"
#include "async_lifetime_guard.h"
#include "src/ui/panel_widgets/tiled_panel_widget.h"
#include "subject_managed_panel.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace helix {

class PrinterState;
class ToolSwitcherTestAccess;

class ToolSwitcherWidget : public TiledPanelWidget {
  public:
    explicit ToolSwitcherWidget(PrinterState& printer_state);
    ~ToolSwitcherWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return "tool_switcher";
    }
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    /// Pills lay themselves out in any box they are given; the compact form
    /// is a sized tile and refuses what TileSizing cannot draw.
    bool fits_at(int width_px, int height_px) const override {
        return !is_compact_at(width_px, height_px) || sizing_.fits(width_px, height_px);
    }
    bool has_overlay_open() const override {
        return picker_.is_visible();
    }

    // Static instance tracker for callbacks from static event handlers
    static ToolSwitcherWidget* s_active_instance;

  private:
    friend class ToolSwitcherTestAccess;

    /// Single-select list of the printer's tools, raised by a tap on the compact
    /// tile. Picking a row issues the change; a tap outside it chooses nothing.
    class ToolPicker : public helix::ui::ContextMenu {
        HELIX_CONTEXT_MENU_KIND(ToolPicker)

      public:
        explicit ToolPicker(ToolSwitcherWidget& owner) : owner_(owner) {}

      protected:
        const char* xml_component_name() const override {
            return "tool_switcher_picker";
        }
        void on_created(lv_obj_t* backdrop) override;

      private:
        ToolSwitcherWidget& owner_;
    };

    PrinterState& printer_state_;
    lv_obj_t* widget_obj_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    ToolPicker picker_{*this};
    /// Compact-mode tool label. Held so the print gate can grey it without
    /// re-running the whole rebuild; nulled by rebuild_pills() and detach().
    lv_obj_t* compact_label_ = nullptr;

    // Physical size the widget was last granted, cached for observer paths
    // (tool_count_observer_, on_active_tool_changed()) that fire later and
    // need to know which layout is currently built without a size to read.
    int current_width_px_ = 0;
    int current_height_px_ = 0;

    ObserverGuard active_tool_observer_;
    ObserverGuard tool_count_observer_;
    ObserverGuard print_state_observer_;

    std::vector<lv_obj_t*> pill_buttons_;

    // Grid layout descriptors for multi-row pill layout. LVGL stores these
    // pointers (no copy), so the backing arrays must outlive the layout.
    std::vector<int32_t> grid_col_dsc_;
    std::vector<int32_t> grid_row_dsc_;

    // tool_switcher_container, cached so teardown can strip its grid layout:
    // LVGL keeps pointers into grid_col_dsc_/grid_row_dsc_ rather than copies.
    lv_obj_t* pill_container_ = nullptr;

    /// 1 while the compact form is shown; XML hides the other form.
    lv_subject_t compact_subject_{};
    lv_subject_t active_label_subject_{};
    char active_label_buf_[32] = {};
    SubjectManager subjects_;

    // MUST stay declared LAST: reverse-declaration destruction makes this the
    // first member torn down, invalidating every captured token before any
    // observer destructs. Without this, queued observer callbacks captured
    // via tok.defer() see token.expired() == false after the observers are
    // already gone and dereference a half-destroyed widget. See temp_stack_widget.h
    // (commit 45abc8c2a, bundle AX3CKAKB).
    helix::AsyncLifetimeGuard lifetime_;

    void rebuild_pills();
    void rebuild_compact();
    void show_tool_picker();
    void handle_tool_selected(int tool_index);

    /// Null every cached widget pointer (pills, compact label, container
    /// hooks, tile root) without touching observers or the widget tree.
    /// Shared by detach() and the raw-delete hook.
    void forget_tile_widgets();

    void on_hooked_root_deleted() override;

    // Layout predicates over the cached granted size — shared by the readers
    // that fire from on_size_changed() itself and the ones that fire later
    // from observers (tool_count_observer_, on_active_tool_changed()).
    bool is_compact_size() const;
    /// Pills show only when every pill fits legibly in this box; otherwise the
    /// compact form does.
    static bool is_compact_at(int width_px, int height_px);

    /// Columns and rows of equal pill cells; zero when no arrangement fits.
    struct PillGrid {
        int cols = 0;
        int rows = 0;
        bool fits() const {
            return cols > 0;
        }
    };
    /// The arrangement of equal cells, each holding the widest tool label
    /// legibly in a width_px x height_px tile, whose cells are squarest.
    static PillGrid pill_grid_at(int width_px, int height_px);
    /// Budget the compact value at the widest label any tool carries.
    void refresh_label_budget();
    void on_active_tool_changed(int tool_index);

    /**
     * @brief Whether a running print blocks a tool change, and why.
     *
     * The same question every other filament surface asks — the filament panel,
     * the AMS sidebar and the AMS context menu all route through
     * helix::ui::print_blocks_filament_op(), the mirror of
     * AmsSubscriptionBackend::refuse_if_printing(). PRINTING always refuses;
     * PAUSED refuses only on a backend whose filament macro homes itself
     * (AD5X IFS). A tool change on a shared-toolhead AMS *is* a filament op, so
     * it gets the same gate rather than a policy of its own.
     *
     * @return AmsErrorHelper::print_active() when blocked, success() otherwise.
     */
    [[nodiscard]] AmsError tool_change_refusal() const;

    /// Grey the pills / compact label whenever tool_change_refusal() refuses.
    /// Called from both rebuild paths and from the print-state observer, so a
    /// recycled instance re-applies it on attach as well as on state change.
    void refresh_print_gating();

    /// Issue the change and report any refusal. Static so the confirmation
    /// modal's stateless event callback shares the one on_error path.
    static void dispatch_tool_change(int tool_index);

  public:
    static void tool_compact_cb(lv_event_t* e);
};

void register_tool_switcher_widget();

} // namespace helix
