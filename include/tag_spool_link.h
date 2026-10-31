// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <unordered_map>

namespace helix::ams {

/// One frame of one lane, as the tag-link policy reads it.
struct TagSpoolSighting {
    /// The lane holds a spool (slot_status_reports_filament()).
    bool occupied = false;
    /// The Spoolman spool the seated spool's tag names
    /// (TagReading::spoolman_id), nullopt when it names none.
    std::optional<int> tag_spool_id;
    /// The spool the lane is linked to now, 0 for none.
    int lane_spoolman_id = 0;
};

/// Which Spoolman spool each lane should be linked to because its tag says so.
///
/// A tag's spool id is acted on when it appears on a lane, never on every frame
/// that repeats it: a new insert links the lane to the spool its tag names, and
/// a spool picked by hand while that same tag stays seated is kept until the
/// tag changes. The first id a lane shows this session finds no insert to act
/// on, so it links only a lane that is linked to nothing; a link made before a
/// restart stands.
///
/// A wanted link stands across frames until settle() ends it or the lane's tag
/// changes, so a lane seated while Spoolman is unreachable still links when it
/// comes back. Pure bookkeeping, no I/O; main thread only.
class TagSpoolLinkTracker {
  public:
    /// Feed one frame. Returns the spool id this lane still wants linked and
    /// no lookup is running for, else nullopt.
    [[nodiscard]] std::optional<int> observe(int slot_index, const TagSpoolSighting& sighting);

    /// A lookup for @p spool_id started while the lane was linked to
    /// @p lane_spoolman_id: observe() stops offering it, and a frame showing the
    /// lane linked to anything else (a pick made meanwhile) drops the want.
    void begin_lookup(int slot_index, int spool_id, int lane_spoolman_id);

    /// Whether @p spool_id is still what the lane wants. A lookup that answers
    /// after the tag changed is answering for a spool no longer seated.
    [[nodiscard]] bool wants(int slot_index, int spool_id) const;

    /// Whether the lane's want comes from a spool put in this session, rather
    /// than from the first id the lane showed, which may be a spool that sat
    /// there across a restart.
    [[nodiscard]] bool want_is_insert(int slot_index) const;

    /// The want for @p spool_id is done with (linked, or found missing):
    /// nothing more is tried for this insert. A no-op once the lane wants
    /// something else.
    void settle(int slot_index, int spool_id);

    /// The lookup for @p spool_id got no answer: observe() offers it again
    /// after kLookupRetryFrames frames, so a Spoolman that is slow or briefly
    /// down is retried without one lookup per status frame.
    void lookup_failed(int slot_index, int spool_id);

    /// Frames a failed lookup waits before observe() offers it again.
    static constexpr int kLookupRetryFrames = 30;

    void clear() {
        lanes_.clear();
    }

  private:
    struct Lane {
        /// The tag id last seen on the lane; nullopt once it read empty.
        std::optional<int> seen;
        std::optional<int> want;
        bool in_flight = false;
        /// The lane's link when the running lookup began.
        int lookup_base = 0;
        /// The want came from a spool put in this session.
        bool insert = false;
        /// Frames left before a failed lookup is offered again.
        int retry_wait = 0;
    };
    /// A lane with no entry has not been seen this session.
    std::unordered_map<int, Lane> lanes_;
};

} // namespace helix::ams
