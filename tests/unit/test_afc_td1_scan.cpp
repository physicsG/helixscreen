// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// AFC TD-1 scanner readings (td1_color / td1_td / td1_scan_time on a lane object)
// land in the slot's generic scan fields (#1151).

#include "ams_backend_afc.h"
#include "ams_types.h"
#include "test_helpers/afc_test_access.h"

#include <string>
#include <vector>

#include "catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;
using json = nlohmann::json;

namespace {

class Td1Helper : public AmsBackendAfc {
  public:
    Td1Helper() : AmsBackendAfc(nullptr, nullptr) {
        std::vector<std::string> names{"lane1", "lane2"};
        AfcTestAccess::initialize_slots(*this, names);
    }
    void feed_lane(const json& data) {
        json params;
        params["AFC_stepper lane1"] = data;
        json notification;
        notification["params"] = json::array({params, 0.0});
        handle_status_update(notification);
    }
};

} // namespace

TEST_CASE("TD-1 lane scan populates the generic slot scan fields", "[ams][afc][td1]") {
    Td1Helper afc;
    afc.feed_lane({{"material", "PLA"},
                   {"color", "#112233"},
                   {"td1_color", "FF8800"},
                   {"td1_td", 1.7},
                   {"td1_scan_time", "2026-10-08T09:30:00"}});

    const SlotInfo slot = afc.get_slot_info(0);
    REQUIRE(slot.has_scan());
    CHECK(*slot.scanned_color_rgb == 0xFF8800);
    CHECK(slot.scanned_td == Catch::Approx(1.7f));
    CHECK(slot.scanned_time == "2026-10-08T09:30:00");
    // The scan is a hint: the lane's own colour is untouched.
    CHECK(slot.color_rgb == 0x112233);
}

TEST_CASE("TD-1 absent or empty readings leave the slot unscanned", "[ams][afc][td1]") {
    Td1Helper afc;

    afc.feed_lane({{"material", "PLA"}});
    CHECK_FALSE(afc.get_slot_info(0).has_scan());

    afc.feed_lane({{"td1_color", ""}, {"td1_td", ""}, {"td1_scan_time", ""}});
    const SlotInfo slot = afc.get_slot_info(0);
    CHECK_FALSE(slot.has_scan());
    CHECK(slot.scanned_td < 0.0f);
}

TEST_CASE("A malformed TD-1 colour changes nothing; an empty one clears the scan",
          "[ams][afc][td1]") {
    Td1Helper afc;
    afc.feed_lane({{"td1_color", "00FF00"}});
    REQUIRE(afc.get_slot_info(0).has_scan());

    afc.feed_lane({{"td1_color", "not-a-colour"}});
    REQUIRE(afc.get_slot_info(0).has_scan());
    CHECK(*afc.get_slot_info(0).scanned_color_rgb == 0x00FF00);

    afc.feed_lane({{"td1_color", ""}});
    CHECK_FALSE(afc.get_slot_info(0).has_scan());
}
