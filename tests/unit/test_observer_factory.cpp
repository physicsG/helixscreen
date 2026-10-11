// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_observer_factory.cpp
 * @brief Unit tests for observer factory helpers
 *
 * Tests the observe_* factory functions for correctness,
 * edge cases, and behavior preservation.
 */

#include "ui_temperature_utils.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/update_queue_test_access.h"
#include "observer_factory.h"
#include "print_lifecycle_state.h"
#include "printer_state.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix::ui;
using namespace helix::ui;
using helix::PrintJobState;
using helix::ui::temperature::deci_to_degrees;

// Helper to drain the update queue after subject changes.
// observe<int> and observe<const char*> defer callbacks via ui_queue_update.
static void drain() {
    UpdateQueueTestAccess::drain(helix::ui::UpdateQueue::instance());
}

// ============================================================================
// Test Panel Class - Mimics a real panel for testing
// ============================================================================

class TestPanel {
  public:
    int int_value = 0;
    int callback_count = 0;
    bool update_called = false;
    std::string string_value;

    void on_value_update() {
        update_called = true;
        callback_count++;
    }

    void reset() {
        int_value = 0;
        callback_count = 0;
        update_called = false;
        string_value.clear();
    }
};

// ============================================================================
// observe<int> Tests (deferred via ui_queue_update)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> stores value", "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe<int>(
        &subject, &panel, [](TestPanel* p, int value) { p->int_value = value; },
        subject_never_freed());

    // Initial callback fires on subscription (deferred)
    drain();
    REQUIRE(panel.int_value == 0);

    // Value change triggers callback
    lv_subject_set_int(&subject, 42);
    drain();
    REQUIRE(panel.int_value == 42);

    // Another change
    lv_subject_set_int(&subject, 100);
    drain();
    REQUIRE(panel.int_value == 100);

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> with transformation",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    // Use transformation inside lambda
    auto guard = observe<int>(
        &subject, &panel, [](TestPanel* p, int raw) { p->int_value = deci_to_degrees(raw); },
        subject_never_freed());

    // Set to 210C (decidegrees = 2100)
    lv_subject_set_int(&subject, 2100);
    drain();
    REQUIRE(panel.int_value == 210);

    // Set to 245C
    lv_subject_set_int(&subject, 2450);
    drain();
    REQUIRE(panel.int_value == 245);

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> null subject returns empty guard",
                 "[factory][observer][edge]") {
    TestPanel panel;

    auto guard = observe<int>(
        nullptr, &panel, [](TestPanel* p, int value) { p->int_value = value; },
        subject_never_freed());

    REQUIRE_FALSE(guard); // Guard should be empty
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> null panel returns empty guard",
                 "[factory][observer][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 42);

    auto guard = observe<int>(
        &subject, static_cast<TestPanel*>(nullptr),
        [](TestPanel* p, int value) { p->int_value = value; }, subject_never_freed());

    REQUIRE_FALSE(guard); // Guard should be empty

    lv_subject_deinit(&subject);
}

// ============================================================================
// observe<int> with Dispatch::Immediate (synchronous, no deferral)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> fires synchronously",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    TestPanel panel;

    auto guard = observe<int>(
        &subject, &panel, [](TestPanel* p, int value) { p->int_value = value; },
        subject_never_freed(), Dispatch::Immediate);

    // No drain needed — immediate fires synchronously
    REQUIRE(panel.int_value == 0);

    lv_subject_set_int(&subject, 42);
    REQUIRE(panel.int_value == 42);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe<const char*> Tests (deferred via ui_queue_update)
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<const char*> handles string values",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe<const char*>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; },
        subject_never_freed());

    // Initial callback fires on subscription (deferred)
    drain();
    REQUIRE(panel.string_value == "");

    // Value change triggers callback
    lv_subject_copy_string(&subject, "test");
    drain();
    REQUIRE(panel.string_value == "test");

    // Another change
    lv_subject_copy_string(&subject, "hello world");
    drain();
    REQUIRE(panel.string_value == "hello world");

    guard.release();
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<const char*> parses axes like ControlsPanel",
                 "[factory][observer][string]") {
    static char buf[16] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    struct AxesState {
        bool x = false, y = false, z = false;
        bool all = false;
    } state;

    auto guard = observe<const char*>(
        &subject, &state,
        [](AxesState* s, const char* axes) {
            s->x = strchr(axes, 'x') != nullptr;
            s->y = strchr(axes, 'y') != nullptr;
            s->z = strchr(axes, 'z') != nullptr;
            s->all = s->x && s->y && s->z;
        },
        subject_never_freed());

    // Empty = nothing homed
    drain();
    REQUIRE(state.x == false);
    REQUIRE(state.all == false);

    // All homed
    lv_subject_copy_string(&subject, "xyz");
    drain();
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == true);
    REQUIRE(state.all == true);

    // Partial homing
    lv_subject_copy_string(&subject, "xy");
    drain();
    REQUIRE(state.x == true);
    REQUIRE(state.y == true);
    REQUIRE(state.z == false);
    REQUIRE(state.all == false);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// observe<const char*> Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<const char*> fires synchronously",
                 "[factory][observer][string]") {
    static char buf[32] = "";
    lv_subject_t subject;
    lv_subject_init_string(&subject, buf, nullptr, sizeof(buf), "");

    TestPanel panel;

    auto guard = observe<const char*>(
        &subject, &panel, [](TestPanel* p, const char* str) { p->string_value = str; },
        subject_never_freed(), Dispatch::Immediate);

    // No drain needed — immediate fires synchronously
    REQUIRE(panel.string_value == "");

    lv_subject_copy_string(&subject, "test");
    REQUIRE(panel.string_value == "test");

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// RAII Cleanup Tests
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: ObserverGuard RAII cleanup works",
                 "[factory][observer][raii]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);

    std::atomic<int> callback_count{0};

    {
        TestPanel panel;
        auto guard = observe<int>(
            &subject, &panel, [&callback_count](TestPanel*, int) { callback_count++; },
            subject_never_freed());

        drain();
        REQUIRE(callback_count.load() == 1); // Initial

        lv_subject_set_int(&subject, 42);
        drain();
        REQUIRE(callback_count.load() == 2);

        // Guard goes out of scope here — drain any pending before panel dies
        drain();
    }

    // After guard destroyed, no more callbacks
    callback_count.store(0);
    lv_subject_set_int(&subject, 100);
    drain();
    REQUIRE(callback_count.load() == 0);

    lv_subject_deinit(&subject);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: LVGL optimizes unchanged values",
                 "[factory][observer][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 50);

    int callback_count = 0;
    TestPanel panel;

    auto guard = observe<int>(
        &subject, &panel, [&callback_count](TestPanel*, int) { callback_count++; },
        subject_never_freed());

    drain();
    callback_count = 0; // Reset after initial

    // Same value - no callback
    lv_subject_set_int(&subject, 50);
    drain();
    REQUIRE(callback_count == 0);

    // Different value - callback
    lv_subject_set_int(&subject, 51);
    drain();
    REQUIRE(callback_count == 1);

    guard.release();
    lv_subject_deinit(&subject);
}

// ============================================================================
// Deferred safety test — observer reassignment during notification
// ============================================================================

TEST_CASE_METHOD(LVGLTestFixture, "Factory: observe<int> safe under observer reassignment",
                 "[factory][observer][safety]") {
    lv_subject_t subject_a;
    lv_subject_t subject_b;
    lv_subject_init_int(&subject_a, 0);
    lv_subject_init_int(&subject_b, 100);

    TestPanel panel;
    ObserverGuard inner_guard;

    // Outer observer reassigns inner_guard when notified — this is the
    // exact pattern that caused the crash in issue #82.
    inner_guard = observe<int>(
        &subject_b, &panel, [](TestPanel* p, int value) { p->int_value = value; },
        subject_never_freed());
    drain();

    auto outer_guard = observe<int>(
        &subject_a, &panel,
        [&inner_guard, &subject_b](TestPanel* p, int /*value*/) {
            // Reassign inner observer — old one is destroyed here.
            // With deferred execution, this is safe.
            inner_guard = observe<int>(
                &subject_b, p, [](TestPanel* pp, int v) { pp->int_value = v * 2; },
                subject_never_freed());
        },
        subject_never_freed());
    drain();

    // Trigger the outer observer — should safely reassign inner
    lv_subject_set_int(&subject_a, 1);
    drain(); // outer fires, reassigns inner_guard
    drain(); // inner's initial callback fires

    // Now inner should use the new handler (value * 2)
    lv_subject_set_int(&subject_b, 50);
    drain();
    REQUIRE(panel.int_value == 100); // 50 * 2

    outer_guard.release();
    inner_guard.release();
    lv_subject_deinit(&subject_a);
    lv_subject_deinit(&subject_b);
}

// ============================================================================
// Dispatch and teardown contract, pinned for every family
// ============================================================================

namespace {
bool queue_empty() {
    return UpdateQueueTestAccess::queue_empty(helix::ui::UpdateQueue::instance());
}

struct Recorder {
    std::vector<int> ints;
    std::vector<std::string> strings;
};
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "Factory: a guard reset before drain drops the queued call",
                 "[factory][observer][safety]") {
    static char buf[16] = "";
    lv_subject_t int_subject;
    lv_subject_t str_subject;
    lv_subject_init_int(&int_subject, 0);
    lv_subject_init_string(&str_subject, buf, nullptr, sizeof(buf), "");
    Recorder rec;
    ObserverGuard guard;

    SECTION("observe<int>") {
        guard = observe<int>(
            &int_subject, &rec, [](Recorder* r, int v) { r->ints.push_back(v); },
            subject_never_freed());
    }
    SECTION("observe<const char*>") {
        guard = observe<const char*>(
            &str_subject, &rec, [](Recorder* r, const char* s) { r->strings.push_back(s); },
            subject_never_freed());
    }
    SECTION("observe_print_state") {
        guard = observe_print_state<Recorder>(
            &int_subject, &rec,
            [](Recorder* r, PrintJobState s) { r->ints.push_back(static_cast<int>(s)); },
            subject_never_freed());
    }
    SECTION("observe_print_lifecycle") {
        guard = observe_print_lifecycle<Recorder>(
            &int_subject, &rec,
            [](Recorder* r, PrintState s) { r->ints.push_back(static_cast<int>(s)); },
            subject_never_freed());
    }

    REQUIRE(guard);
    drain();
    rec = Recorder{};

    lv_subject_set_int(&int_subject, 2);
    lv_subject_copy_string(&str_subject, "x");
    guard.reset();
    drain();

    CHECK(rec.ints.empty());
    CHECK(rec.strings.empty());

    lv_subject_deinit(&int_subject);
    lv_subject_deinit(&str_subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: deferred families deliver the value at notify time",
                 "[factory][observer]") {
    static char buf[16] = "";
    lv_subject_t int_subject;
    lv_subject_t str_subject;
    lv_subject_init_int(&int_subject, 0);
    lv_subject_init_string(&str_subject, buf, nullptr, sizeof(buf), "");
    Recorder rec;

    auto g1 = observe<int>(
        &int_subject, &rec, [](Recorder* r, int v) { r->ints.push_back(v); },
        subject_never_freed());
    auto g2 = observe<const char*>(
        &str_subject, &rec, [](Recorder* r, const char* s) { r->strings.push_back(s); },
        subject_never_freed());

    // Subscription queues one call carrying the current value; nothing runs before drain.
    CHECK(rec.ints.empty());
    CHECK(rec.strings.empty());
    CHECK_FALSE(queue_empty());
    drain();
    CHECK(rec.ints == std::vector<int>{0});
    CHECK(rec.strings == std::vector<std::string>{""});

    // Two notifications before one drain: each call sees its own value, in order, and the
    // string is copied at notify time, not read back from the subject's buffer at drain.
    lv_subject_set_int(&int_subject, 1);
    lv_subject_set_int(&int_subject, 2);
    lv_subject_copy_string(&str_subject, "a");
    lv_subject_copy_string(&str_subject, "b");
    CHECK(rec.ints.size() == 1);
    drain();
    CHECK(rec.ints == std::vector<int>{0, 1, 2});
    CHECK(rec.strings == std::vector<std::string>{"", "a", "b"});

    g1.reset();
    g2.reset();
    lv_subject_deinit(&int_subject);
    lv_subject_deinit(&str_subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: immediate families run inside the notification",
                 "[factory][observer]") {
    static char buf[16] = "";
    lv_subject_t int_subject;
    lv_subject_t str_subject;
    lv_subject_init_int(&int_subject, 0);
    lv_subject_init_string(&str_subject, buf, nullptr, sizeof(buf), "");
    drain();
    Recorder rec;

    auto g1 = observe<int>(
        &int_subject, &rec, [](Recorder* r, int v) { r->ints.push_back(v); }, subject_never_freed(),
        Dispatch::Immediate);
    auto g2 = observe<const char*>(
        &str_subject, &rec, [](Recorder* r, const char* s) { r->strings.push_back(s); },
        subject_never_freed(), Dispatch::Immediate);
    auto g3 = observe_print_state(
        &int_subject, &rec,
        [](Recorder* r, PrintJobState s) { r->ints.push_back(100 + static_cast<int>(s)); },
        subject_never_freed(), Dispatch::Immediate);

    // Subscription fires once, synchronously, and queues nothing.
    CHECK(rec.ints == std::vector<int>{0, 100});
    CHECK(rec.strings == std::vector<std::string>{""});
    CHECK(queue_empty());

    lv_subject_set_int(&int_subject, static_cast<int>(PrintJobState::PAUSED));
    lv_subject_copy_string(&str_subject, "now");
    CHECK(rec.ints == std::vector<int>{0, 100, 2, 102});
    CHECK(rec.strings == std::vector<std::string>{"", "now"});
    CHECK(queue_empty());

    g1.reset();
    g2.reset();
    g3.reset();
    lv_subject_deinit(&int_subject);
    lv_subject_deinit(&str_subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: handler captures are freed after reset and drain",
                 "[factory][observer][raii]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);
    Recorder rec;
    auto state = std::make_shared<int>(0);
    std::weak_ptr<int> weak_state = state;
    ObserverGuard guard;

    SECTION("deferred") {
        guard = observe<int>(
            &subject, &rec, [state](Recorder*, int v) { *state = v; }, subject_never_freed());
    }
    SECTION("immediate") {
        guard = observe<int>(
            &subject, &rec, [state](Recorder*, int v) { *state = v; }, subject_never_freed(),
            Dispatch::Immediate);
    }
    state.reset();
    REQUIRE_FALSE(weak_state.expired());

    // A notification may leave a queued call holding the handler; resetting the guard
    // while it is pending must still free everything once the queue drains.
    lv_subject_set_int(&subject, 3);
    guard.reset();
    drain();
    CHECK(weak_state.expired());

    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "Factory: a dead subject lifetime drops the queued call and frees the context",
                 "[factory][observer][subject_lifetime]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);
    SubjectLifetime lifetime = std::make_shared<bool>(true);
    Recorder rec;
    auto state = std::make_shared<int>(0);
    std::weak_ptr<int> weak_state = state;

    auto guard = observe<int>(
        &subject, &rec,
        [state](Recorder* r, int v) {
            r->ints.push_back(v);
            *state = v;
        },
        lifetime);
    state.reset();
    drain();
    rec = Recorder{};

    lv_subject_set_int(&subject, 9);
    // Owner teardown order: signal death, then deinit frees the observer node.
    *lifetime = false;
    lv_subject_deinit(&subject);
    guard.reset(); // must not touch the freed observer (ASAN)
    drain();

    CHECK(rec.ints.empty());
    CHECK(weak_state.expired());
}

TEST_CASE_METHOD(LVGLTestFixture, "Factory: null subject or owner returns an empty guard",
                 "[factory][observer][edge]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 1);
    int calls = 0;
    auto h_int = [&calls](Recorder*, int) { calls++; };
    auto h_str = [&calls](Recorder*, const char*) { calls++; };
    Recorder rec;

    CHECK_FALSE(observe<int>(nullptr, &rec, h_int, subject_never_freed()));
    CHECK_FALSE(
        observe<int>(&subject, static_cast<Recorder*>(nullptr), h_int, subject_never_freed()));
    CHECK_FALSE(observe<int>(nullptr, &rec, h_int, subject_never_freed(), Dispatch::Immediate));
    CHECK_FALSE(observe<int>(&subject, static_cast<Recorder*>(nullptr), h_int,
                             subject_never_freed(), Dispatch::Immediate));
    CHECK_FALSE(observe<const char*>(nullptr, &rec, h_str, subject_never_freed()));
    CHECK_FALSE(
        observe<const char*>(nullptr, &rec, h_str, subject_never_freed(), Dispatch::Immediate));
    CHECK_FALSE(observe_print_state<Recorder>(
        nullptr, &rec, [](Recorder*, PrintJobState) {}, subject_never_freed()));
    drain();
    CHECK(calls == 0);

    lv_subject_deinit(&subject);
}

namespace {
// Sets *flag when the handler holding it is destroyed. Moves hand the flag over,
// so only the last live copy reports.
struct DestroyProbe {
    bool* flag;
    explicit DestroyProbe(bool* f) : flag(f) {}
    DestroyProbe(const DestroyProbe& o) : flag(o.flag) {}
    DestroyProbe(DestroyProbe&& o) noexcept : flag(std::exchange(o.flag, nullptr)) {}
    ~DestroyProbe() {
        if (flag)
            *flag = true;
    }
};
} // namespace

TEST_CASE_METHOD(LVGLTestFixture,
                 "Factory: an immediate handler that resets its own guard outlives the call",
                 "[factory][observer][safety]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);
    Recorder rec;
    static bool handler_destroyed = false;
    static bool destroyed_mid_call = true;
    static ObserverGuard* self_guard = nullptr;
    ObserverGuard guard;
    self_guard = &guard;

    guard = observe<int>(
        &subject, &rec,
        [probe = DestroyProbe(&handler_destroyed)](Recorder* r, int v) {
            r->ints.push_back(v);
            if (v == 1) {
                self_guard->reset();
                destroyed_mid_call = handler_destroyed;
            }
        },
        subject_never_freed(), Dispatch::Immediate);
    handler_destroyed = false;

    lv_subject_set_int(&subject, 1);
    CHECK_FALSE(destroyed_mid_call);
    CHECK(handler_destroyed);
    CHECK_FALSE(guard);

    lv_subject_set_int(&subject, 2);
    CHECK(rec.ints == std::vector<int>{0, 1});

    self_guard = nullptr;
    lv_subject_deinit(&subject);
}

TEST_CASE_METHOD(LVGLTestFixture,
                 "Factory: a mutable immediate handler keeps its state across notifications",
                 "[factory][observer]") {
    lv_subject_t subject;
    lv_subject_init_int(&subject, 0);
    Recorder rec;

    auto guard = observe<int>(
        &subject, &rec, [n = 0](Recorder* r, int) mutable { r->ints.push_back(++n); },
        subject_never_freed(), Dispatch::Immediate);
    lv_subject_set_int(&subject, 1);
    lv_subject_set_int(&subject, 2);
    CHECK(rec.ints == std::vector<int>{1, 2, 3});

    guard.reset();
    lv_subject_deinit(&subject);
}
