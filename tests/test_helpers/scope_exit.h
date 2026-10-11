// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <utility>

namespace helix::test {

/// Runs `fn` when the scope ends, including when a failed REQUIRE unwinds it, so a
/// test that registers process-global state (runtime widget defs, XML components,
/// XML subjects) cannot leak it into the later cases of its shard.
class ScopeExit {
  public:
    explicit ScopeExit(std::function<void()> fn) : fn_(std::move(fn)) {}
    ~ScopeExit() {
        if (fn_)
            fn_();
    }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

  private:
    std::function<void()> fn_;
};

} // namespace helix::test
