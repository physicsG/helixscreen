// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_hardware_setup_prompter.cpp
 * @brief choose_prompt(): which hardware prompt a discovery pass raises.
 *
 * The decision was three independent boolean conditions, each repeating the same
 * three gates. The table below spells those conditions out as the specification and
 * walks every combination of inputs, so the enum cannot disagree with them on any
 * input and no input can raise two prompts.
 */

#include "hardware_setup_prompter.h"

#include "../catch_amalgamated.hpp"

using helix::choose_prompt;
using helix::HardwarePrompt;
using helix::HardwarePromptGates;

namespace {

/// The three conditions as the discovery pass wrote them out, one per prompt.
struct Spec {
    bool reconfig;
    bool deferred;
    bool mismatch;
};

Spec spec_for(const HardwarePromptGates& g, bool deferred_steps_empty) {
    (void)deferred_steps_empty;
    return {
        g.hw_changed && g.reconfig_steps_pending && !g.print_active && !g.wizard_blocked &&
            !g.reconfig_shown,
        g.hardware_setup_deferred && g.hw_changed && !g.print_active && !g.wizard_blocked &&
            !g.reconfig_steps_pending && !g.deferred_prompt_shown,
        g.hw_changed && !g.print_active && !g.reconfig_steps_pending &&
            !g.hardware_setup_deferred && !g.wizard_blocked && !g.type_mismatch_shown,
    };
}

HardwarePromptGates gates_from_bits(unsigned bits) {
    HardwarePromptGates g;
    g.hw_changed = bits & 1u;
    g.print_active = bits & 2u;
    g.wizard_blocked = bits & 4u;
    g.reconfig_steps_pending = bits & 8u;
    g.hardware_setup_deferred = bits & 16u;
    g.reconfig_shown = bits & 32u;
    g.deferred_prompt_shown = bits & 64u;
    g.type_mismatch_shown = bits & 128u;
    return g;
}

} // namespace

TEST_CASE("choose_prompt agrees with the three gate conditions on every input",
          "[hardware_prompt][discovery]") {
    for (unsigned bits = 0; bits < 256; ++bits) {
        for (bool steps_empty : {false, true}) {
            const HardwarePromptGates g = gates_from_bits(bits);
            const Spec spec = spec_for(g, steps_empty);
            const HardwarePrompt got = choose_prompt(g, [steps_empty] { return steps_empty; });
            CAPTURE(bits, steps_empty);

            // Mutually exclusive: the encoding as one enum loses nothing.
            CHECK(static_cast<int>(spec.reconfig) + static_cast<int>(spec.deferred) +
                      static_cast<int>(spec.mismatch) <=
                  1);

            if (spec.reconfig) {
                CHECK(got == HardwarePrompt::Reconfig);
            } else if (spec.deferred) {
                CHECK(got == (steps_empty ? HardwarePrompt::DeferredSilent
                                          : HardwarePrompt::DeferredOffer));
            } else if (spec.mismatch) {
                CHECK(got == HardwarePrompt::TypeMismatch);
            } else {
                CHECK(got == HardwarePrompt::None);
            }
        }
    }
}

TEST_CASE("choose_prompt asks for the deferred steps only when an offer is due",
          "[hardware_prompt][discovery]") {
    int asked = 0;
    auto counting = [&asked] {
        ++asked;
        return false;
    };

    HardwarePromptGates due;
    due.hw_changed = true;
    due.hardware_setup_deferred = true;
    CHECK(choose_prompt(due, counting) == HardwarePrompt::DeferredOffer);
    CHECK(asked == 1);

    // Computing the steps touches filament-sensor discovery, so none of these may.
    asked = 0;
    HardwarePromptGates g = due;
    g.print_active = true;
    CHECK(choose_prompt(g, counting) == HardwarePrompt::None);
    g = due;
    g.wizard_blocked = true;
    CHECK(choose_prompt(g, counting) == HardwarePrompt::None);
    g = due;
    g.hw_changed = false;
    CHECK(choose_prompt(g, counting) == HardwarePrompt::None);
    g = due;
    g.reconfig_steps_pending = true;
    CHECK(choose_prompt(g, counting) == HardwarePrompt::Reconfig);
    g = due;
    g.deferred_prompt_shown = true;
    CHECK(choose_prompt(g, counting) == HardwarePrompt::None);
    CHECK(asked == 0);
}

TEST_CASE("choose_prompt: a type mismatch never stacks on a deferred debt",
          "[hardware_prompt][discovery]") {
    HardwarePromptGates g;
    g.hw_changed = true;
    CHECK(choose_prompt(g, [] { return true; }) == HardwarePrompt::TypeMismatch);

    g.hardware_setup_deferred = true;
    CHECK(choose_prompt(g, [] { return false; }) == HardwarePrompt::DeferredOffer);

    // The debt answered earlier this session: the mismatch still waits for the next boot.
    g.deferred_prompt_shown = true;
    CHECK(choose_prompt(g, [] { return false; }) == HardwarePrompt::None);
}
