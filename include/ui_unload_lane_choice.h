// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "async_lifetime_guard.h"
#include "filament_mapper.h"
#include "filament_op_execute.h"

#include <functional>
#include <lvgl.h>
#include <optional>
#include <vector>

namespace helix {
class AmsBackend;
} // namespace helix

namespace helix::ui {

/**
 * @brief The slots to offer when the user must say which lane is in the toolhead.
 *
 * @param candidates 0-based slot indices from AmsBackend::unload_lane_question()
 * @param slots      every slot the primary backend publishes
 * @return the entries of @p slots on the primary backend whose index is a
 *         candidate, in candidate order
 */
std::vector<helix::AvailableSlot>
unload_lane_choices(const std::vector<int>& candidates,
                    const std::vector<helix::AvailableSlot>& slots);

/**
 * @brief Ask which lane is in the toolhead before a slotless unload, when the
 *        backend cannot name it.
 *
 * Candidate lanes open the lane picker anchored to @p anchor, with an
 * extruder-only clear and a Cancel row; no lane at all asks to confirm the
 * clear in a dialog. @p on_chosen receives the chosen 0-based slot, or
 * kClearToolheadSlot for the clear. Cancel, a backdrop tap or ESC sends nothing.
 *
 * @return true when the picker took over the unload, false (showing nothing)
 *         when the backend resolves the lane itself
 */
bool choose_unload_lane(helix::AmsBackend* backend, lv_obj_t* anchor,
                        std::function<void(int)> on_chosen);

/**
 * @brief choose_unload_lane() for a slotless unload surface whose answer goes
 *        straight to execute_filament_unload().
 *
 * The answer is the lane (or kClearToolheadSlot) the user named as loaded, so
 * it dispatches with target_is_loaded on the backend live at answer time.
 *
 * @param token when set, an expired token drops the answer
 * @return true when the question took over the unload, false when the caller
 *         unloads as usual
 */
bool ask_lane_then_unload(helix::AmsBackend* backend, lv_obj_t* anchor, FilamentOpSurface surface,
                          std::optional<helix::LifetimeToken> token = std::nullopt);

} // namespace helix::ui
