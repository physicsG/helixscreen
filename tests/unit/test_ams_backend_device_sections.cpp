// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_backend_device_sections.cpp
 * @brief Every DeviceAction must name a section its own backend declares.
 *
 * AmsDeviceOperationsOverlay::populate_section_list() renders a row only for
 * actions whose `section` string equals a declared section id, and
 * AmsOperationSidebar::update_settings_visibility() hides the settings gear
 * entirely when a backend declares no sections. A backend that returns
 * actions with an empty or undeclared section therefore returns actions the
 * UI can never reach. This pins the pairing for every backend at once, so a
 * new backend (or a conditionally-hidden section whose actions survive)
 * cannot ship the mismatch.
 */

#include "../test_helpers/ams_backend_probes.h"
#include "ams_backend.h"
#include "ams_backend_mock.h"
#include "ams_types.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {

void require_actions_in_declared_sections(const char* backend_name,
                                          const helix::AmsBackend& backend) {
    auto sections = backend.get_device_sections();
    auto actions = backend.get_device_actions();

    std::vector<std::string> declared_ids;
    for (const auto& s : sections) {
        declared_ids.push_back(s.id);
    }

    // Every action must land in a declared section: an undeclared one renders
    // nowhere, and so does an empty section string once any section exists.
    for (const auto& a : actions) {
        INFO(backend_name << " action '" << a.id << "' names section '" << a.section << "'");
        CHECK(std::find(declared_ids.begin(), declared_ids.end(), a.section) != declared_ids.end());
    }
}

} // namespace

TEST_CASE("AMS backends declare a section for every device action", "[ams][sections]") {
    AceProbe ace;
    require_actions_in_declared_sections("AmsBackendAce", ace);

    Ad5xIfsProbe ad5x_ifs;
    require_actions_in_declared_sections("AmsBackendAd5xIfs", ad5x_ifs);

    AfcProbe afc;
    require_actions_in_declared_sections("AmsBackendAfc", afc);

    CfsProbe cfs;
    require_actions_in_declared_sections("AmsBackendCfs", cfs);

    HappyHareProbe happy_hare;
    require_actions_in_declared_sections("AmsBackendHappyHare", happy_hare);

    QidiProbe qidi;
    require_actions_in_declared_sections("AmsBackendQidi", qidi);

    SnapmakerProbe snapmaker;
    require_actions_in_declared_sections("AmsBackendSnapmaker", snapmaker);

    ToolChangerProbe toolchanger;
    require_actions_in_declared_sections("AmsBackendToolChanger", toolchanger);

    helix::AmsBackendMock mock;
    require_actions_in_declared_sections("AmsBackendMock", mock);
}
