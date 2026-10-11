// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config.h"
#include "config_test_access.h"
#include "mock_config_storage.h"

#include <memory>

#include "../catch_amalgamated.hpp"

/// Counts the config writes made while it lives: the Config singleton writes to
/// an in-memory store in place of its file, and gets its own store back on exit.
class ScopedConfigWriteCounter {
  public:
    ScopedConfigWriteCounter() {
        helix::Config& cfg = *helix::Config::get_instance();
        // save() writes nothing without a path, or on a read-only filesystem.
        REQUIRE_FALSE(helix::ConfigTestAccess::path(cfg).empty());
        REQUIRE_FALSE(helix::ConfigTestAccess::read_only_mode(cfg));
        original_ = std::move(helix::ConfigTestAccess::storage(cfg));
        original_is_default_ = helix::ConfigTestAccess::storage_is_default(cfg);
        auto store = std::make_unique<helix::test::MockConfigStorage>();
        store_ = store.get();
        helix::ConfigTestAccess::storage(cfg) = std::move(store);
        helix::ConfigTestAccess::storage_is_default(cfg) = false;
    }

    ~ScopedConfigWriteCounter() {
        helix::Config& cfg = *helix::Config::get_instance();
        helix::ConfigTestAccess::storage(cfg) = std::move(original_);
        helix::ConfigTestAccess::storage_is_default(cfg) = original_is_default_;
    }

    ScopedConfigWriteCounter(const ScopedConfigWriteCounter&) = delete;
    ScopedConfigWriteCounter& operator=(const ScopedConfigWriteCounter&) = delete;

    int writes() const {
        return store_->store_calls;
    }

  private:
    std::unique_ptr<helix::ConfigStorage> original_;
    bool original_is_default_ = false;
    helix::test::MockConfigStorage* store_ = nullptr;
};
