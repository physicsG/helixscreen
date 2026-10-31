// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// Plugin constants ride the theme's px breakpoint ladder: a _small/_medium/_large
// triplet resolves to the display's tier, with the app's fallbacks, and every
// one of the plugin's components can use it.

#if HELIX_HAS_PLUGINS

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "plugin_consts.h"
#include "theme_manager.h"

#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using helix::PxTierTables;
using helix::theme_manager_pick_px_tier;
using helix::theme_manager_resolve_px_tables;
using namespace helix::plugin::test;

namespace {

PxTierTables triplet(const std::string& base) {
    PxTierTables t;
    t.small[base] = "10";
    t.medium[base] = "20";
    t.large[base] = "30";
    return t;
}

std::string const_value(const char* name) {
    const char* v = lv_xml_get_const(nullptr, name);
    return v ? v : "";
}

const char* kSizes = R"(<component><consts>
    <px name="ab__w_small" value="10"/><px name="ab__w_medium" value="20"/>
    <px name="ab__w_large" value="30"/><px name="ab__gap" value="4"/>
    <color name="ab__tint" value="0xff0000"/>
    </consts><view extends="lv_obj"/></component>)";

} // namespace

TEST_CASE("pick_px_tier: optional tiers fall back to the triplet", "[plugin][consts]") {
    PxTierTables t = triplet("ab__w");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_micro") == "10");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_tiny") == "10");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_medium") == "20");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_xlarge") == "30");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_xxlarge") == "30");
    t.tiny["ab__w"] = "5";
    t.xlarge["ab__w"] = "40";
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_micro") == "5");
    CHECK(theme_manager_pick_px_tier(t, "ab__w", "_xxlarge") == "40");
}

TEST_CASE_METHOD(LVGLTestFixture, "plugin px triplet resolves to the display's narrow tier",
                 "[plugin][consts]") {
    for (auto [w, h] : {std::pair{800, 480}, std::pair{480, 320}, std::pair{1280, 800}}) {
        lv_display_t* d = lv_display_create(w, h);
        const std::string suffix = theme_manager_get_breakpoint_suffix(responsive_dimension(d));
        PxTierTables t = triplet("ab__w");
        t.micro["ab__w"] = "1";
        t.tiny["ab__w"] = "2";
        t.xlarge["ab__w"] = "40";
        t.xxlarge["ab__w"] = "50";
        const auto resolved = theme_manager_resolve_px_tables(t, d, false);
        REQUIRE(resolved.count("ab__w") == 1);
        CHECK(resolved.at("ab__w") == theme_manager_pick_px_tier(t, "ab__w", suffix.c_str()));
        lv_display_delete(d);
    }
}

TEST_CASE("collect_plugin_consts splits suffixed px from plain constants", "[plugin][consts]") {
    const PluginConsts c = collect_plugin_consts({kSizes});
    CHECK(c.px.small.at("ab__w") == "10");
    CHECK(c.px.medium.at("ab__w") == "20");
    CHECK(c.px.large.at("ab__w") == "30");
    CHECK(c.plain.size() == 2);
    CHECK(std::find(c.plain.begin(), c.plain.end(), PluginConsts::Plain{"px", "ab__gap", "4"}) !=
          c.plain.end());
    CHECK(std::find(c.plain.begin(), c.plain.end(),
                    PluginConsts::Plain{"color", "ab__tint", "0xff0000"}) != c.plain.end());
}

TEST_CASE("missing_tier_errors names the constant and the tier", "[plugin][consts]") {
    PxTierTables t;
    t.small["ab__w"] = "10";
    t.large["ab__w"] = "30";
    const auto errs = missing_tier_errors(t);
    REQUIRE(errs.size() == 1);
    CHECK(errs[0].find("ab__w") != std::string::npos);
    CHECK(errs[0].find("_medium") != std::string::npos);
    CHECK(missing_tier_errors(triplet("ab__w")).empty());
}

TEST_CASE_METHOD(LVGLUITestFixture, "a plugin's constants resolve in another of its components",
                 "[plugin][consts]") {
    HostRig rig(enabled("consts-demo", {}));
    rig.host->load_from("tests/fixtures/plugins-consts");
    REQUIRE(rig.info("consts-demo"));
    REQUIRE(rig.info("consts-demo")->status == PluginStatus::Loaded);

    const std::string suffix =
        theme_manager_get_breakpoint_suffix(responsive_dimension(lv_display_get_default()));
    const std::string expected = std::to_string(100 + 1 + [&] {
        static const char* kOrder[] = {"_micro", "_tiny",   "_small",  "_medium",
                                       "_large", "_xlarge", "_xxlarge"};
        for (int i = 0; i < 7; ++i)
            if (suffix == kOrder[i])
                return i;
        return -1;
    }());
    CHECK(const_value("consts-demo__card_w") == expected);
    CHECK(const_value("consts-demo__gap") == "7");

    lv_obj_t* card =
        static_cast<lv_obj_t*>(lv_xml_create(lv_screen_active(), "consts-demo__card", nullptr));
    REQUIRE(card != nullptr);
    lv_obj_update_layout(card);
    CHECK(lv_obj_get_width(card) == std::stoi(expected));
    CHECK(lv_obj_get_height(card) == 7);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "a partial triplet is a load error naming the constant and tier",
                 "[plugin][consts]") {
    HostRig rig(enabled("consts-bad", {}));
    rig.host->load_from("tests/fixtures/plugins-consts");
    const PluginInfo* info = rig.info("consts-bad");
    REQUIRE(info);
    CHECK(info->status == PluginStatus::Invalid);
    CHECK(info->reason.find("consts-bad__w") != std::string::npos);
    CHECK(info->reason.find("_medium") != std::string::npos);
}

TEST_CASE_METHOD(LVGLUITestFixture, "refresh re-resolves plugin constants on a breakpoint change",
                 "[plugin][consts]") {
    HostRig rig(enabled("consts-demo", {}));
    rig.host->load_from("tests/fixtures/plugins-consts");
    REQUIRE(rig.info("consts-demo")->status == PluginStatus::Loaded);
    const std::string before = const_value("consts-demo__card_w");

    lv_display_t* small = lv_display_create(480, 320);
    rig.host->refresh_layout_constants(small);
    const std::string after = const_value("consts-demo__card_w");
    CHECK(after != before);
    CHECK(after == "102"); // 320 high is the _tiny tier

    rig.host->refresh_layout_constants(lv_display_get_default());
    CHECK(const_value("consts-demo__card_w") == before);
    lv_display_delete(small);
}

TEST_CASE_METHOD(LVGLUITestFixture, "a theme layout refresh runs the registered hooks",
                 "[plugin][consts][theme]") {
    int calls = 0;
    lv_display_t* seen = nullptr;
    const int id = helix::theme_manager_add_layout_refresh_hook([&](lv_display_t* d) {
        ++calls;
        seen = d;
    });
    lv_display_t* display = lv_display_get_default();
    theme_manager_refresh_layout_constants(display);
    CHECK(calls == 1);
    CHECK(seen == display);

    helix::theme_manager_remove_layout_refresh_hook(id);
    theme_manager_refresh_layout_constants(display);
    CHECK(calls == 1);
}

#endif // HELIX_HAS_PLUGINS
