// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"
#include "ui_multiselect.h"

#include "ams_backend.h"
#include "ams_types.h"
#include "display_numbering.h"

#include <optional>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief Picker modal for batch filament load/unload
 *
 * Shown on backends whose toolheads feed independently
 * (AmsBackend::supports_batch_filament_ops()): one row per slot, and the
 * primary button dispatches the whole ticked set as one firmware script in the
 * direction the picker was opened in.
 */
class BatchFilamentModal : public Modal {
  public:
    /// One-shot owned show in ONE direction: create, name the title and the
    /// primary button after the direction, prefill from the active backend,
    /// and hand the instance to ModalStack, which frees it when its entry
    /// goes. The sidebar's Load and Unload buttons each open this.
    static bool show_owned(bool for_load);

    const char* get_name() const override {
        return "Batch Filament";
    }
    const char* component_name() const override {
        return "batch_filament_modal";
    }

    // Pure: can this head act in this direction? Unload needs its filament at
    // the toolhead; load needs filament in the lane AND not at the toolhead,
    // because feeding an empty lane is the no-op the firmware refuses. nullopt
    // lane presence (backend publishes none) reads as loadable: the backend's
    // eligibility check refuses an empty lane at dispatch, so the picker does
    // not guess. nullopt at_toolhead reads as not loaded (never unloadable).
    static bool head_can_act(std::optional<bool> at_toolhead, std::optional<bool> lane_presence,
                             bool for_load);

    // Pure: which rows start ticked, exactly the heads that can act in this
    // direction (head_can_act per row).
    static std::vector<bool>
    prefill_selection(const std::vector<std::optional<bool>>& at_toolhead,
                      const std::vector<std::optional<bool>>& lane_presence, bool for_load);

    // Pure: does any head offer this direction? prefill_selection folded to a
    // single bool, so the sidebar's Load/Unload gating and the picker's
    // prefills cannot disagree about which direction a head serves.
    static bool any_head_for_direction(const std::vector<std::optional<bool>>& at_toolhead,
                                       const std::vector<std::optional<bool>>& lane_presence,
                                       bool for_load);

    // Pure: multiselect keys are slot indices as decimal strings.
    static std::vector<int> selected_slots(const std::vector<std::string>& keys);

    // Pure: row text. The lane name, plus what is in the lane and where it
    // stands: a lane carrying material says loaded (at the toolhead) or ready
    // to load, a lane known empty says Empty with no loaded/ready suffix, and
    // an unanswerable presence leaves the lane name bare. The tick state stops
    // describing the machine the moment the user changes it, so the contents
    // are named in the row.
    static std::string row_label(LaneNoun noun, int slot, const SlotInfo& info,
                                 std::optional<bool> present, bool at_toolhead);

    // Pure: the selected slots the backend says can take this operation,
    // plus the first one it refused and why. Eligibility is
    // direction-dependent, so this runs on button press, not when the rows
    // are built. Out-of-range slots are dropped, not trusted — and there is
    // no eligibility value to read past the table's end, so no refusal to
    // name either.
    struct EligibilitySift {
        std::vector<int> eligible; ///< selected slots that can run
        int dropped = -1;          ///< first selected slot refused, -1 when none
        AmsBackend::FilamentOpEligibility drop_reason = AmsBackend::FilamentOpEligibility::Busy;
    };
    static EligibilitySift
    sift_eligible(const std::vector<int>& selected,
                  const std::vector<AmsBackend::FilamentOpEligibility>& per_slot);

    /// What each picker row needs from the backend. Lane presence answers the
    /// label ("what is in this lane"); toolhead state answers the ticks. They
    /// disagree on a lane holding filament that has not been fed to the nozzle.
    /// at_toolhead is "loaded to the nozzle" (the Load direction's question);
    /// unloadable adds filament parked in the toolhead short of it, which a
    /// heated unload still acts on.
    struct BatchRowSource {
        std::vector<SlotInfo> slots;
        std::vector<std::optional<bool>> lane_presence;
        std::vector<std::optional<bool>> at_toolhead;
        std::vector<std::optional<bool>> unloadable;

        /// The toolhead answer head_can_act() reads for this direction.
        [[nodiscard]] const std::vector<std::optional<bool>>& heads_for(bool for_load) const {
            return for_load ? at_toolhead : unloadable;
        }
    };

    // Pure: gather every row's inputs. Extracted so the gathering itself is
    // testable against a backend whose lane and toolhead answers disagree.
    static BatchRowSource collect_rows(const AmsBackend& backend);

  protected:
    void on_show() override;

    /// btn_primary: dispatches the direction the picker was opened in.
    void on_ok() override;

  private:
    void dispatch(bool load);

    /// The direction the sidebar opened this picker in: names the title, the
    /// primary button, the prefill and the dispatch verb.
    bool for_load_ = false;

    UiMultiselect multiselect_;
};

} // namespace helix::ui
