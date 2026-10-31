// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Makes helix::board::current() answer one board for the scope and restores the build's
// real answer on exit, even when a REQUIRE throws. A leaked override changes every later
// case's board in the shard.

#include "board_identity.h"

namespace helix::test {

class ScopedBoard {
  public:
    explicit ScopedBoard(board::Board board) {
        board::set_current_override(board);
    }
    ~ScopedBoard() {
        board::set_current_override(std::nullopt);
    }
    ScopedBoard(const ScopedBoard&) = delete;
    ScopedBoard& operator=(const ScopedBoard&) = delete;
};

} // namespace helix::test
