// SPDX-License-Identifier: GPL-3.0-or-later

#include "fault_path_position.h"

#include "afc_fault_position.h"

namespace helix::faultpath {

namespace {

struct Provider {
    std::optional<PathSegment> (*position)(std::string_view message);
    std::string (*strip)(std::string_view message);
};

const Provider kProviders[] = {
    {&afc::afc_fault_position, &afc::afc_strip_position_diagram},
};

const Provider* recognising_provider(std::string_view message) {
    for (const Provider& p : kProviders) {
        if (p.position(message)) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

std::optional<PathSegment> fault_position(std::string_view message) {
    const Provider* p = recognising_provider(message);
    return p ? p->position(message) : std::nullopt;
}

std::string strip_position_diagram(std::string_view message) {
    const Provider* p = recognising_provider(message);
    return p ? p->strip(message) : std::string(message);
}

} // namespace helix::faultpath
