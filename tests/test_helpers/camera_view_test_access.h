// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl.h"

#if HELIX_HAS_CAMERA

#include "camera_view.h"

#include <memory>
#include <utility>

namespace helix {

/// Test-only access to CameraView's stream, so a case can hand it a stream in
/// a state the public API reaches only over the network (a worker that gave
/// up, a worker wedged in a request). Requires `friend class
/// CameraViewTestAccess;` on CameraView.
class CameraViewTestAccess {
  public:
    /// Make @p stream the view's stream, as if start() had started it for @p feed.
    /// The owner CameraView hands its reap job: a job dropped unrun releases it.
    static std::shared_ptr<CameraStream> reaping_owner(std::unique_ptr<CameraStream> stream) {
        return CameraView::reaping_owner(std::move(stream));
    }
    static int leaked_streams() {
        return CameraView::leaked_streams_.load();
    }

    static void adopt(CameraView& v, std::unique_ptr<CameraStream> stream, WebcamInfo feed) {
        v.stream_ = std::move(stream);
        v.feed_ = std::move(feed);
    }
};

} // namespace helix

#endif // HELIX_HAS_CAMERA
