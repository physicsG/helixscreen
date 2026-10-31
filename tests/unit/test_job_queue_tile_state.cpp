// SPDX-License-Identifier: GPL-3.0-or-later

/// The queue tile's state is one pure function of six inputs. Each case builds
/// inputs that would match only its own row if the rule above it were removed.

#include "../lvgl_ui_test_fixture.h"
#include "job_queue_state.h"
#include "job_queue_tile_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

using QRS = QueueRunState;
using S = JobQueueTileState;

JobQueueTileInputs in(bool avail, bool loaded, int n, QRS q, bool busy, bool autotr) {
    return {avail, loaded, n, q, busy, autotr};
}

} // namespace

TEST_CASE("tile state: Unavailable wins over everything", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(false, true, 3, QRS::Loading, true, true)) == S::Unavailable);
}

TEST_CASE("tile state: NotLoaded before the first queue response", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, false, 3, QRS::Ready, false, false)) == S::NotLoaded);
}

TEST_CASE("tile state: Empty beats Starting", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 0, QRS::Loading, false, false)) == S::Empty);
}

TEST_CASE("tile state: Starting from Loading and Starting, even while busy", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Loading, true, false)) == S::Starting);
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Starting, true, false)) == S::Starting);
}

TEST_CASE("tile state: Starting while the printer is idle", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Starting, false, false)) == S::Starting);
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Loading, false, true)) == S::Starting);
}

TEST_CASE("tile state: the tile's own start reads as Starting, with no button",
          "[job_queue_tile]") {
    JobQueueTileInputs tapped = in(true, true, 2, QRS::Paused, false, false);
    REQUIRE(job_queue_tile_state(tapped) == S::WaitingForStart);
    tapped.start_in_flight = true;
    CHECK(job_queue_tile_state(tapped) == S::Starting);
    CHECK(job_queue_tile_action(job_queue_tile_state(tapped)) == JobQueueTileAction::None);
    // An emptied queue still reads Empty.
    tapped.job_count = 0;
    CHECK(job_queue_tile_state(tapped) == S::Empty);
}

TEST_CASE("tile state: AutoNext only while printing", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Ready, true, true)) == S::AutoNext);
}

TEST_CASE("tile state: Printing when the queue will not advance by itself", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 1, QRS::Ready, true, false)) == S::Printing);
    CHECK(job_queue_tile_state(in(true, true, 3, QRS::Paused, true, true)) == S::Printing);
}

TEST_CASE("tile state: idle with jobs always offers Start", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 1, QRS::Paused, false, false)) == S::WaitingForStart);
    CHECK(job_queue_tile_state(in(true, true, 1, QRS::Ready, false, false)) == S::WaitingForStart);
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Paused, false, true)) == S::WaitingForStart);
    CHECK(job_queue_tile_state(in(true, true, 2, QRS::Ready, false, true)) == S::WaitingForStart);
}

TEST_CASE("tile state: a job added while printing moves Empty to Printing", "[job_queue_tile]") {
    CHECK(job_queue_tile_state(in(true, true, 0, QRS::Ready, true, false)) == S::Empty);
    CHECK(job_queue_tile_state(in(true, true, 1, QRS::Ready, true, false)) == S::Printing);
}

TEST_CASE("parse_queue_run_state maps Moonraker's strings", "[job_queue_tile]") {
    CHECK(parse_queue_run_state("ready") == QRS::Ready);
    CHECK(parse_queue_run_state("loading") == QRS::Loading);
    CHECK(parse_queue_run_state("starting") == QRS::Starting);
    CHECK(parse_queue_run_state("paused") == QRS::Paused);
    CHECK(parse_queue_run_state("") == QRS::Paused);
    CHECK(parse_queue_run_state("bogus") == QRS::Paused);
}

TEST_CASE("tile action: StartNext only for WaitingForStart", "[job_queue_tile]") {
    for (int i = 0; i <= static_cast<int>(S::Starting); ++i) {
        const auto s = static_cast<S>(i);
        CHECK(job_queue_tile_action(s) ==
              (s == S::WaitingForStart ? JobQueueTileAction::StartNext : JobQueueTileAction::None));
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "tile text: one row per state", "[job_queue_tile]") {
    const std::string name = "Benchy";

    SECTION("Unavailable") {
        auto t = compose_job_queue_tile_text(S::Unavailable, name, 3, 4500, 2);
        CHECK(t.hero == "Job queue not enabled in Moonraker");
        CHECK(t.detail.empty());
        CHECK(t.more.empty());
        CHECK(t.action.empty());
        CHECK(t.action_short.empty());
    }
    SECTION("NotLoaded") {
        auto t = compose_job_queue_tile_text(S::NotLoaded, name, 3, 4500, 2);
        CHECK(t.hero.empty());
        CHECK(t.detail.empty());
        CHECK(t.more.empty());
        CHECK(t.action.empty());
        CHECK(t.action_short.empty());
    }
    SECTION("Empty") {
        auto t = compose_job_queue_tile_text(S::Empty, "", 0, 0, 0);
        CHECK(t.hero == "Queue empty");
        CHECK(t.detail.empty());
        CHECK(t.action.empty());
    }
    SECTION("WaitingForStart") {
        auto t = compose_job_queue_tile_text(S::WaitingForStart, name, 3, 4500, 2);
        CHECK(t.hero == "Benchy");
        CHECK(t.count == "3");
        CHECK(t.detail == "1h 15m");
        CHECK(t.more == "+2 more");
        CHECK(t.action == "Bed clear · Start next");
        CHECK(t.action_short == "Start next");
    }
    SECTION("WaitingForStart with nothing unseen has no more line") {
        auto t = compose_job_queue_tile_text(S::WaitingForStart, name, 3, 4500, 0);
        CHECK(t.more.empty());
    }
    SECTION("WaitingForStart counts only the jobs it does not show") {
        auto t = compose_job_queue_tile_text(S::WaitingForStart, name, 6, 4500, 3);
        CHECK(t.more == "+3 more");
    }
    SECTION("WaitingForStart with an unknown estimate has no detail") {
        auto t = compose_job_queue_tile_text(S::WaitingForStart, name, 3, 0, 2);
        CHECK(t.detail.empty());
    }
    SECTION("AutoNext") {
        auto t = compose_job_queue_tile_text(S::AutoNext, name, 3, 4500, 2);
        CHECK(t.hero == "3 queued");
        CHECK(t.count == "3");
        CHECK(t.detail == "Next: Benchy");
        CHECK(t.more.empty());
        CHECK(t.action.empty());
    }
    SECTION("Printing") {
        auto t = compose_job_queue_tile_text(S::Printing, name, 3, 4500, 2);
        CHECK(t.hero == "3 queued");
        CHECK(t.detail == "Next: Benchy");
        CHECK(t.action.empty());
    }
    SECTION("Starting") {
        auto t = compose_job_queue_tile_text(S::Starting, name, 3, 4500, 2);
        CHECK(t.hero == "Starting Benchy…");
        CHECK(t.detail.empty());
        CHECK(t.action.empty());
    }
}
