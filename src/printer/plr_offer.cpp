// SPDX-License-Identifier: GPL-3.0-or-later
#include "plr_offer.h"

#include "i_moonraker_client.h" // ConnectionState

namespace helix {

bool plr_should_offer(const PlrOfferSignals& s) {
    // recovery_available is the normalized, self-gating capability+availability
    // signal for whichever backend is in play (see PlrOfferSignals docs). No
    // separate printer-type/AMS-backend gate is required; that gate was
    // redundant on stock U1s and wrong for an AFC-modded U1 (bundle UDZJQVQZ)
    // whose AMS backend is not the Snapmaker one.
    return s.recovery_available && s.printer_idle && !s.already_prompted && !s.wizard_active;
}

bool plr_should_rearm(int prev_conn_state, int new_conn_state) {
    auto prev = static_cast<ConnectionState>(prev_conn_state);
    auto next = static_cast<ConnectionState>(new_conn_state);
    return prev == ConnectionState::CONNECTED && next != ConnectionState::CONNECTED;
}

} // namespace helix
