// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "system/android_update_source.h"

#include "../catch_amalgamated.hpp"

using helix::android::decide_update;
using helix::android::OfferedRelease;
using helix::android::PlayUpdateQuery;
using helix::android::update_url;
using helix::android::UpdateVerdict;
using helix::android::version_from_version_code;

namespace {
constexpr const char* kPlay = "com.android.vending";
constexpr const char* kTagBase = "https://github.com/prestonbrown/helixscreen/releases/tag/";
constexpr const char* kLatest = "https://github.com/prestonbrown/helixscreen/releases/latest";

OfferedRelease version_only(const std::string& version) {
    return {"", version, true};
}
} // namespace

TEST_CASE("android update_url: a Play install goes to the Play listing", "[android_update]") {
    CHECK(update_url(kPlay, version_only("1.2.3")) == "market://details?id=org.helixscreen.app");
    CHECK(update_url(kPlay, version_only("")) == "market://details?id=org.helixscreen.app");
    CHECK(update_url(kPlay, {"v1.2.3", "1.2.3", false}) ==
          "market://details?id=org.helixscreen.app");
}

TEST_CASE("android update_url: a sideload goes to the offered GitHub release", "[android_update]") {
    SECTION("no installer reported") {
        CHECK(update_url("", version_only("1.2.3")) == std::string(kTagBase) + "v1.2.3");
    }
    SECTION("installed by adb, a browser or a file manager") {
        CHECK(update_url("com.google.android.packageinstaller", version_only("1.2.3")) ==
              std::string(kTagBase) + "v1.2.3");
        CHECK(update_url("com.android.chrome", version_only("1.1.0-beta.3")) ==
              std::string(kTagBase) + "v1.1.0-beta.3");
    }
    SECTION("a store that merely resembles Play is not Play") {
        CHECK(update_url("com.android.vending.fake", version_only("1.2.3")) ==
              std::string(kTagBase) + "v1.2.3");
        CHECK(update_url("org.fdroid.fdroid", version_only("1.2.3")) ==
              std::string(kTagBase) + "v1.2.3");
    }
    SECTION("build metadata characters are part of a tag") {
        CHECK(update_url("", version_only("1.2.3+build_1")) ==
              std::string(kTagBase) + "v1.2.3+build_1");
    }
}

TEST_CASE("android update_url: the release's own tag wins over the version", "[android_update]") {
    CHECK(update_url("", {"v1.2.3", "9.9.9", true}) == std::string(kTagBase) + "v1.2.3");
    // A tag spelled without the 'v' is used as published, not rewritten.
    CHECK(update_url("", {"1.2.3", "1.2.3", true}) == std::string(kTagBase) + "1.2.3");
    CHECK(update_url("", {"", "1.2.3", true}) == std::string(kTagBase) + "v1.2.3");
}

TEST_CASE("android update_url: a leading v is not doubled", "[android_update]") {
    CHECK(update_url("", version_only("v1.2.3")) == std::string(kTagBase) + "v1.2.3");
    CHECK(update_url("", version_only("V1.2.3")) == std::string(kTagBase) + "v1.2.3");
}

TEST_CASE("android update_url: a release not on GitHub goes to the latest release",
          "[android_update]") {
    CHECK(update_url("", {"v1.3.0-dev.42", "1.3.0-dev.42", false}) == kLatest);
    CHECK(update_url("com.android.chrome", {"", "1.3.0", false}) == kLatest);
}

TEST_CASE("android update_url: no usable version falls back to the latest release",
          "[android_update]") {
    CHECK(update_url("", version_only("")) == kLatest);
    CHECK(update_url("", version_only("v")) == kLatest);
    CHECK(update_url("com.android.chrome", version_only("")) == kLatest);
    // A version from a remote manifest never smuggles a path or query into the URL.
    CHECK(update_url("", version_only("1.2.3/../../evil")) == kLatest);
    CHECK(update_url("", version_only("1.2.3?x=1")) == kLatest);
    CHECK(update_url("", version_only("1.2 3")) == kLatest);
    CHECK(update_url("", {"v1.2.3/../x", "1.2.3", true}) == kLatest);
}

// Expected codes are scripts/android-version-code.sh output for each version.
TEST_CASE("android versionCode decodes back to the version that packed it", "[android_update]") {
    CHECK(version_from_version_code(100000499) == "1.0.4");
    CHECK(version_from_version_code(100000599) == "1.0.5");
    CHECK(version_from_version_code(100000099) == "1.0.0");
    CHECK(version_from_version_code(9911399) == "0.99.113");
    CHECK(version_from_version_code(100100031) == "1.1.0-beta.1");
    CHECK(version_from_version_code(100100030) == "1.1.0-beta");
    CHECK(version_from_version_code(100100062) == "1.1.0-rc.2");
    CHECK(version_from_version_code(100200003) == "1.2.0-alpha.3");
    CHECK(version_from_version_code(100200000) == "1.2.0-alpha");
    CHECK(version_from_version_code(2099999999) == "20.999.999");
}

TEST_CASE("android versionCode outside the packing decodes to nothing", "[android_update]") {
    CHECK_FALSE(version_from_version_code(0).has_value());
    CHECK_FALSE(version_from_version_code(-100000599).has_value());
    // Ordinals 90-98 belong to no stage.
    CHECK_FALSE(version_from_version_code(100000590).has_value());
    CHECK_FALSE(version_from_version_code(100000598).has_value());
    // Above Android's versionCode ceiling.
    CHECK_FALSE(version_from_version_code(2100000001).has_value());
}

namespace {
PlayUpdateQuery play_available(int64_t code) {
    PlayUpdateQuery q;
    q.outcome = PlayUpdateQuery::Outcome::Available;
    q.available_version_code = code;
    return q;
}

PlayUpdateQuery play_outcome(PlayUpdateQuery::Outcome outcome) {
    PlayUpdateQuery q;
    q.outcome = outcome;
    q.detail = "test detail";
    return q;
}
} // namespace

TEST_CASE("android update decision: a non-Play install keeps the manifest result",
          "[android_update]") {
    for (const char* installer : {"", "com.google.android.packageinstaller", "org.fdroid.fdroid",
                                  "com.android.vending.fake"}) {
        CAPTURE(installer);
        // Whatever Play would say, it does not serve this install.
        CHECK(decide_update(installer, play_available(100000599), "1.0.5").kind ==
              UpdateVerdict::Kind::Manifest);
        CHECK(
            decide_update(installer, play_outcome(PlayUpdateQuery::Outcome::NotAvailable), "1.0.5")
                .kind == UpdateVerdict::Kind::Manifest);
        CHECK(decide_update(installer, {}, "").kind == UpdateVerdict::Kind::Manifest);
    }
}

TEST_CASE("android update decision: a Play install announces Play's version", "[android_update]") {
    SECTION("Play serves the version the manifest names") {
        auto v = decide_update(kPlay, play_available(100000599), "1.0.5");
        CHECK(v.kind == UpdateVerdict::Kind::Play);
        CHECK(v.version == "1.0.5");
        CHECK(v.manifest_describes_it);
    }
    SECTION("a manifest version with a leading v still matches") {
        auto v = decide_update(kPlay, play_available(100000599), "v1.0.5");
        CHECK(v.kind == UpdateVerdict::Kind::Play);
        CHECK(v.manifest_describes_it);
    }
    SECTION("Play lags the manifest: Play's version wins") {
        auto v = decide_update(kPlay, play_available(100000599), "1.0.6");
        CHECK(v.kind == UpdateVerdict::Kind::Play);
        CHECK(v.version == "1.0.5");
        CHECK_FALSE(v.manifest_describes_it);
    }
    SECTION("the manifest could not be fetched") {
        auto v = decide_update(kPlay, play_available(100100031), "");
        CHECK(v.kind == UpdateVerdict::Kind::Play);
        CHECK(v.version == "1.1.0-beta.1");
        CHECK_FALSE(v.manifest_describes_it);
    }
}

TEST_CASE("android update decision: a Play install announces nothing Play will not serve",
          "[android_update]") {
    SECTION("Play reports no update while the manifest has a newer release") {
        auto v =
            decide_update(kPlay, play_outcome(PlayUpdateQuery::Outcome::NotAvailable), "1.0.5");
        CHECK(v.kind == UpdateVerdict::Kind::None);
        CHECK_FALSE(v.reason.empty());
    }
    SECTION("the Play query failed or timed out") {
        auto v = decide_update(kPlay, play_outcome(PlayUpdateQuery::Outcome::Failed), "1.0.5");
        CHECK(v.kind == UpdateVerdict::Kind::None);
        CHECK(v.reason.find("test detail") != std::string::npos);
    }
    SECTION("a default query is a failure, not an update") {
        CHECK(decide_update(kPlay, {}, "1.0.5").kind == UpdateVerdict::Kind::None);
    }
    SECTION("Play reports an update whose versionCode is outside the packing") {
        auto v = decide_update(kPlay, play_available(100000595), "1.0.5");
        CHECK(v.kind == UpdateVerdict::Kind::None);
        CHECK_FALSE(v.reason.empty());
    }
}
