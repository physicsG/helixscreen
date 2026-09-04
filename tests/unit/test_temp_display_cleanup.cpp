// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_temp_display_cleanup.cpp
 * @brief TDD tests for temp_display safe observer cleanup on widget deletion
 *
 * Validates that deleting a temp_display widget properly removes all observers
 * from external subjects, preventing use-after-free crashes when the external
 * subject is later notified or deinited.
 *
 * The bug: on_delete() deinits owned string subjects (freeing TempDisplayData)
 * while child labels still have external-subject observers registered. When
 * children are subsequently deleted, LVGL's event chain walks corrupted memory.
 *
 * The fix: call lv_obj_remove_from_subject(label, nullptr) on child labels
 * BEFORE deiniting owned subjects, removing ALL observers from those labels.
 */

#include "ui_temp_display.h"

#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl_test_fixture.h"

#include "../catch_amalgamated.hpp"

/**
 * Helper: count observers on a subject by walking its linked list.
 * LVGL does not expose a public observer count API, so we inspect
 * the internal subs_ll linked list directly.
 */
static int count_subject_observers(lv_subject_t* subject) {
    int count = 0;
    void* node = lv_ll_get_head(&subject->subs_ll);
    while (node != nullptr) {
        count++;
        node = lv_ll_get_next(&subject->subs_ll, node);
    }
    return count;
}

TEST_CASE_METHOD(LVGLTestFixture, "external subject has no observers after temp_display deletion",
                 "[temp_display]") {
    // Register the temp_display custom widget
    ui_temp_display_init();

    // Create an external integer subject simulating a temperature value
    lv_subject_t current_temp_subject;
    lv_subject_init_int(&current_temp_subject, 0);

    // Register it as a global XML subject so temp_display can find it via bind_current
    lv_xml_register_subject(nullptr, "test_current_temp", &current_temp_subject);

    // Create a container to hold the temp_display widget
    lv_obj_t* container = lv_obj_create(test_screen());

    // Create temp_display with bind_current pointing to our external subject
    const char* attrs[] = {"bind_current", "test_current_temp", nullptr};
    lv_obj_t* td = static_cast<lv_obj_t*>(lv_xml_create(container, "temp_display", attrs));
    REQUIRE(td != nullptr);

    // Verify the external subject has at least one observer (from temp_display binding)
    REQUIRE(count_subject_observers(&current_temp_subject) > 0);

    // Delete the container (which deletes the temp_display and all children)
    lv_obj_delete(container);

    // After deletion, the external subject should have zero observers remaining
    CHECK(count_subject_observers(&current_temp_subject) == 0);

    // Clean up the external subject
    lv_subject_deinit(&current_temp_subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "external subject safe after temp_display deletion",
                 "[temp_display]") {
    // Register the temp_display custom widget
    ui_temp_display_init();

    // Create an external integer subject simulating a temperature value
    lv_subject_t current_temp_subject;
    lv_subject_init_int(&current_temp_subject, 0);

    // Register it as a global XML subject so temp_display can find it via bind_current
    lv_xml_register_subject(nullptr, "test_current_temp", &current_temp_subject);

    // Create a container to hold the temp_display widget
    lv_obj_t* container = lv_obj_create(test_screen());

    // Create temp_display with bind_current pointing to our external subject
    const char* attrs[] = {"bind_current", "test_current_temp", nullptr};
    lv_obj_t* td = static_cast<lv_obj_t*>(lv_xml_create(container, "temp_display", attrs));
    REQUIRE(td != nullptr);

    // Delete the container (which deletes the temp_display and all children)
    lv_obj_delete(container);

    // Setting the subject after deletion must not crash. If temp_display cleanup
    // failed to remove the observer, this would dereference freed memory.
    lv_subject_set_int(&current_temp_subject, 12345);

    // If we reach here without crashing, the cleanup was safe
    CHECK(lv_subject_get_int(&current_temp_subject) == 12345);

    // Clean up the external subject
    lv_subject_deinit(&current_temp_subject);
}
