// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_static_subject_registry.cpp
 * @brief A subject source that is rebuilt must not leave its old deinit behind.
 *
 * Entries are keyed by name because the name identifies a subject source, not a
 * registration event. A source that is torn down and re-created re-registers
 * under the same name; if both entries survived, shutdown would run a callback
 * closed over the destroyed instance alongside the live one.
 *
 * Uses deinit_one() rather than deinit_all() so the case never touches the other
 * entries the process-wide registry is holding for the rest of the suite.
 */

#include "../helix_test_fixture.h"
#include "static_subject_registry.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(HelixTestFixture, "StaticSubjectRegistry keeps one entry per name",
                 "[core][subjects]") {
    auto& registry = StaticSubjectRegistry::instance();
    static constexpr const char* kName = "StaticSubjectRegistryTestProbe";

    std::vector<std::string> ran;
    registry.register_deinit(kName, [&ran]() { ran.emplace_back("first"); });
    registry.register_deinit(kName, [&ran]() { ran.emplace_back("second"); });

    REQUIRE(registry.deinit_one(kName));
    // The live registration wins, and nothing from the superseded one is left.
    CHECK(ran == std::vector<std::string>{"second"});
    CHECK_FALSE(registry.deinit_one(kName));
}

TEST_CASE_METHOD(HelixTestFixture, "StaticSubjectRegistry re-registration keeps its slot",
                 "[core][subjects]") {
    // A slot is the source's position in registration order, and deinit_all()
    // walks that in reverse so late registrants (widgets) tear down before the
    // early ones whose subjects they observe. Re-registering must not move the
    // entry: a core singleton that re-runs init_subjects() — a soft restart, a
    // printer switch — would land at the tail and be deinitialized FIRST, ahead
    // of every widget observing it.
    //
    // Read the order rather than firing deinit_all(): the registry is
    // process-wide and holds entries left by earlier fixtures.
    auto& registry = StaticSubjectRegistry::instance();
    static constexpr const char* kEarly = "StaticSubjectRegistrySlotEarly";
    static constexpr const char* kMiddle = "StaticSubjectRegistrySlotMiddle";
    static constexpr const char* kLate = "StaticSubjectRegistrySlotLate";

    auto index_of = [&registry](const char* name) {
        const std::vector<std::string> names = registry.names();
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name)
                return static_cast<long>(i);
        }
        return -1L;
    };

    std::vector<std::string> ran;
    registry.register_deinit(kEarly, [&ran]() { ran.emplace_back("early:first"); });
    registry.register_deinit(kMiddle, [&ran]() { ran.emplace_back("middle"); });
    registry.register_deinit(kLate, [&ran]() { ran.emplace_back("late"); });

    const long early_slot = index_of(kEarly);
    REQUIRE(early_slot >= 0);
    REQUIRE(early_slot < index_of(kMiddle));
    REQUIRE(index_of(kMiddle) < index_of(kLate));

    const size_t before = registry.count();
    registry.register_deinit(kEarly, [&ran]() { ran.emplace_back("early:second"); });

    // Still one entry, still in its original slot ahead of the two later ones.
    CHECK(registry.count() == before);
    CHECK(index_of(kEarly) == early_slot);
    CHECK(index_of(kEarly) < index_of(kMiddle));
    CHECK(index_of(kEarly) < index_of(kLate));

    // And the live registration is the one that runs.
    REQUIRE(registry.deinit_one(kEarly));
    CHECK(ran == std::vector<std::string>{"early:second"});
    CHECK_FALSE(registry.deinit_one(kEarly));

    REQUIRE(registry.deinit_one(kMiddle));
    REQUIRE(registry.deinit_one(kLate));
}
