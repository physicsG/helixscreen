// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_network_settings_hidden_connect.cpp
 * @brief The hidden-network modal must ask the backend for a hidden join.
 *
 * `nmcli device wifi connect <ssid>` matches the SSID against NetworkManager's
 * scan cache, and a hidden SSID is by definition absent from it — so the
 * modal's Connect button has to carry the is_hidden flag all the way to the
 * backend or the join can only ever fail with "No network with SSID found".
 * This pins the first hop of that wiring (modal -> WiFiManager::connect ->
 * backend) by driving the production handlers against a mock-backed manager.
 */

#include "ui_overlay_network_settings.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/network_settings_overlay_test_access.h"
#include "../ui_test_utils.h"
#include "wifi_backend_mock.h"
#include "wifi_manager.h"

#include <memory>

#include "../catch_amalgamated.hpp"

using Access = NetworkSettingsOverlayTestAccess;

namespace {

// LVGLUITestFixture: the modal is created from XML (hidden_network_modal.xml),
// so components must be registered exactly as production does.
class HiddenConnectFixture : public LVGLUITestFixture {
  protected:
    std::shared_ptr<helix::WiFiManager> make_manager(WifiBackendMock** raw_out) {
        auto backend = std::make_unique<WifiBackendMock>();
        *raw_out = backend.get();
        auto wm = std::make_shared<helix::WiFiManager>(std::move(backend));
        wm->init_self_reference(wm);
        return wm;
    }
};

} // namespace

TEST_CASE_METHOD(HiddenConnectFixture,
                 "the hidden-network modal's Connect button asks for a hidden join",
                 "[network_settings][hidden]") {
    WifiBackendMock* raw = nullptr;
    auto wm = make_manager(&raw);

    auto overlay = std::make_unique<NetworkSettingsOverlay>();
    overlay->init_subjects();
    overlay->register_callbacks();
    // Point the overlay at OUR manager before create(): create() would
    // otherwise grab the process-global singleton.
    Access::wifi_manager(*overlay) = wm;
    REQUIRE(overlay->create(test_screen()) != nullptr);

    // Open the hidden-network modal, exactly as "Add other network" does.
    Access::add_other_clicked(*overlay);
    lv_obj_t* modal = Access::hidden_network_modal(*overlay);
    REQUIRE(modal != nullptr);

    // A hidden SSID is typed, never picked from the scan list; "StealthNet"
    // is by construction absent from the mock's seeded list. Security is set
    // to "None" (the modal defaults to WPA) so the handler needs no password.
    lv_obj_t* ssid_input = lv_obj_find_by_name(modal, "ssid_input");
    REQUIRE(ssid_input != nullptr);
    lv_textarea_set_text(ssid_input, "StealthNet");
    lv_obj_t* security = lv_obj_find_by_name(modal, "security_dropdown");
    REQUIRE(security != nullptr);
    lv_dropdown_set_selected(security, 0);

    Access::hidden_connect_clicked(*overlay);

    // The request's hidden-ness is the whole contract here, not the join's
    // outcome — the mock refuses the SSID on its scan list either way.
    CHECK(raw->last_connect_hidden());
}

TEST_CASE_METHOD(HiddenConnectFixture,
                 "the hidden modal refuses an empty SSID without touching the backend",
                 "[network_settings][hidden]") {
    WifiBackendMock* raw = nullptr;
    auto wm = make_manager(&raw);

    auto overlay = std::make_unique<NetworkSettingsOverlay>();
    overlay->init_subjects();
    overlay->register_callbacks();
    Access::wifi_manager(*overlay) = wm;
    REQUIRE(overlay->create(test_screen()) != nullptr);

    Access::add_other_clicked(*overlay);
    lv_obj_t* modal = Access::hidden_network_modal(*overlay);
    REQUIRE(modal != nullptr);
    lv_obj_t* ssid_input = lv_obj_find_by_name(modal, "ssid_input");
    REQUIRE(ssid_input != nullptr);
    lv_textarea_set_text(ssid_input, "");

    Access::hidden_connect_clicked(*overlay);

    // Proof the handler ran and took the refusal branch: the modal's own
    // error label is the visible consequence of the empty-SSID check.
    lv_obj_t* error_label = lv_obj_find_by_name(modal, "error_label");
    REQUIRE(error_label != nullptr);
    CHECK_FALSE(lv_obj_has_flag(error_label, LV_OBJ_FLAG_HIDDEN));
    // And nothing reached the backend, hidden or otherwise.
    CHECK_FALSE(raw->last_connect_hidden());
}

TEST_CASE_METHOD(HiddenConnectFixture,
                 "the hidden modal swaps its form and Connect for a spinner while joining",
                 "[network_settings][hidden]") {
    WifiBackendMock* raw = nullptr;
    auto wm = make_manager(&raw);

    auto overlay = std::make_unique<NetworkSettingsOverlay>();
    overlay->init_subjects();
    overlay->register_callbacks();
    Access::wifi_manager(*overlay) = wm;
    REQUIRE(overlay->create(test_screen()) != nullptr);

    Access::add_other_clicked(*overlay);
    lv_obj_t* modal = Access::hidden_network_modal(*overlay);
    REQUIRE(modal != nullptr);
    lv_obj_t* form = lv_obj_find_by_name(modal, "hidden_form_container");
    lv_obj_t* spinner = lv_obj_find_by_name(modal, "connecting_container");
    lv_obj_t* connect = lv_obj_find_by_name(modal, "connect_btn");
    lv_obj_t* cancel = lv_obj_find_by_name(modal, "cancel_btn");
    REQUIRE(form != nullptr);
    REQUIRE(spinner != nullptr);
    REQUIRE(connect != nullptr);
    REQUIRE(cancel != nullptr);
    CHECK_FALSE(lv_obj_has_flag(form, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(spinner, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(connect, LV_OBJ_FLAG_HIDDEN));

    lv_obj_t* ssid_input = lv_obj_find_by_name(modal, "ssid_input");
    REQUIRE(ssid_input != nullptr);
    lv_textarea_set_text(ssid_input, "StealthNet");
    lv_obj_t* security = lv_obj_find_by_name(modal, "security_dropdown");
    REQUIRE(security != nullptr);
    lv_dropdown_set_selected(security, 0);

    Access::hidden_connect_clicked(*overlay);

    CHECK(lv_obj_has_flag(form, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(spinner, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(connect, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(cancel, LV_OBJ_FLAG_HIDDEN));
}
