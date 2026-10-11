// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace helix::detection {

enum class DetectionKind { Spaghetti, DirtyBed, Residue, DirtyNozzle, Unknown };

/// Map a Snapmaker U1 `print_stats.exception.code` to a kind.
/// Stock defect_detection.py: 1=dirty-bed, 2=noodle/spaghetti, 3=residue,
/// 4=dirty-nozzle. 0 / -1 / anything else = not a visual defect we surface.
inline DetectionKind kind_from_u1_code(int code) {
    switch (code) {
    case 1:
        return DetectionKind::DirtyBed;
    case 2:
        return DetectionKind::Spaghetti;
    case 3:
        return DetectionKind::Residue;
    case 4:
        return DetectionKind::DirtyNozzle;
    default:
        return DetectionKind::Unknown;
    }
}

/// Normalized "a detector reported a defect" event.
struct DetectionEvent {
    std::string source_id;
    DetectionKind kind = DetectionKind::Unknown;
    bool attributable = false;
    std::optional<float> confidence;
    bool already_paused = false;
    std::string message;
};

/// The printer's own stored on/off + pause choice, as the vendor stack keeps
/// it. Seeded into HelixScreen settings once, on the first start where a
/// capable source exists; after that the settings own both values.
struct DetectionPreference {
    bool enabled = true;
    bool pause = true;
};

/// A backend that can report print-failure detections.
class DetectionSource {
  public:
    using Callback = std::function<void(const DetectionEvent&)>;

    virtual ~DetectionSource() = default;
    virtual std::string id() const = 0;
    virtual bool available() const = 0;
    virtual bool can_tune() const {
        return false;
    }
    /// Whether the source's own firmware pauses the print on a detection.
    /// The default (false) is the common shape: HelixScreen sends the pause,
    /// so the pause-on-detect setting governs the source and its settings
    /// row shows. Sources paused in firmware override to true; the setting
    /// is inert for them, their row hides, and a warn-only response still
    /// escalates to the modal.
    virtual bool self_pauses() const {
        return false;
    }
    /// Lower the source's sensitivity (the response modal's Tune button).
    /// Called only while can_tune() is true, on the main thread.
    virtual void tune() {}
    /// Re-run the local capability probe. Called on every connect: answers
    /// that depend on state saved after boot (e.g. the wizard's printer
    /// type) can change without a restart. Sources whose capability arrives
    /// over the wire keep the base no-op.
    virtual void refresh_capability() {}
    /// The printer's stored preference, or nullopt when this source has none
    /// (settings then keep their defaults: on, pause on detect).
    virtual std::optional<DetectionPreference> printer_preference() const {
        return std::nullopt;
    }
    /// Callback fires on the MAIN thread (already marshaled).
    virtual void set_callback(Callback cb) = 0;
};

} // namespace helix::detection
