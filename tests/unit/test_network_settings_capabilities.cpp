// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_network_settings_capabilities.cpp
 * @brief The Forget control and the WiFi radio toggle are shown only when the
 *        backend can perform them (prestonbrown/helixscreen#1429).
 */

#include "ui_overlay_network_settings.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/network_settings_overlay_test_access.h"
#include "../test_helpers/scoped_runtime_config.h"
#include "wifi_backend_mock.h"
#include "wifi_manager.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using Access = NetworkSettingsOverlayTestAccess;

namespace {

/// A backend that owns neither the credential store nor the radio, the shape
/// of a printer whose network daemon manages WiFi.
class NoForgetNoToggleBackend : public WifiBackendMock {
  public:
    bool supports_forget() const override {
        return false;
    }
    bool supports_radio_toggle() const override {
        return false;
    }
};

class CapabilityFixture : public LVGLUITestFixture {
  protected:
    ScopedRuntimeConfig scoped_config;

    lv_obj_t* open_overlay(NetworkSettingsOverlay& overlay, std::unique_ptr<WifiBackend> backend) {
        get_runtime_config()->test_mode = true;
        get_runtime_config()->use_real_wifi = false;
        auto wm = std::make_shared<helix::WiFiManager>(std::move(backend));
        wm->init_self_reference(wm);
        overlay.init_subjects();
        overlay.register_callbacks();
        Access::wifi_manager(overlay) = wm;
        lv_obj_t* root = overlay.create(test_screen());
        REQUIRE(root != nullptr);
        overlay.on_activate();
        return root;
    }

    static bool hidden(lv_obj_t* root, const char* name) {
        lv_obj_t* obj = lv_obj_find_by_name(root, name);
        REQUIRE(obj != nullptr);
        return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
};

} // namespace

TEST_CASE_METHOD(CapabilityFixture,
                 "Network settings offers Forget and the radio toggle when supported",
                 "[network_settings][1429]") {
    NetworkSettingsOverlay overlay;
    lv_obj_t* root = open_overlay(overlay, std::make_unique<WifiBackendMock>());

    CHECK_FALSE(hidden(root, "forget_network_icon"));
    CHECK_FALSE(hidden(root, "wlan_toggle"));
}

TEST_CASE_METHOD(
    CapabilityFixture,
    "Network settings hides Forget and the radio toggle when the backend cannot do them",
    "[network_settings][1429]") {
    NetworkSettingsOverlay overlay;
    lv_obj_t* root = open_overlay(overlay, std::make_unique<NoForgetNoToggleBackend>());

    CHECK(hidden(root, "forget_network_icon"));
    CHECK(hidden(root, "wlan_toggle"));
}
