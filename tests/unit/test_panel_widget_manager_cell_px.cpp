// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_widget_manager_cell_px.cpp
 * @brief The pixel size a widget is told must match the size it is given.
 *
 * populate_widgets() promises each widget a pixel size via on_size_changed(),
 * then LVGL lays the same widget out from the grid descriptor. The descriptor
 * has inter-track gutters; the promise is computed from the ungapped content
 * width. LVGL's own geometry is therefore the oracle: whatever it renders is
 * what the widget should have been told.
 */

#include "../test_fixtures.h"
#include "config.h"
#include "grid_layout.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "panel_widget.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "theme_manager.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Records the pixel size populate_widgets() promised, alongside the LVGL
/// object it was promised for, so the promise can be checked against the
/// geometry the grid actually produced.
struct SizeOracleWidget : helix::PanelWidget {
    static lv_obj_t* s_obj;
    static int s_width_px;
    static int s_height_px;

    static void reset() {
        s_obj = nullptr;
        s_width_px = -1;
        s_height_px = -1;
    }

    void attach(lv_obj_t* widget_obj, lv_obj_t*) override {
        s_obj = widget_obj;
    }
    void detach() override {}
    void on_size_changed(int, int, int width_px, int height_px) override {
        s_width_px = width_px;
        s_height_px = height_px;
    }
    const char* id() const override {
        return "shutdown";
    }
    std::string get_component_name() const override {
        return "test_size_oracle_widget";
    }
};

lv_obj_t* SizeOracleWidget::s_obj = nullptr;
int SizeOracleWidget::s_width_px = -1;
int SizeOracleWidget::s_height_px = -1;

/// Swap a registry factory for the duration of a test and restore it after.
class ScopedOracleFactory {
  public:
    explicit ScopedOracleFactory(const char* id) : id_(id) {
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        original_ = def->factory;
        helix::register_widget_factory(id, [](const std::string&) {
            return std::unique_ptr<PanelWidget>(new SizeOracleWidget());
        });
    }
    ~ScopedOracleFactory() {
        helix::register_widget_factory(id_, original_);
    }

  private:
    const char* id_;
    WidgetFactory original_;
};

/// Records the pixel size promised for a MULTI-CELL widget (colspan >= 2).
/// The colspan==1 oracle above cannot exercise LVGL's per-track remainder
/// distribution — with only one track in play there is nothing to distribute
/// unevenly. A span multiplies grid_cell_metrics()'s single uniform cell
/// value, so this is the case that actually stresses the approximation.
struct SpanOracleWidget : helix::PanelWidget {
    static lv_obj_t* s_obj;
    static int s_width_px;
    static int s_height_px;

    static void reset() {
        s_obj = nullptr;
        s_width_px = -1;
        s_height_px = -1;
    }

    void attach(lv_obj_t* widget_obj, lv_obj_t*) override {
        s_obj = widget_obj;
    }
    void detach() override {}
    void on_size_changed(int, int, int width_px, int height_px) override {
        s_width_px = width_px;
        s_height_px = height_px;
    }
    const char* id() const override {
        return "tips";
    }
    std::string get_component_name() const override {
        return "test_size_oracle_widget_span";
    }
};

lv_obj_t* SpanOracleWidget::s_obj = nullptr;
int SpanOracleWidget::s_width_px = -1;
int SpanOracleWidget::s_height_px = -1;

/// Swap a registry factory for the duration of a test and restore it after.
class ScopedSpanOracleFactory {
  public:
    explicit ScopedSpanOracleFactory(const char* id) : id_(id) {
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        original_ = def->factory;
        helix::register_widget_factory(id, [](const std::string&) {
            return std::unique_ptr<PanelWidget>(new SpanOracleWidget());
        });
    }
    ~ScopedSpanOracleFactory() {
        helix::register_widget_factory(id_, original_);
    }

  private:
    const char* id_;
    WidgetFactory original_;
};

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Widget is told the pixel width the grid actually gave it",
                 "[manager][grid_metrics]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_size_oracle_widget",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    SizeOracleWidget::reset();

    // theme_manager_resolve_px_tokens() reads "ui_xml" as a relative path. Run
    // from anywhere but the repo root and every spacing token silently becomes
    // 0, the gutters vanish, and this test would pass without proving anything.
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    ScopedOracleFactory oracle("shutdown");

    const std::string panel_id = "test_cell_px_oracle";
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"},
                          {"widgets",
                           {{{"id", "shutdown"},
                             {"enabled", true},
                             {"col", 0},
                             {"row", 0},
                             {"colspan", 1},
                             {"rowspan", 1}}}}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);

    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 800, 480);
    process_lvgl(10);

    auto widgets = mgr.populate_widgets(panel_id, container, /*page_index=*/1);

    REQUIRE(SizeOracleWidget::s_obj != nullptr);
    REQUIRE(SizeOracleWidget::s_width_px > 0);

    lv_obj_update_layout(container);
    const int rendered_w = lv_obj_get_width(SizeOracleWidget::s_obj);

    // The promise is approximate by design, not exact: grid_cell_metrics()
    // models one uniform float cell, but LVGL's own grid allocator distributes
    // remainder pixels across tracks iteratively (lib/lvgl/src/layouts/grid/
    // lv_grid.c:435-446), so individual tracks can differ from each other and
    // from the uniform model by up to a couple of px once static_cast<int>
    // truncates the promise. 2px covers that; the gutter-blind bug this test
    // guards against is off by roughly a whole gutter, well outside it either way.
    INFO("promised=" << SizeOracleWidget::s_width_px << " rendered=" << rendered_w
                     << " gutter=" << theme_manager_get_spacing("space_xs"));
    REQUIRE(std::abs(SizeOracleWidget::s_width_px - rendered_w) <= 2);

    mgr.clear_panel_config(panel_id);
}

TEST_CASE_METHOD(XMLTestFixture,
                 "Multi-cell widget is told the pixel width the grid actually gave it",
                 "[manager][grid_metrics]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_size_oracle_widget_span",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    SpanOracleWidget::reset();

    // theme_manager_resolve_px_tokens() reads "ui_xml" as a relative path. Run
    // from anywhere but the repo root and every spacing token silently becomes
    // 0, the gutters vanish, and this test would pass without proving anything.
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    ScopedSpanOracleFactory oracle("tips");

    const std::string panel_id = "test_cell_px_oracle_span";
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"},
                          {"widgets",
                           {{{"id", "tips"},
                             {"enabled", true},
                             {"col", 0},
                             {"row", 0},
                             {"colspan", 2},
                             {"rowspan", 1}}}}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);

    lv_obj_t* container = lv_obj_create(test_screen());
    // 809, not 800: the fixture's breakpoint (Medium, 6 cols) is fixed by the
    // 800x480 screen regardless of this container's width, but the content
    // width left after border/padding must leave a genuine remainder once the
    // (cols-1) inter-track gutters are subtracted -- otherwise LVGL's grid
    // allocator has nothing to distribute unevenly across tracks and this
    // test would exercise the same clean-division path as the colspan==1
    // case above. Verified below rather than assumed.
    lv_obj_set_size(container, 809, 480);
    process_lvgl(10);
    // get_content_width() reads the COMPUTED coord, which a freshly-sized
    // object does not have until a layout pass runs (see tests/CLAUDE.md
    // "lv_obj_get_width() reads the COMPUTED coord"). Without this the query
    // below reads 0 and the remainder check passes for the wrong reason.
    lv_obj_update_layout(container);

    const int gutter = theme_manager_get_spacing("space_xs");
    const int content_w = lv_obj_get_content_width(container);
    const int content_h = lv_obj_get_content_height(container);
    const int cols = GridLayout::get_cols(UiBreakpoint::Medium, content_w, content_h);
    INFO("content_w=" << content_w << " cols=" << cols << " gutter=" << gutter);
    REQUIRE((content_w - (cols - 1) * gutter) % cols != 0);

    auto widgets = mgr.populate_widgets(panel_id, container, /*page_index=*/1);

    REQUIRE(SpanOracleWidget::s_obj != nullptr);
    REQUIRE(SpanOracleWidget::s_width_px > 0);

    lv_obj_update_layout(container);
    const int rendered_w = lv_obj_get_width(SpanOracleWidget::s_obj);

    // See the colspan==1 case above for why 2px, not 1px: a uniform-cell model
    // plus integer truncation cannot reproduce LVGL's per-track remainder
    // distribution (lv_grid.c:435-446) exactly, and colspan>=2 is the case
    // that actually gives that distribution something to divide unevenly.
    INFO("promised=" << SpanOracleWidget::s_width_px << " rendered=" << rendered_w
                     << " gutter=" << gutter);
    REQUIRE(std::abs(SpanOracleWidget::s_width_px - rendered_w) <= 2);

    mgr.clear_panel_config(panel_id);
}

TEST_CASE_METHOD(XMLTestFixture,
                 "grid rows come from the container's content box, not from the widget footprint",
                 "[panel_widget_manager][square]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_size_oracle_widget",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    SizeOracleWidget::reset();

    ScopedOracleFactory oracle("shutdown");

    // A single 1x1 widget in the top-left cell must still build the full row
    // count the container's content box earns. Sizing the row axis to the
    // widgets' footprint stretches every track to fill the container height, so
    // a sparse page never gets square cells; sizing it from the panel
    // resolution instead of the content box measures a rectangle the tracks
    // never occupy.
    const std::string panel_id = "test_row_track_from_grid";
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"},
                          {"widgets",
                           {{{"id", "shutdown"},
                             {"enabled", true},
                             {"col", 0},
                             {"row", 0},
                             {"colspan", 1},
                             {"rowspan", 1}}}}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);

    lv_obj_t* container = lv_obj_create(test_screen());
    lv_obj_set_size(container, 710, 466);
    process_lvgl(10);

    // populate_widgets() reads the breakpoint off the theme manager's subject,
    // not off the container, so the expected row count below is only the right
    // oracle while that subject says Medium. Assert it rather than assume it —
    // a different tier would silently compare against the wrong number.
    lv_subject_t* bp_subj = theme_manager_get_breakpoint_subject();
    REQUIRE(bp_subj != nullptr);
    REQUIRE(lv_subject_get_int(bp_subj) == static_cast<int>(UiBreakpoint::Medium));

    auto widgets = mgr.populate_widgets(panel_id, container, /*page_index=*/1);
    REQUIRE(SizeOracleWidget::s_obj != nullptr);

    // Same content box the manager measures, so the oracle is the rule under
    // test applied to the real container rather than a second guess at it.
    lv_obj_update_layout(container);
    const int expected_rows =
        helix::GridLayout::get_rows(UiBreakpoint::Medium, lv_obj_get_content_width(container),
                                    lv_obj_get_content_height(container));
    // Guard the premise: if the grid were only as tall as the one widget on the
    // page, the assertion below would be comparing a number against itself and
    // would pass no matter how the row axis is derived.
    REQUIRE(expected_rows > GridLayout::TRACKS_PER_CELL);

    const int32_t* rows = lv_obj_get_style_grid_row_dsc_array(container, LV_PART_MAIN);
    INFO("expected_rows=" << expected_rows << " built=" << helix::grid_count_tracks(rows));
    CHECK(helix::grid_count_tracks(rows) == expected_rows);

    mgr.clear_panel_config(panel_id);
}
