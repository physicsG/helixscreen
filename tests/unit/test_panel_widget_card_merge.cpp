// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_panel_widget_card_merge.cpp
 * @brief A merged card background must line up with the widgets it backs.
 *
 * populate_widgets() draws one card object behind each connected run of
 * widgets that opt into the shared background. The flood fill and the grid are
 * both addressed in TRACKS, so a widget parked on an odd track - reachable by
 * drag for any widget whose registry def supports half-cell resolution - is
 * backed like any other. The fill used to work in cells, which truncated such a
 * position, so those widgets were dropped from the merge and drew no background
 * at all.
 *
 * Two invariants. Containment: a card that overlaps a widget at all must cover
 * it completely — a card offset by half a cell leaves the widget's far edge
 * hanging outside its own background, which is the visible artifact. Coverage:
 * a widget that asked for the shared card must actually be behind one.
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

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Minimal stand-in so the test does not depend on any real widget's XML.
struct StubWidget : helix::PanelWidget {
    std::string id_;
    explicit StubWidget(std::string id) : id_(std::move(id)) {}
    void attach(lv_obj_t*, lv_obj_t*) override {}
    void detach() override {}
    const char* id() const override {
        return id_.c_str();
    }
    std::string get_component_name() const override {
        return "test_card_merge_stub";
    }
};

/// Swap a registry factory for the duration of a test and restore it after.
class ScopedStubFactory {
  public:
    explicit ScopedStubFactory(const char* id) : id_(id) {
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        original_ = def->factory;
        helix::register_widget_factory(id, [](const std::string& wid) {
            return std::unique_ptr<PanelWidget>(new StubWidget(wid));
        });
    }
    ~ScopedStubFactory() {
        helix::register_widget_factory(id_, original_);
    }

  private:
    const char* id_;
    WidgetFactory original_;
};

/// Every direct child of the container that is not one of the widget objects is
/// a card background — populate_widgets() names each widget object after its
/// widget id and leaves the cards unnamed.
std::vector<lv_obj_t*> card_backgrounds(lv_obj_t* container,
                                        const std::vector<std::string>& widget_ids) {
    std::vector<lv_obj_t*> widgets;
    for (const auto& id : widget_ids) {
        if (lv_obj_t* w = lv_obj_find_by_name(container, id.c_str())) {
            widgets.push_back(w);
        }
    }
    std::vector<lv_obj_t*> cards;
    for (uint32_t i = 0; i < lv_obj_get_child_count(container); i++) {
        lv_obj_t* child = lv_obj_get_child(container, i);
        bool is_widget = false;
        for (lv_obj_t* w : widgets) {
            if (w == child) {
                is_widget = true;
                break;
            }
        }
        if (!is_widget) {
            cards.push_back(child);
        }
    }
    return cards;
}

bool areas_overlap(const lv_area_t& a, const lv_area_t& b) {
    return a.x1 <= b.x2 && b.x1 <= a.x2 && a.y1 <= b.y2 && b.y1 <= a.y2;
}

/// `outer` covers `inner` entirely. Tolerance absorbs the grid allocator's
/// per-track remainder distribution (lv_grid.c), which can shift an edge by a
/// pixel; the misalignment this guards against is a whole half-cell.
bool area_contains(const lv_area_t& outer, const lv_area_t& inner, int tol = 2) {
    return outer.x1 <= inner.x1 + tol && outer.y1 <= inner.y1 + tol && outer.x2 + tol >= inner.x2 &&
           outer.y2 + tol >= inner.y2;
}

/// Seed a page holding exactly `widgets`, populate it, and assert the card
/// invariant. Returns the number of card backgrounds produced.
size_t check_card_containment(const std::string& panel_id, const nlohmann::json& widgets,
                              const std::vector<std::string>& ids, lv_obj_t* screen) {
    auto* cfg = Config::get_instance();
    cfg->set<nlohmann::json>(
        cfg->df() + "panel_widgets/" + panel_id,
        nlohmann::json{{"main_page_index", 0},
                       {"next_page_id", 2},
                       {"pages",
                        {{{"id", "main"}, {"widgets", nlohmann::json::array()}},
                         {{"id", "spy"}, {"widgets", widgets}}}}});

    auto& mgr = PanelWidgetManager::instance();
    mgr.get_widget_config(panel_id).mark_dirty();
    mgr.clear_panel_config(panel_id);

    lv_obj_t* container = lv_obj_create(screen);
    lv_obj_set_size(container, 800, 480);
    lv_obj_update_layout(container);

    auto held = mgr.populate_widgets(panel_id, container, /*page_index=*/1);
    lv_obj_update_layout(container);

    auto cards = card_backgrounds(container, ids);
    std::vector<bool> backed(ids.size(), false);
    for (lv_obj_t* card : cards) {
        lv_area_t card_area;
        lv_obj_get_coords(card, &card_area);
        for (size_t i = 0; i < ids.size(); i++) {
            lv_obj_t* w = lv_obj_find_by_name(container, ids[i].c_str());
            if (!w) {
                continue;
            }
            lv_area_t w_area;
            lv_obj_get_coords(w, &w_area);
            if (!areas_overlap(card_area, w_area)) {
                continue;
            }
            backed[i] = true;
            INFO("widget " << ids[i] << " at [" << w_area.x1 << "," << w_area.y1 << " " << w_area.x2
                           << "," << w_area.y2 << "] vs card [" << card_area.x1 << ","
                           << card_area.y1 << " " << card_area.x2 << "," << card_area.y2 << "]");
            CHECK(area_contains(card_area, w_area));
        }
    }

    // Coverage. Without this the containment loop passes vacuously for any
    // widget the merge pass declined to back - which is exactly how the
    // cell-coordinate version looked correct while drawing nothing.
    for (size_t i = 0; i < ids.size(); i++) {
        const auto* def = helix::find_widget_def(ids[i]);
        if (!def || !def->merges_into_card || !lv_obj_find_by_name(container, ids[i].c_str())) {
            continue;
        }
        INFO("widget " << ids[i] << " asked for the shared card");
        CHECK(backed[i]);
    }

    mgr.clear_panel_config(panel_id);
    return cards.size();
}

const int TPC = GridLayout::TRACKS_PER_CELL;

} // namespace

TEST_CASE_METHOD(XMLTestFixture, "Card merge: adjacent aligned widgets share one covering card",
                 "[manager][card_merge]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_card_merge_stub",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    ScopedStubFactory a("shutdown");
    ScopedStubFactory b("lock");

    // Two whole-cell widgets side by side, both on cell boundaries.
    nlohmann::json widgets = {{{"id", "shutdown"},
                               {"enabled", true},
                               {"col", 0},
                               {"row", 0},
                               {"colspan", TPC},
                               {"rowspan", TPC}},
                              {{"id", "lock"},
                               {"enabled", true},
                               {"col", TPC},
                               {"row", 0},
                               {"colspan", TPC},
                               {"rowspan", TPC}}};

    size_t cards = check_card_containment("test_card_merge_aligned", widgets, {"shutdown", "lock"},
                                          test_screen());
    // They are adjacent and both single-cell, so the flood fill merges them.
    CHECK(cards == 1);
}

// A half-cell-capable widget snaps on a single track (snap_step_for), so it can
// legally sit on an odd track. Converting that position to cell coordinates
// truncated it, so the pass excluded the widget outright and it rendered on the
// bare panel background. In tracks it gets a card that lines up with it.
TEST_CASE_METHOD(XMLTestFixture, "Card merge: a widget on an odd track still gets its card",
                 "[manager][card_merge]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_card_merge_stub",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    // The premise: this widget really is half-cell capable, so an odd track is
    // reachable by drag. If that ever changes the test is no longer meaningful.
    const auto* def = helix::find_widget_def("shutdown");
    REQUIRE(def != nullptr);
    REQUIRE(def->supports_half_col);
    REQUIRE(TPC > 1);

    ScopedStubFactory a("shutdown");
    ScopedStubFactory b("lock");

    // shutdown sits one track right of the cell boundary; lock is aligned and
    // close enough that a truncated card would land on top of it.
    nlohmann::json widgets = {{{"id", "shutdown"},
                               {"enabled", true},
                               {"col", TPC + 1},
                               {"row", 0},
                               {"colspan", TPC},
                               {"rowspan", TPC}},
                              {{"id", "lock"},
                               {"enabled", true},
                               {"col", 0},
                               {"row", 0},
                               {"colspan", TPC},
                               {"rowspan", TPC}}};

    // One empty track separates them, so they stay two components rather than
    // fusing across the gap - two cards, each covering its own widget.
    size_t cards =
        check_card_containment("test_card_merge_odd", widgets, {"shutdown", "lock"}, test_screen());
    CHECK(cards == 2);
}

// A widget sized to an odd number of tracks - 1.5 cells - is the other half of
// the same bug: the span truncated as well as the position.
TEST_CASE_METHOD(XMLTestFixture, "Card merge: a widget 1.5 cells wide gets a card that fits it",
                 "[manager][card_merge]") {
    helix::init_widget_registrations();
    lv_xml_register_component_from_data(
        "test_card_merge_stub",
        "<component><view extends=\"lv_obj\" width=\"100%\" height=\"100%\"/></component>");
    REQUIRE(theme_manager_get_spacing("space_xs") > 0);

    // The premise: clock resizes on odd track counts and wants the shared card.
    const auto* def = helix::find_widget_def("clock");
    REQUIRE(def != nullptr);
    REQUIRE(def->supports_half_col);
    REQUIRE(def->merges_into_card);
    REQUIRE(def->effective_max_colspan() >= 3);

    ScopedStubFactory a("clock");

    nlohmann::json widgets = {{{"id", "clock"},
                               {"enabled", true},
                               {"col", 0},
                               {"row", 0},
                               {"colspan", 3}, // 1.5 cells
                               {"rowspan", TPC}}};

    size_t cards =
        check_card_containment("test_card_merge_odd_span", widgets, {"clock"}, test_screen());
    CHECK(cards == 1);
}
