// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"

namespace helix::test {

/// An AmsBackendMock whose BackendTraits are exactly what the test sets, whatever
/// persona the mock stands in for. Lets a case prove a caller reads the trait and not
/// the backend's AmsType.
class TraitProbeBackend : public AmsBackendMock {
  public:
    explicit TraitProbeBackend(const BackendTraits& traits) : AmsBackendMock(4), traits_(traits) {}

    [[nodiscard]] BackendTraits traits() const override {
        return traits_;
    }

  private:
    BackendTraits traits_;
};

} // namespace helix::test
