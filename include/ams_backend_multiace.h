// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ams_backend_snapmaker.h"

#include <array>
#include <optional>

/**
 * @file ams_backend_multiace.h
 * @brief decay71/multiACE — 1-4 Anycubic ACE units bolted onto a Snapmaker U1
 *
 * multiACE does not replace the U1's filament handling, it feeds it. The four
 * SnapSwap toolheads stay exactly what they were — `print_task_config`, the
 * `channel_state` load latch, the motion/port sensors, the native load model —
 * and each head is independently either on its stock feeder or fed from one of
 * the ACE units. So this derives from AmsBackendSnapmaker rather than forking
 * it: unit 0 remains the U1's four heads with all of that behaviour intact, and
 * units 1..N are the ACE hardware.
 *
 * ## What the `ace` Klipper object carries
 *
 * Everything needed arrives over the WebSocket via MultiAce.get_status(); the
 * optional FastAPI service is not required.
 *
 * - `mode` — "head" (an ACE binds to ONE head and all its slots feed that head)
 *   or "multi" (slot *s* of every ACE feeds head *s*). This changes the
 *   slot→head mapping, so per-unit topology genuinely differs by mode.
 * - `device_count`, `active_device`
 * - `head_ace` / `head_feeder` / `head_manual` — per-head source *kind*, keyed
 *   by head index as a STRING ("0".."3"), not an int.
 * - `head_source[h]` — `{ace_index, slot}` when the head is ACE-fed, else null.
 * - `aces[]` — per-unit inventory: connected, temp, humidity, `gate_status[]`,
 *   `slots[]`, dryer state.
 *
 * ## Why the dispatch override exists
 *
 * An ACE-fed head must be driven with `ACE_LOAD_HEAD` / `ACE_UNLOAD_HEAD`, not
 * the U1's native path. Sending the native unload to an ACE-fed head does move
 * the filament, but it terminates at `preload_finish` because the ACE performs
 * the retract — the UI waits for `unload_finish` and hangs on "Unloading"
 * forever. Heads on their stock feeder keep the inherited native path.
 *
 * @see docs/devel/plans/2026-08-09-snapmaker-u1-multiace-plan.md § 4 Phase 2
 * @see tests/fixtures/snapmaker_u1/u1-multiace-head-mode-idle.json
 */
class AmsBackendMultiAce : public AmsBackendSnapmaker {
    friend class MultiAceTestAccess;

  public:
    AmsBackendMultiAce(MoonrakerAPI* api, helix::MoonrakerClient* client);
    ~AmsBackendMultiAce() override = default;

    /// Upper bound on ACE units multiACE supports.
    static constexpr int MAX_ACE_UNITS = 4;
    /// Slots per ACE unit (Pro / Pro 2 are both 4-bay).
    static constexpr int ACE_SLOTS_PER_UNIT = 4;

    [[nodiscard]] AmsType get_type() const override {
        return AmsType::MULTIACE;
    }

    /**
     * @brief How a given U1 head is fed.
     *
     * Mirrors multiACE's own three-way split. UNKNOWN is the pre-first-frame
     * answer and must not be confused with FEEDER — before `ace` has been seen
     * we do not yet know, and guessing FEEDER would dispatch the native path to
     * a head the ACE actually owns.
     */
    enum class HeadSource { UNKNOWN, FEEDER, ACE, MANUAL };

    /// How head @p head is fed, per the last `ace` frame.
    [[nodiscard]] HeadSource head_source_kind(int head) const;

    /// Which (unit, slot) is seated at head @p head, or nullopt when the head is
    /// not ACE-fed. `unit_index` is the GLOBAL unit index (ace_index + 1), since
    /// unit 0 is the U1 itself.
    struct SeatedSource {
        int unit_index = -1; ///< Global AmsUnit index (ace_index + 1)
        int ace_index = -1;  ///< multiACE's own 0-based device index
        int slot = -1;       ///< Slot within that ACE
    };
    [[nodiscard]] std::optional<SeatedSource> seated_source(int head) const;

  protected:
    void handle_status_update(const nlohmann::json& notification) override;
    const char* backend_log_tag() const override {
        return "[AMS multiACE]";
    }

    // ACE-fed heads take the ACE command path; feeder heads fall through to the
    // inherited native U1 path. See the class comment.
    AmsError do_load_filament(int slot_index) override;
    AmsError do_unload_filament(int slot_index) override;

  private:
    /// Parse the `ace` object. Caller must hold mutex_.
    void parse_ace_object_locked(const nlohmann::json& ace, bool& changed);
    /// Rebuild units 1..N from the parsed ACE inventory. Caller must hold mutex_.
    void rebuild_ace_units_locked();

    /// multiACE's `mode`: true when one ACE binds to one head (all its slots
    /// feed that head) rather than slot *s* feeding head *s*.
    bool head_mode_ = true;
    int device_count_ = 0;

    std::array<HeadSource, NUM_TOOLS> head_kind_{
        {HeadSource::UNKNOWN, HeadSource::UNKNOWN, HeadSource::UNKNOWN, HeadSource::UNKNOWN}};
    std::array<std::optional<SeatedSource>, NUM_TOOLS> head_seated_{};

    /// Per-ACE inventory as last parsed, indexed by multiACE's ace_index.
    struct AceUnitState {
        bool connected = false;
        /// Wire protocol generation ("v1" / "v2") — the only field that says
        /// which ACE model this is. See ace_model_name().
        std::string protocol;
        int temp = 0;
        int humidity = 0;
        std::array<bool, ACE_SLOTS_PER_UNIT> gate_present{{false, false, false, false}};
        std::array<std::string, ACE_SLOTS_PER_UNIT> material{};
        /// nullopt = the ACE reported no colour, so keep SlotInfo's default
        /// rather than painting the slot black.
        std::array<std::optional<uint32_t>, ACE_SLOTS_PER_UNIT> color_rgb{};
    };
    std::array<AceUnitState, MAX_ACE_UNITS> ace_units_{};
};
