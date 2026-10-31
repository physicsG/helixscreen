// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "subject_initializer.h"

// Friend access to SubjectInitializer's phases: the full init_panels() builds every
// panel in the app, so a test drives the one phase it is about.
struct SubjectInitializerTestAccess {
    static void init_core_subjects(SubjectInitializer& si) {
        si.init_core_subjects();
    }
    static void init_temperature_control(SubjectInitializer& si, helix::IMoonrakerAPI* api) {
        si.init_temperature_control(api);
    }
};
