// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The built-in print-failure detection sources.
//
// This module is the ONLY place that names a concrete DetectionSource.
// Startup asks it to register them all; each source then decides for itself
// whether it is capable on the connected printer (a local probe, or
// DetectionSource::on_objects_listed) and stays inert where it is not.
//
// Adding a source means adding one Entry to the table in
// detection_source_table.cpp - no call site changes.

#include "detection_manager.h"

#include <memory>
#include <optional>
#include <vector>

namespace helix {
class PrinterState;
} // namespace helix

namespace helix::detection {

struct SourceTableEntry {
    /// Construct the source and install its observers.
    std::unique_ptr<DetectionSource> (*build)(helix::PrinterState* state);
    /// The user's stored policy for this source, or nullopt when the source
    /// has no policy setting (it then keeps DeferToSource).
    std::optional<DetectionPolicy> (*stored_policy)();
};

/// Every built-in source, in registration order.
const std::vector<SourceTableEntry>& builtin_sources();

/// Build every built-in source, register it with @p dm and apply its stored
/// policy. Main thread, once per session.
void register_builtin_sources(DetectionManager& dm, helix::PrinterState* state);

} // namespace helix::detection
