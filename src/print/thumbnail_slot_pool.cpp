// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_slot_pool.h"

#include <algorithm>

namespace helix {

ThumbnailSlotPool::ThumbnailSlotPool(size_t slot_bytes, size_t max_slots, AllocFn alloc,
                                     FreeFn free)
    : slot_bytes_(slot_bytes), max_slots_(max_slots), alloc_(alloc), free_(free) {}

ThumbnailSlotPool::~ThumbnailSlotPool() {
    for (uint8_t* slot : all_) {
        free_(slot);
    }
}

uint8_t* ThumbnailSlotPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!free_list_.empty()) {
        uint8_t* slot = free_list_.back();
        free_list_.pop_back();
        return slot;
    }
    if (all_.size() >= max_slots_) {
        return nullptr;
    }
    // Reserve the bookkeeping first so recording the slot cannot fail after it
    // was allocated.
    if (all_.capacity() < max_slots_) {
        all_.reserve(max_slots_);
        free_list_.reserve(max_slots_);
    }
    auto* slot = static_cast<uint8_t*>(alloc_(slot_bytes_));
    if (slot) {
        all_.push_back(slot);
    }
    return slot;
}

void ThumbnailSlotPool::release(uint8_t* slot) {
    if (!slot) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.push_back(slot);
}

void ThumbnailSlotPool::trim() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint8_t* slot : free_list_) {
        all_.erase(std::find(all_.begin(), all_.end(), slot));
        free_(slot);
    }
    free_list_.clear();
}

size_t ThumbnailSlotPool::in_use() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_.size() - free_list_.size();
}

size_t ThumbnailSlotPool::allocated() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return all_.size();
}

} // namespace helix
