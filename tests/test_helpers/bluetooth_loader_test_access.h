// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "bluetooth_loader.h"

namespace helix::bluetooth {

// Friend of BluetoothLoader: presents a loaded plugin with no shared context yet, using
// the caller's init(), and puts the singleton back as it found it.
class BluetoothLoaderTestAccess {
  public:
    class FakeLoaded {
      public:
        FakeLoaded(BluetoothLoader& loader, helix_bt_init_fn init)
            : loader_(loader), available_(loader.available_), init_(loader.init),
              deinit_(loader.deinit), shared_ctx_(loader.shared_ctx_) {
            loader.available_ = true;
            loader.init = init;
            loader.deinit = nullptr; // the fake context is never deinited
            loader.shared_ctx_ = nullptr;
        }
        ~FakeLoaded() {
            loader_.available_ = available_;
            loader_.init = init_;
            loader_.deinit = deinit_;
            loader_.shared_ctx_ = shared_ctx_;
        }
        FakeLoaded(const FakeLoaded&) = delete;
        FakeLoaded& operator=(const FakeLoaded&) = delete;

      private:
        BluetoothLoader& loader_;
        bool available_;
        helix_bt_init_fn init_;
        helix_bt_deinit_fn deinit_;
        helix_bt_context* shared_ctx_;
    };
};

} // namespace helix::bluetooth
