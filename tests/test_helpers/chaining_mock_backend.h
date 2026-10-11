// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"

namespace helix::test {

/// An AmsBackendMock that chains a prerequisite operation implicitly, as AD5X
/// IFS does: tapping bypass with a lane loaded unloads it first. The mock's
/// default Happy Hare persona refuses that chain, like the hardware.
class ChainingMockBackend : public AmsBackendMock {
  public:
    using AmsBackendMock::AmsBackendMock;

    [[nodiscard]] BackendTraits traits() const override {
        BackendTraits t = AmsBackendMock::traits();
        t.allows_implicit_chaining = true;
        return t;
    }
};

} // namespace helix::test
