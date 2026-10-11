// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lane_source_store.h"
#include "lane_sources.h"

#include <cstdint>
#include <optional>
#include <string>

namespace helix::ams {

/// What firmware stated about a lane's binding in the frame just parsed.
///
/// Every field is the caller's own reading; nothing here is fetched. The
/// backend holds its mutex_ while it fills this in, which is what makes the
/// own-write pair below single-shot in one place rather than raced between
/// two.
struct BindingReading {
    /// The spool id firmware itself names, or 0 for "no spool".
    ///
    /// Firmware's own word, never a SlotInfo field the override merge has
    /// rewritten: a merged struct re-supplies the stored record's id, so
    /// reading one here would compare a record against itself and no binding
    /// would ever look broken.
    int firmware_spool_id = 0;

    /// AmsBackend::printer_reports_spool_ids(). Only on a backend whose
    /// firmware names an id while a spool is loaded does 0 mean ejected;
    /// elsewhere 0 is the everyday reading and says nothing.
    bool printer_reports_spool_ids = false;

    /// SettingsManager::get_ams_keep_spool_info_on_eject(). True keeps a
    /// declared identity standing over a lane that has gone empty, which is
    /// the ghost lane the UI already renders.
    bool keep_spool_info_on_eject = true;

    /// AmsBackend::own_write_expectation()'s pair: the id firmware reported
    /// before we wrote, and the id we wrote. A frame naming either of those is
    /// our own write coming back and is not an external re-bind. Suppresses
    /// Rebound only - an eject is never an echo of a write of ours.
    int own_write_old_id = 0;
    int own_write_new_id = 0;
};

/// Whether the identity declared on a lane still describes what is in it.
enum class BindingVerdict {
    Holds,   ///< Nothing contradicts the declared binding.
    Rebound, ///< Firmware names a different spool than the declared one.
    Ejected, ///< Firmware names no spool, and the user asked lanes to start fresh.
};

/// Compare what a lane's sources declare against what firmware just stated.
///
/// The declared binding is what the sources that can DECLARE one say, which is
/// Spoolman and LocalUser ranked as resolve() ranks them. VendorCache is
/// deliberately not consulted: it is firmware's own persisted metadata, so
/// including it would compare firmware against itself, and the frame that
/// refreshes it lands on either side of this call depending on which parser
/// ran, which would make the verdict depend on parse order. A lane with no
/// declared id therefore holds, whatever firmware says, exactly as an
/// unlinked record was never re-bound or ejected.
///
/// Pure: same inputs, same answer, no clock, no globals, no I/O, no logging.
[[nodiscard]] BindingVerdict classify_binding(const LaneSources& sources,
                                              const BindingReading& reading);

/// Drop every record that describes the spool a lane was bound to before its
/// binding changed: Spoolman, LocalUser and Remembered, whole and together.
///
/// A lane's declared identity is one statement about one spool however many
/// sources carry it, so a colour a person typed onto the lane goes with the id:
/// it said what was loaded then, and a changed binding says that is no longer
/// the spool on the lane. Remembered goes for the same reason, being our own
/// copy of what described the spool that was here before. VendorCache, Sensed
/// and Metered are left alone - the first is the reading that should paint once
/// the stale ones are gone, and the other two are presence and consumption,
/// which are not identity.
///
/// Firmware breaking a binding and a person changing one are one event with two
/// triggers, so this is the one definition of the drop: reconcile_binding()
/// calls it for the first and AmsBackend::commit_user_edit() (ams_backend.h)
/// for the second.
///
/// A lane that is not a lane is dropped, silently, like drop_lane_source().
void drop_previous_spool_declarations(LaneId lane);

/// Apply classify_binding() to a lane, and on a verdict other than Holds drop
/// the records that declared the binding it just broke, through
/// drop_previous_spool_declarations().
///
/// Returns the verdict so the caller can clear its persisted copy of the same
/// record. Dropping the in-memory sources alone does not outlive a reboot:
/// ingest_legacy_records() files the stored record back onto the lane at the
/// next backend start.
BindingVerdict reconcile_binding(LaneId lane, const BindingReading& reading);

/// What the hardware physically read off the spool in a lane, and nothing
/// else (prestonbrown/helixscreen#1710).
///
/// Every field is evidence about THIS spool only when the reader in the slot
/// produced it: a tag UID, or material and colour decoded from a tag. A value
/// the firmware remembers across inserts (a colour the user set on the
/// printer's own menu, a saved slot table) is not a read of the spool now in
/// the slot, and leaving it out is what makes an untagged insert classify as
/// NoEvidence instead of as the spool that was there before. A firmware-named
/// spool id is binding evidence and belongs to classify_binding(), not here.
struct SpoolEvidence {
    /// A per-spool tag identifier nobody can set through the UI. Empty when
    /// the spool has no tag or the reader has not read it yet.
    std::string tag_uid;
    /// Material decoded from the spool. Empty when none was read.
    std::string material;
    /// Colour decoded from the spool, 0xRRGGBB, the primary colour of a
    /// multi-colour spool. nullopt when none was read, never the no-colour
    /// sentinel.
    std::optional<uint32_t> color_rgb;
    /// The reader finished with this spool, so an empty tag_uid means the
    /// spool carries no tag rather than "not read yet". A non-empty tag_uid
    /// implies it.
    bool tag_read_complete = false;
};

/// Whether the spool just inserted into a lane is the one that was there.
enum class InsertVerdict {
    SameSpool, ///< Keep everything, silently.
    /// Drop what described the previous spool, as a re-bind does
    /// (drop_previous_spool_declarations()). Firmware is left alone: it
    /// already holds the new spool's reading.
    DifferentSpool,
    NoEvidence, ///< Keep everything, and ask the user whether it is the same spool.
};

/// Compare what the hardware read off the inserted spool against what it read
/// off the one before. `before` is nullopt when no reading of the previous
/// spool exists.
///
/// A finished tag read on both sides decides alone when either found a tag:
/// a new tag is a new spool whatever its contents say, the same tag is the
/// same spool even when its contents were rewritten, and a tag appearing or
/// disappearing is a different spool. Otherwise material and colour decide, and they
/// must BOTH match to call it the same spool: two spools of one material and
/// colour are interchangeable, and a manufacturer change between them is the
/// user's to correct. Any field read on both sides that differs is a
/// different spool. Anything less is no evidence.
///
/// Pure: same inputs, same answer, no clock, no globals, no I/O, no logging.
[[nodiscard]] InsertVerdict classify_insert(const std::optional<SpoolEvidence>& before,
                                            const SpoolEvidence& after);

} // namespace helix::ams
