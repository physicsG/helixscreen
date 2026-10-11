// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "usb_backend_linux.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <memory>
#include <utility>

namespace helix::test {

// Test-only seam. The fallback mounter is a private member so its whole
// lifecycle - construction, poll() passes, the shutdown unmount - stays on the
// monitor thread. Tests inject an automount built on a fake ops table to
// observe that lifecycle through the real start()/stop() wiring.
class UsbBackendLinuxTestAccess {
  public:
    /// Installs the automount start() will run. Must be called before start():
    /// start() only constructs the production automounter when none is present.
    static void set_automount(UsbBackendLinux& backend,
                              std::unique_ptr<helix::usb::UsbAutomount> automount) {
        backend.automount_ = std::move(automount);
    }
};

} // namespace helix::test

#endif // __linux__ && !__ANDROID__
