// SPDX-License-Identifier: GPL-3.0-or-later

#include "screensaver_frame.h"

#include <cstddef>
#include <limits>

namespace helix::ui {

void merge_dirty_areas(std::vector<DirtyRect>& areas, size_t max_areas) {
    areas.erase(
        std::remove_if(areas.begin(), areas.end(), [](const DirtyRect& r) { return r.empty(); }),
        areas.end());
    const size_t limit = std::max<size_t>(max_areas, 1);
    while (areas.size() > limit) {
        size_t best_a = 0;
        size_t best_b = 1;
        int64_t best_cost = std::numeric_limits<int64_t>::max();
        for (size_t a = 0; a < areas.size(); a++) {
            for (size_t b = a + 1; b < areas.size(); b++) {
                DirtyRect joined = areas[a];
                joined.add(areas[b]);
                const int64_t cost = joined.area() - areas[a].area() - areas[b].area();
                if (cost < best_cost) {
                    best_cost = cost;
                    best_a = a;
                    best_b = b;
                }
            }
        }
        areas[best_a].add(areas[best_b]);
        areas.erase(areas.begin() + static_cast<std::ptrdiff_t>(best_b));
    }
}

} // namespace helix::ui
