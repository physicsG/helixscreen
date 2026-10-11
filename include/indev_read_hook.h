// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <lvgl.h>
#include <vector>

namespace helix {

/**
 * @brief One read callback chained in front of several input devices
 *
 * A hook that transforms samples runs in place of each device's own read
 * callback and has to call that device's driver first. Every device keeps the
 * callback the hook replaced, so a touch panel and a mouse behind the same hook
 * never read through each other's driver.
 *
 * The devices must outlive the hook's use of them: restore_all() reads each
 * device's current callback. Main thread only, like every indev.
 */
class IndevReadHook {
  public:
    explicit IndevReadHook(lv_indev_read_cb_t hook) : hook_(hook) {}

    IndevReadHook(const IndevReadHook&) = delete;
    IndevReadHook& operator=(const IndevReadHook&) = delete;

    /**
     * @brief Put the hook in front of @p indev's read callback
     *
     * A device already running the hook function is left as it is, so the hook
     * never records itself as a device's original and recurses on the next read.
     *
     * @param indev a live device, never nullptr
     * @return true when this call put the hook in front of @p indev
     */
    bool install(lv_indev_t* indev) {
        const lv_indev_read_cb_t current = lv_indev_get_read_cb(indev);
        if (current == hook_) {
            return false;
        }
        links_.push_back({indev, current});
        lv_indev_set_read_cb(indev, hook_);
        return true;
    }

    /**
     * @brief Run the read callback the hook replaced on @p indev
     *
     * @return false, with @p data untouched, when the hook does not front
     *         @p indev or there was no callback to replace
     */
    bool read_original(lv_indev_t* indev, lv_indev_data_t* data) const {
        const Link* link = find(indev);
        if (link == nullptr || link->original == nullptr) {
            return false;
        }
        link->original(indev, data);
        return true;
    }

    /// Whether the hook fronts @p indev. Compares the pointer only, so a deleted
    /// device can be asked about.
    bool fronts(const lv_indev_t* indev) const {
        return find(indev) != nullptr;
    }

    /**
     * @brief Drop @p indev without reading or writing it
     *
     * For a device being deleted, whose read callback is irrelevant and which
     * cannot be read once freed.
     */
    void forget(const lv_indev_t* indev) {
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                    [indev](const Link& link) { return link.indev == indev; }),
                     links_.end());
    }

    /**
     * @brief Hand every device back the callback the hook replaced, then forget them
     *
     * A device whose read callback is no longer the hook is left as it is: a
     * wrapper installed on top still calls the hook, and only that wrapper's
     * owner can take it off.
     */
    void restore_all() {
        for (const Link& link : links_) {
            if (lv_indev_get_read_cb(link.indev) == hook_) {
                lv_indev_set_read_cb(link.indev, link.original);
            }
        }
        links_.clear();
    }

  private:
    struct Link {
        lv_indev_t* indev;
        lv_indev_read_cb_t original;
    };

    const Link* find(const lv_indev_t* indev) const {
        for (const Link& link : links_) {
            if (link.indev == indev) {
                return &link;
            }
        }
        return nullptr;
    }

    lv_indev_read_cb_t hook_;
    std::vector<Link> links_;
};

} // namespace helix
