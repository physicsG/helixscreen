// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"
#include "lvgl/src/draw/lv_draw_buf_private.h" // handler fields: no public setters

#include <algorithm>
#include <cstddef>
#include <map>
#include <vector>

/// Counts the large draw-buffer allocations made through LVGL's default handlers,
/// and can refuse them to stand in for an exhausted heap.
class BufAllocSpy {
  public:
    static constexpr size_t BIG = 200 * 1024;

    BufAllocSpy() : handlers_(lv_draw_buf_get_handlers()) {
        orig_malloc_ = handlers_->buf_malloc_cb;
        orig_free_ = handlers_->buf_free_cb;
        handlers_->buf_malloc_cb = &BufAllocSpy::malloc_cb;
        handlers_->buf_free_cb = &BufAllocSpy::free_cb;
        self_ = this;
    }
    ~BufAllocSpy() {
        handlers_->buf_malloc_cb = orig_malloc_;
        handlers_->buf_free_cb = orig_free_;
        self_ = nullptr;
    }

    int allocations = 0;   ///< big allocations that succeeded
    int attempts = 0;      ///< big allocations requested, refused or not
    size_t live_bytes = 0; ///< big buffers currently allocated
    size_t peak_bytes = 0; ///< most big-buffer bytes ever live at once
    bool refuse_big = false;
    std::vector<size_t> sizes; ///< every big allocation that succeeded, in order

  private:
    static void* malloc_cb(size_t size, lv_color_format_t cf) {
        BufAllocSpy& s = *self_;
        if (size < BIG)
            return s.orig_malloc_(size, cf);
        ++s.attempts;
        if (s.refuse_big)
            return nullptr;
        void* p = s.orig_malloc_(size, cf);
        if (p) {
            ++s.allocations;
            s.sizes.push_back(size);
            s.live_[p] = size;
            s.live_bytes += size;
            s.peak_bytes = std::max(s.peak_bytes, s.live_bytes);
        }
        return p;
    }
    static void free_cb(void* p) {
        BufAllocSpy& s = *self_;
        auto it = s.live_.find(p);
        if (it != s.live_.end()) {
            s.live_bytes -= it->second;
            s.live_.erase(it);
        }
        s.orig_free_(p);
    }

    static inline BufAllocSpy* self_ = nullptr;
    lv_draw_buf_handlers_t* handlers_;
    lv_draw_buf_malloc_cb_t orig_malloc_;
    lv_draw_buf_free_cb_t orig_free_;
    std::map<void*, size_t> live_;
};
