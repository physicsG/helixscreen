// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace helix::non_filament_pause {

/// Answers "is the current pause explained by something other than filament?"
/// A subsystem that recognises its own pauses (print-defect detection)
/// installs one; the runout surfaces ask before they read the sensors, so one
/// pause gets one explanation. Header-only so every build that carries a
/// runout surface links, whether or not it carries a subsystem that installs
/// an explainer. Main thread only.
using Explainer = std::function<bool()>;

// The slots are never destroyed: an owner that dies during static destruction
// (a process-lifetime panel) still removes itself here at exit.
inline Explainer& explainer_slot() {
    static auto* explainer = new Explainer();
    return *explainer;
}

/// Install (or, with an empty function, remove) the explainer.
inline void set_explainer(Explainer explainer) {
    explainer_slot() = std::move(explainer);
}

/// True when the installed explainer claims the current pause. With none
/// installed every pause is open to the runout surfaces.
inline bool explained() {
    const Explainer& explainer = explainer_slot();
    return explainer && explainer();
}

/// Told when a pause the runout surfaces may already have answered turns out
/// to have another cause (its explanation can land a frame after the pause),
/// so a runout dialog raised for it can close. Keyed by owner for removal.
using Listener = std::function<void()>;

inline std::vector<std::pair<const void*, Listener>>& listener_slots() {
    static auto* listeners = new std::vector<std::pair<const void*, Listener>>();
    return *listeners;
}

inline void add_listener(const void* owner, Listener listener) {
    listener_slots().emplace_back(owner, std::move(listener));
}

inline void remove_listener(const void* owner) {
    auto& slots = listener_slots();
    for (auto it = slots.begin(); it != slots.end();) {
        it = it->first == owner ? slots.erase(it) : it + 1;
    }
}

/// Tell every listener the current pause has a non-filament cause.
inline void announce() {
    // A copy: a listener may remove itself (or another) while it runs.
    const auto listeners = listener_slots();
    for (const auto& [owner, listener] : listeners) {
        (void)owner;
        if (listener) {
            listener();
        }
    }
}

} // namespace helix::non_filament_pause
