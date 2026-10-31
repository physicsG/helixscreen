// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ams_backend_mock.h"

namespace helix::test {

/// An AmsBackendMock that reports the AmsType the test names, for code that keys
/// on which kind of filament system is registered rather than on its behaviour.
class TypedMockBackend : public AmsBackendMock {
  public:
    explicit TypedMockBackend(AmsType type) : AmsBackendMock(4), type_(type) {}

    /// With @p absent true the backend reads like one whose firmware object
    /// stands with no hardware behind it (AmsSystemInfo::external_spool_only).
    void set_hardware_absent(bool absent) {
        hardware_absent_ = absent;
    }

    [[nodiscard]] AmsSystemInfo get_system_info() const override {
        AmsSystemInfo info = AmsBackendMock::get_system_info();
        info.external_spool_only = hardware_absent_;
        return info;
    }

    [[nodiscard]] AmsType get_type() const override {
        return type_;
    }

  private:
    AmsType type_;
    bool hardware_absent_ = false;
};

} // namespace helix::test
