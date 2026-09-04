// Copyright (C) 2025-2026 356C LLC
// tests/test_helpers/scoped_home_confirm_prompter.h
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "filament_op_router.h"

#include <utility>

/// RAII guard for helix::ui's process-global home-confirm prompter slot.
///
/// A bare set_home_confirm_prompter(lambda) / set_home_confirm_prompter({})
/// pair only resets the slot if the test reaches the second call. A REQUIRE
/// failure between them unwinds past the manual reset (Catch2 throws to abort
/// the test case), leaving the just-installed prompter in the global slot for
/// every test that runs after it in the shard. Those lambdas routinely
/// capture test-local stack state by reference (a `bool*`, a
/// `std::function<void()>&`), so the next call to ensure_homed_then()
/// anywhere in the shard invokes a dangling reference -- turning one red
/// assertion into a segfault that masks it.
///
/// Construct with the prompter to install (or default-construct to leave the
/// slot cleared); the destructor unconditionally restores the default (an
/// empty HomeConfirmPrompter), including when unwinding from a failed
/// REQUIRE.
class ScopedHomeConfirmPrompter {
  public:
    ScopedHomeConfirmPrompter() = default;
    explicit ScopedHomeConfirmPrompter(helix::ui::HomeConfirmPrompter prompter) {
        helix::ui::set_home_confirm_prompter(std::move(prompter));
    }
    ~ScopedHomeConfirmPrompter() {
        helix::ui::set_home_confirm_prompter({});
    }

    ScopedHomeConfirmPrompter(const ScopedHomeConfirmPrompter&) = delete;
    ScopedHomeConfirmPrompter& operator=(const ScopedHomeConfirmPrompter&) = delete;
    ScopedHomeConfirmPrompter(ScopedHomeConfirmPrompter&&) = delete;
    ScopedHomeConfirmPrompter& operator=(ScopedHomeConfirmPrompter&&) = delete;
};
