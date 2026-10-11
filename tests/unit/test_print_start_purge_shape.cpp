// SPDX-License-Identifier: GPL-3.0-or-later

#include "print_start_purge_shape.h"

#include "../catch_amalgamated.hpp"

using helix::AmsAction;
using helix::PurgeEvidence;
using helix::PurgeFlowTracker;

namespace {

struct Spot {
    double x, y, z;
};

// Two geometries per shape: a rear-right spot low over a 350 bed and a
// front-left spot higher over a 250 bed.
const Spot kSpots[] = {{290.0, 293.0, 0.5}, {4.0, 10.0, 2.0}};

/// Feeds 250ms samples for `ms`; returns whether the tracker ever reported a stretch.
template <typename Sample> bool run(PurgeFlowTracker& t, int ms, Sample sample) {
    bool seen = false;
    for (int now = 0; now <= ms; now += 250) {
        double x, y, z;
        bool fwd;
        sample(now, x, y, z, fwd);
        seen = t.note_sample(static_cast<uint64_t>(now), fwd, x, y, z) || seen;
    }
    return seen;
}

} // namespace

TEST_CASE("Purge shape: stationary sustained flow is a purge, the shapes around it are not",
          "[print][voron][purge]") {
    for (const Spot& s : kSpots) {
        INFO("spot " << s.x << "," << s.y << " z0=" << s.z);
        PurgeFlowTracker t;

        SECTION("blob: forward flow, Z rising, XY still") {
            CHECK(run(t, 6000, [&](int ms, double& x, double& y, double& z, bool& f) {
                x = s.x + 0.01 * (ms % 2), y = s.y, z = s.z + ms * 0.0004, f = true;
            }));
        }
        SECTION("chute: forward flow, XYZ still") {
            CHECK(run(t, 6000, [&](int, double& x, double& y, double& z, bool& f) {
                x = s.x, y = s.y, z = s.z, f = true;
            }));
        }
        SECTION("flow shorter than the sustain window") {
            CHECK_FALSE(run(t, static_cast<int>(helix::PURGE_FLOW_SUSTAIN_MS) - 250,
                            [&](int, double& x, double& y, double& z, bool& f) {
                                x = s.x, y = s.y, z = s.z, f = true;
                            }));
        }
        SECTION("purge line: forward flow, XY moving, Z flat") {
            CHECK_FALSE(run(t, 6000, [&](int ms, double& x, double& y, double& z, bool& f) {
                x = s.x + ms * 0.02, y = s.y, z = s.z, f = true;
            }));
        }
        SECTION("load pulses and retracts, XYZ still") {
            CHECK_FALSE(run(t, 12000, [&](int ms, double& x, double& y, double& z, bool& f) {
                x = s.x, y = s.y, z = s.z;
                f = (ms % 3000) < 2000; // 2s push, 1s stop or retract
            }));
        }
        SECTION("probing: Z moving, no flow") {
            CHECK_FALSE(run(t, 6000, [&](int ms, double& x, double& y, double& z, bool& f) {
                x = s.x, y = s.y, z = s.z + ((ms / 500) % 2 ? 5.0 : 0.0), f = false;
            }));
        }
        SECTION("Z dropping under flow ends the stretch") {
            CHECK_FALSE(run(t, 6000, [&](int ms, double& x, double& y, double& z, bool& f) {
                x = s.x, y = s.y, z = s.z + ((ms / 1000) % 2 ? 1.0 : 0.0), f = true;
            }));
        }
    }
}

TEST_CASE("Purge shape: the filament system gates an inferred purge", "[print][voron][purge]") {
    PurgeEvidence e;
    e.heaters_at_target = true;
    e.stationary_flow = true;

    SECTION("no filament system: the motion stands alone") {
        CHECK(helix::is_purge(e));
        e.heaters_at_target = false;
        CHECK_FALSE(helix::is_purge(e));
    }
    SECTION("a load still pushing filament to the nozzle is not a purge") {
        e.ams_present = true;
        e.action = AmsAction::LOADING;
        e.filament_loaded = false;
        CHECK_FALSE(helix::is_purge(e));
    }
    SECTION("a purge inside a load, once the filament is loaded, is a purge") {
        e.ams_present = true;
        e.action = AmsAction::LOADING;
        e.filament_loaded = true;
        CHECK(helix::is_purge(e));
    }
    SECTION("an unload, select, cut or tip is not a purge, however sustained") {
        e.ams_present = true;
        e.filament_loaded = true;
        for (AmsAction a : {AmsAction::UNLOADING, AmsAction::SELECTING, AmsAction::CUTTING,
                            AmsAction::FORMING_TIP}) {
            e.action = a;
            CHECK_FALSE(helix::is_purge(e));
        }
        e.action = AmsAction::IDLE;
        CHECK(helix::is_purge(e));
    }
    SECTION("with a filament system, no filament loaded is not a purge") {
        e.ams_present = true;
        e.action = AmsAction::IDLE;
        e.filament_loaded = false;
        CHECK_FALSE(helix::is_purge(e));
    }
    SECTION("a declared purge needs no motion") {
        e.ams_present = true;
        e.action = AmsAction::PURGING;
        e.stationary_flow = false;
        e.heaters_at_target = false;
        CHECK(helix::is_purge(e));
    }
    SECTION("no flow is no purge") {
        e.stationary_flow = false;
        CHECK_FALSE(helix::is_purge(e));
    }
}
