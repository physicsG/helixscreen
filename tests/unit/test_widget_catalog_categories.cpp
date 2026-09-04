// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_widget_catalog_categories.cpp
 * @brief Add Widget catalog: category grouping and two-level teardown (#1016)
 *
 * The catalog is a flat file-static state machine with three teardown paths that
 * must each fire on_close exactly once, and it now has a sub-page pushed on top
 * of it. These tests pin both halves: that the grouping partitions the registry
 * exactly, and that diving in and out never loses or double-fires the callback
 * GridEditMode relies on to un-hide its dots overlay.
 */

#include "ui_nav_manager.h"
#include "ui_update_queue.h"
#include "ui_widget_catalog_overlay.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/config_test_access.h"
#include "config.h"
#include "display_settings_manager.h"
#include "grid_layout.h"
#include "panel_widget_config.h"
#include "panel_widget_registry.h"

#include <array>
#include <cctype>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

/// Direct children of a container — the catalog builds one row per entry, so the
/// child count IS the row count.
uint32_t child_count(lv_obj_t* obj) {
    return obj ? lv_obj_get_child_count(obj) : 0;
}

/// header_bar renders its title with text_transform="uppercase", so the rendered
/// string is compared case-insensitively against the category name.
std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

// ============================================================================
// Fixture
// ============================================================================

class WidgetCatalogCategoryFixture : public LVGLUITestFixture {
  public:
    WidgetCatalogCategoryFixture() {
        // Overlay close callbacks reach us either synchronously (animations off)
        // or via lv_async_call after the slide-out completes. Switching
        // animations off takes the wall-clock timing out of the assertions
        // without changing which callback runs — both paths converge on the same
        // registered close callback. Restored in the destructor: it is a
        // process-wide setting and the next test did not ask for it.
        animations_were_enabled_ = DisplaySettingsManager::instance().get_animations_enabled();
        DisplaySettingsManager::instance().set_animations_enabled(false);

        // Production keeps the active root panel at panel_stack_[0], and overlay
        // code reads panel_stack_.back() to find what sits beneath. Without this
        // the second push still looks like the first.
        for (auto& p : root_panels_) {
            p = lv_obj_create(lv_screen_active());
        }
        NavigationManager::instance().set_panels(root_panels_.data());
        NavigationManager::instance().set_active(PanelId::Home);
        settle();

        ConfigTestAccess::data(config_) = json::object();
        widget_config_ = std::make_unique<PanelWidgetConfig>("home", config_);
        widget_config_->load();
    }

    ~WidgetCatalogCategoryFixture() override {
        // Never leave the file-static catalog state open — the next test's
        // show() would warn and no-op.
        force_close();
        // The teardown chain is queue -> queue -> lv_obj_delete_async, so a
        // single drain leaves a deletion armed against widgets this fixture is
        // about to free. settle() runs both halves to exhaustion.
        settle();
        // NavigationManager holds panel_widgets_ / panel_stack_ pointers into the
        // screen the base fixture is about to delete.
        NavigationManager::instance().deinit_subjects();
        DisplaySettingsManager::instance().set_animations_enabled(animations_were_enabled_);
    }

    /// Run the queue until the two-level teardown has fully unwound.
    ///
    /// go_back() runs its whole body through UpdateQueue, and the catalog's own
    /// pop is queued *behind* the sub-page's pop, so the second pop is only
    /// enqueued once the first batch has run. One drain is never enough.
    void settle() {
        for (int i = 0; i < 4; i++) {
            helix::ui::UpdateQueue::instance().drain();
            process_lvgl(10);
        }
    }

    void force_close() {
        auto& nav = NavigationManager::instance();
        for (int i = 0; i < 4 && WidgetCatalogOverlay::active_root(); i++) {
            nav.go_back();
            settle();
        }
    }

    /// Open the catalog and let the push land.
    void open_catalog() {
        WidgetCatalogOverlay::show(
            lv_screen_active(), *widget_config_,
            [this](const std::string& id) { selected_ids_.push_back(id); },
            [this]() { close_count_++; });
        settle();
    }

    /// The <setting_group> holding the top-level category rows.
    static lv_obj_t* category_group() {
        lv_obj_t* root = WidgetCatalogOverlay::active_root();
        return root ? lv_obj_find_by_name(root, "category_group") : nullptr;
    }

    /// The sub-page scroll container holding one row per widget in the category.
    static lv_obj_t* category_scroll() {
        lv_obj_t* page = WidgetCatalogOverlay::active_category_root();
        return page ? lv_obj_find_by_name(page, "catalog_scroll") : nullptr;
    }

    /// Tap the Nth top-level category row.
    void dive(size_t index) {
        lv_obj_t* group = category_group();
        REQUIRE(group != nullptr);
        REQUIRE(index < child_count(group));
        lv_obj_send_event(lv_obj_get_child(group, static_cast<int32_t>(index)), LV_EVENT_CLICKED,
                          nullptr);
        settle();
    }

    /// Back out of whatever is on top — what the inherited header back button does.
    void header_back() {
        NavigationManager::instance().go_back();
        settle();
    }

    Config config_;
    std::unique_ptr<PanelWidgetConfig> widget_config_;
    std::array<lv_obj_t*, UI_PANEL_COUNT> root_panels_{};
    int close_count_ = 0;
    bool animations_were_enabled_ = true;
    std::vector<std::string> selected_ids_;
};

// ============================================================================
// Grouping — pure, no overlay needed
// ============================================================================

TEST_CASE("Widget catalog: every def belongs to exactly one category", "[widget_catalog][1016]") {
    const auto& defs = get_all_widget_defs();
    const auto& categories = get_widget_categories();
    REQUIRE_FALSE(defs.empty());
    REQUIRE_FALSE(categories.empty());

    std::multiset<std::string> seen;
    for (const auto& cat : categories) {
        for (const auto* def : WidgetCatalogOverlay::widgets_in_category(cat.id)) {
            seen.insert(def->id);
        }
    }

    // Nothing orphaned, nothing duplicated: the categories partition the registry.
    CHECK(seen.size() == defs.size());
    for (const auto& def : defs) {
        INFO("widget id: " << def.id);
        CHECK(seen.count(def.id) == 1);
    }
}

TEST_CASE("Widget catalog: a category holds exactly its own defs, in registry order",
          "[widget_catalog][1016]") {
    const auto& defs = get_all_widget_defs();

    for (const auto& cat : get_widget_categories()) {
        // Derived independently of widgets_in_category() — a filter that returned
        // everything, or reordered, fails here.
        std::vector<std::string> expected;
        for (const auto& def : defs) {
            if (def.category == cat.id) {
                expected.push_back(def.id);
            }
        }

        std::vector<std::string> actual;
        for (const auto* def : WidgetCatalogOverlay::widgets_in_category(cat.id)) {
            actual.push_back(def->id);
        }

        INFO("category: " << cat.display_name);
        CHECK(actual == expected);
        CHECK_FALSE(actual.empty()); // an empty category would be a dead menu row
    }
}

TEST_CASE("Widget catalog: every category def resolves and is uniquely identified",
          "[widget_catalog][1016]") {
    std::set<int> ids;
    for (const auto& cat : get_widget_categories()) {
        const WidgetCategoryDef* found = find_widget_category(cat.id);
        REQUIRE(found != nullptr);
        CHECK(found->id == cat.id);
        CHECK(std::string(found->display_name) == cat.display_name);
        REQUIRE(found->icon != nullptr);
        CHECK(std::string(found->icon).size() > 0);
        CHECK(ids.insert(static_cast<int>(cat.id)).second);
    }
}

// ============================================================================
// Top level
// ============================================================================

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: top level lists one row per category, not per widget",
                 "[widget_catalog][1016]") {
    open_catalog();
    REQUIRE(WidgetCatalogOverlay::active_root() != nullptr);

    lv_obj_t* group = category_group();
    REQUIRE(group != nullptr);

    const auto& categories = get_widget_categories();
    CHECK(child_count(group) == categories.size());
    // The whole point of #1016: the flat 37-row list is gone.
    CHECK(child_count(group) < get_all_widget_defs().size());
}

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: each category row dives into that category's widgets",
                 "[widget_catalog][1016]") {
    const auto& categories = get_widget_categories();

    for (size_t i = 0; i < categories.size(); i++) {
        open_catalog();
        dive(i);

        lv_obj_t* scroll = category_scroll();
        INFO("category: " << categories[i].display_name);
        REQUIRE(scroll != nullptr);
        CHECK(child_count(scroll) ==
              WidgetCatalogOverlay::widgets_in_category(categories[i].id).size());

        // The sub-page wears the category's own name. overlay_panel bakes the
        // title at parse time, so this only holds if the props reach it.
        lv_obj_t* page = WidgetCatalogOverlay::active_category_root();
        REQUIRE(page != nullptr);
        lv_obj_t* title = lv_obj_find_by_name(page, "header_title");
        REQUIRE(title != nullptr);
        CHECK(upper(lv_label_get_text(title)) == upper(categories[i].display_name));

        force_close();
        CHECK(WidgetCatalogOverlay::active_root() == nullptr);
    }
}

// ============================================================================
// Teardown
// ============================================================================

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: backing out of a category keeps the catalog open",
                 "[widget_catalog][1016]") {
    open_catalog();
    lv_obj_t* root = WidgetCatalogOverlay::active_root();
    REQUIRE(root != nullptr);

    dive(0);
    REQUIRE(WidgetCatalogOverlay::active_category_root() != nullptr);
    CHECK(close_count_ == 0);

    header_back();

    // Back at the category list: the sub-page is gone, the catalog survives it,
    // and GridEditMode has NOT been told the catalog closed.
    CHECK(WidgetCatalogOverlay::active_category_root() == nullptr);
    CHECK(WidgetCatalogOverlay::active_root() == root);
    CHECK(close_count_ == 0);
    CHECK(NavigationManager::instance().is_panel_on_top(root));

    // And the category list is still usable — a second dive works.
    dive(1);
    CHECK(WidgetCatalogOverlay::active_category_root() != nullptr);
    CHECK(close_count_ == 0);
}

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: closing from the top level fires on_close exactly once",
                 "[widget_catalog][1016]") {
    open_catalog();
    REQUIRE(WidgetCatalogOverlay::active_root() != nullptr);

    header_back();

    CHECK(close_count_ == 1);
    CHECK(WidgetCatalogOverlay::active_root() == nullptr);

    // Extra unwinding must not fire it again.
    header_back();
    CHECK(close_count_ == 1);
}

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: closing from a dived-in state fires on_close exactly once",
                 "[widget_catalog][1016]") {
    open_catalog();
    dive(0);
    REQUIRE(WidgetCatalogOverlay::active_category_root() != nullptr);

    // Two levels, two backs: out of the category, then out of the catalog.
    header_back();
    CHECK(close_count_ == 0);

    header_back();
    CHECK(close_count_ == 1);
    CHECK(WidgetCatalogOverlay::active_root() == nullptr);
    CHECK(WidgetCatalogOverlay::active_category_root() == nullptr);
}

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: selecting a widget inside a category closes both levels",
                 "[widget_catalog][1016]") {
    // Find a single-instance, ungated widget and make sure it reads as unplaced,
    // so its row is clickable rather than dimmed.
    const auto& categories = get_widget_categories();
    size_t cat_index = 0;
    size_t row_index = 0;
    std::string target_id;
    bool found = false;
    for (size_t c = 0; c < categories.size() && !found; c++) {
        const auto defs = WidgetCatalogOverlay::widgets_in_category(categories[c].id);
        for (size_t r = 0; r < defs.size(); r++) {
            if (!defs[r]->multi_instance && defs[r]->hardware_gate_subject == nullptr) {
                cat_index = c;
                row_index = r;
                target_id = defs[r]->id;
                found = true;
                break;
            }
        }
    }
    REQUIRE(found);

    // Force the target off so the catalog treats it as placeable.
    auto& entries = widget_config_->mutable_entries();
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].id == target_id) {
            widget_config_->set_enabled(i, false);
        }
    }
    REQUIRE_FALSE(widget_config_->is_enabled(target_id));

    open_catalog();
    dive(cat_index);

    lv_obj_t* scroll = category_scroll();
    REQUIRE(scroll != nullptr);
    REQUIRE(row_index < child_count(scroll));

    lv_obj_send_event(lv_obj_get_child(scroll, static_cast<int32_t>(row_index)), LV_EVENT_CLICKED,
                      nullptr);
    settle();

    // The selection is reported exactly once, with the id of the row tapped.
    REQUIRE(selected_ids_.size() == 1);
    CHECK(selected_ids_[0] == target_id);

    // And both overlays are gone, with the close reported exactly once.
    CHECK(close_count_ == 1);
    CHECK(WidgetCatalogOverlay::active_root() == nullptr);
    CHECK(WidgetCatalogOverlay::active_category_root() == nullptr);
    CHECK_FALSE(NavigationManager::instance().has_open_overlays());
}

// ============================================================================
// Hardware gating applies to multi-instance widgets too
// ============================================================================

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: a gated multi-instance widget is dimmed and unselectable",
                 "[widget_catalog][1016]") {
    // power_device and thermistor are the only defs that are BOTH multi_instance
    // and hardware-gated. The multi-instance branch used to hardcode the gate
    // off, so on a printer with no Moonraker power device the Power row rendered
    // bright and clickable and minted an instance that could never work.
    const PanelWidgetDef* target = nullptr;
    for (const auto& def : get_all_widget_defs()) {
        if (def.multi_instance && def.hardware_gate_subject) {
            target = &def;
            break;
        }
    }
    REQUIRE(target != nullptr);
    INFO("gated multi-instance widget: " << target->id);

    // Close the gate: the subject must exist, or the catalog reads "available"
    // and this test would pass for the wrong reason.
    lv_subject_t* gate = lv_xml_get_subject(nullptr, target->hardware_gate_subject);
    REQUIRE(gate != nullptr);
    lv_subject_set_int(gate, 0);

    size_t cat_index = 0, row_index = 0;
    bool found = false;
    const auto& categories = get_widget_categories();
    for (size_t c = 0; c < categories.size() && !found; c++) {
        const auto defs = WidgetCatalogOverlay::widgets_in_category(categories[c].id);
        for (size_t r = 0; r < defs.size(); r++) {
            if (defs[r]->id == std::string(target->id)) {
                cat_index = c;
                row_index = r;
                found = true;
                break;
            }
        }
    }
    REQUIRE(found);

    open_catalog();
    dive(cat_index);

    lv_obj_t* scroll = category_scroll();
    REQUIRE(scroll != nullptr);
    REQUIRE(row_index < child_count(scroll));
    lv_obj_t* row = lv_obj_get_child(scroll, static_cast<int32_t>(row_index));
    REQUIRE(row != nullptr);

    // Dimmed and not tappable.
    CHECK_FALSE(lv_obj_has_flag(row, LV_OBJ_FLAG_CLICKABLE));
    CHECK(lv_obj_get_style_opa(row, LV_PART_MAIN) < LV_OPA_COVER);

    // Tapping it must not mint an instance.
    lv_obj_send_event(row, LV_EVENT_CLICKED, nullptr);
    settle();
    CHECK(selected_ids_.empty());

    force_close();
}

// ============================================================================
// Size badge units
// ============================================================================

/// The deepest label in a row's right-hand group is the size badge. "Placed"
/// sits in the same group as a bare label, so the badge is found by its own
/// container rather than by position among siblings.
static std::string find_badge_text(lv_obj_t* row) {
    std::string best;
    std::function<void(lv_obj_t*)> walk = [&](lv_obj_t* obj) {
        for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
            lv_obj_t* child = lv_obj_get_child(obj, static_cast<int32_t>(i));
            if (lv_obj_check_type(child, &lv_label_class)) {
                const char* txt = lv_label_get_text(child);
                if (txt && std::string(txt).find('x') != std::string::npos) {
                    best = txt;
                }
            }
            walk(child);
        }
    };
    walk(row);
    return best;
}

TEST_CASE_METHOD(WidgetCatalogCategoryFixture,
                 "Widget catalog: size badges are cells, not half-cell tracks",
                 "[widget_catalog][1016]") {
    // The registry stores spans in tracks and a track is half a cell. Printing
    // them raw badged every one-cell widget as "2x2" and disagreed with both the
    // grid the user sees and the sizes the user guide documents.
    const auto& cats = get_widget_categories();
    open_catalog();

    bool checked_any = false;
    for (size_t c = 0; c < cats.size(); c++) {
        const auto defs = WidgetCatalogOverlay::widgets_in_category(cats[c].id);
        dive(c);
        lv_obj_t* scroll = category_scroll();
        REQUIRE(scroll != nullptr);
        REQUIRE(child_count(scroll) == defs.size());

        for (size_t r = 0; r < defs.size(); r++) {
            lv_obj_t* row = lv_obj_get_child(scroll, static_cast<int32_t>(r));
            const std::string badge = find_badge_text(row);
            INFO("widget " << defs[r]->id << " badge '" << badge << "'");
            REQUIRE_FALSE(badge.empty());

            const int col_cells = defs[r]->colspan / GridLayout::TRACKS_PER_CELL;
            const int row_cells = defs[r]->rowspan / GridLayout::TRACKS_PER_CELL;
            std::string expect = std::to_string(col_cells) +
                                 (defs[r]->colspan % GridLayout::TRACKS_PER_CELL ? ".5" : "") +
                                 "x" + std::to_string(row_cells) +
                                 (defs[r]->rowspan % GridLayout::TRACKS_PER_CELL ? ".5" : "");
            CHECK(badge == expect);

            // The mutation this guards against: raw track counts. Every shipping
            // widget is at least one whole cell, so a raw span always differs.
            CHECK(badge !=
                  std::to_string(defs[r]->colspan) + "x" + std::to_string(defs[r]->rowspan));
            checked_any = true;
        }
        header_back();
    }

    CHECK(checked_any);
    force_close();
}
