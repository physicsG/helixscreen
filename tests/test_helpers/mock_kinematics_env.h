// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>
#include <string>

namespace helix_test {

/// Points HELIX_MOCK_KINEMATICS at `value` for the scope and restores whatever
/// was there before, so one failed assertion cannot leak a kinematics persona
/// into the other mock-driven tests in this process.
class MockKinematicsEnv {
  public:
    explicit MockKinematicsEnv(const char* value) {
        const char* prev = std::getenv("HELIX_MOCK_KINEMATICS");
        had_previous_ = prev != nullptr;
        if (had_previous_) {
            previous_ = prev;
        }
        setenv("HELIX_MOCK_KINEMATICS", value, 1);
    }
    ~MockKinematicsEnv() {
        if (had_previous_) {
            setenv("HELIX_MOCK_KINEMATICS", previous_.c_str(), 1);
        } else {
            unsetenv("HELIX_MOCK_KINEMATICS");
        }
    }

  private:
    bool had_previous_ = false;
    std::string previous_;
};

} // namespace helix_test
