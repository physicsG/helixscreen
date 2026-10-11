// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "panel_widget_manager.h"

#include <memory>
#include <utility>

namespace helix_test {

/// RAII registration of a shared_ptr<T> as a PanelWidgetManager shared
/// resource, unregistered on scope exit.
///
/// PanelWidgetManager is a process-wide singleton, so a resource registered
/// into it and never removed dangles for every test that runs afterward in
/// the same binary once its owner is destroyed. Being a destructor rather
/// than a trailing statement in the test body, the removal also runs when a
/// REQUIRE fails partway through: Catch2 unwinds the stack on assertion
/// failure, and a plain `mgr.clear_shared_resources()` written after the
/// point of failure never executes.
///
/// The destructor removes only T's slot (unregister_shared_resource<T>()),
/// not the whole map, so two of these for different T can be nested safely -
/// unlike a bare `clear_shared_resources()`, which would wipe both.
template <typename T> class ScopedSharedResource {
  public:
    explicit ScopedSharedResource(std::shared_ptr<T> resource) : resource_(std::move(resource)) {
        helix::PanelWidgetManager::instance().register_shared_resource<T>(resource_);
    }

    ~ScopedSharedResource() {
        helix::PanelWidgetManager::instance().unregister_shared_resource<T>();
    }

    ScopedSharedResource(const ScopedSharedResource&) = delete;
    ScopedSharedResource& operator=(const ScopedSharedResource&) = delete;

    T& get() {
        return *resource_;
    }
    T* ptr() {
        return resource_.get();
    }

  private:
    std::shared_ptr<T> resource_;
};

} // namespace helix_test
