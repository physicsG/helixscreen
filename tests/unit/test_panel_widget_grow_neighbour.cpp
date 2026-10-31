// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_widget_grow_neighbour.cpp
 * @brief A saved widget that grows to draw its content never takes a saved
 *        neighbour's tracks.
 *
 * The anchored pass grows a saved span until the widget's fits_at() accepts
 * it. The neighbour on the far side of that growth holds a saved position of
 * its own, and it must keep it at its own span: growth goes into free tracks,
 * or not at all. Run with: ./build/bin/helix-tests "[grow_neighbour]"
 */

#include "../test_fixtures.h"
#include "../test_helpers/layout_manager_test_access.h"
#include "config.h"
#include "grid_layout.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "layout_manager.h"
#include "panel_widget.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;

/// Stand-in for any registry widget. `min_width_px` > 0 makes fits_at() refuse
/// narrower boxes, the way TipsWidget refuses a width that breaks a title.
struct GrowSpyWidget : helix::PanelWidget {
    GrowSpyWidget(std::string wid, int min_width_px) : wid_(std::move(wid)), min_w_(min_width_px) {}

    void attach(lv_obj_t*, lv_obj_t*) override {}
    void detach() override {}
    bool fits_at(int width_px, int /*height_px*/) const override {
        return width_px >= min_w_;
    }
    const char* id() const override {
        return wid_.c_str();
    }
    std::string get_component_name() const override {
        return "test_grow_spy_widget";
    }

  private:
    std::string wid_;
    int min_w_;
};

class ScopedGrowFactories {
  public:
    ScopedGrowFactories(const std::vector<std::pair<std::string, int>>& ids) {
        for (const auto& [id, min_w] : ids) {
            const auto* def = helix::find_widget_def(id);
            REQUIRE(def != nullptr);
            saved_.emplace_back(id, def->factory);
            const std::string wid = id;
            const int w = min_w;
            helix::register_widget_factory(id, [wid, w](const std::string&) {
                return std::unique_ptr<PanelWidget>(new GrowSpyWidget(wid, w));
            });
        }
    }
    ~ScopedGrowFactories() {
        for (auto& [id, factory] : saved_) {
            helix::register_widget_factory(id, factory);
        }
    }

  private:
    std::vector<std::pair<std::string, WidgetFactory>> saved_;
};

struct Cell {
    int col = -1, row = -1, colspan = -1, rowspan = -1;
};

Cell placed_cell(lv_obj_t* container, const char* widget_id) {
    Cell out;
    const uint32_t n = lv_obj_get_child_count(container);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t* child = lv_obj_get_child(container, i);
        const char* nm = child ? lv_obj_get_name(child) : nullptr;
        if (!nm || std::string(nm) != widget_id) {
            continue;
        }
        out.col = static_cast<int>(lv_obj_get_style_grid_cell_column_pos(child, LV_PART_MAIN));
        out.row = static_cast<int>(lv_obj_get_style_grid_cell_row_pos(child, LV_PART_MAIN));
        out.colspan = static_cast<int>(lv_obj_get_style_grid_cell_column_span(child, LV_PART_MAIN));
        out.rowspan = static_cast<int>(lv_obj_get_style_grid_cell_row_span(child, LV_PART_MAIN));
        return out;
    }
    return out;
}

nlohmann::json entry(const char* id, int col, int row, int colspan, int rowspan) {
    return {{"id", id},   {"enabled", true},    {"col", col},
            {"row", row}, {"colspan", colspan}, {"rowspan", rowspan}};
}

/// Page 1 is a secondary page, so no registry defaults are appended to it.
nlohmann::json make_layout(nlohmann::json widgets) {
    return nlohmann::json{{"main_page_index", 0},
                          {"next_page_id", 2},
                          {"pages",
                           {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                            {{"id", "spy"}, {"widgets", std::move(widgets)}}}}};
}

class GrowNeighbourFixture : public XMLTestFixture {
  public:
    GrowNeighbourFixture() {
        helix::init_widget_registrations();
        lv_xml_register_component_from_data(
            "test_grow_spy_widget",
            "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
        helix::LayoutManager::instance().init(kPanelW, kPanelH);
        lv_subject_t* bp_subj = theme_manager_get_breakpoint_subject();
        REQUIRE(bp_subj != nullptr);
        lv_subject_set_int(bp_subj, to_int(UiBreakpoint::Medium));

        cols = GridLayout::get_cols(UiBreakpoint::Medium, kPanelW, kPanelH);
        rows = GridLayout::get_rows(UiBreakpoint::Medium, kPanelW, kPanelH);
        // The layouts below need three widgets side by side on one band.
        REQUIRE(cols >= 12);
        REQUIRE(rows >= 4);
        const CellMetrics m =
            grid_cell_metrics(kPanelW, kPanelH, cols, rows, GridLayout::gutter_px());
        // Accepts five tracks and refuses four.
        five_tracks_px = static_cast<int>(grid_track_extent(m.cell_w, m.gutter, 5));
        REQUIRE(static_cast<int>(grid_track_extent(m.cell_w, m.gutter, 4)) < five_tracks_px);
    }
    ~GrowNeighbourFixture() override {
        LayoutManagerTestAccess::reset(helix::LayoutManager::instance());
    }

    lv_obj_t* populate(const std::string& panel_id, nlohmann::json widgets) {
        auto* cfg = Config::get_instance();
        cfg->set<nlohmann::json>(cfg->df() + "panel_widgets/" + panel_id,
                                 make_layout(std::move(widgets)));
        auto& mgr = PanelWidgetManager::instance();
        mgr.get_widget_config(panel_id).mark_dirty();
        mgr.clear_panel_config(panel_id);

        lv_obj_t* container = lv_obj_create(test_screen());
        lv_obj_set_style_pad_all(container, 0, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_size(container, kPanelW, kPanelH);
        process_lvgl(10);
        widgets_ = mgr.populate_widgets(panel_id, container, /*page_index=*/1);
        return container;
    }

    int cols = 0;
    int rows = 0;
    int five_tracks_px = 0;

  private:
    std::vector<std::unique_ptr<PanelWidget>> widgets_;
};

} // namespace

TEST_CASE_METHOD(GrowNeighbourFixture,
                 "A growing widget boxed in on both sides keeps its span and its neighbours",
                 "[panel_widget][manager][grow_neighbour]") {
    // The 1.0 home layout's top band at 800x480: tips between a widget on its
    // left and active_spool flush on its right, tips too narrow for its titles.
    ScopedGrowFactories f({{"lock", 0}, {"tips", five_tracks_px}, {"shutdown", 0}});
    const std::string panel_id = "test_grow_neighbour_boxed";

    nlohmann::json widgets = nlohmann::json::array();
    widgets.push_back(entry("lock", 2, 0, 2, 2));
    widgets.push_back(entry("tips", 4, 0, 4, 4));
    widgets.push_back(entry("shutdown", 8, 0, 2, 2));
    lv_obj_t* c = populate(panel_id, std::move(widgets));

    const Cell neighbour = placed_cell(c, "shutdown");
    CHECK(neighbour.col == 8);
    CHECK(neighbour.row == 0);
    CHECK(neighbour.colspan == 2);
    CHECK(neighbour.rowspan == 2);

    const Cell left = placed_cell(c, "lock");
    CHECK(left.col == 2);
    CHECK(left.colspan == 2);

    const Cell tips = placed_cell(c, "tips");
    CHECK(tips.col == 4);
    CHECK(tips.row == 0);
    CHECK(tips.colspan == 4);
    CHECK(tips.rowspan == 4);

    PanelWidgetManager::instance().clear_panel_config(panel_id);
}

TEST_CASE_METHOD(GrowNeighbourFixture,
                 "A growing widget with free tracks on its left grows into them",
                 "[panel_widget][manager][grow_neighbour]") {
    ScopedGrowFactories f({{"tips", five_tracks_px}, {"shutdown", 0}});
    const std::string panel_id = "test_grow_neighbour_free_left";

    nlohmann::json widgets = nlohmann::json::array();
    widgets.push_back(entry("tips", 4, 0, 4, 4));
    widgets.push_back(entry("shutdown", 8, 0, 2, 2));
    lv_obj_t* c = populate(panel_id, std::move(widgets));

    const Cell neighbour = placed_cell(c, "shutdown");
    CHECK(neighbour.col == 8);
    CHECK(neighbour.row == 0);
    CHECK(neighbour.colspan == 2);
    CHECK(neighbour.rowspan == 2);

    const Cell tips = placed_cell(c, "tips");
    CHECK(tips.col == 3);
    CHECK(tips.row == 0);
    CHECK(tips.colspan == 5);
    CHECK(tips.rowspan == 4);

    PanelWidgetManager::instance().clear_panel_config(panel_id);
}

TEST_CASE_METHOD(GrowNeighbourFixture,
                 "A shifted grown seat is drawn but never persisted over the saved origin",
                 "[panel_widget][manager][grow_neighbour]") {
    ScopedGrowFactories f({{"tips", five_tracks_px}, {"shutdown", 0}, {"lock", 0}});
    const std::string panel_id = "test_grow_neighbour_persist";
    auto* cfg = Config::get_instance();
    const std::string panel_path = cfg->df() + "panel_widgets/" + panel_id;

    nlohmann::json widgets = nlohmann::json::array();
    widgets.push_back(entry("tips", 4, 0, 4, 4));
    widgets.push_back(entry("shutdown", 8, 0, 2, 2));
    // Unpositioned, so its auto-placement makes this populate save.
    widgets.push_back(entry("lock", -1, -1, 2, 2));
    lv_obj_t* c = populate(panel_id, std::move(widgets));

    // Premise: this grid draws tips shifted left and grown.
    const Cell tips = placed_cell(c, "tips");
    REQUIRE(tips.col == 3);
    REQUIRE(tips.colspan == 5);

    // Reload from the JSON store.
    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);
    const nlohmann::json after = cfg->get<nlohmann::json>(panel_path, nlohmann::json());
    INFO("persisted layout: " << after.dump());
    REQUIRE(after.contains("pages"));

    const nlohmann::json* saved_tips = nullptr;
    const nlohmann::json* saved_lock = nullptr;
    for (const auto& w : after["pages"][1]["widgets"]) {
        if (w["id"] == "tips")
            saved_tips = &w;
        if (w["id"] == "lock")
            saved_lock = &w;
    }
    REQUIRE(saved_tips != nullptr);
    REQUIRE(saved_lock != nullptr);
    // The save really happened: the auto-placed widget got a position.
    REQUIRE((*saved_lock)["col"].get<int>() >= 0);

    CHECK((*saved_tips)["col"].get<int>() == 4);
    CHECK((*saved_tips)["row"].get<int>() == 0);
    CHECK((*saved_tips)["colspan"].get<int>() == 4);
    CHECK((*saved_tips)["rowspan"].get<int>() == 4);

    const auto& entries = mgr.get_widget_config(panel_id).page_entries(1);
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [](const PanelWidgetEntry& e) { return e.id == "tips"; });
    REQUIRE(it != entries.end());
    CHECK(it->col == 4);
    CHECK(it->row == 0);
    CHECK(it->colspan == 4);
    CHECK(it->rowspan == 4);

    mgr.clear_panel_config(panel_id);
}
