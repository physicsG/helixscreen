// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// When a lane's tag names a Spoolman spool, which frames ask for a link.

#include "tag_spool_link.h"

#include "catch_amalgamated.hpp"

using helix::ams::TagSpoolLinkTracker;
using helix::ams::TagSpoolSighting;

namespace {

TagSpoolSighting seated(std::optional<int> tag, int linked = 0) {
    return TagSpoolSighting{true, tag, linked};
}

TagSpoolSighting empty_lane(int linked = 0) {
    return TagSpoolSighting{false, std::nullopt, linked};
}

} // namespace

TEST_CASE("a lane's first tag id links only a lane linked to nothing", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    CHECK(t.observe(0, seated(42)) == 42);
    CHECK_FALSE(t.observe(1, seated(42, 7)));
    CHECK_FALSE(t.observe(2, seated(42, 42)));
}

TEST_CASE("an insert links the lane to its tag's spool, replacing the link", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    CHECK_FALSE(t.observe(0, empty_lane(7)));
    CHECK(t.observe(0, seated(42, 7)) == 42);
}

TEST_CASE("a different tag on a seated lane is a new spool", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    CHECK_FALSE(t.observe(0, seated(42, 42)));
    CHECK(t.observe(0, seated(43, 42)) == 43);
}

TEST_CASE("a pick made while the same tag stays seated is kept", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.settle(0, 42);
    CHECK_FALSE(t.observe(0, seated(42, 42)));
    CHECK_FALSE(t.observe(0, seated(42, 99)));
    CHECK_FALSE(t.observe(0, seated(42, 0)));
}

TEST_CASE("pulling and re-seating the same tag is a new insert", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.settle(0, 42);
    CHECK_FALSE(t.observe(0, seated(42, 99)));
    CHECK_FALSE(t.observe(0, empty_lane(99)));
    CHECK(t.observe(0, seated(42, 99)) == 42);
}

TEST_CASE("a seated spool with no tag id changes nothing", "[ams][tag_link]") {
    TagSpoolLinkTracker t;

    SECTION("an unread tag after a seen one keeps the lane's id") {
        REQUIRE(t.observe(0, seated(42, 42)) == std::nullopt);
        CHECK_FALSE(t.observe(0, seated(std::nullopt, 99)));
        CHECK_FALSE(t.observe(0, seated(42, 99)));
    }

    SECTION("a lane first seen without an id still treats its first id as the first") {
        CHECK_FALSE(t.observe(0, seated(std::nullopt, 7)));
        CHECK_FALSE(t.observe(0, seated(42, 7)));
    }

    SECTION("an insert whose tag reads a frame late still links") {
        CHECK_FALSE(t.observe(0, empty_lane(7)));
        CHECK_FALSE(t.observe(0, seated(std::nullopt, 7)));
        CHECK(t.observe(0, seated(42, 7)) == 42);
    }
}

TEST_CASE("a wanted link stands until it is settled", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    CHECK(t.observe(0, seated(42)) == 42);
    CHECK(t.wants(0, 42));
    t.settle(0, 42);
    CHECK_FALSE(t.wants(0, 42));
    CHECK_FALSE(t.observe(0, seated(42)));
}

TEST_CASE("a running lookup is not offered again", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.begin_lookup(0, 42, 0);
    CHECK_FALSE(t.observe(0, seated(42)));
    CHECK(t.wants(0, 42));
}

TEST_CASE("a lookup answering after the tag changed is stale", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.begin_lookup(0, 42, 0);
    CHECK(t.observe(0, seated(43)) == 43);
    CHECK_FALSE(t.wants(0, 42));
    t.settle(0, 42);
    CHECK(t.wants(0, 43));
}

TEST_CASE("a lookup answering after the lane emptied is stale", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.begin_lookup(0, 42, 0);
    CHECK_FALSE(t.observe(0, empty_lane()));
    CHECK_FALSE(t.wants(0, 42));
}

TEST_CASE("a want the lane already holds settles itself", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    CHECK_FALSE(t.observe(0, seated(42, 42)));
    CHECK_FALSE(t.wants(0, 42));
}

TEST_CASE("lanes are tracked apart", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.settle(0, 42);
    CHECK(t.observe(1, seated(42)) == 42);
}

TEST_CASE("a lane linked by hand while its lookup runs keeps the pick", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.begin_lookup(0, 42, 0);
    CHECK_FALSE(t.observe(0, seated(42, 99)));
    CHECK_FALSE(t.wants(0, 42));
    CHECK_FALSE(t.observe(0, seated(42, 99)));
}

TEST_CASE("a running lookup holds while the lane's link stands still", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42, 7)) == std::nullopt);
    CHECK_FALSE(t.observe(0, empty_lane(7)));
    REQUIRE(t.observe(0, seated(42, 7)) == 42);
    t.begin_lookup(0, 42, 7);
    CHECK_FALSE(t.observe(0, seated(42, 7)));
    CHECK(t.wants(0, 42));
}

TEST_CASE("only a spool put in this session is an insert", "[ams][tag_link]") {
    TagSpoolLinkTracker t;

    SECTION("the first id a lane shows is not") {
        REQUIRE(t.observe(0, seated(42)) == 42);
        CHECK_FALSE(t.want_is_insert(0));
    }
    SECTION("an id after the lane read empty is") {
        CHECK_FALSE(t.observe(0, empty_lane()));
        REQUIRE(t.observe(0, seated(42)) == 42);
        CHECK(t.want_is_insert(0));
    }
    SECTION("a different id on a seated lane is") {
        CHECK_FALSE(t.observe(0, seated(42, 42)));
        REQUIRE(t.observe(0, seated(43, 42)) == 43);
        CHECK(t.want_is_insert(0));
    }
}

TEST_CASE("a lookup that got no answer is tried again, not on every frame", "[ams][tag_link]") {
    TagSpoolLinkTracker t;
    REQUIRE(t.observe(0, seated(42)) == 42);
    t.begin_lookup(0, 42, 0);
    t.lookup_failed(0, 42);
    CHECK(t.wants(0, 42));
    for (int frame = 0; frame < TagSpoolLinkTracker::kLookupRetryFrames; ++frame) {
        REQUIRE_FALSE(t.observe(0, seated(42)));
    }
    CHECK(t.observe(0, seated(42)) == 42);
}
