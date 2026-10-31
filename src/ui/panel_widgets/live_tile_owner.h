// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which instance of a single-instance home widget publishes its subjects.
//
// The subjects are module-level, so only the instance bound to the live tile may write
// them: a fits_at probe never attaches and so never claims, and a rebuild's incoming
// instance claims on attach. Ownership is a stack, so when an instance that took it
// detaches, the one it displaced gets it back instead of the tile going dead.

#include <algorithm>
#include <vector>

namespace helix {

template <typename W> class LiveTileOwner {
  public:
    void claim(W* widget) {
        release(widget);
        attached_.push_back(widget);
    }

    /// Drop @p widget. Returns the instance that owns the tile afterwards when @p widget
    /// was the owner and another is still attached, so the caller can have it republish.
    W* release(W* widget) {
        const bool was_owner = is_owner(widget);
        attached_.erase(std::remove(attached_.begin(), attached_.end(), widget), attached_.end());
        return was_owner && !attached_.empty() ? attached_.back() : nullptr;
    }

    [[nodiscard]] bool is_owner(const W* widget) const {
        return !attached_.empty() && attached_.back() == widget;
    }

    [[nodiscard]] W* owner() const {
        return attached_.empty() ? nullptr : attached_.back();
    }

  private:
    std::vector<W*> attached_;
};

} // namespace helix
