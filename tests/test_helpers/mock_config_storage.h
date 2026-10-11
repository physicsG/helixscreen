// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config_storage.h"

#include <optional>
#include <string>

namespace helix::test {

class MockConfigStorage : public helix::ConfigStorage {
  public:
    std::optional<std::string> doc;
    std::string corrupt_stash;
    bool ro = false;
    int store_calls = 0;
    // Simulates "present but unreadable" (e.g. permission denied): load()
    // reports a read error, matching the real ConfigStorage::load() contract
    // (config_storage.h).
    bool unreadable = false;
    bool small = false;

    explicit MockConfigStorage(std::optional<std::string> initial = std::nullopt)
        : doc(std::move(initial)) {}

    std::optional<std::string> load(std::string& read_error) override {
        if (unreadable && doc) {
            read_error = "mock: document present but unreadable";
            return std::nullopt;
        }
        return doc;
    }
    bool store(const std::string& bytes) override {
        if (ro)
            return false;
        doc = bytes;
        store_calls++;
        return true;
    }
    void preserve_corrupt() override {
        if (doc)
            corrupt_stash = *doc;
        doc.reset();
    }
    bool read_only() override {
        return ro;
    }
    std::string describe() const override {
        return "mock://config";
    }
    bool small_footprint() const override {
        return small;
    }
};

} // namespace helix::test
