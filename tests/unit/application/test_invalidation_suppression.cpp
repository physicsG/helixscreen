// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/lv_display_private.h" // inv_en_cnt, restored after every test
#include "invalidation_suppression.h"
#include "lvgl_test_fixture.h"

#include "../../catch_amalgamated.hpp"

using helix::InvalidationSuppression;

namespace {

/// Puts the default display's invalidation count back however a test exits, so a failed
/// assertion cannot leave rendering suppressed for the rest of the shard.
class InvalidationSuppressionFixture : public LVGLTestFixture {
  public:
    InvalidationSuppressionFixture()
        : m_display(lv_display_get_default()), m_saved_count(m_display->inv_en_cnt) {
        // Every test starts from the count a display is created with, whatever earlier
        // tests in the shard left behind.
        m_display->inv_en_cnt = 1;
    }
    ~InvalidationSuppressionFixture() override {
        lv_display_set_default(m_display);
        m_display->inv_en_cnt = m_saved_count;
    }

  protected:
    lv_display_t* display() const {
        return m_display;
    }

  private:
    lv_display_t* m_display;
    decltype(lv_display_t::inv_en_cnt) m_saved_count;
};

/// A second display for the length of a test.
class ScopedSecondDisplay {
  public:
    ScopedSecondDisplay() : m_display(lv_display_create(64, 64)) {}
    ~ScopedSecondDisplay() {
        if (m_display) {
            lv_display_delete(m_display);
        }
    }
    ScopedSecondDisplay(const ScopedSecondDisplay&) = delete;
    ScopedSecondDisplay& operator=(const ScopedSecondDisplay&) = delete;

    lv_display_t* get() const {
        return m_display;
    }

  private:
    lv_display_t* m_display;
};

/// True when a single disable from any other owner turns invalidation off, which holds
/// only while the display's count is exactly 1.
bool one_disable_suppresses(lv_display_t* disp) {
    lv_display_enable_invalidation(disp, false);
    const bool suppressed = !lv_display_is_invalidation_enabled(disp);
    lv_display_enable_invalidation(disp, true);
    return suppressed;
}

} // namespace

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression begin disables and end re-enables exactly once",
                 "[application][display][invalidation_suppression]") {
    REQUIRE(lv_display_is_invalidation_enabled(display()));
    REQUIRE(one_disable_suppresses(display()));
    InvalidationSuppression suppression;

    suppression.begin();
    CHECK(suppression.active());
    CHECK_FALSE(lv_display_is_invalidation_enabled(display()));

    suppression.end();
    CHECK_FALSE(suppression.active());
    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
}

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression end twice re-enables once",
                 "[application][display][invalidation_suppression]") {
    InvalidationSuppression suppression;
    suppression.begin();
    suppression.end();
    suppression.end();

    CHECK_FALSE(suppression.active());
    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
}

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression begin twice disables once",
                 "[application][display][invalidation_suppression]") {
    InvalidationSuppression suppression;
    suppression.begin();
    suppression.begin();
    CHECK(suppression.active());
    CHECK_FALSE(lv_display_is_invalidation_enabled(display()));

    suppression.end();
    CHECK_FALSE(suppression.active());
    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
}

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression end without begin changes nothing",
                 "[application][display][invalidation_suppression]") {
    InvalidationSuppression suppression;
    suppression.end();

    CHECK_FALSE(suppression.active());
    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
}

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression re-enables the display it disabled, not the default",
                 "[application][display][invalidation_suppression]") {
    ScopedSecondDisplay other;
    REQUIRE(other.get() != nullptr);
    REQUIRE(lv_display_get_default() == display());
    InvalidationSuppression suppression;

    suppression.begin();
    CHECK_FALSE(lv_display_is_invalidation_enabled(display()));
    CHECK(lv_display_is_invalidation_enabled(other.get()));

    lv_display_set_default(other.get());
    suppression.end();
    lv_display_set_default(display());

    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
    CHECK(lv_display_is_invalidation_enabled(other.get()));
    CHECK(one_disable_suppresses(other.get()));
}

TEST_CASE_METHOD(InvalidationSuppressionFixture,
                 "InvalidationSuppression with no display does nothing",
                 "[application][display][invalidation_suppression]") {
    InvalidationSuppression suppression;
    lv_display_set_default(nullptr);
    suppression.begin();
    const bool active_without_display = suppression.active();
    suppression.end();
    lv_display_set_default(display());

    CHECK_FALSE(active_without_display);
    CHECK(lv_display_is_invalidation_enabled(display()));
    CHECK(one_disable_suppresses(display()));
}
