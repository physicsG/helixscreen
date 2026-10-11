// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_temperature_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui::temperature;

TEST_CASE("is_residual_hot: hot above the threshold, cool at or below it",
          "[temperature][residual]") {
    CHECK(is_residual_hot(RESIDUAL_HEAT_THRESHOLD_DECI + 1));
    CHECK(is_residual_hot(2100));
    CHECK_FALSE(is_residual_hot(RESIDUAL_HEAT_THRESHOLD_DECI));
    CHECK_FALSE(is_residual_hot(250));
    CHECK_FALSE(is_residual_hot(0));
}

TEST_CASE("is_residual_hot: threshold is 50C in decidegrees", "[temperature][residual]") {
    CHECK(RESIDUAL_HEAT_THRESHOLD_DECI == 500);
}
