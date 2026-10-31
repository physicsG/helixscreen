// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

namespace helix {

/// Runs a callable when the enclosing scope unwinds, on every path including
/// exceptions and early returns.
template <typename F> class ScopeExit {
  public:
    explicit ScopeExit(F fn) : fn_(std::move(fn)) {}
    ~ScopeExit() {
        fn_();
    }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

  private:
    F fn_;
};

} // namespace helix
