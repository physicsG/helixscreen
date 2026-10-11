// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Fixed-size buffers for card thumbnails, allocated the first time each is
// needed and then reused until the pool goes. Freeing and reallocating an 80KB
// thumbnail for every card that scrolls past fragments a small heap until no
// block that size is left; a slot handed back is handed out again instead.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace helix {

class ThumbnailSlotPool {
  public:
    using AllocFn = void* (*)(size_t);
    using FreeFn = void (*)(void*);

    /// Up to @p max_slots buffers of @p slot_bytes each, from @p alloc.
    ThumbnailSlotPool(size_t slot_bytes, size_t max_slots, AllocFn alloc, FreeFn free);
    ~ThumbnailSlotPool();
    ThumbnailSlotPool(const ThumbnailSlotPool&) = delete;
    ThumbnailSlotPool& operator=(const ThumbnailSlotPool&) = delete;

    /// A free slot, allocating one while fewer than max_slots exist. nullptr
    /// when every slot is in use or the allocation failed.
    uint8_t* acquire();
    /// Hands @p slot back for reuse. Safe from any thread.
    void release(uint8_t* slot);
    /// Frees every slot handed back, so their memory serves something else;
    /// the pool allocates again as needed.
    void trim();

    size_t slot_bytes() const {
        return slot_bytes_;
    }
    /// Slots allocated so far, in use or free.
    size_t allocated() const;
    /// Slots handed out and not yet handed back.
    size_t in_use() const;

  private:
    const size_t slot_bytes_;
    const size_t max_slots_;
    const AllocFn alloc_;
    const FreeFn free_;
    mutable std::mutex mutex_;
    std::vector<uint8_t*> all_;
    std::vector<uint8_t*> free_list_;
};

} // namespace helix
