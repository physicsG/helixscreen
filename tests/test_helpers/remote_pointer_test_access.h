// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "remote_pointer.h"

namespace helix::remote {

/// The process-wide RemotePointer creates its device once; a test that checks how
/// it is created deletes the device first.
struct RemotePointerTestAccess {
    static void destroy_device(RemotePointer& p) {
        if (p.indev_) {
            lv_indev_delete(p.indev_);
            p.indev_ = nullptr;
        }
    }
    static lv_indev_t* device(const RemotePointer& p) {
        return p.indev_;
    }
};

} // namespace helix::remote
