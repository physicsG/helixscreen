// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget_registry.h"

#include <utility>

#include "../catch_amalgamated.hpp"

namespace helix_test {

/// Replaces the registry factory of widget @p id for a scope and puts the
/// original back on exit. The registry is process-wide, so a factory left
/// swapped would build the next test's widgets too.
class ScopedWidgetFactory {
  public:
    ScopedWidgetFactory(const char* id, helix::WidgetFactory factory) : id_(id) {
        const auto* def = helix::find_widget_def(id);
        REQUIRE(def != nullptr);
        original_ = def->factory;
        helix::register_widget_factory(id, std::move(factory));
    }

    ~ScopedWidgetFactory() {
        helix::register_widget_factory(id_, original_);
    }

    ScopedWidgetFactory(const ScopedWidgetFactory&) = delete;
    ScopedWidgetFactory& operator=(const ScopedWidgetFactory&) = delete;

  private:
    const char* id_;
    helix::WidgetFactory original_;
};

} // namespace helix_test
