// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// helix::SharedCallback<Sig> carries the Moonraker network layer's callbacks,
// which are copied into wrappers and the pending-request table. These tests pin
// the sharing rule (copies run one callable), the trivial inline path that copies
// by value, and the ownership rule: the callable is destroyed exactly once, with
// its last copy, whichever thread releases it.

#include "helix_shared_callback.h"

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::SharedCallback;

namespace {

/// Counts destructor runs of every live or moved-from instance.
struct DtorCounter {
    int* count;
    explicit DtorCounter(int* c) : count(c) {}
    DtorCounter(DtorCounter&& o) noexcept : count(o.count) {
        o.count = nullptr;
    }
    DtorCounter(const DtorCounter&) = delete;
    ~DtorCounter() {
        if (count != nullptr) {
            ++*count;
        }
    }
};

} // namespace

TEST_CASE("SharedCallback: copies share one callable", "[helix_callback]") {
    SharedCallback<int()> a = [n = 0, s = std::string("x")]() mutable { return ++n; };
    SharedCallback<int()> b = a;
    REQUIRE(a() == 1);
    REQUIRE(b() == 2);
}

TEST_CASE("SharedCallback: a trivially copyable callable copies by value", "[helix_callback]") {
    SharedCallback<int()> a = [n = 0]() mutable { return ++n; };
    SharedCallback<int()> b = a;
    REQUIRE(a() == 1);
    REQUIRE(b() == 1);
}

TEST_CASE("SharedCallback: the callable is destroyed once, with the last copy",
          "[helix_callback]") {
    int dtors = 0;

    SECTION("copies and moves") {
        SharedCallback<void()> a = [c = std::make_shared<DtorCounter>(&dtors)]() {};
        {
            SharedCallback<void()> b = a;
            SharedCallback<void()> c = std::move(b);
            REQUIRE_FALSE(b);
            SharedCallback<void()> d;
            d = c;
        }
        REQUIRE(dtors == 0);
        a = nullptr;
        REQUIRE(dtors == 1);
    }

    SECTION("copy-assigned over a live callable") {
        int other_dtors = 0;
        SharedCallback<void()> a = [c = DtorCounter(&dtors)]() {};
        SharedCallback<void()> b = [c = DtorCounter(&other_dtors)]() {};
        b = a;
        REQUIRE(other_dtors == 1);
        REQUIRE(dtors == 0);
        a = nullptr;
        REQUIRE(dtors == 0);
        b = nullptr;
        REQUIRE(dtors == 1);
    }

    SECTION("copied and released on several threads") {
        SharedCallback<void()> a = [c = DtorCounter(&dtors)]() {};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([copy = a]() {
                for (int i = 0; i < 2000; ++i) {
                    SharedCallback<void()> local = copy;
                    local();
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
        REQUIRE(dtors == 0);
        a = nullptr;
        REQUIRE(dtors == 1);
    }
}

TEST_CASE("SharedCallback: holds a move-only capture", "[helix_callback]") {
    int seen = 0;
    SharedCallback<void()> a = [p = std::make_unique<int>(9), &seen]() { seen = *p; };
    SharedCallback<void()> b = a;
    b();
    REQUIRE(seen == 9);
}

TEST_CASE("SharedCallback: empty states", "[helix_callback]") {
    REQUIRE_FALSE(SharedCallback<void()>{});
    REQUIRE(SharedCallback<void()>{nullptr} == nullptr);
    REQUIRE_FALSE(SharedCallback<void()>{std::function<void()>{}});

    SharedCallback<void()> empty;
    REQUIRE_THROWS_AS(empty(), std::bad_function_call);
}

TEST_CASE("SharedCallback and Callback: an empty one converts to an empty one",
          "[helix_callback][1874]") {
    REQUIRE_FALSE(helix::Callback<void()>{SharedCallback<void()>{}});
    REQUIRE_FALSE(SharedCallback<void()>{helix::Callback<void()>{}});
#if defined(_LIBCPP_VERSION)
    // An empty one would become a non-empty std::function, so neither converts to one
    // implicitly. Only libc++ honours the refusal: libstdc++ prefers std::function's
    // own converting constructor.
    static_assert(!std::is_convertible_v<SharedCallback<void()>, std::function<void()>>);
    static_assert(!std::is_convertible_v<helix::Callback<void()>, std::function<void()>>);
#endif
}
