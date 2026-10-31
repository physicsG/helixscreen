// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_pointer.h"

#include "display_manager.h"
#include "input_settings_manager.h"

#include <spdlog/spdlog.h>

#include <thread>

namespace helix::remote {

lv_point_t remote_image_to_indev_point(lv_display_rotation_t rot, int32_t orig_w, int32_t orig_h,
                                       int32_t x, int32_t y) {
    switch (rot) {
    case LV_DISPLAY_ROTATION_90:
        return {y, orig_h - x - 1};
    case LV_DISPLAY_ROTATION_180:
        return {orig_w - x - 1, orig_h - y - 1};
    case LV_DISPLAY_ROTATION_270:
        return {orig_w - y - 1, x};
    default:
        return {x, y};
    }
}

RemotePointer& RemotePointer::instance() {
    static RemotePointer inst;
    return inst;
}

void RemotePointer::read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    auto& self = instance();
    data->point.x = self.x_.load(std::memory_order_acquire);
    data->point.y = self.y_.load(std::memory_order_acquire);
    data->state = self.pressed_.load(std::memory_order_acquire) ? LV_INDEV_STATE_PRESSED
                                                                : LV_INDEV_STATE_RELEASED;
    // This is a separate indev from the physical touchscreen, so its sleep-aware
    // wrapper never sees these samples. Keep the wake gesture out of LVGL here too.
    const auto out = self.wake_latch_.filter(
        {data->state == LV_INDEV_STATE_PRESSED, data->point.x, data->point.y}, lv_tick_get(),
        [](int32_t x, int32_t y) {
            auto* dm = DisplayManager::instance();
            return dm && dm->note_wake_press(x, y);
        });
    data->state = out.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point.x = out.x;
    data->point.y = out.y;
    // Published last, so a transport-thread waiter that sees this count has
    // necessarily seen the state it was set against.
    self.reads_.fetch_add(1, std::memory_order_release);
}

bool RemotePointer::ensure_created() {
    if (indev_ != nullptr) {
        return true;
    }

    lv_display_t* disp = lv_display_get_default();
    if (disp == nullptr) {
        spdlog::warn("[RemotePointer] No default display — cannot create synthetic pointer");
        return false;
    }

    indev_ = lv_indev_create();
    if (indev_ == nullptr) {
        spdlog::error("[RemotePointer] lv_indev_create failed");
        return false;
    }

    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    wake_latch_ = {};
    lv_indev_set_read_cb(indev_, read_cb);
    lv_indev_set_display(indev_, disp);
    InputSettingsManager::instance().apply_to_pointer(indev_);

    spdlog::info("[RemotePointer] Synthetic pointer device created");
    return true;
}

void RemotePointer::set_state(int32_t x, int32_t y, bool pressed) {
    // Position before button state: a reader that observes the press has also
    // observed the coordinates it happened at, which is what LVGL's press
    // handling assumes.
    x_.store(x, std::memory_order_release);
    y_.store(y, std::memory_order_release);
    logical_x_.store(x, std::memory_order_release);
    logical_y_.store(y, std::memory_order_release);
    pressed_.store(pressed, std::memory_order_release);
}

bool RemotePointer::set_logical_state(int32_t x, int32_t y, bool pressed) {
    if (indev_ == nullptr) {
        return false;
    }
    lv_display_t* disp = lv_indev_get_display(indev_);
    if (disp == nullptr) {
        set_state(x, y, pressed);
        return true;
    }
    const lv_point_t raw = remote_image_to_indev_point(
        lv_display_get_rotation(disp), lv_display_get_original_horizontal_resolution(disp),
        lv_display_get_original_vertical_resolution(disp), x, y);
    set_state(raw.x, raw.y, pressed);
    logical_x_.store(x, std::memory_order_release);
    logical_y_.store(y, std::memory_order_release);
    return true;
}

RemotePointer::WaitResult
RemotePointer::wait_for_samples(uint64_t baseline, std::chrono::milliseconds timeout,
                                const std::function<bool()>& keep_going) const {
    constexpr auto kPoll = std::chrono::milliseconds(5);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const uint64_t target = baseline + 2;
    while (read_count() < target) {
        if (keep_going && !keep_going()) {
            return WaitResult::Stopped;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return WaitResult::TimedOut;
        }
        std::this_thread::sleep_for(kPoll);
    }
    return WaitResult::Sampled;
}

uint64_t RemotePointer::read_count() const {
    return reads_.load(std::memory_order_acquire);
}

bool RemotePointer::is_created() const {
    return indev_ != nullptr;
}

int32_t RemotePointer::x() const {
    return logical_x_.load(std::memory_order_acquire);
}

int32_t RemotePointer::y() const {
    return logical_y_.load(std::memory_order_acquire);
}

bool RemotePointer::pressed() const {
    return pressed_.load(std::memory_order_acquire);
}

} // namespace helix::remote
