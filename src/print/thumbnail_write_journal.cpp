// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_write_journal.h"

#include <spdlog/spdlog.h>

#include <utility>

namespace helix {

void ThumbnailWriteJournal::note_write(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.size() >= kMaxPending) {
        // Latch rather than log per write: an unread journal overflows once and
        // then keeps overflowing, and the reader's rescan is what clears it.
        if (!overflowed_) {
            spdlog::debug("[ThumbnailWriteJournal] {} unread writes, falling back to rescan",
                          kMaxPending);
        }
        overflowed_ = true;
        return;
    }
    pending_.push_back(path);
}

std::vector<std::string> ThumbnailWriteJournal::drain(bool* overflowed_out) {
    std::vector<std::string> taken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        taken.swap(pending_);
        if (overflowed_out) {
            *overflowed_out = overflowed_;
        }
        overflowed_ = false;
    }
    return taken;
}

void ThumbnailWriteJournal::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    overflowed_ = false;
}

} // namespace helix
